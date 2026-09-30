#include <string.h>

#include "qosa_def.h"
#include "qosa_sys.h"
#include "qosa_log.h"

#define QOS_LOG_TAG LOG_TAG_DEMO

#include "alarm_app.h"
#include "alarm_cloud.h"
#include "alarm_config.h"


#include "qcm_mqtt.h"
#include "qcm_mqtt_config.h"
#include "qcm_sha1.h"
#include "qcm_utils.h"
#include "qosa_rtc.h"

#ifdef CONFIG_QCM_MQTT_FUNC

/**
 * @struct cloud_msg_t
 * @brief MQTT 回调转投到本模块任务的消息体。
 */
typedef struct
{
    int   msg_id; /*!< MQTT 事件 ID */
    void *argv;   /*!< 事件参数指针 */
} cloud_msg_t;

/**
 * @struct cloud_ctx_t
 * @brief 云配置通道运行时上下文。
 */
typedef struct
{
    qosa_uint8_t client_idx;                /*!< MQTT 客户端句柄 */
    qosa_bool_t  client_created;            /*!< 客户端是否已创建 */
    int          msg_id;                    /*!< MQTT 报文标识 */
    qosa_msgq_t  msgq;                      /*!< 事件队列 */
    char         client_id[QOSA_ARRAY_BYTE_512]; /*!< 阿里云拼接后的 ClientId */
    char         username[QOSA_ARRAY_BYTE_128];  /*!< 阿里云用户名 */
    char         password[QOSA_ARRAY_BYTE_128];  /*!< 阿里云签名密码 */
} cloud_ctx_t;

static cloud_ctx_t g_cloud_ctx = {0};

/**
 * @brief 生成阿里云 MQTT 登录三要素。
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

    qcm_utils_int64_to_text(tv_sec, tv_sec_buf, 10);
    qosa_snprintf(content,
                  QOSA_ARRAY_BYTE_1024,
                  "clientId%sdeviceName%sproductKey%stimestamp%s",
                  ALARM_MQTT_CLIENT_ID,
                  ALARM_MQTT_DEVICE_NAME,
                  ALARM_MQTT_PRODUCT_KEY,
                  tv_sec_buf);

    qosa_snprintf(g_cloud_ctx.client_id,
                  sizeof(g_cloud_ctx.client_id),
                  "%s|securemode=3,signmethod=hmacsha1,timestamp=%s|",
                  ALARM_MQTT_CLIENT_ID,
                  tv_sec_buf);
    qosa_snprintf(g_cloud_ctx.username,
                  sizeof(g_cloud_ctx.username),
                  "%s&%s",
                  ALARM_MQTT_DEVICE_NAME,
                  ALARM_MQTT_PRODUCT_KEY);

    qcm_hmac_sha1((unsigned char *)ALARM_MQTT_DEVICE_SECRET,
                  qosa_strlen(ALARM_MQTT_DEVICE_SECRET),
                  (unsigned char *)content,
                  qosa_strlen(content),
                  digest);
    qosa_free(content);

    qosa_memset(g_cloud_ctx.password, 0, sizeof(g_cloud_ctx.password));
    for (i = 0; i < (int)sizeof(digest); i++)
    {
        qosa_snprintf(g_cloud_ctx.password + (i * 2), sizeof(g_cloud_ctx.password) - (i * 2), "%02x", digest[i]);
    }

    QLOGI("[cloud][step3] login info ready, clientid=%s", g_cloud_ctx.client_id);
    QLOGD("[cloud][step3] username=%s", g_cloud_ctx.username);

    return 0;
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
    cloud_msg_t msg = {0};

    QOSA_UNUSED(user_param);

    if ((evt_param == QOSA_NULL) || (g_cloud_ctx.msgq == QOSA_NULL))
    {
        return;
    }

    msg.msg_id = (int)event_id;
    msg.argv = evt_param;

    QLOGD("[cloud][cb] event=%d", event_id);

    if (qosa_msgq_release(g_cloud_ctx.msgq, sizeof(msg), (qosa_uint8_t *)&msg, QOSA_NO_WAIT) != QOSA_OK)
    {
        QLOGE("[cloud][cb] event %d dropped", event_id);
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

    QLOGI("[cloud][step1] open %s:%d", ALARM_MQTT_SERVER_ADDR, ALARM_MQTT_SERVER_PORT);

    if (qcm_mqtt_client_default_config(&option) != QCM_MQTT_RES_OK)
    {
        QLOGE("[cloud][step1] default config failed");
        return -1;
    }

    option.version = QCM_MQTT_VERSION_V3_1_1;
    option.sim_id = ALARM_SIM_ID;
    option.pdp_cid = ALARM_MQTT_PDP_CID;
    option.kalive_time = ALARM_MQTT_KEEP_ALIVE_S;
    option.delivery_time = ALARM_MQTT_DELIVERY_TIME_S;
    option.delivery_cnt = ALARM_MQTT_DELIVERY_CNT;
    option.clean_session = QOSA_TRUE;

    if (g_cloud_ctx.client_created == QOSA_FALSE)
    {
        g_cloud_ctx.client_idx = qcm_mqtt_client_create();
        g_cloud_ctx.client_created = QOSA_TRUE;
        QLOGI("[cloud][step1] client created, idx=%d", g_cloud_ctx.client_idx);
    }

    if (qcm_mqtt_client_init(g_cloud_ctx.client_idx, &option, cloud_event_cb, QOSA_NULL) != QCM_MQTT_RES_OK)
    {
        QLOGE("[cloud][step1] client init failed");
        return -1;
    }

    if (qcm_mqtt_client_open(g_cloud_ctx.client_idx, ALARM_MQTT_SERVER_ADDR, ALARM_MQTT_SERVER_PORT) != QCM_MQTT_RES_OK)
    {
        QLOGE("[cloud][step1] client open failed");
        return -1;
    }

    QLOGI("[cloud][step1] open requested, waiting open event");
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
        QLOGE("[cloud][step3] login info build failed");
        return;
    }

    if (qcm_mqtt_client_connect(g_cloud_ctx.client_idx,
                                g_cloud_ctx.client_id,
                                qosa_strlen(g_cloud_ctx.client_id),
                                g_cloud_ctx.username,
                                qosa_strlen(g_cloud_ctx.username),
                                g_cloud_ctx.password,
                                qosa_strlen(g_cloud_ctx.password)) != QCM_MQTT_RES_OK)
    {
        QLOGE("[cloud][step3] connect request failed");
        return;
    }

    QLOGI("[cloud][step3] connect requested, waiting connect event");
}

/**
 * @brief 订阅联系人配置下发 Topic.
 *
 * @return void
 */
static void cloud_subscribe(void)
{
    qcm_mqtt_sub_topic_t  topic = {0};
    qcm_mqtt_sub_config_t sub_info = {0};

    qcm_mqtt_malloc_data(&topic.topic, qosa_strlen(ALARM_MQTT_TOPIC_CONTACTS), ALARM_MQTT_TOPIC_CONTACTS);
    topic.qos = QCM_MQTT_AT_LEAST_ONCE_DELIVERY;

    sub_info.msg_id = g_cloud_ctx.msg_id++;
    sub_info.topics = &topic;
    sub_info.topic_cnt = 1;

    if (qcm_mqtt_client_subscribe(g_cloud_ctx.client_idx, &sub_info) != QCM_MQTT_RES_OK)
    {
        QLOGE("[cloud][step4] subscribe request failed");
    }
    else
    {
        QLOGI("[cloud][step4] subscribe requested, topic=%s", ALARM_MQTT_TOPIC_CONTACTS);
    }

    qcm_mqtt_free_data(&topic.topic);
}

/**
 * @brief 向指定 Topic 发布一条文本消息.
 *
 * @param[in] tag 日志前缀，用于区分发布场景.
 * @param[in] topic 目标 Topic.
 * @param[in] payload 以 '\0' 结尾的载荷.
 * @return void
 */
static void cloud_publish(const char *tag, const char *topic, const char *payload)
{
    qcm_mqtt_pub_config_t pub_info = {0};

    pub_info.msg_id = g_cloud_ctx.msg_id++;
    pub_info.qos = QCM_MQTT_AT_LEAST_ONCE_DELIVERY;
    pub_info.retain = QOSA_FALSE;
    pub_info.topic.data_ptr = (char *)topic;
    pub_info.topic.data_len = qosa_strlen(topic);
    pub_info.payload.data_ptr = (char *)payload;
    pub_info.payload.data_len = qosa_strlen(payload);

    if (qcm_mqtt_client_publish(g_cloud_ctx.client_idx, &pub_info) != QCM_MQTT_RES_OK)
    {
        QLOGE("[cloud][%s] publish failed, topic=%s", tag, topic);
        return;
    }

    QLOGI("[cloud][%s] publish topic=%s payload=%s", tag, topic, payload);
}

/**
 * @brief 上电完成后向云端通知设备已上线.
 *
 * @return void
 */
static void cloud_publish_online(void)
{
    char                   payload[ALARM_MQTT_ONLINE_JSON_MAX] = {0};
    const alarm_contact_t *list = QOSA_NULL;
    qosa_uint8_t           count = 0;
    qosa_uint32_t          used = 0;
    int                    written = 0;
    qosa_uint8_t           i = 0;

    list = alarm_contact_store_get(&count);

    written = qosa_snprintf(payload,
                            sizeof(payload),
                            "{\"event\":\"online\",\"device\":\"%s\",\"count\":%d,\"contacts\":[",
                            ALARM_MQTT_DEVICE_NAME,
                            count);
    if (written < 0)
    {
        return;
    }
    used = (qosa_uint32_t)written;

    for (i = 0; (i < count) && (used < sizeof(payload)); i++)
    {
        written = qosa_snprintf(payload + used,
                                sizeof(payload) - used,
                                "%s{\"name\":\"%s\",\"phone\":\"%s\"}",
                                (i == 0) ? "" : ",",
                                list[i].name,
                                list[i].phone_number);
        if (written < 0)
        {
            return;
        }
        used += (qosa_uint32_t)written;
    }

    if (used >= sizeof(payload))
    {
        QLOGE("[cloud][step5] online payload truncated");
        return;
    }

    (void)qosa_snprintf(payload + used, sizeof(payload) - used, "]}");

    cloud_publish("step5", ALARM_MQTT_TOPIC_STATUS, payload);
}

/**
 * @brief 向云端发布配置处理回执.
 *
 * @param[in] ok 是否处理成功.
 * @param[in] count 生效的联系人数.
 * @param[in] persisted 是否已成功写入 NV.
 * @param[in] reason 失败原因，成功时可为 QOSA_NULL.
 * @return void
 */
static void cloud_publish_ack(qosa_bool_t ok, qosa_uint8_t count, qosa_bool_t persisted, const char *reason)
{
    char payload[ALARM_MQTT_ACK_JSON_MAX] = {0};

    if (ok == QOSA_TRUE)
    {
        qosa_snprintf(payload,
                      sizeof(payload),
                      "{\"result\":\"OK\",\"count\":%d,\"persisted\":%d}",
                      count,
                      (persisted == QOSA_TRUE) ? 1 : 0);
    }
    else
    {
        qosa_snprintf(payload, sizeof(payload), "{\"result\":\"FAIL\",\"reason\":\"%s\"}", (reason == QOSA_NULL) ? "unknown" : reason);
    }

    cloud_publish("ack", ALARM_MQTT_TOPIC_ACK, payload);
}

/**
 * @brief 处理一条联系人配置下发报文.
 *
 * @param[in] payload 报文内容，不保证以 '\0' 结尾.
 * @param[in] payload_len 报文长度.
 * @return void
 */
static void cloud_handle_contacts_payload(const char *payload, qosa_uint32_t payload_len)
{
    alarm_contact_t  parsed[ALARM_CONTACT_MAX] = {0};
    alarm_event_t    event = {0};
    qosa_uint8_t     count = 0;
    const char      *reason = QOSA_NULL;
    char            *json = QOSA_NULL;
    int              store_ret = 0;

    if ((payload == QOSA_NULL) || (payload_len == 0) || (payload_len >= ALARM_CONTACT_JSON_MAX))
    {
        QLOGW("[cloud][recv] bad payload len=%d", payload_len);
        cloud_publish_ack(QOSA_FALSE, 0, QOSA_FALSE, "payload_size");
        return;
    }

    json = qosa_malloc(payload_len + 1);
    if (json == QOSA_NULL)
    {
        cloud_publish_ack(QOSA_FALSE, 0, QOSA_FALSE, "no_memory");
        return;
    }
    qosa_memcpy(json, payload, payload_len);
    json[payload_len] = '\0';

    QLOGI("[cloud][recv] contacts payload=%s", json);

    if (alarm_contacts_parse_json(json, parsed, &count, &reason) != 0)
    {
        QLOGW("[cloud][recv] contacts rejected, reason=%s", reason);
        qosa_free(json);
        cloud_publish_ack(QOSA_FALSE, 0, QOSA_FALSE, reason);
        return;
    }
    qosa_free(json);

    store_ret = alarm_contact_store_update(parsed, count);
    if (store_ret < 0)
    {
        QLOGE("[cloud][recv] contacts store update failed");
        cloud_publish_ack(QOSA_FALSE, 0, QOSA_FALSE, "store_error");
        return;
    }

    event.type = ALARM_EVENT_CONTACTS_UPDATED;
    (void)alarm_post_event(&event);

    QLOGI("[cloud][recv] contacts accepted, count=%d persisted=%d", count, (store_ret == 0) ? 1 : 0);
    cloud_publish_ack(QOSA_TRUE, count, (store_ret == 0) ? QOSA_TRUE : QOSA_FALSE, QOSA_NULL);
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

    if (notify == QOSA_NULL)
    {
        return;
    }

    if (qcm_mqtt_client_read_subcribe_message(resp_ptr->client_id, notify->store_id, &recv_pub) != QCM_MQTT_RES_OK)
    {
        QLOGE("[cloud][recv] read store_id=%d failed", notify->store_id);
        return;
    }

    QLOGI("[cloud][recv] topic=%s topic_len=%d payload_len=%d",
          recv_pub.topic.data_ptr,
          recv_pub.topic.data_len,
          recv_pub.payload.data_len);

    if (topic_match(&recv_pub.topic, ALARM_MQTT_TOPIC_CONTACTS) == QOSA_TRUE)
    {
        cloud_handle_contacts_payload(recv_pub.payload.data_ptr, recv_pub.payload.data_len);
    }
    else
    {
        QLOGD("[cloud][recv] topic not handled");
    }

    qcm_mqtt_free_data(&recv_pub.topic);
    qcm_mqtt_free_data(&recv_pub.payload);
}

/**
 * @brief 分发一条 MQTT 事件.
 *
 * @param[in] msg 事件消息.
 * @return int 0 表示连接可继续，-1 表示需要重连.
 */
static int cloud_dispatch(const cloud_msg_t *msg)
{
    qcm_mqtt_common_resp_t *resp_ptr = (qcm_mqtt_common_resp_t *)msg->argv;
    int                     ret = 0;

    if (resp_ptr == QOSA_NULL)
    {
        return 0;
    }

    switch (msg->msg_id)
    {
        case QCM_MQTT_CLIENT_OPEN_EVENT:
            QLOGI("[cloud][step2] open event, result=%x", resp_ptr->result);
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
            QLOGI("[cloud][step3] connect event, result=%x code=%d", resp_ptr->result, resp_ptr->pocotrol_code);
            if (resp_ptr->result == QCM_MQTT_RES_OK)
            {
                cloud_subscribe();
            }
            else
            {
                ret = -1;
            }
            break;

        case QCM_MQTT_CLIENT_SUBACK_EVENT:
        {
            qcm_mqtt_suback_resp_t *suback = (qcm_mqtt_suback_resp_t *)resp_ptr->data;
            if ((suback != QOSA_NULL) && (suback->qoss != QOSA_NULL))
            {
                qosa_free(suback->qoss);
            }
            QLOGI("[cloud][step5] suback, result=%x", resp_ptr->result);
            if (resp_ptr->result == QCM_MQTT_RES_OK)
            {
                cloud_publish_online();
                QLOGI("[cloud][ready] channel is up, waiting contacts config");
            }
            break;
        }

        case QCM_MQTT_CLIENT_PUBLISH_EVENT:
            QLOGD("[cloud][pub] publish event, msg_id=%d result=%x", resp_ptr->msg_id, resp_ptr->result);
            break;

        case QCM_MQTT_CLIENT_NEW_MESSAGE_EVENT:
            cloud_read_new_message(resp_ptr);
            break;

        case QCM_MQTT_CLIENT_STATE_EVENT:
        case QCM_MQTT_CLIENT_DISCONNECT_EVENT:
        case QCM_MQTT_CLIENT_CLOSE_EVENT:
            QLOGW("[cloud][down] link down, event=%d result=%x", msg->msg_id, resp_ptr->result);
            ret = -1;
            break;

        default:
            QLOGD("[cloud][evt] unhandled event=%d", msg->msg_id);
            break;
    }

    if (resp_ptr->data != QOSA_NULL)
    {
        qosa_free(resp_ptr->data);
    }
    qosa_free(resp_ptr);

    return ret;
}

/**
 * @brief 清空事件队列中的残留消息，避免重连后串扰.
 *
 * @return void
 */
static void cloud_drain_msgq(void)
{
    cloud_msg_t msg = {0};

    while (qosa_msgq_wait(g_cloud_ctx.msgq, (qosa_uint8_t *)&msg, sizeof(msg), QOSA_NO_WAIT) == QOSA_OK)
    {
        qcm_mqtt_common_resp_t *resp_ptr = (qcm_mqtt_common_resp_t *)msg.argv;
        if (resp_ptr != QOSA_NULL)
        {
            if (resp_ptr->data != QOSA_NULL)
            {
                qosa_free(resp_ptr->data);
            }
            qosa_free(resp_ptr);
        }
        qosa_memset(&msg, 0, sizeof(msg));
    }
}

/**
 * @brief 云配置通道任务：连接、订阅、收包，断线后自动重连.
 *
 * @param[in] argv 未使用.
 * @return void
 */
static void cloud_task(void *argv)
{
    cloud_msg_t msg = {0};

    QOSA_UNUSED(argv);

    QLOGI("[cloud][step0] task started, wait %ds for network", ALARM_MQTT_BOOT_DELAY_S);
    qosa_task_sleep_sec(ALARM_MQTT_BOOT_DELAY_S);

    while (1)
    {
        cloud_drain_msgq();

        if (cloud_open() != 0)
        {
            (void)qcm_mqtt_client_close(g_cloud_ctx.client_idx);
            QLOGW("[cloud][retry] open failed, retry in %ds", ALARM_MQTT_RETRY_DELAY_S);
            qosa_task_sleep_sec(ALARM_MQTT_RETRY_DELAY_S);
            continue;
        }

        while (1)
        {
            qosa_memset(&msg, 0, sizeof(msg));
            if (qosa_msgq_wait(g_cloud_ctx.msgq, (qosa_uint8_t *)&msg, sizeof(msg), QOSA_WAIT_FOREVER) != QOSA_OK)
            {
                continue;
            }

            if (cloud_dispatch(&msg) != 0)
            {
                break;
            }
        }

        (void)qcm_mqtt_client_close(g_cloud_ctx.client_idx);
        QLOGW("[cloud][retry] link closed, reconnect in %ds", ALARM_MQTT_RETRY_DELAY_S);
        qosa_task_sleep_sec(ALARM_MQTT_RETRY_DELAY_S);
    }
}

/**
 * @brief 启动云平台配置下发通道（阿里云 MQTT）。
 *
 * @return int 0 表示任务创建成功，非 0 表示失败。
 * @note 该模块不感知报警状态机内部，仅通过 alarm_post_event 与联系人存储对外交互。
 */
int alarm_cloud_init(void)
{
    qosa_task_t task = QOSA_NULL;

    if (g_cloud_ctx.msgq != QOSA_NULL)
    {
        QLOGW("[cloud][init] channel already initialized");
        return 0;
    }

    g_cloud_ctx.msg_id = 1;

    if (qosa_msgq_create(&g_cloud_ctx.msgq, sizeof(cloud_msg_t), ALARM_MQTT_MSGQ_DEPTH) != QOSA_OK)
    {
        QLOGE("[cloud][init] msgq create failed");
        return -1;
    }

    if (qosa_task_create(&task, ALARM_MQTT_TASK_STACK_SIZE, QOSA_PRIORITY_NORMAL, "alarm_cloud", cloud_task, QOSA_NULL) != QOSA_OK)
    {
        QLOGE("[cloud][init] task create failed");
        (void)qosa_msgq_delete(g_cloud_ctx.msgq);
        g_cloud_ctx.msgq = QOSA_NULL;
        return -1;
    }

    QLOGI("[cloud][init] channel init done, sub=%s ack=%s", ALARM_MQTT_TOPIC_CONTACTS, ALARM_MQTT_TOPIC_ACK);
    return 0;
}

#else

/**
 * @brief 启动云平台配置下发通道（占位实现）。
 *
 * @return int 固定返回 -1。
 * @note 未使能 CONFIG_QCM_MQTT_FUNC 时 MQTT 能力被裁剪，仅打印告警。
 */
int alarm_cloud_init(void)
{
    QLOGW("[cloud][init] MQTT client functionality is disabled");
    return -1;
}
#endif/* CONFIG_QCM_MQTT_FUNC */
