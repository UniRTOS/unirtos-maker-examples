#include <string.h>

#include "qosa_def.h"
#include "qosa_sys.h"
#include "qosa_log.h"
#include "car_log.h"

#include "car_cloud.h"
#include "car_cloud_config.h"

#include "qcm_mqtt.h"
#include "qcm_mqtt_config.h"
#include "qcm_sha1.h"
#include "qcm_utils.h"
#include "qosa_rtc.h"

#ifdef CONFIG_QCM_MQTT_FUNC

/* 事件队列元素类型：MQTT 回调转投的事件，或业务层提交的发送请求。 */
typedef enum
{
    CLOUD_Q_EVENT = 0, /*!< MQTT 事件（evt_arg 由 MQTT 层分配，由本模块释放） */
    CLOUD_Q_PUBLISH,   /*!< 业务层提交的发送请求（topic/payload 为堆内存副本） */
} cloud_q_kind_e;

/**
 * @struct cloud_q_item_t
 * @brief 云通道任务队列元素。
 */
typedef struct
{
    cloud_q_kind_e kind;     /*!< 元素类型 */
    int            event_id; /*!< MQTT 事件 ID，仅 CLOUD_Q_EVENT 有效 */
    void          *evt_arg;  /*!< MQTT 事件参数，仅 CLOUD_Q_EVENT 有效 */
    char          *topic;    /*!< 目标 Topic，仅 CLOUD_Q_PUBLISH 有效 */
    char          *payload;  /*!< 载荷，仅 CLOUD_Q_PUBLISH 有效 */
} cloud_q_item_t;

/**
 * @struct cloud_ctx_t
 * @brief 云通道运行时上下文。
 */
typedef struct
{
    qosa_uint8_t      client_idx;                   /*!< MQTT 客户端句柄 */
    qosa_bool_t       client_created;               /*!< 客户端是否已创建 */
    qosa_bool_t       connected;                    /*!< 是否已 CONNECT 成功 */
    int               msg_id;                       /*!< MQTT 报文标识 */
    qosa_msgq_t       msgq;                         /*!< 事件/发送请求队列 */
    car_cloud_rx_cb_t rx_cb;                        /*!< 下行报文回调 */
    char              client_id[QOSA_ARRAY_BYTE_512]; /*!< 拼接后的 ClientId */
    char              username[QOSA_ARRAY_BYTE_128];  /*!< 用户名 */
    char              password[QOSA_ARRAY_BYTE_128];  /*!< 签名密码 */
} cloud_ctx_t;

static cloud_ctx_t g_cloud_ctx = {0};

/**
 * @brief 按阿里云 IoT 规则生成 MQTT 登录三要素（HMAC-SHA1 签名）。
 *
 * @return int 0 表示成功，-1 表示内存不足。
 */
static int build_login_info(void)
{
    qosa_time_t   tv_sec = 0;
    char         *content = QOSA_NULL;
    char          tv_sec_buf[65] = {0};
    unsigned char digest[20] = {0};
    int           i = 0;

    tv_sec = qosa_get_system_time_seconds();
    tv_sec &= 0x7FFFFFFFFFFFFFFF;

    content = qosa_malloc(QOSA_ARRAY_BYTE_1024);
    if (content == QOSA_NULL)
    {
        return -1;
    }
    qosa_memset(content, 0, QOSA_ARRAY_BYTE_1024);

    qcm_utils_int64_to_text((qosa_int64_t)tv_sec, tv_sec_buf, 10);
    qosa_snprintf(content,
                  QOSA_ARRAY_BYTE_1024,
                  "clientId%sdeviceName%sproductKey%stimestamp%s",
                  CAR_CLOUD_CLIENT_ID_RAW,
                  CAR_CLOUD_DEVICE_NAME,
                  CAR_CLOUD_PRODUCT_KEY,
                  tv_sec_buf);

    qosa_snprintf(g_cloud_ctx.client_id,
                  sizeof(g_cloud_ctx.client_id),
                  "%s|securemode=3,signmethod=hmacsha1,timestamp=%s|",
                  CAR_CLOUD_CLIENT_ID_RAW,
                  tv_sec_buf);
    qosa_snprintf(g_cloud_ctx.username,
                  sizeof(g_cloud_ctx.username),
                  "%s&%s",
                  CAR_CLOUD_DEVICE_NAME,
                  CAR_CLOUD_PRODUCT_KEY);

    qcm_hmac_sha1((unsigned char *)CAR_CLOUD_DEVICE_SECRET,
                  (int)qosa_strlen(CAR_CLOUD_DEVICE_SECRET),
                  (unsigned char *)content,
                  (int)qosa_strlen(content),
                  digest);
    qosa_free(content);

    qosa_memset(g_cloud_ctx.password, 0, sizeof(g_cloud_ctx.password));
    for (i = 0; i < (int)sizeof(digest); i++)
    {
        qosa_snprintf(g_cloud_ctx.password + (i * 2), sizeof(g_cloud_ctx.password) - (i * 2), "%02x", digest[i]);
    }

    CAR_LOGI("[cloud][step3] login info ready, clientid=%s", g_cloud_ctx.client_id);
    CAR_LOGD("[cloud][step3] username=%s", g_cloud_ctx.username);

    return 0;
}

/**
 * @brief 释放一条 MQTT 事件响应对象（含其内部 data）。
 *
 * @param[in] event_id MQTT 事件 ID.
 * @param[in] resp MQTT 通用响应对象，可为 QOSA_NULL.
 * @return void
 */
static void cloud_free_event(int event_id, qcm_mqtt_common_resp_t *resp)
{
    if (resp == QOSA_NULL)
    {
        return;
    }

    if (resp->data != QOSA_NULL)
    {
        /* SUBACK 的 qoss 由 MQTT 层单独分配，需要先释放再释放响应体。 */
        if (event_id == (int)QCM_MQTT_CLIENT_SUBACK_EVENT)
        {
            qcm_mqtt_suback_resp_t *suback = (qcm_mqtt_suback_resp_t *)resp->data;

            if (suback->qoss != QOSA_NULL)
            {
                qosa_free(suback->qoss);
                suback->qoss = QOSA_NULL;
            }
        }
        qosa_free(resp->data);
        resp->data = QOSA_NULL;
    }

    qosa_free(resp);
}

/**
 * @brief 释放一个队列元素占据的资源。
 *
 * @param[in,out] item 队列元素。
 * @return void
 */
static void cloud_item_free(cloud_q_item_t *item)
{
    if (item == QOSA_NULL)
    {
        return;
    }

    if (item->kind == CLOUD_Q_PUBLISH)
    {
        if (item->topic != QOSA_NULL)
        {
            qosa_free(item->topic);
        }
        if (item->payload != QOSA_NULL)
        {
            qosa_free(item->payload);
        }
    }
    else
    {
        cloud_free_event(item->event_id, (qcm_mqtt_common_resp_t *)item->evt_arg);
    }

    qosa_memset(item, 0, sizeof(*item));
}

/**
 * @brief MQTT 事件回调，仅做转投，不在回调上下文中处理业务。
 *
 * @param[in] event_id 事件 ID.
 * @param[in] evt_param 事件参数.
 * @param[in] user_param 用户参数，未使用.
 * @return void
 */
static void cloud_event_cb(qcm_mqtt_client_event_e event_id, void *evt_param, void *user_param)
{
    cloud_q_item_t item = {0};

    QOSA_UNUSED(user_param);

    if ((evt_param == QOSA_NULL) || (g_cloud_ctx.msgq == QOSA_NULL))
    {
        return;
    }

    item.kind = CLOUD_Q_EVENT;
    item.event_id = (int)event_id;
    item.evt_arg = evt_param;

    CAR_LOGD("[cloud][cb] event=%d", event_id);

    if (qosa_msgq_release(g_cloud_ctx.msgq, sizeof(item), (qosa_uint8_t *)&item, QOSA_NO_WAIT) != QOSA_OK)
    {
        /* 队列满时丢弃事件并释放 MQTT 层分配的对象，避免内存泄漏。 */
        cloud_free_event((int)event_id, (qcm_mqtt_common_resp_t *)evt_param);
        CAR_LOGE("[cloud][cb] event %d dropped", (int)event_id);
    }
}

/**
 * @brief 初始化并打开 MQTT 连接.
 *
 * @return int 0 表示已发起连接，-1 表示失败.
 */
static int cloud_open(void)
{
    qcm_mqtt_config_t option = {0};

    CAR_LOGI("[cloud][step1] open %s:%d", CAR_CLOUD_SERVER_ADDR, CAR_CLOUD_SERVER_PORT);

    if (qcm_mqtt_client_default_config(&option) != QCM_MQTT_RES_OK)
    {
        CAR_LOGE("[cloud][step1] default config failed");
        return -1;
    }

    option.version = QCM_MQTT_VERSION_V3_1_1;
    option.sim_id = CAR_CLOUD_SIM_ID;
    option.pdp_cid = CAR_CLOUD_PDP_CID;
    option.kalive_time = CAR_CLOUD_KEEP_ALIVE_S;
    option.delivery_time = CAR_CLOUD_DELIVERY_TIME_S;
    option.delivery_cnt = CAR_CLOUD_DELIVERY_CNT;
    option.clean_session = QOSA_TRUE;

    if (g_cloud_ctx.client_created == QOSA_FALSE)
    {
        g_cloud_ctx.client_idx = qcm_mqtt_client_create();
        if (g_cloud_ctx.client_idx == 0)
        {
            CAR_LOGE("[cloud][step1] client create failed");
            return -1;
        }
        g_cloud_ctx.client_created = QOSA_TRUE;
        CAR_LOGI("[cloud][step1] client created, idx=%d", g_cloud_ctx.client_idx);
    }

    if (qcm_mqtt_client_init(g_cloud_ctx.client_idx, &option, cloud_event_cb, QOSA_NULL) != QCM_MQTT_RES_OK)
    {
        CAR_LOGE("[cloud][step1] client init failed");
        return -1;
    }

    if (qcm_mqtt_client_open(g_cloud_ctx.client_idx, CAR_CLOUD_SERVER_ADDR, CAR_CLOUD_SERVER_PORT) != QCM_MQTT_RES_OK)
    {
        CAR_LOGE("[cloud][step1] client open failed");
        return -1;
    }

    CAR_LOGI("[cloud][step1] open requested, waiting open event");
    return 0;
}

/**
 * @brief 发起 MQTT CONNECT.
 *
 * @return void
 */
static void cloud_connect(void)
{
    if (build_login_info() != 0)
    {
        CAR_LOGE("[cloud][step3] login info build failed");
        return;
    }

    if (qcm_mqtt_client_connect(g_cloud_ctx.client_idx,
                                g_cloud_ctx.client_id,
                                (int)qosa_strlen(g_cloud_ctx.client_id),
                                g_cloud_ctx.username,
                                (int)qosa_strlen(g_cloud_ctx.username),
                                g_cloud_ctx.password,
                                (int)qosa_strlen(g_cloud_ctx.password)) != QCM_MQTT_RES_OK)
    {
        CAR_LOGE("[cloud][step3] connect request failed");
        return;
    }

    CAR_LOGI("[cloud][step3] connect requested, waiting connect event");
}

/**
 * @brief 订阅下行指令 Topic.
 *
 * @return void
 */
static void cloud_subscribe(void)
{
    qcm_mqtt_sub_topic_t  topic = {0};
    qcm_mqtt_sub_config_t sub_info = {0};

    if (qcm_mqtt_malloc_data(&topic.topic, (qosa_uint32_t)qosa_strlen(CAR_CLOUD_TOPIC_DOWNLINK), CAR_CLOUD_TOPIC_DOWNLINK) !=
        QCM_MQTT_RES_OK)
    {
        CAR_LOGE("[cloud][step4] topic alloc failed");
        return;
    }
    topic.qos = QCM_MQTT_AT_LEAST_ONCE_DELIVERY;

    sub_info.msg_id = g_cloud_ctx.msg_id++;
    sub_info.topics = &topic;
    sub_info.topic_cnt = 1;

    if (qcm_mqtt_client_subscribe(g_cloud_ctx.client_idx, &sub_info) != QCM_MQTT_RES_OK)
    {
        CAR_LOGE("[cloud][step4] subscribe request failed");
    }
    else
    {
        CAR_LOGI("[cloud][step4] subscribe requested, topic=%s", CAR_CLOUD_TOPIC_DOWNLINK);
    }

    qcm_mqtt_free_data(&topic.topic);
}

/**
 * @brief 立即向指定 Topic 发布一条文本消息（调用方必须处于云通道任务上下文）。
 *
 * @param[in] tag 日志前缀，用于区分发布场景.
 * @param[in] topic 目标 Topic.
 * @param[in] payload 以 '\0' 结尾的载荷.
 * @return void
 */
static void cloud_publish_now(const char *tag, const char *topic, const char *payload)
{
    qcm_mqtt_pub_config_t pub_info = {0};

    pub_info.msg_id = g_cloud_ctx.msg_id++;
    pub_info.qos = QCM_MQTT_AT_LEAST_ONCE_DELIVERY;
    pub_info.retain = QOSA_FALSE;
    pub_info.topic.data_ptr = (char *)topic;
    pub_info.topic.data_len = (qosa_uint32_t)qosa_strlen(topic);
    pub_info.payload.data_ptr = (char *)payload;
    pub_info.payload.data_len = (qosa_uint32_t)qosa_strlen(payload);

    if (qcm_mqtt_client_publish(g_cloud_ctx.client_idx, &pub_info) != QCM_MQTT_RES_OK)
    {
        CAR_LOGE("[cloud][%s] publish failed, topic=%s", tag, topic);
        return;
    }

    CAR_LOGI("[cloud][%s] publish topic=%s payload=%s", tag, topic, payload);
}

/**
 * @brief 连接建立后向上行 Topic 发布一次上线通知.
 *
 * @return void
 */
static void cloud_publish_online(void)
{
    char payload[CAR_CLOUD_ONLINE_JSON_MAX] = {0};

    qosa_snprintf(payload, sizeof(payload), "{\"event\":\"online\",\"device\":\"%s\"}", CAR_CLOUD_DEVICE_NAME);

    cloud_publish_now("online", CAR_CLOUD_TOPIC_UPLINK, payload);
}

/**
 * @brief 处理一条队列中的发送请求.
 *
 * @param[in] item 发送请求元素.
 * @return void
 */
static void cloud_handle_publish(cloud_q_item_t *item)
{
    if (g_cloud_ctx.connected == QOSA_FALSE)
    {
        CAR_LOGW("[cloud][tx] link down, drop publish topic=%s", (item->topic == QOSA_NULL) ? "?" : item->topic);
        return;
    }

    cloud_publish_now("tx", item->topic, item->payload);
}

/**
 * @brief 判断收到的 Topic 是否与期望值一致.
 *
 * SDK 回传的 data_len 可能包含结尾符，因此按实际字符串长度比较.
 *
 * @param[in] topic 收到的 Topic 数据块.
 * @param[in] expect 期望的 Topic 字符串.
 * @return qosa_bool_t QOSA_TRUE 表示匹配.
 */
static qosa_bool_t topic_match(const qcm_mqtt_data_t *topic, const char *expect)
{
    size_t expect_len = 0;
    size_t actual_len = 0;

    if ((topic == QOSA_NULL) || (topic->data_ptr == QOSA_NULL))
    {
        return QOSA_FALSE;
    }

    expect_len = strlen(expect);
    while ((actual_len < topic->data_len) && (topic->data_ptr[actual_len] != '\0'))
    {
        actual_len++;
    }

    if (actual_len != expect_len)
    {
        return QOSA_FALSE;
    }

    return (memcmp(expect, topic->data_ptr, expect_len) == 0) ? QOSA_TRUE : QOSA_FALSE;
}

/**
 * @brief 读取并分发新到达的订阅消息.
 *
 * @param[in] resp_ptr MQTT 通用响应对象.
 * @return void
 */
static void cloud_read_new_message(qcm_mqtt_common_resp_t *resp_ptr)
{
    qcm_mqtt_new_msg_notify_t *notify = (qcm_mqtt_new_msg_notify_t *)resp_ptr->data;
    qcm_mqtt_recv_pub_t        recv_pub = {0};
    char                      *payload = QOSA_NULL;

    if (notify == QOSA_NULL)
    {
        return;
    }

    if (qcm_mqtt_client_read_subcribe_message(resp_ptr->client_id, notify->store_id, &recv_pub) != QCM_MQTT_RES_OK)
    {
        CAR_LOGE("[cloud][rx] read store_id=%d failed", notify->store_id);
        return;
    }

    CAR_LOGI("[cloud][rx] topic=%s topic_len=%d payload_len=%d",
          (recv_pub.topic.data_ptr == QOSA_NULL) ? "?" : recv_pub.topic.data_ptr,
          (int)recv_pub.topic.data_len,
          (int)recv_pub.payload.data_len);

    if ((recv_pub.payload.data_ptr == QOSA_NULL) || (recv_pub.payload.data_len == 0) ||
        (recv_pub.payload.data_len >= CAR_CLOUD_RX_PAYLOAD_MAX) || (g_cloud_ctx.rx_cb == QOSA_NULL))
    {
        if (recv_pub.payload.data_len >= CAR_CLOUD_RX_PAYLOAD_MAX)
        {
            CAR_LOGW("[cloud][rx] payload too long, len=%d", (int)recv_pub.payload.data_len);
        }
        qcm_mqtt_free_data(&recv_pub.topic);
        qcm_mqtt_free_data(&recv_pub.payload);
        return;
    }

    /* 回调要求以 '\0' 结尾，MQTT 层只保证二进制长度。 */
    payload = qosa_malloc(recv_pub.payload.data_len + 1);
    if (payload == QOSA_NULL)
    {
        CAR_LOGE("[cloud][rx] no memory for payload");
        qcm_mqtt_free_data(&recv_pub.topic);
        qcm_mqtt_free_data(&recv_pub.payload);
        return;
    }
    qosa_memcpy(payload, recv_pub.payload.data_ptr, recv_pub.payload.data_len);
    payload[recv_pub.payload.data_len] = '\0';

    if (topic_match(&recv_pub.topic, CAR_CLOUD_TOPIC_DOWNLINK) != QOSA_TRUE)
    {
        CAR_LOGW("[cloud][rx] unexpected topic, still dispatched");
    }

    g_cloud_ctx.rx_cb((recv_pub.topic.data_ptr == QOSA_NULL) ? "" : recv_pub.topic.data_ptr,
                      payload,
                      recv_pub.payload.data_len);

    qosa_free(payload);
    qcm_mqtt_free_data(&recv_pub.topic);
    qcm_mqtt_free_data(&recv_pub.payload);
}

/**
 * @brief 分发一条 MQTT 事件.
 *
 * @param[in] item 队列元素.
 * @return int 0 表示连接可继续，-1 表示需要重连.
 */
static int cloud_dispatch(const cloud_q_item_t *item)
{
    qcm_mqtt_common_resp_t *resp_ptr = (qcm_mqtt_common_resp_t *)item->evt_arg;
    int                     ret = 0;

    if (resp_ptr == QOSA_NULL)
    {
        return 0;
    }

    switch ((qcm_mqtt_client_event_e)item->event_id)
    {
        case QCM_MQTT_CLIENT_OPEN_EVENT:
            CAR_LOGI("[cloud][step2] open event, result=%x", resp_ptr->result);
            if (resp_ptr->result == QCM_MQTT_RES_OK)
            {
                cloud_connect();
            }
            else
            {
                ret = -1;
            }
            break;

        case QCM_MQTT_CLIENT_CONNECT_EVENT:
            CAR_LOGI("[cloud][step3] connect event, result=%x code=%d", resp_ptr->result, resp_ptr->pocotrol_code);
            if (resp_ptr->result == QCM_MQTT_RES_OK)
            {
                g_cloud_ctx.connected = QOSA_TRUE;
                cloud_subscribe();
            }
            else
            {
                ret = -1;
            }
            break;

        case QCM_MQTT_CLIENT_SUBACK_EVENT:
            CAR_LOGI("[cloud][step5] suback, result=%x", resp_ptr->result);
            if (resp_ptr->result == QCM_MQTT_RES_OK)
            {
                cloud_publish_online();
                CAR_LOGI("[cloud][ready] channel is up, waiting downlink commands");
            }
            break;

        case QCM_MQTT_CLIENT_PUBLISH_EVENT:
            CAR_LOGD("[cloud][pub] publish event, msg_id=%d result=%x", resp_ptr->msg_id, resp_ptr->result);
            break;

        case QCM_MQTT_CLIENT_NEW_MESSAGE_EVENT:
            cloud_read_new_message(resp_ptr);
            break;

        case QCM_MQTT_CLIENT_STATE_EVENT:
        case QCM_MQTT_CLIENT_DISCONNECT_EVENT:
        case QCM_MQTT_CLIENT_CLOSE_EVENT:
            CAR_LOGW("[cloud][down] link down, event=%d result=%x", item->event_id, resp_ptr->result);
            ret = -1;
            break;

        default:
            CAR_LOGD("[cloud][evt] unhandled event=%d", item->event_id);
            break;
    }

    return ret;
}

/**
 * @brief 清空队列中的残留消息，避免重连后串扰.
 *
 * @return void
 */
static void cloud_drain_msgq(void)
{
    cloud_q_item_t item = {0};

    while (qosa_msgq_wait(g_cloud_ctx.msgq, (qosa_uint8_t *)&item, sizeof(item), QOSA_NO_WAIT) == QOSA_OK)
    {
        cloud_item_free(&item);
    }
}

/**
 * @brief 云通道任务：连接、订阅、收发消息，断线后自动重连.
 *
 * @param[in] argv 未使用.
 * @return void
 */
static void cloud_task(void *argv)
{
    cloud_q_item_t item = {0};
    qosa_uint32_t  idle_ms = 0;

    QOSA_UNUSED(argv);

    CAR_LOGI("[cloud][step0] task started, wait %ds for network", CAR_CLOUD_BOOT_DELAY_S);
    qosa_task_sleep_sec(CAR_CLOUD_BOOT_DELAY_S);

    while (1)
    {
        cloud_drain_msgq();

        if (cloud_open() != 0)
        {
            (void)qcm_mqtt_client_close(g_cloud_ctx.client_idx);
            CAR_LOGW("[cloud][retry] open failed, retry in %ds", CAR_CLOUD_RETRY_DELAY_S);
            qosa_task_sleep_sec(CAR_CLOUD_RETRY_DELAY_S);
            continue;
        }

        idle_ms = 0;
        while (1)
        {
            qosa_memset(&item, 0, sizeof(item));
            if (qosa_msgq_wait(g_cloud_ctx.msgq, (qosa_uint8_t *)&item, sizeof(item), CAR_CLOUD_EVENT_WAIT_MS) !=
                QOSA_OK)
            {
                /* 连接阶段长时间没有事件（CONNECT 未被应答）视为异常，主动重连。 */
                idle_ms += CAR_CLOUD_EVENT_WAIT_MS;
                if ((g_cloud_ctx.connected == QOSA_FALSE) && (idle_ms >= CAR_CLOUD_CONNECT_TIMEOUT_MS))
                {
                    CAR_LOGW("[cloud][retry] connect timeout after %dms", (int)idle_ms);
                    break;
                }
                if (g_cloud_ctx.connected == QOSA_TRUE)
                {
                    idle_ms = 0;
                }
                continue;
            }

            idle_ms = 0;

            if (item.kind == CLOUD_Q_PUBLISH)
            {
                cloud_handle_publish(&item);
                cloud_item_free(&item);
                continue;
            }

            if (cloud_dispatch(&item) != 0)
            {
                g_cloud_ctx.connected = QOSA_FALSE;
                cloud_item_free(&item);
                break;
            }

            cloud_item_free(&item);
        }

        g_cloud_ctx.connected = QOSA_FALSE;
        (void)qcm_mqtt_client_close(g_cloud_ctx.client_idx);
        cloud_drain_msgq();
        CAR_LOGW("[cloud][retry] link closed, reconnect in %ds", CAR_CLOUD_RETRY_DELAY_S);
        qosa_task_sleep_sec(CAR_CLOUD_RETRY_DELAY_S);
    }
}

/**
 * @brief 启动云通道（阿里云 MQTT）。
 *
 * @param[in] rx_cb 下行报文回调，可为 QOSA_NULL。
 * @return int 0 表示任务创建成功，非 0 表示失败。
 */
int car_cloud_init(car_cloud_rx_cb_t rx_cb)
{
    qosa_task_t task = QOSA_NULL;

    if (g_cloud_ctx.msgq != QOSA_NULL)
    {
        CAR_LOGW("[cloud][init] channel already initialized");
        return 0;
    }

    g_cloud_ctx.msg_id = 1;
    g_cloud_ctx.connected = QOSA_FALSE;
    g_cloud_ctx.rx_cb = rx_cb;

    if (qosa_msgq_create(&g_cloud_ctx.msgq, sizeof(cloud_q_item_t), CAR_CLOUD_MSGQ_DEPTH) != QOSA_OK)
    {
        CAR_LOGE("[cloud][init] msgq create failed");
        return -1;
    }

    if (qosa_task_create(&task,
                         CAR_CLOUD_TASK_STACK_SIZE,
                         CAR_CLOUD_TASK_PRIO,
                         "car_cloud",
                         cloud_task,
                         QOSA_NULL) != QOSA_OK)
    {
        CAR_LOGE("[cloud][init] task create failed");
        (void)qosa_msgq_delete(g_cloud_ctx.msgq);
        g_cloud_ctx.msgq = QOSA_NULL;
        return -1;
    }

    CAR_LOGI("[cloud][init] channel init done, sub=%s pub=%s", CAR_CLOUD_TOPIC_DOWNLINK, CAR_CLOUD_TOPIC_UPLINK);
    return 0;
}

/**
 * @brief 向指定 Topic 发布一条文本消息（异步入队）。
 *
 * @param[in] topic 目标 Topic。
 * @param[in] payload 以 '\0' 结尾的载荷。
 * @return int 0 表示已入队，-1 表示参数非法、队列无效或队列已满。
 */
int car_cloud_publish(const char *topic, const char *payload)
{
    cloud_q_item_t item = {0};
    size_t         topic_len = 0;
    size_t         payload_len = 0;

    if ((topic == QOSA_NULL) || (payload == QOSA_NULL) || (g_cloud_ctx.msgq == QOSA_NULL))
    {
        return -1;
    }

    topic_len = strlen(topic);
    payload_len = strlen(payload);
    if ((topic_len == 0) || (topic_len >= CAR_CLOUD_TOPIC_MAX) || (payload_len >= CAR_CLOUD_PUBLISH_MAX))
    {
        CAR_LOGW("[cloud][tx] invalid publish, topic_len=%d payload_len=%d", (int)topic_len, (int)payload_len);
        return -1;
    }

    item.kind = CLOUD_Q_PUBLISH;
    item.topic = qosa_malloc(topic_len + 1);
    item.payload = qosa_malloc(payload_len + 1);
    if ((item.topic == QOSA_NULL) || (item.payload == QOSA_NULL))
    {
        CAR_LOGE("[cloud][tx] no memory for publish");
        if (item.topic != QOSA_NULL)
        {
            qosa_free(item.topic);
        }
        if (item.payload != QOSA_NULL)
        {
            qosa_free(item.payload);
        }
        return -1;
    }

    qosa_memcpy(item.topic, topic, topic_len + 1);
    qosa_memcpy(item.payload, payload, payload_len + 1);

    if (qosa_msgq_release(g_cloud_ctx.msgq, sizeof(item), (qosa_uint8_t *)&item, QOSA_NO_WAIT) != QOSA_OK)
    {
        CAR_LOGW("[cloud][tx] queue full, publish dropped");
        qosa_free(item.topic);
        qosa_free(item.payload);
        return -1;
    }

    return 0;
}

/**
 * @brief 向默认上行 Topic 发布一条文本消息。
 *
 * @param[in] payload 以 '\0' 结尾的载荷。
 * @return int 0 表示已入队，-1 表示失败。
 */
int car_cloud_publish_uplink(const char *payload)
{
    return car_cloud_publish(CAR_CLOUD_TOPIC_UPLINK, payload);
}

/**
 * @brief 查询云通道当前是否已连接。
 *
 * @return int 1 表示已连接，0 表示未连接。
 */
int car_cloud_is_connected(void)
{
    return (g_cloud_ctx.connected == QOSA_TRUE) ? 1 : 0;
}

#else /* !CONFIG_QCM_MQTT_FUNC */

int car_cloud_init(car_cloud_rx_cb_t rx_cb)
{
    QOSA_UNUSED(rx_cb);
    CAR_LOGW("[cloud][init] MQTT client functionality is disabled (CONFIG_QCM_MQTT_FUNC=n)");
    return -1;
}

int car_cloud_publish(const char *topic, const char *payload)
{
    QOSA_UNUSED(topic);
    QOSA_UNUSED(payload);
    return -1;
}

int car_cloud_publish_uplink(const char *payload)
{
    QOSA_UNUSED(payload);
    return -1;
}

int car_cloud_is_connected(void)
{
    return 0;
}

#endif /* CONFIG_QCM_MQTT_FUNC */
