#include <string.h>

#include "qosa_def.h"
#include "qosa_sys.h"
#include "qosa_log.h"
#include "qosa_uart.h"

#define QOS_LOG_TAG LOG_TAG_DEMO

#include "peripherals.h"
#include "chatbot_config.h"
#include "chatbot_types.h"
#include "chat_bus.h"

static qosa_uart_status_monitor_t g_wake_uart_monitor = {0};

/**
 * @brief 投递唤醒事件到事件总线。
 */
static void post_wake_event(void)
{
    chat_event_t event = {0};

    event.type = CHAT_EVT_WAKE_DETECTED;
    (void)chat_bus_post_event(&event);
}

/**
 * @brief ASRPRO 串口接收回调：当接收缓存命中唤醒关键字时投递唤醒事件。
 *
 * @note 协议帧格式沿用现有 ASRPRO 对接实现（关键字匹配触发），具体波特率/帧头帧尾
 *       以实际模块参数为准（见 chatbot_config.h 的 CHATBOT_WAKE_* 宏）。
 */
static void wake_uart_rx_cb(qosa_uart_cb_param_t *param)
{
    unsigned char buffer[CHATBOT_WAKE_RX_BUF_SIZE] = {0};
    int           available = 0;
    int           read_len = 0;

    if ((param == QOSA_NULL) || ((param->event_id & QOSA_UART_EVENT_RX_INDICATE) == 0))
    {
        return;
    }

    available = qosa_uart_read_available(CHATBOT_WAKE_UART_PORT);
    if (available <= 0)
    {
        return;
    }

    if (available > (int)(sizeof(buffer) - 1))
    {
        available = (int)(sizeof(buffer) - 1);
    }

    read_len = qosa_uart_read(CHATBOT_WAKE_UART_PORT, buffer, (unsigned int)available);
    if (read_len <= 0)
    {
        return;
    }

    if (strstr((const char *)buffer, CHATBOT_WAKE_KEYWORD) != QOSA_NULL)
    {
        QLOGI("[wake_uart] keyword \"%s\" matched, post wake event", CHATBOT_WAKE_KEYWORD);
        post_wake_event();
    }
}

/**
 * @brief 配置并打开 ASRPRO 唤醒串口及接收回调。
 *
 * @return 0 表示初始化成功；返回负值表示回调、串口配置或打开失败。
 */
int wake_uart_init(void)
{
    g_wake_uart_monitor.callback = wake_uart_rx_cb;
    g_wake_uart_monitor.event_mask = QOSA_UART_EVENT_RX_INDICATE;
    g_wake_uart_monitor.user_data = QOSA_NULL;

    if (qosa_uart_register_cb(CHATBOT_WAKE_UART_PORT, &g_wake_uart_monitor) != QOSA_UART_SUCCESS)
    {
        QLOGE("[wake_uart] register cb failed");
        return -1;
    }

    qosa_uart_config_t dcb_config = {0};
    dcb_config.baudrate = CHATBOT_WAKE_UART_BAUDRATE;
    dcb_config.data_bit = QOSA_UART_DATABIT_8;
    dcb_config.stop_bit = QOSA_UART_STOP_1;
    dcb_config.parity_bit = QOSA_UART_PARITY_NONE;
    dcb_config.flow_ctrl = QOSA_FC_NONE;

    if (qosa_uart_ioctl(CHATBOT_WAKE_UART_PORT, QOSA_UART_IOCTL_SET_DCB_CFG, (void *)&dcb_config) != QOSA_UART_SUCCESS)
    {
        QLOGE("[wake_uart] set dcb config failed");
        return -1;
    }

    if (qosa_uart_open(CHATBOT_WAKE_UART_PORT) != QOSA_UART_SUCCESS)
    {
        QLOGE("[wake_uart] open port failed");
        return -1;
    }

    QLOGI("[wake_uart] init done, port=%d baud=%d", CHATBOT_WAKE_UART_PORT, CHATBOT_WAKE_UART_BAUDRATE);
    return 0;
}
