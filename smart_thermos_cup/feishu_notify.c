/*****************************************************************/ /**
 * @file feishu_notify.c
 * @brief 通过飞书群自定义机器人 Webhook（HTTPS POST）推送文本消息
 *
 * 调用 feishu_notify_send() 只是入队，联网与 HTTPS 请求在独立任务中完成，
 * 不会阻塞 LVGL 刷新和 DHT 读取。
 **********************************************************************/
#include "qosa_def.h"
#include "qosa_sys.h"
#include "qosa_log.h"
#include "qosa_datacall.h"
#include "qurl_api.h"
#include <string.h>

#define QOS_LOG_TAG   LOG_TAG_DEMO

#include "feishu_config.h"
#include "feishu_notify.h"

#define FEISHU_QUEUE_LEN    4
#define FEISHU_RESP_MAX     256
#define FEISHU_BODY_MAX     (FEISHU_TEXT_MAX * 2 + 96)

typedef struct
{
    char text[FEISHU_TEXT_MAX];
} feishu_msg_t;

static qosa_msgq_t s_queue;
static int         s_ready;

/* 仅任务内使用，避免占用栈 */
static char s_body[FEISHU_BODY_MAX];
static char s_resp[FEISHU_RESP_MAX];
static long s_resp_len;

static long feishu_write_cb(char *buf, long size, void *arg)
{
    long room = (long)sizeof(s_resp) - 1 - s_resp_len;
    long n    = (size < room) ? size : room;

    (void)arg;
    if (n > 0)
    {
        memcpy(s_resp + s_resp_len, buf, (size_t)n);
        s_resp_len += n;
        s_resp[s_resp_len] = '\0';
    }

    return size;
}

/* 把文本转义后追加到 dst（JSON 字符串内部），返回新的写入位置，容量不足时截断 */
static size_t json_append_escaped(char *dst, size_t pos, size_t cap, const char *src)
{
    for (; *src != '\0'; src++)
    {
        unsigned char c = (unsigned char)*src;

        if (c == '"' || c == '\\')
        {
            if (pos + 2 >= cap)
            {
                break;
            }
            dst[pos++] = '\\';
            dst[pos++] = (char)c;
        }
        else if (c == '\n')
        {
            if (pos + 2 >= cap)
            {
                break;
            }
            dst[pos++] = '\\';
            dst[pos++] = 'n';
        }
        else if (c < 0x20)
        {
            continue; /* 其他控制字符直接丢弃 */
        }
        else
        {
            if (pos + 1 >= cap)
            {
                break;
            }
            dst[pos++] = (char)c;
        }
    }

    return pos;
}

static void feishu_build_body(const char *text)
{
    static const char head[] = "{\"msg_type\":\"text\",\"content\":{\"text\":\"";
    static const char tail[] = "\"}}";
    size_t            pos    = 0;
    size_t            cap    = sizeof(s_body) - sizeof(tail); /* 给结尾留位置 */

    memcpy(s_body, head, sizeof(head) - 1);
    pos = sizeof(head) - 1;

    pos = json_append_escaped(s_body, pos, cap, FEISHU_KEYWORD);
    pos = json_append_escaped(s_body, pos, cap, text);

    memcpy(s_body + pos, tail, sizeof(tail)); /* 含结尾 0 */
}

/* 等待驻网，并确保 PDP 已激活 */
static int feishu_network_ready(void)
{
    qosa_datacall_conn_t conn;

    QLOGI("[smart_thermos_cup] feishu: waiting attach...");
    if (!qosa_datacall_wait_attached(FEISHU_SIM_ID, FEISHU_ATTACH_TIMEOUT_S))
    {
        QLOGE("[smart_thermos_cup] feishu: wait attach timeout");
        return -1;
    }
    QLOGI("[smart_thermos_cup] feishu: attached");

    conn = qosa_datacall_conn_new(FEISHU_SIM_ID, FEISHU_PDP_ID, QOSA_DATACALL_CONN_TCPIP);
    if (conn == QOSA_DATACALL_CONN_INVALID)
    {
        QLOGE("[smart_thermos_cup] feishu: datacall conn invalid");
        return -1;
    }

    if (!qosa_datacall_get_status(conn))
    {
        qosa_datacall_errno_e err;

        QLOGI("[smart_thermos_cup] feishu: pdp inactive, starting datacall...");
        err = qosa_datacall_start(conn, FEISHU_DATACALL_TIMEOUT_S);
        if (err != QOSA_DATACALL_OK)
        {
            QLOGE("[smart_thermos_cup] feishu: datacall start failed 0x%x", (unsigned int)err);
            return -1;
        }
    }
    QLOGI("[smart_thermos_cup] feishu: pdp active");

    return 0;
}

/* 发送一次 HTTPS POST，成功返回 0 */
static int feishu_post_once(const char *text)
{
    qurl_ecode_t   ret;
    qurl_core_t    core    = QOSA_NULL;
    qurl_slist_t   headers = QOSA_NULL;
    qurl_tls_cfg_t tls_cfg     = {0}; /* 必须清零：qurl_tls_cfg_init 只设置部分字段，其余指针(alpn/psk)会被 qurl 解引用 */
    long           resp_code = 0;
    int            result    = -1;

    feishu_build_body(text);
    s_resp_len = 0;
    s_resp[0]  = '\0';

    ret = qurl_core_create(&core);
    if (ret != QURL_OK)
    {
        QLOGE("[smart_thermos_cup] feishu: qurl create failed 0x%x", (unsigned int)ret);
        return -1;
    }

    /* 证书校验默认关闭（QURL_TLS_VERIFY_NONE），需要校验时再配置 CA 证书；
       飞书域名需要 SNI，必须打开 */
    qurl_tls_cfg_init(&tls_cfg);
    tls_cfg.negotiate_timeout = FEISHU_TLS_TIMEOUT_S;
    tls_cfg.bits.sni_enable   = QOSA_TRUE;

    headers = qurl_slist_add_strdup(headers, "Content-Type: application/json");

    qurl_core_setopt(core, QURL_OPT_URL, FEISHU_WEBHOOK_URL);
    qurl_core_setopt(core, QURL_OPT_NETWORK_ID, (long)FEISHU_PDP_ID);
    qurl_core_setopt(core, QURL_OPT_TLS_CFG, &tls_cfg);
    qurl_core_setopt(core, QURL_OPT_HTTP_HEADER, headers);
    qurl_core_setopt(core, QURL_OPT_HTTP_POST, 1L);
    qurl_core_setopt(core, QURL_OPT_UPLOAD_DATA, s_body);
    qurl_core_setopt(core, QURL_OPT_UPLOAD_SIZE, (long)strlen(s_body));
    qurl_core_setopt(core, QURL_OPT_WRITE_CB, feishu_write_cb);
    qurl_core_setopt(core, QURL_OPT_TIMEOUT_MS, (long)FEISHU_HTTP_TIMEOUT_MS);

    QLOGI("[smart_thermos_cup] feishu: perform start, body_len=%d", (int)strlen(s_body));
    ret = qurl_core_perform(core);
    qurl_core_getinfo(core, QURL_INFO_RESP_CODE, &resp_code);
    QLOGI("[smart_thermos_cup] feishu: perform done ret=0x%x http=%ld", (unsigned int)ret, resp_code);

    if (ret != QURL_OK)
    {
        QLOGE("[smart_thermos_cup] feishu: perform failed 0x%x, http=%ld", (unsigned int)ret, resp_code);
    }
    else if (resp_code != 200 || strstr(s_resp, "\"code\":0") == QOSA_NULL)
    {
        /* 关键词不匹配、签名/IP 限制等错误也会返回 200，但 code 非 0 */
        QLOGE("[smart_thermos_cup] feishu: rejected, http=%ld resp=%s", resp_code, s_resp);
    }
    else
    {
        QLOGI("[smart_thermos_cup] feishu: sent ok");
        result = 0;
    }

    qurl_slist_del_all(headers);
    qurl_core_delete(core);

    return result;
}

static void feishu_task(void *argv)
{
    feishu_msg_t msg;

    (void)argv;

    QLOGI("[smart_thermos_cup] feishu: task started");

    for (;;)
    {
        if (qosa_msgq_wait(s_queue, (qosa_uint8_t *)&msg, sizeof(msg), QOSA_WAIT_FOREVER) != QOSA_OK)
        {
            continue;
        }

        QLOGI("[smart_thermos_cup] feishu: got msg, len=%d", (int)strlen(msg.text));

        for (int i = 0; i < FEISHU_RETRY_COUNT; i++)
        {
            QLOGI("[smart_thermos_cup] feishu: try %d/%d", i + 1, FEISHU_RETRY_COUNT);
            if (feishu_network_ready() == 0 && feishu_post_once(msg.text) == 0)
            {
                break;
            }
            qosa_task_sleep_ms(FEISHU_RETRY_DELAY_MS);
        }
    }
}

int feishu_notify_init(void)
{
    qosa_task_t task_ref = QOSA_NULL;

    if (qosa_msgq_create(&s_queue, sizeof(feishu_msg_t), FEISHU_QUEUE_LEN) != QOSA_OK)
    {
        QLOGE("[smart_thermos_cup] feishu: msgq create failed");
        return -1;
    }

    QLOGI("[smart_thermos_cup] feishu: qurl_global_init ret=0x%x", (unsigned int)qurl_global_init());

    if (qosa_task_create(&task_ref, FEISHU_TASK_STACK_SIZE, QOSA_PRIORITY_NORMAL, "feishu", feishu_task, QOSA_NULL) != 0)
    {
        QLOGE("[smart_thermos_cup] feishu: task create failed");
        return -1;
    }

    s_ready = 1;
    QLOGI("[smart_thermos_cup] feishu: init ok, task stack=%d", FEISHU_TASK_STACK_SIZE);
    return 0;
}

int feishu_notify_send(const char *text)
{
    feishu_msg_t msg;
    size_t       len;

    if (!s_ready || text == QOSA_NULL)
    {
        return -1;
    }

    memset(&msg, 0, sizeof(msg));
    len = strlen(text);
    if (len >= sizeof(msg.text))
    {
        /* 超长截断：退到完整 UTF-8 字符边界，避免产生半个汉字 */
        len = sizeof(msg.text) - 1;
        while (len > 0 && ((unsigned char)text[len] & 0xC0) == 0x80)
        {
            len--;
        }
    }
    memcpy(msg.text, text, len);

    if (qosa_msgq_release(s_queue, sizeof(msg), (qosa_uint8_t *)&msg, QOSA_NO_WAIT) != QOSA_OK)
    {
        QLOGE("[smart_thermos_cup] feishu: queue full, drop message");
        return -1;
    }

    QLOGI("[smart_thermos_cup] feishu: msg queued");
    return 0;
}
