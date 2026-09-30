#include <string.h>

#include "qosa_def.h"
#include "qosa_log.h"
#include "qosa_uart.h"

#define QOS_LOG_TAG LOG_TAG_DEMO

#include "alarm_app.h"
#include "alarm_config.h"
#include "alarm_machine.h"

/* ASR 串口回调配置对象，初始化时注册到 UART 驱动层。 */
static qosa_uart_status_monitor_t g_asr_uart_monitor = {0};

/**
 * @brief 投递语音触发事件。
 *
 * @return void
 */
static void post_voice_trigger(void)
{
    alarm_event_t event = {0};

    event.type = ALARM_EVENT_VOICE_TRIGGER;
    (void)alarm_post_event(&event);
}

/**
 * @brief ASR 串口接收回调。
 *
 * @param[in] param 串口回调参数。
 * @return void
 * @note 对单次读取到的内容做子串匹配，命中 "SOS" / "help" / "CALL" 任一即触发报警。
 *       匹配基于单帧缓存，若 ASR 模块分帧上报需保证关键词落在同一帧内。
 */
static void asr_uart_cb(qosa_uart_cb_param_t *param)
{
    unsigned char buffer[64] = {0};
    int           available = 0;
    int           read_len = 0;

    if ((param == QOSA_NULL) || ((param->event_id & QOSA_UART_EVENT_RX_INDICATE) == 0))
    {
        return;
    }

    available = qosa_uart_read_available(param->port);
    if (available <= 0)
    {
        return;
    }

    if (available > (int)(sizeof(buffer) - 1))
    {
        available = (int)(sizeof(buffer) - 1);
    }

    read_len = qosa_uart_read(param->port, buffer, (unsigned int)available);
    if (read_len <= 0)
    {
        return;
    }

    /* 先补字符串结束符，再打印与检索，避免 printf("%s") 越界读取。 */
    buffer[read_len] = '\0';
    QLOGI("[alarm] asr uart read %d bytes: %s", read_len, buffer);

    if ((strstr((const char *)buffer, "SOS") != QOSA_NULL) ||
        (strstr((const char *)buffer, "help") != QOSA_NULL) ||
        (strstr((const char *)buffer, "CALL") != QOSA_NULL))
    {
        post_voice_trigger();
    }
}

/**
 * @brief 初始化 ASR 串口监听。
 *
 * @return int 0 表示成功，非 0 表示串口打开/配置/回调注册失败。
 */
int asr_pro_init(void)
{
    qosa_uart_config_t config = {0};
    int                ret = 0;

    config.baudrate = ALARM_ASR_UART_BAUD;
    config.data_bit = ALARM_ASR_UART_DATABIT;
    config.stop_bit = ALARM_ASR_UART_STOPBIT;
    config.parity_bit = ALARM_ASR_UART_PARITY;
    config.flow_ctrl = ALARM_ASR_UART_FLOWCTRL;

    ret = qosa_uart_open(ALARM_ASR_UART_PORT);
    if ((ret != QOSA_UART_SUCCESS) && (ret != QOSA_UART_OPEN_REPEAT_ERR))
    {
        QLOGW("[alarm] uart open failed ret=%d", ret);
        return ret;
    }

    ret = qosa_uart_ioctl(ALARM_ASR_UART_PORT, QOSA_UART_IOCTL_SET_DCB_CFG, &config);
    if (ret != QOSA_UART_SUCCESS)
    {
        QLOGW("[alarm] uart ioctl set cfg failed ret=%d", ret);
        return ret;
    }

    g_asr_uart_monitor.event_mask = QOSA_UART_EVENT_RX_INDICATE;
    g_asr_uart_monitor.callback = asr_uart_cb;
    g_asr_uart_monitor.user_data = QOSA_NULL;

    ret = qosa_uart_register_cb(ALARM_ASR_UART_PORT, &g_asr_uart_monitor);
    if (ret != QOSA_UART_SUCCESS)
    {
        QLOGW("[alarm] uart register cb failed ret=%d", ret);
    }

    return ret;
}
