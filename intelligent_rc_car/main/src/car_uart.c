#include "qosa_def.h"
#include "qosa_sys.h"
#include "qosa_log.h"
#include "car_log.h"

#include "qosa_uart.h"
#include "qosa_pinctrl.h"
#include "qosa_rtc.h"

#include <string.h>

#include "car_uart.h"

#define CAR_UART_PORT            QOSA_UART_PORT_1
#define CAR_UART_TX_PIN          (18)
#define CAR_UART_RX_PIN          (17)
#define CAR_UART_PIN_FUNC        (1)  /*!< pin17/18 的 UART1 复用功能号 */
#define CAR_UART_BAUD            QOSA_UART_BAUD_9600
#define CAR_UART_TASK_STACK_SIZE (4096)
#define CAR_UART_TASK_PRIO       QOSA_PRIORITY_NORMAL
#define CAR_UART_MSGQ_DEPTH      (8)
#define CAR_UART_RX_POLL_MS      (1000) /*!< 兜底轮询周期，防止 RX 通知因队列满丢失后接收停滞 */
#define CAR_UART_RX_CHUNK        (64)
#define CAR_UART_RX_LINE_MAX     (96)
#define CAR_UART_ACK_PREFIX      "CMD:" /*!< UNO 确认行格式：CMD:<命令字节> */
#define CAR_UART_ACK_TIMEOUT_MS  (300)
#define CAR_UART_ACK_MAX_RETRY   (5)

typedef enum
{
    CAR_UART_Q_TX = 0, /*!< 发送请求 */
    CAR_UART_Q_RX,     /*!< 接收通知（来自 UART 回调） */
} car_uart_q_kind_e;

typedef struct
{
    car_uart_q_kind_e kind;
    qosa_uint8_t      len;
    qosa_uint8_t      need_ack;
    qosa_uint8_t      data[CAR_UART_TX_MAX];
} car_uart_q_item_t;

typedef struct
{
    qosa_msgq_t   msgq;
    qosa_bool_t   ready;
    volatile qosa_bool_t rx_pending; /*!< 已有 RX 通知在队列中，避免 RX 洪泛挤占 TX 队列 */
    char          line[CAR_UART_RX_LINE_MAX];
    qosa_uint32_t line_len;
    qosa_uint8_t  ack_byte;     /*!< 等待确认的命令字节，0 表示无 */
    qosa_uint8_t  ack_retries;
    qosa_uint32_t ack_deadline; /*!< 确认截止时刻（ms） */
} car_uart_ctx_t;

static car_uart_ctx_t g_uart_ctx = {0};

static qosa_uint32_t car_uart_now_ms(void)
{
#if defined(CONFIG_QOSA_SYS_TICK_PER_SECOND) && (CONFIG_QOSA_SYS_TICK_PER_SECOND > 0)
    return qosa_get_system_tick_cnt() * (1000 / CONFIG_QOSA_SYS_TICK_PER_SECOND);
#else
    return qosa_get_system_tick_cnt();
#endif
}

static void car_uart_arm_ack(qosa_uint8_t byte)
{
    g_uart_ctx.ack_byte = byte;
    g_uart_ctx.ack_retries = 0;
    g_uart_ctx.ack_deadline = car_uart_now_ms() + CAR_UART_ACK_TIMEOUT_MS;
}

static void car_uart_check_ack_line(void)
{
    const qosa_uint32_t plen = (qosa_uint32_t)(sizeof(CAR_UART_ACK_PREFIX) - 1);

    if ((g_uart_ctx.ack_byte == 0) || (g_uart_ctx.line_len != plen + 1) ||
        (memcmp(g_uart_ctx.line, CAR_UART_ACK_PREFIX, plen) != 0))
    {
        return;
    }
    if ((qosa_uint8_t)g_uart_ctx.line[plen] == g_uart_ctx.ack_byte)
    {
        CAR_LOGI("[uart][ack] '%c' confirmed, retries=%d", (char)g_uart_ctx.ack_byte, (int)g_uart_ctx.ack_retries);
        g_uart_ctx.ack_byte = 0;
    }
}

/* 超时未收到 CMD:<x> 则重发，超过次数放弃。 */
static void car_uart_ack_poll(void)
{
    qosa_uint8_t byte = g_uart_ctx.ack_byte;

    if ((byte == 0) || ((qosa_int32_t)(car_uart_now_ms() - g_uart_ctx.ack_deadline) < 0))
    {
        return;
    }
    if (g_uart_ctx.ack_retries >= CAR_UART_ACK_MAX_RETRY)
    {
        CAR_LOGW("[uart][ack] '%c' not confirmed after %d retries, give up", (char)byte, (int)g_uart_ctx.ack_retries);
        g_uart_ctx.ack_byte = 0;
        return;
    }
    g_uart_ctx.ack_retries++;
    g_uart_ctx.ack_deadline = car_uart_now_ms() + CAR_UART_ACK_TIMEOUT_MS;
    (void)qosa_uart_write(CAR_UART_PORT, &byte, 1);
    CAR_LOGW("[uart][ack] '%c' timeout, resend #%d", (char)byte, (int)g_uart_ctx.ack_retries);
}

static qosa_uint32_t car_uart_wait_ms(void)
{
    qosa_int32_t remain = 0;

    if (g_uart_ctx.ack_byte == 0)
    {
        return CAR_UART_RX_POLL_MS;
    }
    remain = (qosa_int32_t)(g_uart_ctx.ack_deadline - car_uart_now_ms());
    if (remain <= 0)
    {
        return 1;
    }
    return (remain < CAR_UART_RX_POLL_MS) ? (qosa_uint32_t)remain : CAR_UART_RX_POLL_MS;
}

/* 回调中只投递通知，不做串口 I/O。 */
static void car_uart_event_cb(qosa_uart_cb_param_t *param)
{
    car_uart_q_item_t item = {0};

    if ((param == QOSA_NULL) || (g_uart_ctx.msgq == QOSA_NULL))
    {
        return;
    }
    if ((param->event_id & QOSA_UART_EVENT_RX_INDICATE) == 0)
    {
        return;
    }

    if (g_uart_ctx.rx_pending == QOSA_TRUE)
    {
        return;
    }

    item.kind = CAR_UART_Q_RX;
    g_uart_ctx.rx_pending = QOSA_TRUE;
    if (qosa_msgq_release(g_uart_ctx.msgq, sizeof(item), (qosa_uint8_t *)&item, QOSA_NO_WAIT) != QOSA_OK)
    {
        g_uart_ctx.rx_pending = QOSA_FALSE;
    }
}

static void car_uart_flush_line(void)
{
    if (g_uart_ctx.line_len == 0)
    {
        return;
    }
    g_uart_ctx.line[g_uart_ctx.line_len] = '\0';
    CAR_LOGD("[uart][rx] %s", g_uart_ctx.line);
    car_uart_check_ack_line();
    g_uart_ctx.line_len = 0;
}

static void car_uart_rx_byte(qosa_uint8_t ch)
{
    if (ch == '\r')
    {
        return;
    }
    if (ch == '\n')
    {
        car_uart_flush_line();
        return;
    }
    if (g_uart_ctx.line_len >= (CAR_UART_RX_LINE_MAX - 1))
    {
        car_uart_flush_line();
    }
    g_uart_ctx.line[g_uart_ctx.line_len++] = (char)ch;
}

static void car_uart_drain_rx(void)
{
    qosa_uint8_t buf[CAR_UART_RX_CHUNK];
    int          n = 0;
    int          i = 0;

    while (1)
    {
        n = qosa_uart_read(CAR_UART_PORT, buf, sizeof(buf));
        if (n <= 0)
        {
            break;
        }
        for (i = 0; i < n; i++)
        {
            car_uart_rx_byte(buf[i]);
        }
        if (n < (int)sizeof(buf))
        {
            break;
        }
    }
}

static void car_uart_do_tx(const car_uart_q_item_t *item)
{
    int ret = qosa_uart_write(CAR_UART_PORT, (unsigned char *)item->data, item->len);

    if (ret != (int)item->len)
    {
        CAR_LOGW("[uart][tx] write ret=%d, expect %d", ret, (int)item->len);
        if (item->need_ack)
        {
            g_uart_ctx.ack_byte = 0; /* 新命令发送失败也不能继续重发被取代的旧命令 */
        }
        return;
    }
    if (item->need_ack)
    {
        car_uart_arm_ack(item->data[0]);
    }
    CAR_LOGI("[uart][tx] %d byte(s), first='%c'(0x%02x)", (int)item->len, (char)item->data[0], item->data[0]);
}

static void car_uart_task(void *argv)
{
    car_uart_q_item_t item = {0};

    QOSA_UNUSED(argv);

    while (1)
    {
        qosa_memset(&item, 0, sizeof(item));
        if (qosa_msgq_wait(g_uart_ctx.msgq, (qosa_uint8_t *)&item, sizeof(item), car_uart_wait_ms()) != QOSA_OK)
        {
            car_uart_drain_rx();
            car_uart_ack_poll();
            continue;
        }

        if (item.kind == CAR_UART_Q_TX)
        {
            car_uart_do_tx(&item);
        }
        else
        {
            /* 先清标志再读，读取期间新到的数据会重新投递通知。 */
            g_uart_ctx.rx_pending = QOSA_FALSE;
            car_uart_drain_rx();
        }
        car_uart_ack_poll();
    }
}

static int car_uart_hw_open(void)
{
    qosa_uart_status_monitor_t monitor = {0};
    qosa_uart_config_t         cfg = {0};
    pinctrl_option_t           pin_opt = {0};
    qosa_uart_mode_e           mode = QOSA_UART_MODE_NORMAL;

    pin_opt.func = CAR_UART_PIN_FUNC;
    pin_opt.pull = QOSA_PIN_PULL_UP;
    pin_opt.driver = QOSA_PIN_DRIVE_LEVEL_0;
    if ((qosa_pinctrl_set_option(CAR_UART_TX_PIN, pin_opt) != 0) || (qosa_pinctrl_set_option(CAR_UART_RX_PIN, pin_opt) != 0))
    {
        CAR_LOGE("[uart][init] pin mux failed");
        return -1;
    }

    monitor.event_mask = QOSA_UART_EVENT_RX_INDICATE;
    monitor.callback = car_uart_event_cb;
    monitor.user_data = QOSA_NULL;
    if (qosa_uart_register_cb(CAR_UART_PORT, &monitor) != QOSA_UART_SUCCESS)
    {
        CAR_LOGE("[uart][init] register cb failed");
        return -1;
    }

    cfg.baudrate = CAR_UART_BAUD;
    cfg.data_bit = QOSA_UART_DATABIT_8;
    cfg.stop_bit = QOSA_UART_STOP_1;
    cfg.parity_bit = QOSA_UART_PARITY_NONE;
    cfg.flow_ctrl = QOSA_FC_NONE;
    if (qosa_uart_ioctl(CAR_UART_PORT, QOSA_UART_IOCTL_SET_DCB_CFG, (void *)&cfg) != QOSA_UART_SUCCESS)
    {
        CAR_LOGE("[uart][init] set dcb failed");
        return -1;
    }

    if (qosa_uart_open(CAR_UART_PORT) != QOSA_UART_SUCCESS)
    {
        CAR_LOGE("[uart][init] open failed");
        return -1;
    }

    /* EIGEN 平台主串口可能处于 AT 模式，显式切到用户数据模式。 */
    if (qosa_uart_ioctl(CAR_UART_PORT, QOSA_UART_IOCTL_SET_CCIO_MODE, (void *)&mode) != QOSA_UART_SUCCESS)
    {
        CAR_LOGW("[uart][init] set normal mode failed, continue");
    }

    return 0;
}

int car_uart_init(void)
{
    qosa_task_t task = QOSA_NULL;

    if (g_uart_ctx.msgq != QOSA_NULL)
    {
        return 0;
    }

    if (qosa_msgq_create(&g_uart_ctx.msgq, sizeof(car_uart_q_item_t), CAR_UART_MSGQ_DEPTH) != QOSA_OK)
    {
        CAR_LOGE("[uart][init] msgq create failed");
        return -1;
    }

    if (car_uart_hw_open() != 0)
    {
        goto fail;
    }

    if (qosa_task_create(&task, CAR_UART_TASK_STACK_SIZE, CAR_UART_TASK_PRIO, "car_uart", car_uart_task, QOSA_NULL) !=
        QOSA_OK)
    {
        CAR_LOGE("[uart][init] task create failed");
        (void)qosa_uart_close(CAR_UART_PORT);
        goto fail;
    }

    g_uart_ctx.ready = QOSA_TRUE;
    CAR_LOGI("[uart][init] UART1 ready, tx=pin%d rx=pin%d baud=%d", CAR_UART_TX_PIN, CAR_UART_RX_PIN, (int)CAR_UART_BAUD);
    return 0;

fail:
    (void)qosa_uart_register_cb(CAR_UART_PORT, QOSA_NULL);
    (void)qosa_msgq_delete(g_uart_ctx.msgq);
    g_uart_ctx.msgq = QOSA_NULL;
    return -1;
}

static int car_uart_enqueue(const qosa_uint8_t *data, qosa_uint32_t len, qosa_uint8_t need_ack)
{
    car_uart_q_item_t item = {0};

    if ((g_uart_ctx.ready != QOSA_TRUE) || (data == QOSA_NULL) || (len == 0) || (len > CAR_UART_TX_MAX))
    {
        return -1;
    }

    item.kind = CAR_UART_Q_TX;
    item.need_ack = need_ack;
    item.len = (qosa_uint8_t)len;
    qosa_memcpy(item.data, data, len);

    if (qosa_msgq_release(g_uart_ctx.msgq, sizeof(item), (qosa_uint8_t *)&item, QOSA_NO_WAIT) != QOSA_OK)
    {
        CAR_LOGW("[uart][tx] queue full, drop");
        return -1;
    }
    return 0;
}

int car_uart_send(const qosa_uint8_t *data, qosa_uint32_t len)
{
    return car_uart_enqueue(data, len, 0);
}

int car_uart_send_acked(qosa_uint8_t byte)
{
    return car_uart_enqueue(&byte, 1, 1);
}
