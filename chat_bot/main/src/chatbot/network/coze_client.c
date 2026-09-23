#include <string.h>

#include "qosa_def.h"
#include "qosa_sys.h"
#include "qosa_log.h"
#include "qcm_websocket.h"
#include "qcm_vtls_cfg.h"

#define QOS_LOG_TAG LOG_TAG_DEMO

#include "coze_internal.h"
#include "coze_network.h"
#include "chatbot_config.h"
#include "chatbot_types.h"
#include "chat_bus.h"

static qcm_ssl_config_t g_ssl_config = {0};
static char             g_ws_url[160] = {0};
static char             g_auth_header_value[160] = {0};
static qosa_bool_t      g_client_connecting = QOSA_FALSE;

#define CHATBOT_WS_RX_JSON_ACCUM_MAX   (32 * 1024)

typedef struct
{
    char *data;
    int   len;
} ws_rx_chunk_t;

static char g_ws_rx_json_accum[CHATBOT_WS_RX_JSON_ACCUM_MAX] = {0};
static int  g_ws_rx_json_len = 0;

/*!< 协议解析 worker：RECV 回调运行在 qcm_websocket 的 web_client 收包任务上下文,
    若在回调里同步做 cJSON 解析 + base64 解码（较重 CPU 操作），会占住收包任务、
    阻塞底层继续从 socket 取数，导致 TCP 接收窗口被耗尽、下行数据停滞。
    因此回调内只做 drain + 整块入队（轻量），JSON 重组与业务分发移到该 worker 任务。 */
static qosa_task_t g_proto_worker_task = QOSA_NULL;
static qosa_msgq_t g_proto_worker_queue = QOSA_NULL;

/**
 * @brief 判断字符是否为空白字符。
 *
 * @param[in] c 待判断的字符。
 * @return QOSA_TRUE 表示为空白字符；否则返回 QOSA_FALSE。
 */
static qosa_bool_t coze_is_json_blank(char c)
{
    return (c == ' ') || (c == '\t') || (c == '\r') || (c == '\n');
}

/**
 * @brief 将 WebSocket 接收片段累积并按 JSON 对象边界分发。
 *
 * @param[in] chunk     接收数据片段。
 * @param[in] chunk_len 片段长度，单位为字节。
 */
/* qcm_websocket 底层可能把同一 JSON 事件拆成多段回调，也可能一次回调里包含多个事件。
   这里按 JSON 对象边界重组后再分发，避免半包/粘包导致上层解析失败。 */
static void coze_dispatch_json_stream(const char *chunk, int chunk_len)
{
    if ((chunk == QOSA_NULL) || (chunk_len <= 0))
    {
        return;
    }

    if (chunk_len >= CHATBOT_WS_RX_JSON_ACCUM_MAX)
    {
        QLOGW("[chat_coze_client] recv chunk too large, drop len=%d", chunk_len);
        g_ws_rx_json_len = 0;
        return;
    }

    if ((g_ws_rx_json_len + chunk_len) >= CHATBOT_WS_RX_JSON_ACCUM_MAX)
    {
        QLOGW("[chat_coze_client] recv accum overflow, drop buffered=%d new=%d", g_ws_rx_json_len, chunk_len);
        g_ws_rx_json_len = 0;
    }

    qosa_memcpy(g_ws_rx_json_accum + g_ws_rx_json_len, chunk, (qosa_size_t)chunk_len);
    g_ws_rx_json_len += chunk_len;
    g_ws_rx_json_accum[g_ws_rx_json_len] = '\0';

    int consumed = 0;
    int depth = 0;
    int obj_start = -1;
    qosa_bool_t in_string = QOSA_FALSE;
    qosa_bool_t escaped = QOSA_FALSE;

    for (int i = 0; i < g_ws_rx_json_len; i++)
    {
        char ch = g_ws_rx_json_accum[i];

        if (in_string == QOSA_TRUE)
        {
            if (escaped == QOSA_TRUE)
            {
                escaped = QOSA_FALSE;
            }
            else if (ch == '\\')
            {
                escaped = QOSA_TRUE;
            }
            else if (ch == '"')
            {
                in_string = QOSA_FALSE;
            }
            continue;
        }

        if (ch == '"')
        {
            in_string = QOSA_TRUE;
            continue;
        }

        if (ch == '{')
        {
            if (depth == 0)
            {
                obj_start = i;
            }
            depth++;
            continue;
        }

        if ((ch == '}') && (depth > 0))
        {
            depth--;
            if ((depth == 0) && (obj_start >= 0))
            {
                int obj_len = i - obj_start + 1;
                coze_protocol_dispatch_downlink(g_ws_rx_json_accum + obj_start, obj_len);
                consumed = i + 1;
                obj_start = -1;
            }
        }
    }

    if (consumed > 0)
    {
        int remain = g_ws_rx_json_len - consumed;
        if (remain > 0)
        {
            memmove(g_ws_rx_json_accum, g_ws_rx_json_accum + consumed, (qosa_size_t)remain);
        }
        g_ws_rx_json_len = remain;
        g_ws_rx_json_accum[g_ws_rx_json_len] = '\0';
        return;
    }

    if (depth == 0)
    {
        qosa_bool_t has_non_blank = QOSA_FALSE;
        for (int i = 0; i < g_ws_rx_json_len; i++)
        {
            if (coze_is_json_blank(g_ws_rx_json_accum[i]) == QOSA_FALSE)
            {
                has_non_blank = QOSA_TRUE;
                break;
            }
        }

        if (has_non_blank == QOSA_TRUE)
        {
            QLOGW("[chat_coze_client] recv non-json payload, drop len=%d", g_ws_rx_json_len);
        }
        g_ws_rx_json_len = 0;
        g_ws_rx_json_accum[0] = '\0';
    }
}

/* ============================ 协议解析 worker 任务 ============================ */
/**
 * @brief 入队一个原始数据块，成功入队后 data 所有权转移给 worker（由其负责释放）。
 *
 * data == QOSA_NULL 且 len == 0 表示"会话边界重置"标记：worker 收到后清空 JSON 重组
 * 累积缓冲，用于连接建立/关闭时丢弃上一会话残留的半包数据。
 *
 * @param[in] data 数据块指针。
 * @param[in] len  数据块长度，单位为字节。
 * @return 0 表示入队成功；返回负值表示队列满/未初始化，调用方需自行释放 data。
 */
static int coze_proto_worker_enqueue(char *data, int len)
{
    if (g_proto_worker_queue == QOSA_NULL)
    {
        return -1;
    }

    ws_rx_chunk_t item;
    item.data = data;
    item.len = len;
    if (qosa_msgq_release(g_proto_worker_queue, sizeof(ws_rx_chunk_t), (qosa_uint8_t *)&item, QOSA_NO_WAIT) != QOSA_ERROR_OK)
    {
        return -1;
    }
    return 0;
}

/**
 * @brief 协议解析 worker 任务入口，消费数据块并分发完整 JSON 事件。
 *
 * @param[in] ctx 任务上下文，当前未使用。
 */
static void coze_proto_worker_process(void *ctx)
{
    (void)ctx;

    while (1)
    {
        ws_rx_chunk_t item;
        if (qosa_msgq_wait(g_proto_worker_queue, (qosa_uint8_t *)&item, sizeof(ws_rx_chunk_t), QOSA_WAIT_FOREVER) != 0)
        {
            continue;
        }

        if (item.data == QOSA_NULL)
        {
            /* 会话边界重置：丢弃上一会话可能残留的半包 */
            g_ws_rx_json_len = 0;
            g_ws_rx_json_accum[0] = '\0';
            continue;
        }

        coze_dispatch_json_stream(item.data, item.len);
        qosa_free(item.data);
    }
}

/**
 * @brief 幂等创建协议解析 worker 队列和任务，进程生命周期内只创建一次。
 *
 * @return 0 表示 worker 已就绪；返回负值表示资源创建失败。
 */
static int coze_proto_worker_start(void)
{
    if (g_proto_worker_queue == QOSA_NULL)
    {
        if (qosa_msgq_create(&g_proto_worker_queue, sizeof(ws_rx_chunk_t), CHATBOT_PROTO_WORKER_QUEUE_DEPTH) != QOSA_ERROR_OK)
        {
            QLOGE("[chat_coze_client] proto worker queue create failed");
            return -1;
        }
    }

    if (g_proto_worker_task == QOSA_NULL)
    {
        if (qosa_task_create(&g_proto_worker_task, CHATBOT_PROTO_WORKER_TASK_STACK_SIZE,
                             CHATBOT_PROTO_WORKER_TASK_PRIORITY, "coze_proto", coze_proto_worker_process, QOSA_NULL) != 0)
        {
            QLOGE("[chat_coze_client] proto worker task create failed");
            return -1;
        }
    }
    return 0;
}

/**
 * @brief 连接建立/关闭时调用：丢弃 worker 队列中未消费的旧数据，并投递重置标记
 *        通知 worker 清空 JSON 重组累积缓冲（累积缓冲只由 worker 线程访问，
 *        跨会话重置必须经由队列完成）。
 */
static void coze_proto_worker_post_reset(void)
{
    if (g_proto_worker_queue == QOSA_NULL)
    {
        /* worker 未启动时无残留可清（尚无任何数据经 worker 累积） */
        return;
    }

    /* 先排空队列里未消费的旧数据块，保证 reset 标记前没有旧会话残留 */
    ws_rx_chunk_t item;
    while (qosa_msgq_wait(g_proto_worker_queue, (qosa_uint8_t *)&item, sizeof(ws_rx_chunk_t), QOSA_NO_WAIT) == 0)
    {
        if (item.data != QOSA_NULL)
        {
            qosa_free(item.data);
        }
    }
    (void)coze_proto_worker_enqueue(QOSA_NULL, 0);
}

/**
 * @brief 拷贝一段接收数据并异步投递给协议解析 worker 解析。
 *
 * @param[in] data 接收数据片段，调用期间必须保持有效。
 * @param[in] len  片段长度，单位为字节。
 * @return 0 表示已复制并入队；返回负值表示参数、内存或队列失败（入队失败时已自行释放拷贝）。
 */
static int coze_proto_worker_dispatch_async(const char *data, int len)
{
    if ((data == QOSA_NULL) || (len <= 0))
    {
        return -1;
    }
    if (g_proto_worker_queue == QOSA_NULL)
    {
        QLOGW("[chat_coze_client] proto worker not ready, drop len=%d", len);
        return -1;
    }

    char *copy = qosa_malloc((qosa_uint32_t)len);
    if (copy == QOSA_NULL)
    {
        QLOGW("[chat_coze_client] proto worker alloc failed, len=%d", len);
        return -1;
    }
    qosa_memcpy(copy, data, (qosa_size_t)len);
    if (coze_proto_worker_enqueue(copy, len) != 0)
    {
        QLOGW("[chat_coze_client] proto worker queue full, drop len=%d", len);
        qosa_free(copy);
        return -1;
    }
    return 0;
}

/**
 * @brief 排空 WebSocket 接收水位线中的数据并异步交给协议 worker（忽略通知携带的 size，循环读到真正读空为止）。
 *
 * @note 关键背景（SDK 内部确认的边沿触发丢失问题）：`qcm_ws_rx_wm_non_empty_cb`
 *       是"水位线由空变非空"的边沿触发通知，通过 `qcm_ws_msg_send_2` 投递到
 *       SDK 内部共享消息队列（`QCM_WEB_MSGQ_SIZE=10`，与高频 `QCM_WEB_MSG_SOCKET_EVENT`
 *       共用同一队列）。数据突发时该队列可能被挤满，投递失败时 SDK
 *       仅打印 `err msg_id=%d` 并静默丢弃、不重试；由于通知是边沿触发且丢失不补发，
 *       一旦此次通知被丢，水位线因无人排空而永远保持非空，之后即使底层 socket
 *       持续收到更多数据（`qcm_socket_ssl_read` 仍不断成功），也再不会触发任何
 *       后续通知——表现为"服务器仍在发送但客户端再收不到任何下行事件"，且无
 *       断连/错误可查。故本函数除了被动响应通知调用外，还被 `coze_client_poll_recv()`
 *       在外部任务里周期性主动调用，作为通知丢失时的兜底，防止水位线永久卡死。
 *
 * @param[in] client_id   WebSocket 客户端 ID。
 * @param[in] notify_size 水位线通知携带的数据量，当前仅为兼容参数。
 */
static void coze_client_drain_watermark(int client_id, int notify_size)
{
    (void)notify_size; /* drain 明细不打日志，notify_size 无业务用途 */
    int   drain_cap = QCM_WEB_CFG_READ_BUF_LEN_MAX;
    char *rx_buf = qosa_malloc((qosa_uint32_t)drain_cap + 1);
    if (rx_buf == QOSA_NULL)
    {
        QLOGW("[chat_coze_client] recv(buffer) alloc failed, cap=%d", drain_cap);
        return;
    }

    for (;;)
    {
        int           total_got = 0;
        qcm_web_err_e read_ret = QCM_WEB_ERR_OK;
        while (total_got < drain_cap)
        {
            int got = drain_cap - total_got;
            read_ret = qcm_ws_read_proc(client_id, rx_buf + total_got, &got);
            if ((read_ret != QCM_WEB_ERR_OK) || (got <= 0))
            {
                break;
            }
            total_got += got;
        }

        if (total_got <= 0)
        {
            break;
        }

        /* drain 属正常接收过程，且 uplink_task 每 10ms 高频
           poll_recv() 主动调用时每次有数据都会命中打印，属高频日志主源之一，
           故常规 drain 不打印；alloc/队列满等异常已有独立 WARN。 */

        /* drain 出的原始数据拷贝入队，由 worker 任务做 JSON 重组与业务分发,
           避免在收包任务上下文同步做 cJSON 解析/base64 解码导致 TCP 窗口枯竭。 */
        rx_buf[total_got] = '\0';
        (void)coze_proto_worker_dispatch_async(rx_buf, total_got);

        if (total_got < drain_cap)
        {
            /* 本轮没有把整块缓冲区读满，说明水位线已经真正读空，无需再轮询 */
            break;
        }
    }
    qosa_free(rx_buf);
}

/**
 * @brief 主动轮询并排空 WebSocket 接收缓冲，作为边沿触发通知丢失时的兜底。
 *
 * 供 uplink_task_process 高频循环周期性调用；无数据可读时立即返回，开销可忽略。
 */
void coze_client_poll_recv(void)
{
    coze_client_drain_watermark(CHATBOT_COZE_CLIENT_ID, 0);
}

/**
 * @brief 处理 WebSocket 连接、接收和断开回调：区分连接开启/接收数据/连接关闭。
 *
 * 数据面（音频帧）与控制面（其余事件）的分流在 coze_protocol_dispatch_downlink() 中完成，
 * 本回调只负责把原始数据转交，不解析业务语义。
 *
 * @param[in] client_id WebSocket 客户端 ID。
 * @param[in] cb_t      WebSocket 回调数据，回调期间由底层保持有效。
 * @return 0 表示回调已处理。
 */
static int coze_ws_recv_cb(int client_id, qcm_web_cb_t *cb_t)
{
    if (cb_t == QOSA_NULL)
    {
        return -1;
    }

    switch (cb_t->type)
    {
        case QCM_WEB_TYPE_OPEN:
        {
            g_client_connecting = QOSA_FALSE;
            /* 新连接开始前通知 worker 丢弃上一会话残留的半包 JSON */
            coze_proto_worker_post_reset();
            if (cb_t->result != 0)
            {
                QLOGE("[chat_coze_client] connect failed, result=%d", cb_t->result);
                chat_event_t event = {0};
                event.type = CHAT_EVT_WS_CONNECT_FAILED;
                (void)chat_bus_post_event(&event);
                break;
            }

            QLOGI("[chat_coze_client] tcp/tls/ws handshake done, sending chat.update");
            /* chat.update 含 event_subscriptions + prologue_content 等字段，JSON 可能超 1KB，
               缓冲区过小会导致整个配置帧发送失败，故预留 2048 容量。 */
            char update_buf[2048] = {0};
            int  update_len = coze_protocol_build_update_event(update_buf, sizeof(update_buf), coze_session_get_mode());
            if (update_len > 0)
            {
                QLOGI("[chat_coze_client] chat.update: %s", update_buf);
                (void)coze_client_send_raw(update_buf, update_len, QCM_WEB_OPCODE_TEXT);
            }
            else
            {
                QLOGE("[chat_coze_client] build chat.update failed (buffer too small?)");
            }
            /* 是否真正建立业务会话，以后续收到的 chat.created 事件为准（见 coze_protocol.c）。 */
            break;
        }

        case QCM_WEB_TYPE_RECV:
            if (cb_t->size > 0)
            {
                if (cb_t->data != QOSA_NULL)
                {
                    /* PUSH 模式：回调直接携带数据。为避免占住收包任务，拷贝后交由 worker 解析。 */
                    QLOGD("[chat_coze_client] recv(push) %d bytes", cb_t->size);
                    (void)coze_proto_worker_dispatch_async(cb_t->data, cb_t->size);
                }
                else
                {
                    /* BUFFER 模式下 qcm_ws_rx_wm_non_empty_cb 是"水位线由空变非空"的边沿触发通知,
                       cb_t->size 只是触发那一刻单次入队的大小，不代表水位线里当前实际可读总量,
                       必须忽略 cb_t->size、循环读到真正读空为止（见 coze_client_drain_watermark 注释,
                       该通知本身在 SDK 内部消息队列满时还可能被直接丢弃，故另有 poll 兜底）。 */
                    coze_client_drain_watermark(client_id, cb_t->size);
                }
            }
            break;

        case QCM_WEB_TYPE_CLOSE:
        {
            /* 连接关闭：通知 worker 丢弃残留的半包 JSON */
            coze_proto_worker_post_reset();
            int tx_total_size = 0;
            int send_size = 0;
            if (qcm_ws_client_get_send_size(client_id, &tx_total_size, &send_size) == QCM_WEB_ERR_OK)
            {
                QLOGW("[chat_coze_client] connection closed, result=%d tx_total=%d send_size=%d pending=%d",
                      cb_t->result, tx_total_size, send_size, tx_total_size - send_size);
            }
            else
            {
                QLOGW("[chat_coze_client] connection closed, result=%d", cb_t->result);
            }
            g_client_connecting = QOSA_FALSE;
            chat_event_t event = {0};
            event.type = CHAT_EVT_WS_DISCONNECTED;
            (void)chat_bus_post_event(&event);
            break;
        }

        default:
            break;
    }

    return 0;
}

/**
 * @brief 配置 TLS/WebSocket 参数并发起 Coze WebSocket 连接。
 *
 * @return 0 表示连接请求已提交；返回负值表示配置或连接启动失败。
 */
int coze_client_connect(void)
{
    if (g_client_connecting == QOSA_TRUE)
    {
        return 0; /* 幂等：正在连接中 */
    }

    /* 确保接收数据有独立解析任务消费，避免 RECV 回调在收包任务上下文同步解析。
       若 worker 启动失败不阻断连接：下次 connect 会幂等重试创建。 */
    if (coze_proto_worker_start() != 0)
    {
        QLOGW("[chat_coze_client] proto worker start failed, recv data will be dropped");
    }

    qosa_snprintf(g_ws_url, sizeof(g_ws_url), CHATBOT_COZE_WS_URL_FMT, CHATBOT_COZE_BOT_ID);

    qcm_web_config_t *cfg_ptr = qcm_ws_cfg_get_all(CHATBOT_COZE_CONFIG_ID);
    if (cfg_ptr == QOSA_NULL)
    {
        QLOGE("[chat_coze_client] get config ptr failed");
        return -1;
    }
    cfg_ptr->conn.pdp_cid = CHATBOT_COZE_PDP_CID;
    cfg_ptr->conn.sim_cid = CHATBOT_COZE_SIM_CID;

    (void)qcm_ws_cfg_set(CHATBOT_COZE_CONFIG_ID, QCM_WEB_CFG_CONN_URL, (void *)g_ws_url, (int)qosa_strlen(g_ws_url));

    qosa_memset(&g_ssl_config, 0, sizeof(g_ssl_config));
    g_ssl_config.ssl_version = QCM_SSL_VERSION_3; /* TLS 1.2，如需 TLS1.3 可改为 QCM_SSL_VERSION_5 */
    g_ssl_config.transport = QCM_SSL_TLS_PROTOCOL;
    g_ssl_config.sni_enable = QOSA_TRUE;
    g_ssl_config.ssl_negotiate_timeout = 30; /* 缺省 0 会导致 TLS 握手在到达 OPEN 回调前静默失败 */
    /* 安全提示：当前未校验服务器证书，量产部署时应改为 QCM_SSL_VERIFY_SERVER 并配置 CA 证书 */
    g_ssl_config.auth_mode = QCM_SSL_VERIFY_NULL;
    (void)qcm_ws_cfg_set(CHATBOT_COZE_CONFIG_ID, QCM_WEB_CFG_CONN_SSLCONFIG, (void *)&g_ssl_config, 0);

    (void)qcm_ws_cfg_set(CHATBOT_COZE_CONFIG_ID, QCM_WEB_CFG_CONN_TIMEOUT, QOSA_NULL, CHATBOT_COZE_CONN_TIMEOUT_MS / 1000);
    (void)qcm_ws_cfg_set(CHATBOT_COZE_CONFIG_ID, QCM_WEB_CFG_PING_INTERVAL, QOSA_NULL, CHATBOT_COZE_PING_INTERVAL_S);
    (void)qcm_ws_cfg_set(CHATBOT_COZE_CONFIG_ID, QCM_WEB_CFG_READ_BUFFERSZ, QOSA_NULL, QCM_WEB_CFG_READ_BUF_LEN_MAX);
    (void)qcm_ws_cfg_set(CHATBOT_COZE_CONFIG_ID, QCM_WEB_CFG_WRITE_BUFFERSZ, QOSA_NULL, QCM_WEB_CFG_WRITE_BUF_LEN_MAX);
    (void)qcm_ws_cfg_set(CHATBOT_COZE_CONFIG_ID, QCM_WEB_CFG_RECV_CB, (void *)coze_ws_recv_cb, 0);
    QLOGI("[chat_coze_client] ws buffer cfg: read=%d write=%d", QCM_WEB_CFG_READ_BUF_LEN_MAX, QCM_WEB_CFG_WRITE_BUF_LEN_MAX);

    qosa_snprintf(g_auth_header_value, sizeof(g_auth_header_value), "Bearer %s", CHATBOT_COZE_AUTH_TOKEN);
    qcm_web_header_data_t auth_header = {0};
    auth_header.key = (char *)"Authorization";
    auth_header.key_len = (int)qosa_strlen(auth_header.key);
    auth_header.value = g_auth_header_value;
    auth_header.value_len = (int)qosa_strlen(g_auth_header_value);
    (void)qcm_ws_cfg_set(CHATBOT_COZE_CONFIG_ID, QCM_WEB_CFG_REQHEAD, (void *)&auth_header, 0);

    qcm_web_open_t open_t = {0};
    open_t.client_id = CHATBOT_COZE_CLIENT_ID;
    open_t.config_id = CHATBOT_COZE_CONFIG_ID;

    QLOGI("[chat_coze_client] connecting url=%s", g_ws_url);
    qcm_web_err_e ret = qcm_ws_open_proc(&open_t);
    if (ret != QCM_WEB_ERR_OK)
    {
        QLOGE("[chat_coze_client] open proc failed, ret=%d", ret);
        return -1;
    }

    g_client_connecting = QOSA_TRUE;
    return 0;
}

/**
 * @brief 通过当前 WebSocket 连接发送原始帧。
 *
 * @param[in] data   待发送的数据。
 * @param[in] len    数据长度，单位为字节。
 * @param[in] opcode WebSocket 帧操作码。
 * @return 0 表示发送成功；返回负值表示未连接或发送失败。
 */
int coze_client_send_raw(const char *data, int len, qcm_web_opcode_e opcode)
{
    if ((data == QOSA_NULL) || (len <= 0))
    {
        return -1;
    }

    qcm_web_err_e ret = qcm_ws_write_proc(CHATBOT_COZE_CLIENT_ID, (char *)data, len, opcode);
    if (ret != QCM_WEB_ERR_OK)
    {
        int tx_total_size = 0;
        int send_size = 0;
        if (qcm_ws_client_get_send_size(CHATBOT_COZE_CLIENT_ID, &tx_total_size, &send_size) == QCM_WEB_ERR_OK)
        {
            QLOGW("[chat_coze_client] send raw failed, ret=%d len=%d tx_total=%d send_size=%d pending=%d",
                  ret, len, tx_total_size, send_size, tx_total_size - send_size);
        }
        else
        {
            QLOGW("[chat_coze_client] send raw failed, ret=%d, len=%d", ret, len);
        }
        return -1;
    }
    return 0;
}

/**
 * @brief 关闭当前 Coze WebSocket 连接并复位客户端连接状态。
 */
void coze_client_close(void)
{
    g_client_connecting = QOSA_FALSE;
    (void)qcm_ws_close_proc(CHATBOT_COZE_CLIENT_ID);
}
