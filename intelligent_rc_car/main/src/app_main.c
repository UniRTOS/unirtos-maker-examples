#include "include.h"

//QOSA core definition header
#include "qosa_def.h"
//Include QOSA system API header
#include "qosa_sys.h"
//Include QOSA log system header file
#include "qosa_log.h"
#include "car_log.h"

#include "unirtos_app_init_registry.h"
#include "qosa_sdk_version.h"
#include "qosa_dev_eigen.h"
#include "car_cloud.h"
#include "car_cloud_config.h"
#include "car_cmd.h"
#include "car_uart.h"

//Define stack size of status task as 2048 bytes
#define CAR_APP_STATUS_TASK_STACK_SIZE 2048
//Define status task priority as normal
#define CAR_APP_STATUS_TASK_PRIO QOSA_PRIORITY_NORMAL
//Failsafe check tick in milliseconds
#define CAR_APP_TICK_MS 500
//Status report period in milliseconds
#define CAR_APP_STATUS_PERIOD_MS 10000
//Max length of an ack payload
#define CAR_APP_ACK_MAX 96

//Global task handle of the status report task
static qosa_task_t g_car_status_task = QOSA_NULL;

static void car_publish_ack(int seq, const char *cmd, const char *result)
{
    char ack[CAR_APP_ACK_MAX] = {0};

    if (seq >= 0)
    {
        (void)qosa_snprintf(ack, sizeof(ack), "{\"seq\":%d,\"cmd\":\"%s\",\"result\":\"%s\"}", seq, cmd, result);
    }
    else
    {
        (void)qosa_snprintf(ack, sizeof(ack), "{\"cmd\":\"%s\",\"result\":\"%s\"}", cmd, result);
    }
    (void)car_cloud_publish_uplink(ack);
}

/**
 * @brief 下行命令回调：解析 JSON -> UART 单字节 -> 上行回执。
 *
 * @param[in] topic 下行 Topic。
 * @param[in] payload 以 '\0' 结尾的 JSON 文本。
 * @param[in] payload_len payload 字节数。
 * @return void
 */
static void car_on_cloud_rx(const char *topic, const char *payload, qosa_uint32_t payload_len)
{
    int         seq = -1;
    car_cmd_e   cmd = CAR_CMD_INVALID;
    char        byte = 0;
    const char *result = "ok";

    CAR_LOGI("[main][rx] topic=%s len=%d payload=%s", topic, (int)payload_len, payload);

    cmd = car_cmd_parse(payload, &seq);
    if (cmd == CAR_CMD_INVALID)
    {
        CAR_LOGW("[main][cmd] bad command");
        car_publish_ack(seq, "", "bad_cmd");
        return;
    }

    byte = car_cmd_to_uart_byte(cmd);
    /* 加减速重复执行会多调一档，不走确认重发；运动命令幂等，走确认重发。 */
    if (((cmd == CAR_CMD_SPEED_UP) || (cmd == CAR_CMD_SPEED_DOWN))
            ? (car_uart_send((const qosa_uint8_t *)&byte, 1) != 0)
            : (car_uart_send_acked((qosa_uint8_t)byte) != 0))
    {
        result = "uart_busy";
    }

    CAR_LOGI("[main][cmd] %s -> '%c' %s", car_cmd_name(cmd), byte, result);
    car_publish_ack(seq, car_cmd_name(cmd), result);
}

/**
 * @brief 状态任务：每 CAR_APP_TICK_MS 检测云端链路，断开时发送停车（failsafe）；
 *        每 CAR_APP_STATUS_PERIOD_MS 上报一次在线状态。
 *
 * @param[in] ctx 未使用。
 * @return void
 */
static void car_status_process(void *ctx)
{
    char          payload[96] = {0};
    qosa_bool_t   was_connected = QOSA_FALSE;
    qosa_bool_t   connected = QOSA_FALSE;
    qosa_uint32_t elapsed_ms = 0;
    char          stop = car_cmd_to_uart_byte(CAR_CMD_STOP);

    QOSA_UNUSED(ctx);
    CAR_LOGV("[main] status task started, sdk=%s", qosa_sdk_get_version());
    
    if (car_uart_init() != 0)
    {
        CAR_LOGW("[main] uart channel unavailable, commands will be acked as uart_busy");
    }

    if (car_cloud_init(car_on_cloud_rx) != 0)
    {
        CAR_LOGW("[main] cloud channel unavailable, check CONFIG_QCM_MQTT_FUNC and triple config");
    }

    while (1)
    {
        qosa_task_sleep_ms(CAR_APP_TICK_MS);
        elapsed_ms += CAR_APP_TICK_MS;

        connected = (car_cloud_is_connected() == 1) ? QOSA_TRUE : QOSA_FALSE;
        if ((was_connected == QOSA_TRUE) && (connected == QOSA_FALSE))
        {
            CAR_LOGW("[main][failsafe] cloud link down, stop car");
            if (car_uart_send_acked((qosa_uint8_t)stop) != 0)
            {
                CAR_LOGE("[main][failsafe] stop send failed");
            }
        }
        was_connected = connected;

        if (elapsed_ms < CAR_APP_STATUS_PERIOD_MS)
        {
            continue;
        }
        elapsed_ms = 0;

        if (connected == QOSA_FALSE)
        {
            CAR_LOGD("[main] cloud link is down");
            continue;
        }

        (void)qosa_snprintf(payload, sizeof(payload), "{\"event\":\"status\",\"online\":1}");
        (void)car_cloud_publish_uplink(payload);
    }
}

/**
 * @brief 小车应用初始化入口。
 *
 * 先初始化 UART1 下行通道，再创建云端 MQTT 收发通道与状态上报任务。
 *
 * @return void
 */
void unir_smallcar_init(void)
{
    CAR_LOGI("[main] enter smallcar demo, sdk=%s", qosa_sdk_get_version());
    if(qosa_gpio_set_voltage(VOL_3_30V) != (qosa_gpio_error_e)QOSA_OK) {
        CAR_LOGE("[main] failed to set GPIO voltage");
    }

    if (g_car_status_task == QOSA_NULL)
    {
        (void)qosa_task_create(&g_car_status_task,
                               CAR_APP_STATUS_TASK_STACK_SIZE,
                               CAR_APP_STATUS_TASK_PRIO,
                               "car_status",
                               car_status_process,
                               QOSA_NULL);
    }
}
UNIRTOS_APP_EXPORT(700, "unir_smallcar_demo", unir_smallcar_init);
