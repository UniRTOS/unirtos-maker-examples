/*****************************************************************/ /**
 * @file touch_cst816t.c
 * @brief CST816T 触摸驱动（实现与 small-screen-demo 中已验证的版本一致）
 *
 * INT 中断回调只置标志位，I2C 读操作放到任务循环里（tp_poll）完成。
 * 芯片工作在纯手势模式；CLICK 等手势发生时，坐标寄存器仍可读到触点坐标。
 **********************************************************************/
#include "qosa_def.h"
#include "qosa_sys.h"
#include "qosa_log.h"
#include "qosa_gpio.h"
#include "qosa_iic.h"
#include "qosa_pinctrl.h"
#include "qosa_dev_eigen.h"
#include "touch_cst816t.h"

#define QOS_LOG_TAG   LOG_TAG_DEMO

#define TP_IIC_PORT       0
#define TP_REST_PIN       66
#define TP_REST_FUNC      0
#define TP_INT_PIN        67
#define TP_INT_FUNC       0
#define TP_IIC_SCL        57
#define TP_IIC_SDA        58
#define TP_IIC_SCL_FUNC   2
#define TP_IIC_SDA_FUNC   2

#define TP_REG_CHIP_ID        0xA7
#define TP_CHIP_ID_CST816T    0xB5
#define TP_MACHINE_ADDRESS    0x15

#define TP_REG_GESTURE        0x01
#define TP_REG_XPOS_H         0x03

#define TP_REG_IRQ_CTL        0xFA
#define TP_IRQ_MODE_GESTURE   0x11
#define TP_REG_NOR_SCAN_PER   0xEE
#define TP_REG_IRQ_PULSE_W    0xED
#define TP_REG_DIS_SLEEP      0xFE

/* 一次滑动可能被芯片重复上报，处理一次滑动后在此时间内忽略后续滑动/长按 */
#define TP_GESTURE_COOLDOWN_MS 400

/* 芯片长时间无触摸可能进入休眠，周期性重新下发 DIS_SLEEP 保活 */
#define TP_KEEPALIVE_MS        2000

static qosa_gpio_num_e      g_tp_rst_gpio     = QOSA_GPIO_MAX;
static qosa_gpio_num_e      g_tp_int_gpio     = QOSA_GPIO_MAX;
static volatile qosa_bool_t g_tp_irq_flag     = QOSA_FALSE;
static uint32_t             g_tp_cooldown_ms  = 0;
static uint32_t             g_tp_keepalive_ms = 0;

static qosa_i2c_error_e tp_read_reg(qosa_uint8_t reg, qosa_uint8_t *val, qosa_uint8_t len)
{
    return qosa_i2c_read(TP_IIC_PORT, TP_MACHINE_ADDRESS, reg, val, len);
}

static qosa_i2c_error_e tp_write_reg(qosa_uint8_t reg, qosa_uint8_t val)
{
    return qosa_i2c_write(TP_IIC_PORT, TP_MACHINE_ADDRESS, reg, &val, 1);
}

static qosa_bool_t tp_read_point(uint16_t *x, uint16_t *y)
{
    qosa_uint8_t buf[4] = {0};

    if (tp_read_reg(TP_REG_XPOS_H, buf, 4) != QOSA_I2C_SUCCESS)
    {
        return QOSA_FALSE;
    }

    *x = (uint16_t)(((buf[0] & 0x0F) << 8) | buf[1]);
    *y = (uint16_t)(((buf[2] & 0x0F) << 8) | buf[3]);
    return QOSA_TRUE;
}

/* INT 下降沿中断：仅置标志位，不做 I2C/LOG 操作 */
static void tp_int_isr(void *argv)
{
    (void)argv;
    g_tp_irq_flag = QOSA_TRUE;
}

int tp_init(void)
{
    qosa_pin_cfg_t pin_cfg = {0};
    qosa_int_cfg_t int_cfg = {0};
    qosa_uint8_t   id      = 0;

    qosa_pin_set_func(TP_IIC_SCL, TP_IIC_SCL_FUNC);
    qosa_pin_set_func(TP_IIC_SDA, TP_IIC_SDA_FUNC);
    qosa_i2c_init(TP_IIC_PORT, QOSA_IIC_STANDARD_MODE);

    qosa_get_pin_default_cfg(TP_REST_PIN, &pin_cfg);
    g_tp_rst_gpio = pin_cfg.gpio_num;
    qosa_pin_set_func(pin_cfg.pin_num, TP_REST_FUNC);
    qosa_gpio_init(pin_cfg.gpio_num, QOSA_GPIO_DIRECTION_OUTPUT, QOSA_GPIO_PULL_NONE, QOSA_GPIO_LEVEL_LOW);

    qosa_get_pin_default_cfg(TP_INT_PIN, &pin_cfg);
    g_tp_int_gpio = pin_cfg.gpio_num;
    qosa_pin_set_func(pin_cfg.pin_num, TP_INT_FUNC);

    int_cfg.gpio_num      = g_tp_int_gpio;
    int_cfg.gpio_pull     = QOSA_GPIO_PULL_UP;
    int_cfg.gpio_debounce = QOSA_GPIO_DEBOUNCE_DIS;
    int_cfg.interrupt_cb  = tp_int_isr;
    int_cfg.user_ctx      = QOSA_NULL;
    int_cfg.options       = 0;
    qosa_interrupt_register(&int_cfg);
    qosa_interrupt_enable(g_tp_int_gpio, QOSA_GPIO_TRIGGER_FALLING_EDGE);

    qosa_gpio_set_level(g_tp_rst_gpio, QOSA_GPIO_LEVEL_HIGH);
    qosa_task_sleep_ms(100);
    qosa_gpio_set_level(g_tp_rst_gpio, QOSA_GPIO_LEVEL_LOW);
    qosa_task_sleep_ms(100);
    qosa_gpio_set_level(g_tp_rst_gpio, QOSA_GPIO_LEVEL_HIGH);
    qosa_task_sleep_ms(100);

    if (tp_read_reg(TP_REG_CHIP_ID, &id, 1) != QOSA_I2C_SUCCESS)
    {
        QLOGE("[smart_thermos_cup] touch chip id read failed");
        return -1;
    }
    if (id != TP_CHIP_ID_CST816T)
    {
        QLOGE("[smart_thermos_cup] unexpected touch chip id 0x%02x", id);
    }

    (void)tp_write_reg(TP_REG_IRQ_CTL, TP_IRQ_MODE_GESTURE);
    (void)tp_write_reg(TP_REG_NOR_SCAN_PER, 0x01);
    (void)tp_write_reg(TP_REG_IRQ_PULSE_W, 0x1C);
    (void)tp_write_reg(TP_REG_DIS_SLEEP, 0x01);

    return 0;
}

qosa_bool_t tp_poll(tp_event_t *ev, uint32_t poll_interval_ms)
{
    uint8_t gesture = TP_GESTURE_NONE;

    if (g_tp_cooldown_ms > 0)
    {
        g_tp_cooldown_ms = (g_tp_cooldown_ms > poll_interval_ms) ? (g_tp_cooldown_ms - poll_interval_ms) : 0;
    }

    g_tp_keepalive_ms += poll_interval_ms;
    if (g_tp_keepalive_ms >= TP_KEEPALIVE_MS)
    {
        g_tp_keepalive_ms = 0;
        (void)tp_write_reg(TP_REG_DIS_SLEEP, 0x01);
    }

    if (!g_tp_irq_flag)
    {
        return QOSA_FALSE;
    }
    g_tp_irq_flag = QOSA_FALSE;

    if (tp_read_reg(TP_REG_GESTURE, &gesture, 1) != QOSA_I2C_SUCCESS || gesture == TP_GESTURE_NONE)
    {
        return QOSA_FALSE;
    }

    if (gesture == TP_GESTURE_UP || gesture == TP_GESTURE_DOWN || gesture == TP_GESTURE_LEFT ||
        gesture == TP_GESTURE_RIGHT || gesture == TP_GESTURE_LONGPRESS)
    {
        if (g_tp_cooldown_ms > 0)
        {
            return QOSA_FALSE;
        }
        g_tp_cooldown_ms = TP_GESTURE_COOLDOWN_MS;
    }

    ev->gesture   = gesture;
    ev->has_point = QOSA_FALSE;
    ev->x         = 0;
    ev->y         = 0;
    if (gesture == TP_GESTURE_CLICK || gesture == TP_GESTURE_DCLICK)
    {
        ev->has_point = tp_read_point(&ev->x, &ev->y);
    }

    return QOSA_TRUE;
}
