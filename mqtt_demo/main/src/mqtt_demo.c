/*****************************************************************/
/**
 * @file mqtt_demo.c
 * @brief MQTT Demo Application
 * @author lisa.yue@quectel.com
 * @date 2026-10-27
 *
 * @copyright Copyright (c) 2023 Quectel Wireless Solution, Co., Ltd.
 * All Rights Reserved. Quectel Wireless Solution Proprietary and Confidential.
 *
 * @par EDIT HISTORY FOR MODULE
 * <table>
 * <tr><th>Date <th>Version <th>Author <th>Description
 * <tr><td>2026-10-27 <td>1.0 <td>lisa.yue <td> Initial version
 * </table>
 **********************************************************************/

#include "qosa_sys.h"
#include "qosa_def.h"
#include "qosa_log.h"
#include "qcm_mqtt.h"
#include "qcm_mqtt_config.h"
#include "qcm_proj_config.h"
#include "unirtos_app_init_registry.h"

#include "cJSON.h"

/* TODO: Replace with the broker address, port, login information, and topics
 *
 */
#define MQTT_DEMO_SERVER_ADDR "101.37.104.185" /*!< MQTT server address */
#define MQTT_DEMO_SERVER_PORT 41321            /*!< MQTT server port */
#define MQTT_DEMO_CLIENTID "mqttx_12345678"    /*!< MQTT client unique identifier */
#define MQTT_DEMO_USERNAME "quectel"           /*!< MQTT connection username, empty string means no authentication */
#define MQTT_DEMO_PASSWARD "12345678"          /*!< MQTT connection password, empty string means no authentication */

#define MQTT_DEMO_TOPIC_SUB "mqtt_topic_sub" /*!< MQTT test topic for subscribing messages */

#define QOS_LOG_TAG LOG_TAG

#define MQTT_DEMO_TASK_STACK_SIZE 4096 /*!< MQTT demo task stack size in bytes */

#define MQTT_DEMO_MSG_MAX_CNT 8

#define MQTT_DEMO_CLIENT_ID 0     /*!< MQTT client ID */
#define MQTT_DEMO_SIMID 0         /*!< SIM ID */
#define MQTT_DEMO_PDPID 1         /*!< PDP context ID for network connection */
#define MQTT_DEMO_KEEP_ALIVE 15   /*!< Keep alive time in seconds */
#define MQTT_DEMO_DELIVERY_TIME 5 /*!< Retransmission interval in seconds */
#define MQTT_DEMO_DELIVERY_CNT 3  /*!< Retransmission count */

#define MQTT_DEMO_LEN_MAX 257

#define MQTT_DEMO_TOPIC_PUB "mqtt_topic_pub"                                      /*!< MQTT development board test topic for publishing messages */
#define MQTT_DEMO_FIRST_SUBSCRIBE_PAYLOAD "Hello Welcome to test mqtt subscribe!" /*!< Initial payload published after subscription succeeds */

#define MQTT_CLIENT_TIMER_PUBLISH_EVENT 0x70

/**
 * @brief MQTT command enumeration
 */
typedef enum
{
    SET_PUB_PERIOD = 0,
    SET_SUBSCRIBE_TOPIC,
    SET_UNSUBSCRIBE_TOPIC,
    DISCONNECT,
} mqtt_cmd_e;

/**
 * @brief MQTT command information structure
 *
 * @param cmd MQTT command "SET_PUB_PERIOD, SET_SUBSCRIBE_TOPIC, SET_UNSUBSCRIBE_TOPIC, DISCONNECT"
 * @param val_int Publish interval in seconds for SET_PUB_PERIOD, or QoS for SET_SUBSCRIBE_TOPIC
 * @param val_str String parameter: Topic for subscribing/unsubscribing topic
 *
 */
typedef struct
{
    mqtt_cmd_e cmd;
    int val_int;
    char val_str[64];
} mqtt_cmd_info_t;

/**
 * @brief mqtt_demo_msg_t structure definition
 *
 * @param msg_id Event ID for identifying different event types
 * @param param1 Data parameter 1 for passing integer data
 * @param argv Data parameter 2 for passing pointer type data
 */
typedef struct
{
    int msg_id; /*!< Event ID */
    int param1; /*!< Data */
    void *argv; /*!< Data */
} mqtt_demo_msg_t;

/**
 * @brief MQTT demo context structure
 */
typedef struct
{
    qosa_uint8_t client_id;      /*!< MQTT client handle */
    qosa_uint32_t port;          /*!< MQTT server port */
    int msg_id;                  /*!< Packet identifier */
    qosa_msgq_t msgq;            /*!< Message queue */
    qosa_timer_t publish_timer;  /*!< Timer for periodic publishing */
    qosa_uint32_t publish_count; /*!< Number of periodic publish timer events */

} mqtt_demo_ctx_t;

// Client context
static mqtt_demo_ctx_t *g_mqtt_ctx = QOSA_NULL;

/*===========================================================================
 *  Functions
 ===========================================================================*/

/**
 * @brief publish timer callback function
 *
 * @param user_param pointer to the publish count variable (qosa_uint32_t)
 */
static void unir_mqtt_publish_timer_cb(void *user_param)
{
    qosa_uint32_t *publish_count = (qosa_uint32_t *)user_param;
    (*publish_count)++;

    mqtt_demo_msg_t msg = {0};
    msg.msg_id = MQTT_CLIENT_TIMER_PUBLISH_EVENT;
    msg.param1 = *publish_count;
    qosa_msgq_release(g_mqtt_ctx->msgq, sizeof(mqtt_demo_msg_t), (qosa_uint8_t *)&msg, QOSA_NO_WAIT);
}

/**
 * @brief Parse MQTT command payload from JSON data
 *
 * @param data JSON payload string
 * @param data_len Length of the JSON payload
 * @param info Pointer to the structure to store parsed command information
 * @return int 0 on success, -1 on failure
 */
static int unir_mqtt_parse_cmd_payload(const char *data, qosa_uint32_t data_len, mqtt_cmd_info_t *info)
{
    cJSON *root = QOSA_NULL;
    cJSON *cmd_item = QOSA_NULL;
    cJSON *val_int_item = QOSA_NULL;
    cJSON *val_str_item = QOSA_NULL;
    int ret = -1;

    /* Parse payload and read the required command field. */
    root = cJSON_ParseWithLength(data, data_len);
    if (root == QOSA_NULL)
    {
        QLOGE("[mqtt_demo] Failed to parse JSON payload");
        goto cleanup;
    }

    cmd_item = cJSON_GetObjectItemCaseSensitive(root, "cmd");
    if (!cJSON_IsNumber(cmd_item))
    {
        QLOGE("[mqtt_demo] cmd field missing or not number");
        goto cleanup;
    }
    info->cmd = (mqtt_cmd_e)cJSON_GetNumberValue(cmd_item);

    /* Optional integer argument defaults to zero. */
    val_int_item = cJSON_GetObjectItemCaseSensitive(root, "val_int");
    if (cJSON_IsNumber(val_int_item))
    {
        info->val_int = (int)cJSON_GetNumberValue(val_int_item);
    }
    else
    {
        info->val_int = 0;
    }

    /* Validate command and its integer argument. */
    if (info->cmd < SET_PUB_PERIOD || info->cmd > DISCONNECT)
    {
        QLOGE("[mqtt_demo] Invalid cmd value: %d", info->cmd);
        goto cleanup;
    }

    if (info->cmd == SET_PUB_PERIOD && info->val_int == 0)
    {
        QLOGE("[mqtt_demo] invalid val_int value: %d", info->val_int);
        goto cleanup;
    }

    if (info->cmd == SET_SUBSCRIBE_TOPIC && (info->val_int < 0 || info->val_int > 2))
    {
        QLOGE("[mqtt_demo] Invalid val_int for SET_SUBSCRIBE_TOPIC, must be 0, 1, or 2");
        goto cleanup;
    }

    /* Copy the optional string argument when it fits the destination. */
    val_str_item = cJSON_GetObjectItemCaseSensitive(root, "val_str");
    if (cJSON_IsString(val_str_item) && (val_str_item->valuestring != NULL))
    {
        if (qosa_strlen(val_str_item->valuestring) >= sizeof(info->val_str))
        {
            QLOGE("[mqtt_demo] val_str too long");
            goto cleanup;
        }
        qosa_strncpy(info->val_str, val_str_item->valuestring, sizeof(info->val_str));
    }

    ret = 0; /* Success */
    goto cleanup;

cleanup:
    if (root != QOSA_NULL)
    {
        cJSON_Delete(root);
    }
    return ret;
}

/**
 * @brief Free MQTT demo context resources
 */
static void unir_mqtt_demo_ctx_free(void)
{
    // stop and delete publish timer if it exists
    if (g_mqtt_ctx->publish_timer != QOSA_NULL)
    {
        qosa_timer_stop(g_mqtt_ctx->publish_timer);
        qosa_timer_delete(g_mqtt_ctx->publish_timer);
        g_mqtt_ctx->publish_timer = QOSA_NULL;
    }

    /* Free message queue resources */
    if (g_mqtt_ctx->msgq != QOSA_NULL)
    {
        QLOGI("[mqtt_demo] Deleting message queue");
        qosa_msgq_delete(g_mqtt_ctx->msgq);
        g_mqtt_ctx->msgq = QOSA_NULL;
    }

    /* Free MQTT context structure */
    QLOGI("[mqtt_demo] Freeing MQTT context");
    qosa_free(g_mqtt_ctx);
    g_mqtt_ctx = QOSA_NULL;
}

/**
 * @brief Initialize MQTT demo context and configuration parameters
 *
 */
static int unir_mqtt_demo_ctx_init(qcm_mqtt_config_t *mqtt_option)
{
    int ret = 0;

    if (mqtt_option == QOSA_NULL)
    {
        return -1;
    }
    // Allocate the global context if it does not exist
    if (g_mqtt_ctx == QOSA_NULL)
    {
        g_mqtt_ctx = qosa_malloc(sizeof(mqtt_demo_ctx_t));
        if (g_mqtt_ctx == QOSA_NULL)
        {
            return -1;
        }
    }
    qosa_memset(g_mqtt_ctx, 0, sizeof(mqtt_demo_ctx_t));
    QLOGI("[mqtt_demo] MQTT context initialized");

    // If message queue is not created, create one for MQTT message processing
    if (g_mqtt_ctx->msgq == QOSA_NULL)
    {
        ret = qosa_msgq_create(&g_mqtt_ctx->msgq, sizeof(mqtt_demo_msg_t), MQTT_DEMO_MSG_MAX_CNT);
        if (ret != QOSA_OK)
        {
            QLOGE("[mqtt_demo] Failed to create message queue");
            return -1;
        }
    }

    // Initialize basic fields in context
    g_mqtt_ctx->msg_id = 1; // This parameter value can be 0 only when <QoS>=0
    g_mqtt_ctx->port = MQTT_DEMO_SERVER_PORT;

    // Fill MQTT connection configuration parameters
    mqtt_option->version = QCM_MQTT_VERSION_V3_1_1;
    mqtt_option->sim_id = MQTT_DEMO_SIMID;
    mqtt_option->pdp_cid = MQTT_DEMO_PDPID;
    mqtt_option->kalive_time = MQTT_DEMO_KEEP_ALIVE;
    mqtt_option->delivery_time = MQTT_DEMO_DELIVERY_TIME;
    mqtt_option->delivery_cnt = MQTT_DEMO_DELIVERY_CNT;
    mqtt_option->clean_session = QOSA_TRUE;
    // Other options can be configured by user, such as will message, etc.


    QLOGV("[mqtt_demo] mqtt version:%d", mqtt_option->version);

    return 0;
}

/**
 * @brief MQTT client event handling callback function
 *
 * This callback function is used to receive MQTT events
 *
 * @param event_id Event ID identifying the type of MQTT event that occurred
 * @param evt_param Event parameter pointer containing event-related data
 * @param user_param User-defined parameter pointer for passing user-specific data
 */
static void unir_mqtt_demo_event_cb(qcm_mqtt_client_event_e event_id, void *evt_param, void *user_param)
{
    mqtt_demo_msg_t msg = {0};
    int ret = 0;

    QOSA_UNUSED(user_param);
    // Check if event parameter is null, return directly if null
    if (evt_param == QOSA_NULL)
    {
        QLOGE("[mqtt_demo] client event callback received null event parameter");
        return;
    }

    QLOGV("[mqtt_demo] client event:%d,evt_param:%x", event_id, evt_param);

    // Package message structure, save event information to message
    msg.msg_id = event_id;
    msg.argv = evt_param;

    ret = qosa_msgq_release(g_mqtt_ctx->msgq, sizeof(mqtt_demo_msg_t), (qosa_uint8_t *)&msg, QOSA_NO_WAIT);

    // Check message sending result, log error if sending fails
    if (ret != QOSA_OK)
    {
        QLOGE("[mqtt_demo] ret=%x", ret);
    }
}

/**
 * @brief Initialize the MQTT client and open its transport connection
 *
 * @return 0 on success, -1 on failure
 */
static int unir_mqtt_demo_open(void)
{
    int ret = 0;
    qcm_mqtt_config_t mqtt_option = {0}; // MQTT configuration

    /* Initialize MQTT client default configuration */
    ret = qcm_mqtt_client_default_config(&mqtt_option);
    if (ret != QCM_MQTT_RES_OK)
    {
        QLOGE("[mqtt_demo] client default config failed!");
        goto exit;
    }

    ret = unir_mqtt_demo_ctx_init(&mqtt_option);
    if (ret != 0)
    {
        QLOGE("[mqtt_demo] init failed!");
        goto exit;
    }

    /* Create MQTT client instance */
    g_mqtt_ctx->client_id = qcm_mqtt_client_create();
    QLOGI("[mqtt_demo] init client_id=%d", g_mqtt_ctx->client_id);

    /* Initialize MQTT client and set event callback function */
    ret = qcm_mqtt_client_init(g_mqtt_ctx->client_id, &mqtt_option, unir_mqtt_demo_event_cb, QOSA_NULL);
    if (ret != QCM_MQTT_RES_OK)
    {
        QLOGE("[mqtt_demo] client init failed!");
        goto exit;
    }

    /* Open the underlying TCP connection to the MQTT broker. */
    ret = qcm_mqtt_client_open(g_mqtt_ctx->client_id, MQTT_DEMO_SERVER_ADDR, g_mqtt_ctx->port);
    if (ret != QCM_MQTT_RES_OK)
    {
        QLOGE("[mqtt_demo] client open failed!");
        goto exit;
    }

    return 0;
exit:
    return -1;
}

/**
 * @brief Connect to MQTT server
 *
 * This function is used to establish a connection between the MQTT client and server,
 * using predefined client ID, username, and password for connection authentication
 * @note Some cloud platforms or servers may require special handling of client_id,
 * modify according to actual situation
 */
static int unir_mqtt_demo_connect(void)
{
    qcm_mqtt_eercode_e ret = 0; // Store MQTT connection operation return value

    QLOGV("[mqtt_demo] enter");
    /* Call MQTT client connection interface, pass connection parameters including client ID, username, password, etc., equivalent to CONNECT control packet connection */
    ret = qcm_mqtt_client_connect(
        g_mqtt_ctx->client_id,
        MQTT_DEMO_CLIENTID,
        qosa_strlen(MQTT_DEMO_CLIENTID),
        MQTT_DEMO_USERNAME,
        qosa_strlen(MQTT_DEMO_USERNAME),
        MQTT_DEMO_PASSWARD,
        qosa_strlen(MQTT_DEMO_PASSWARD));

    /* Check connection result, log error and return error code if connection fails */
    if (ret != QCM_MQTT_RES_OK)
    {
        QLOGE("[mqtt_demo] connect fail=%x", ret);
        return -1;
    }
    return 0;
}

/**
 * @brief MQTT publish message demo function
 *
 * This function demonstrates the MQTT client's message publishing functionality.
 * The function constructs an MQTT message and publishes it to the specified topic.
 *
 * @note When Qos level is 0, the packet identifier field does not exist
 */
static void unir_mqtt_demo_publish(const char *payload)
{
    qcm_mqtt_quality_of_service_e qos = QCM_MQTT_AT_LEAST_ONCE_DELIVERY; // Qos level
    qosa_bool_t retain = QOSA_FALSE;                                     // Retain message
    qcm_mqtt_eercode_e ret = 0;
    qcm_mqtt_pub_config_t pub_info = {0};

    QLOGV("[mqtt_demo] publish");
    // Increment packet ID and execute MQTT message publish operation
    pub_info.msg_id = g_mqtt_ctx->msg_id++;
    pub_info.qos = qos;
    pub_info.retain = retain;
    pub_info.topic.data_ptr = (char *)MQTT_DEMO_TOPIC_PUB;
    pub_info.topic.data_len = qosa_strlen(MQTT_DEMO_TOPIC_PUB);
    pub_info.payload.data_ptr = (char *)payload;
    pub_info.payload.data_len = qosa_strlen(payload);

    ret = qcm_mqtt_client_publish(g_mqtt_ctx->client_id, &pub_info);

    // Check publish operation result, log error information if failed
    if (ret != QCM_MQTT_RES_OK)
    {
        QLOGE("[mqtt_demo] publish error=%x", ret);
    }
}

/**
 * @brief Subscribe to one MQTT topic
 *
 * @note Multiple topics can be added in one subscription, there's no explicit limit in the protocol, but it's not recommended to subscribe to too many topics at once
 */
static void unir_mqtt_demo_subscribe(int qos, const char *topic)
{
    qcm_mqtt_client_info_t client_info = {0};                     // Initialize MQTT client info structure
    qcm_mqtt_sub_topic_t sub_topic[QCM_MQTT_MAX_TOPIC_NUM] = {0}; // Initialize topic array
    int i, topic_cnt = 0;                                         // Subscribe topic counter
    qcm_mqtt_eercode_e ret = 0;                                   // Subscribe operation return value
    qcm_mqtt_sub_config_t sub_info = {0};

    // Check MQTT client connection state, return directly if not connected
    if (qcm_mqtt_client_get_state_info(g_mqtt_ctx->client_id, &client_info) != QCM_MQTT_RES_OK || client_info.client_state != QCM_MQTT_STATE_MQTT_CONNECTED)
    {
        QLOGE("[mqtt_demo] state err");
        return;
    }

    // Set the topic and its requested QoS level
    topic_cnt = 0;

  
    qcm_mqtt_malloc_data(&sub_topic[topic_cnt].topic, qosa_strlen(topic), (char *)topic);

    sub_topic[topic_cnt].qos = qos;
    topic_cnt += 1;

    // Increment packet ID and execute subscribe operation
    sub_info.msg_id = g_mqtt_ctx->msg_id++;
    sub_info.topics = sub_topic;
    sub_info.topic_cnt = topic_cnt;
    ret = qcm_mqtt_client_subscribe(g_mqtt_ctx->client_id, &sub_info);

    QLOGD("[mqtt_demo] topic_cnt=%d,err=%x", topic_cnt, ret);
    // Free topic memory
    for (i = 0; i < topic_cnt; i++)
    {
        qcm_mqtt_free_data(&sub_topic[i].topic);
    }

    // Check subscribe operation result, log error and return if failed
    if (ret != QCM_MQTT_RES_OK)
    {
        QLOGE("[mqtt_demo] ret=%x", ret);
    }

    return;
}

/**
 * @brief MQTT client unsubscribe topic demo function
 *
 * This function will unsubscribe from the specified topic
 */
static void unir_mqtt_demo_unsubscribe(const char *topic)
{
    qcm_mqtt_eercode_e ret = 0;                 // Define MQTT operation return code
    int i, topic_cnt = 0;                       // Subscribe topic counter
    qcm_mqtt_sub_topic_t un_sub_topic[2] = {0}; // Initialize topic array
    qcm_mqtt_sub_config_t un_sub_info = {0};

    /* Set topics to unsubscribe from */
    topic_cnt = 0;

    qcm_mqtt_malloc_data(&un_sub_topic[topic_cnt].topic, qosa_strlen(topic), (char *)topic);
    un_sub_topic[topic_cnt].qos = QCM_MQTT_AT_LEAST_ONCE_DELIVERY;
    topic_cnt += 1;

    /* Execute unsubscribe operation */
    un_sub_info.msg_id = g_mqtt_ctx->msg_id++;
    un_sub_info.topics = un_sub_topic;
    un_sub_info.topic_cnt = topic_cnt;
    ret = qcm_mqtt_client_unsubscribe(g_mqtt_ctx->client_id, &un_sub_info);
    if (ret != QCM_MQTT_RES_OK)
    {
        QLOGE("[mqtt_demo] ret=%x", ret);
    }

    // Free topic memory
    for (i = 0; i < topic_cnt; i++)
    {
        qcm_mqtt_free_data(&un_sub_topic[i].topic);
    }
    return;
}

/**
 * @brief Disconnect MQTT client connection
 *
 * @note The client will send a disconnect packet, indicating a normal disconnection, which will not trigger the will message
 */
static void unir_mqtt_demo_disconnect(void)
{
    qcm_mqtt_eercode_e ret = 0;

    /* Call MQTT client disconnect interface */
    ret = qcm_mqtt_client_disconnect(g_mqtt_ctx->client_id, QOSA_NULL);
    if (ret != QCM_MQTT_RES_OK)
    {
        QLOGE("[mqtt_demo] ret=%x", ret);
    }
}

/**
 * @brief Close MQTT client connection
 *
 * @note close means disconnecting the TCP connection, if you don't want a normal disconnection, you can skip disconnect
 * @note Choose between disconnect and close interfaces based on business needs
 */
static void unir_mqtt_demo_close(void)
{
    qcm_mqtt_eercode_e ret = 0;

    /* Call MQTT client close interface and check the return result */
    ret = qcm_mqtt_client_close(g_mqtt_ctx->client_id);
    if (ret != QCM_MQTT_RES_OK)
    {
        QLOGE("[mqtt_demo] ret=%x", ret);
    }
}

/**
 * @brief MQTT event handling function, used to process various event notifications from MQTT client
 *
 * This function processes different MQTT operation results based on the incoming message type (msg_id),
 * including responses to connection, publish, subscribe, unsubscribe, and disconnect operations
 *
 * @param msg Pointer to MQTT message structure, containing event ID and parameters
 *            Parameter content varies depending on msg_id, typically various response structures
 *
 * @return 0 on success, -1 or other error codes on failure
 */
static int unir_mqtt_demo_event_handle(mqtt_demo_msg_t *msg)
{
    qosa_uint8_t client_id = 0; // Client ID, used to identify current MQTT connection
    int ret = 0;                // Function return value

    if (msg == QOSA_NULL)
    {
        return -1;
    }
    qcm_mqtt_common_resp_t *resp_ptr = (qcm_mqtt_common_resp_t *)msg->argv;
    client_id = resp_ptr->client_id;
    QLOGV("[mqtt_demo] msg->msg_id=%d, client_id=%d", msg->msg_id, client_id);
    // Process different MQTT events based on message ID
    if (msg->msg_id == QCM_MQTT_CLIENT_OPEN_EVENT)
    {
        QLOGV("[mqtt_demo] state open = %x", resp_ptr->result);

        // If TCP connection is successful, try to perform connect connection
        if (resp_ptr->result == QCM_MQTT_RES_OK)
        {
            ret = unir_mqtt_demo_connect();
            if (ret != 0)
            {
                QLOGE("[mqtt_demo] connect err");
            }
        }
    }
    else if (msg->msg_id == QCM_MQTT_CLIENT_CONNECT_EVENT)
    {
        QLOGV("[mqtt_demo] state connect = %x", resp_ptr->result);

        // Subscribe to the demo topic after a successful connection
        if (resp_ptr->result == QCM_MQTT_RES_OK)
        {
            QLOGV("[mqtt_demo] pocotrol_code = %d", resp_ptr->pocotrol_code);
            unir_mqtt_demo_subscribe(QCM_MQTT_AT_LEAST_ONCE_DELIVERY, MQTT_DEMO_TOPIC_SUB);
        }
    }
    else if (msg->msg_id == QCM_MQTT_CLIENT_PUBLISH_EVENT)
    {
        // Publish message result returned
        QLOGV("[mqtt_demo] state publish");
        qcm_mqtt_send_resp_t *send_ptr = (qcm_mqtt_send_resp_t *)(resp_ptr->data);

        QLOGD("[mqtt_demo] msg_id:%d, send_result=%d", resp_ptr->msg_id, send_ptr->send_result);
    }
    else if (msg->msg_id == QCM_MQTT_CLIENT_SUBSCRIBE_EVENT)
    {
        // Subscribe message result returned
        QLOGV("[mqtt_demo] state subscribe = %x", resp_ptr->result);
    }
    else if (msg->msg_id == QCM_MQTT_CLIENT_SUBACK_EVENT)
    {
        // Subscribe message result returned
        QLOGV("[mqtt_demo] state subscribe = %x", resp_ptr->result);
        qcm_mqtt_suback_resp_t *suback_ptr = (qcm_mqtt_suback_resp_t *)resp_ptr->data;

        if (resp_ptr->result == QCM_MQTT_RES_OK)
        {
            if (suback_ptr != QOSA_NULL)
            {
                QLOGI("[mqtt_demo] qos_cnt:%d", suback_ptr->qos_cnt);
                qosa_free(suback_ptr->qoss); // Free the granted QoS array
            }

            // After successful subscription, try to publish message
            unir_mqtt_demo_publish(MQTT_DEMO_FIRST_SUBSCRIBE_PAYLOAD);

            if (g_mqtt_ctx->publish_timer != QOSA_NULL && (qosa_timer_is_running(g_mqtt_ctx->publish_timer) != QOSA_TRUE))
                // Start periodic publishing every 5 seconds
                qosa_timer_start(g_mqtt_ctx->publish_timer, 5 * 1000U, QOSA_TRUE);

        }
    }
    else if (msg->msg_id == QCM_MQTT_CLIENT_UNSUBSCRIBE_EVENT)
    {
        // Unsubscribe message result returned
        QLOGV("[mqtt_demo] state unsubscribe = %x", resp_ptr->result);
    }
    else if (msg->msg_id == QCM_MQTT_CLIENT_UNSUBACK_EVENT)
    {
        QLOGV("[mqtt_demo] client unsubscribe success");
    }
    else if (msg->msg_id == QCM_MQTT_CLIENT_STATE_EVENT)
    {
        // Log the close cause reported by the client state event
        QLOGV("[mqtt_demo] client state state");
        qcm_mqtt_close_cause_t *close_cause_ptr = (qcm_mqtt_close_cause_t *)resp_ptr->data;
        if (close_cause_ptr != QOSA_NULL)
        {
            QLOGV("[mqtt_demo] close cause = %d", close_cause_ptr->close_cause);
        }
    }
    else if (msg->msg_id == QCM_MQTT_CLIENT_DISCONNECT_EVENT)
    {
        // disconnect event
        qosa_timer_stop(g_mqtt_ctx->publish_timer);
        QLOGE("[mqtt_demo] client state disconnect = %d", resp_ptr->pocotrol_code);
    }
    else if (msg->msg_id == QCM_MQTT_CLIENT_CLOSE_EVENT)
    {
        // close event
        QLOGV("[mqtt_demo] client state close = %x", resp_ptr->result);
    }

    if (resp_ptr->data != QOSA_NULL)
    {
        qosa_free(resp_ptr->data);
    }
    qosa_free(resp_ptr);

    return ret;
}

/**
 * @brief Handle new MQTT messages received
 * 
 * @param msg message containing the custom MQTT message data
 */
static void unir_mqtt_demo_recv_new_message(mqtt_demo_msg_t *msg)
{
    qosa_uint8_t client_id = 0;                         // Client ID
    qcm_mqtt_recv_pub_t recv_pub = {0};                 // Received topic and payload
    qcm_mqtt_new_msg_notify_t *new_msg_ptr = QOSA_NULL; // New message notification structure
    qosa_uint8_t store_id = 0;                          // Store ID

    // Read the client ID and stored-message ID from the notification
    qcm_mqtt_common_resp_t *resp_ptr = (qcm_mqtt_common_resp_t *)msg->argv;
    client_id = resp_ptr->client_id;

    new_msg_ptr = (qcm_mqtt_new_msg_notify_t *)resp_ptr->data;
    store_id = new_msg_ptr->store_id;
    QLOGD("[mqtt_demo] client_id=%d, new_message=%d", client_id, store_id);
    // Read subscribed message content from MQTT client
    if (qcm_mqtt_client_read_subcribe_message(client_id, store_id, &recv_pub) == QCM_MQTT_RES_OK)
    {
        QLOGD("[mqtt_demo] topic = %s, payload_len = %d", recv_pub.topic.data_ptr, recv_pub.payload.data_len);

        if (recv_pub.payload.data_ptr != QOSA_NULL)
        {
            QLOGD("[mqtt_demo] payload = %s", recv_pub.payload.data_ptr);
            mqtt_cmd_info_t info = {0};
            if (unir_mqtt_parse_cmd_payload(recv_pub.payload.data_ptr, recv_pub.payload.data_len, &info) == 0)
            {
                unir_mqtt_demo_publish("parse cmd json success");
                switch (info.cmd)
                {
                case SET_PUB_PERIOD:
                    QLOGD("[mqtt_demo] SET_PUB_PERIOD, val_int=%d", info.val_int);
                    qosa_timer_stop(g_mqtt_ctx->publish_timer);
                    g_mqtt_ctx->publish_count = 0;
                    qosa_timer_start(g_mqtt_ctx->publish_timer, info.val_int * 1000U, QOSA_TRUE);
                    break;
                case SET_SUBSCRIBE_TOPIC:
                    QLOGD("[mqtt_demo] SET_SUBSCRIBE_TOPIC, val_int=%d, val_str=%s", info.val_int, info.val_str);
                    unir_mqtt_demo_subscribe(info.val_int, info.val_str);
                    break;
                case SET_UNSUBSCRIBE_TOPIC:
                    QLOGD("[mqtt_demo] SET_UNSUBSCRIBE_TOPIC, val_str=%s", info.val_str);
                    unir_mqtt_demo_unsubscribe(info.val_str);
                    break;
                case DISCONNECT:
                    QLOGD("[mqtt_demo] DISCONNECT");
                    qosa_timer_stop(g_mqtt_ctx->publish_timer);
                    unir_mqtt_demo_disconnect();
                    break;
                default:
                    QLOGE("[mqtt_demo] UNKNOWN CMD %d", info.cmd);
                    break;
                }
            }
            else
            {
                QLOGE("[mqtt_demo] not the cmd json format, payload=%s", recv_pub.payload.data_ptr);
            }
        }
        // Free topic and payload memory
        qcm_mqtt_free_data(&recv_pub.topic);
        qcm_mqtt_free_data(&recv_pub.payload);
    }

    if (resp_ptr->data != QOSA_NULL)
    {
        qosa_free(resp_ptr->data);
    }
    qosa_free(resp_ptr);
}

/**
 * @brief Handle a queued timer event by publishing an MQTT message
 *
 * @param msg message containing the publish count
 */
static void unir_mqtt_demo_timer_publish(mqtt_demo_msg_t *msg)
{
    QLOGV("[mqtt_demo] state publish event periodically");
    int publish_count = msg->param1;
    char payload[MQTT_DEMO_LEN_MAX] = {0};
    qosa_snprintf(payload, MQTT_DEMO_LEN_MAX, "Hello Welcome to test mqtt! publish_count=%d", publish_count);
    unir_mqtt_demo_publish(payload);
}

/**
 * @brief Main task function for the MQTT demo, responsible for initializing the MQTT client and handling MQTT events
 * 
 * @param argv Pointer to task arguments (not used in this demo)
 */
static void unir_mqtt_demo_task(void *argv)
{
    int ret = 0;                       // Function return value
    mqtt_demo_msg_t msg = {0};         // MQTT message structure
    qcm_mqtt_client_info_t info = {0}; // MQTT client information structure
    QOSA_UNUSED(argv);

    // Allow system initialization to complete before opening the MQTT client
    qosa_task_sleep_sec(10);
    ret = unir_mqtt_demo_open();
    if (ret != 0)
    {
        goto exit;
    }

    // Create the timer; it is started after subscription or by a command
    g_mqtt_ctx->publish_count = 0;
    if (qosa_timer_create(&g_mqtt_ctx->publish_timer, unir_mqtt_publish_timer_cb, &g_mqtt_ctx->publish_count) != QOSA_OK)
    {
        QLOGE("[mqtt_demo] Failed to create publish timer\n");
        goto exit;
    }

    // Main event processing loop
    while (1)
    {
        qosa_memset(&msg, 0, sizeof(mqtt_demo_msg_t));
        qosa_msgq_wait(g_mqtt_ctx->msgq, (qosa_uint8_t *)&msg, sizeof(mqtt_demo_msg_t), QOSA_WAIT_FOREVER);
        QLOGD("[mqtt_demo] msg_id=%d", msg.msg_id);

        // Dispatch and process different events based on message ID
        switch (msg.msg_id)
        {
        case QCM_MQTT_CLIENT_OPEN_EVENT:
            ret = unir_mqtt_demo_event_handle(&msg);
            break;
        case QCM_MQTT_CLIENT_CONNECT_EVENT:
            ret = unir_mqtt_demo_event_handle(&msg);
            break;
        case QCM_MQTT_CLIENT_PUBLISH_EVENT:
            ret = unir_mqtt_demo_event_handle(&msg);
            break;
        case QCM_MQTT_CLIENT_SUBSCRIBE_EVENT:
            ret = unir_mqtt_demo_event_handle(&msg);
            break;
        case QCM_MQTT_CLIENT_SUBACK_EVENT:
            ret = unir_mqtt_demo_event_handle(&msg);
            break;
        case QCM_MQTT_CLIENT_UNSUBSCRIBE_EVENT:
            ret = unir_mqtt_demo_event_handle(&msg);
            break;
        case QCM_MQTT_CLIENT_UNSUBACK_EVENT:
            ret = unir_mqtt_demo_event_handle(&msg);
            break;
        case QCM_MQTT_CLIENT_NEW_MESSAGE_EVENT:
            unir_mqtt_demo_recv_new_message(&msg);
            break;
        case QCM_MQTT_CLIENT_STATE_EVENT:
            ret = unir_mqtt_demo_event_handle(&msg);
            break;
        case QCM_MQTT_CLIENT_DISCONNECT_EVENT:
            ret = unir_mqtt_demo_event_handle(&msg);
            break;
        case QCM_MQTT_CLIENT_CLOSE_EVENT:
            ret = unir_mqtt_demo_event_handle(&msg);
            break;
        case MQTT_CLIENT_TIMER_PUBLISH_EVENT:
            unir_mqtt_demo_timer_publish(&msg);
            break;
        default:
            break;
        }

        // Get and record current MQTT client state
        qosa_memset(&info, 0, sizeof(qcm_mqtt_client_info_t));
        qcm_mqtt_client_get_state_info(g_mqtt_ctx->client_id, &info);
        QLOGD("[mqtt_demo] state=%d", info.client_state);

        // Close the client when its state indicates that shutdown is in progress
        if (info.client_state == QCM_MQTT_STATE_TCP_CLOSING || info.client_state == QCM_MQTT_STATE_MQTT_DISCONNECTING)
        {
            unir_mqtt_demo_close();
        }

        // If connection is closed or in initial state, exit task
        if (info.client_state == QCM_MQTT_STATE_TCP_CLOSED || info.client_state == QCM_MQTT_STATE_INIT)
        {
            goto exit;
        }

        // If error occurs during processing, also exit task
        if (ret != 0)
        {
            goto exit;
        }
    }

exit:
    QLOGV("[mqtt_demo] end");
    // Free resources
    unir_mqtt_demo_ctx_free();
}

/**
 * @note This function is responsible for creating the Easy MQTT demonstration task.
 * The task demonstrates the basic usage of MQTT API.
 */
void unir_mqtt_demo_init(void)
{
    int err = 0;
    qosa_task_t mqtt_task = QOSA_NULL;

    err = qosa_task_create(&mqtt_task, MQTT_DEMO_TASK_STACK_SIZE, QOSA_PRIORITY_NORMAL, "mqtt_demo", unir_mqtt_demo_task, QOSA_NULL);
    if (err != QOSA_OK)
    {
        QLOGE("[mqtt_demo] task create error");
        return;
    }
}
UNIRTOS_APP_EXPORT(327, "mqtt_demo", unir_mqtt_demo_init);