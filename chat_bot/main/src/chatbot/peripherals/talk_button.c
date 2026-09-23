#include "qosa_def.h"
#include "qosa_log.h"
#include "qosa_gpio.h"
#include "qosa_pinctrl.h"
#include "qosa_rtc.h"

#define QOS_LOG_TAG LOG_TAG_DEMO

#include "peripherals.h"
#include "chatbot_config.h"
#include "chatbot_types.h"
#include "chat_bus.h"

/*!< 软件防抖窗口(ms)：机械按键在按下/释放瞬间会连续抖动出多个沿,
     若每次沿都向事件总线投递，会产生"松开后又立刻按下的幽灵事件"，
     导致 PTT 状态机错乱（上行持续不结束）。两次上报之间间隔小于该窗口的
     事件一律丢弃。依赖 CONFIG_QOSA_SYS_TICK_PER_SECOND=1000（1 tick=1ms）。
     若平台 tick 频率不同需按 1000/tps 换算。 */
#define TALK_BUTTON_DEBOUNCE_MS     50

static volatile qosa_uint32_t s_last_evt_tick = 0;
static volatile qosa_uint32_t s_last_evt_type = 0xFFFFFFFF;

/**
 * @brief PTT 按键中断回调：读取消抖后的电平状态并投递按键按下/释放事件。
 *
 * @param[in] argv 中断上下文，未使用。
 */
static void talk_button_irq_cb(void *argv)
{
    qosa_gpio_level_e level = QOSA_GPIO_LEVEL_HIGH;
    chat_event_t event = {0};
    (void)argv;

    if (qosa_gpio_get_level(CHATBOT_TALK_BUTTON_GPIO, &level) != QOSA_GPIO_SUCCESS)
    {
        return;
    }

    chat_event_type_e evt_type = (level == CHATBOT_TALK_BUTTON_ACTIVE_LEVEL) ? CHAT_EVT_BUTTON_PRESSED : CHAT_EVT_BUTTON_RELEASED;

    /* 软件防抖：同状态重复上报或间隔过短(<窗口)的抖动沿直接忽略 */
    qosa_uint32_t now = qosa_get_system_tick_cnt();
    if ((evt_type == s_last_evt_type) || ((now - s_last_evt_tick) < TALK_BUTTON_DEBOUNCE_MS))
    {
        s_last_evt_tick = now;
        return;
    }
    s_last_evt_tick = now;
    s_last_evt_type = evt_type;

    event.type = evt_type;
    (void)chat_bus_post_event(&event);
}

/**
 * @brief 配置 PTT 按键引脚、上拉和双边沿中断。
 *
 * @return 0 表示初始化成功；返回负值表示引脚复用或中断配置失败。
 */
int talk_button_init(void)
{
    qosa_int_cfg_t cfg = {0};
    if (qosa_pin_set_func(CHATBOT_TALK_BUTTON_PIN_NUM, CHATBOT_TALK_BUTTON_PIN_FUNC) != QOSA_PINCTRL_SUCCESS)
    {
        QLOGE("[chat_talk_button] pin mux failed, pin=%d func=%d", CHATBOT_TALK_BUTTON_PIN_NUM, CHATBOT_TALK_BUTTON_PIN_FUNC);
        return -1;
    }
    cfg.gpio_num = CHATBOT_TALK_BUTTON_GPIO;
    cfg.gpio_debounce = QOSA_GPIO_DEBOUNCE_EN;
    cfg.gpio_pull = QOSA_GPIO_PULL_UP;
    cfg.interrupt_cb = talk_button_irq_cb;
    cfg.options = 0;

    if (qosa_interrupt_register(&cfg) != QOSA_GPIO_SUCCESS ||
        qosa_interrupt_enable(CHATBOT_TALK_BUTTON_GPIO, QOSA_GPIO_TRIGGER_BOTH_EDGE) != QOSA_GPIO_SUCCESS)
    {
        QLOGE("[chat_talk_button] interrupt init failed");
        return -1;
    }
    QLOGI("[chat_talk_button] init done, gpio=%d active=low", CHATBOT_TALK_BUTTON_GPIO);
    return 0;
}