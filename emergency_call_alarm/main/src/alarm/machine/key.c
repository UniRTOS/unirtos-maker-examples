#include "qosa_def.h"
#include "qosa_log.h"
#include "qosa_gpio.h"
#include "qosa_pinctrl.h"

#define QOS_LOG_TAG LOG_TAG_DEMO

#include "alarm_app.h"
#include "alarm_config.h"
#include "alarm_machine.h"

/**
 * @brief SOS 中断回调.
 *
 * @param[in] argv 中断上下文参数，当前未使用。
 * @return void
 * @note 仅投递边沿事件，具体确认逻辑在状态机中完成。
 */
static void sos_isr(void *argv)
{
    alarm_event_t event = {0};

    QOSA_UNUSED(argv);
    event.type = ALARM_EVENT_SOS_EDGE;
    QLOGW("[alarm] sos interrupt triggered, post event");
    (void)alarm_post_event(&event);
}

/**
 * @brief 初始化 SOS 按键中断.
 *
 * @return int 0 表示成功，非 0 表示引脚配置或中断注册失败.
 */
int key_init(void)
{
    qosa_pin_cfg_t pin_cfg = {0};
    qosa_int_cfg_t int_cfg = {0};
    int            ret = 0;

    ret = qosa_get_pin_default_cfg(ALARM_SOS_PIN_NUM, &pin_cfg);
    if (ret != QOSA_GPIO_SUCCESS)
    {
        QLOGW("[alarm] get sos pin cfg failed ret=%d", ret);
        return ret;
    }

    /* 将引脚复用到 GPIO 功能，作为中断输入源。 */
    (void)qosa_pin_set_func(pin_cfg.pin_num, pin_cfg.gpio_func);

    int_cfg.gpio_num = pin_cfg.gpio_num;
    int_cfg.gpio_pull = QOSA_GPIO_PULL_UP;
    int_cfg.gpio_debounce = QOSA_GPIO_DEBOUNCE_EN;
    int_cfg.interrupt_cb = sos_isr;
    int_cfg.user_ctx = QOSA_NULL;

    ret = qosa_interrupt_register(&int_cfg);
    if (ret != QOSA_GPIO_SUCCESS)
    {
        QLOGW("[alarm] register sos interrupt failed ret=%d", ret);
        return ret;
    }

    /* 使用下降沿触发：与 ALARM_SOS_ACTIVE_LEVEL（低电平有效）一致。改动有效电平
     * 时需同步调整此处的触发沿。 */
    ret = qosa_interrupt_enable(pin_cfg.gpio_num, QOSA_GPIO_TRIGGER_FALLING_EDGE);
    if (ret != QOSA_GPIO_SUCCESS)
    {
        QLOGW("[alarm] enable sos interrupt failed ret=%d", ret);
    }

    return ret;
}
