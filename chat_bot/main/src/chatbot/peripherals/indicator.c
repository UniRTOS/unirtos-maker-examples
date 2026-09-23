#include "qosa_def.h"
#include "qosa_sys.h"
#include "qosa_log.h"
#include "qosa_gpio.h"
#include "qosa_pinctrl.h"

#define QOS_LOG_TAG LOG_TAG_DEMO

#include "peripherals.h"
#include "chatbot_config.h"

static qosa_gpio_num_e g_indicator_gpio_num = QOSA_GPIO_MAX;

/**
 * @brief 初始化状态指示 LED 输出引脚。
 *
 * @note 状态反馈 LED 与工具层 led_control 示例外设相互独立，各自持有引脚与极性宏。
 *       本板 LED 为低电平点亮，因此初始电平取"熄灭"（HIGH），避免上电常亮。
 */
int indicator_init(void)
{
    qosa_pin_cfg_t pin_cfg = {0};

    if (qosa_get_pin_default_cfg(CHATBOT_LED_PIN_NUM, &pin_cfg) != QOSA_GPIO_SUCCESS)
    {
        QLOGE("[indicator] get pin default cfg failed, pin_num=%d", CHATBOT_LED_PIN_NUM);
        return -1;
    }

    (void)qosa_pin_set_func(pin_cfg.pin_num, pin_cfg.gpio_func);
    if (qosa_gpio_init(pin_cfg.gpio_num, QOSA_GPIO_DIRECTION_OUTPUT, QOSA_GPIO_PULL_NONE, CHATBOT_LED_INACTIVE_LEVEL) != QOSA_GPIO_SUCCESS)
    {
        QLOGE("[indicator] gpio init failed");
        return -1;
    }

    g_indicator_gpio_num = pin_cfg.gpio_num;
    QLOGI("[indicator] init done, pin_num=%d gpio_num=%d active_level=%d",
          pin_cfg.pin_num, g_indicator_gpio_num, CHATBOT_LED_ACTIVE_LEVEL);
    return 0;
}

/**
 * @brief 将核心状态机的状态/显示子态映射为 LED 输出。
 *
 * 硬件：低电平点亮（点亮=LOW / 熄灭=HIGH），极性集中定义在 chatbot_config.h。
 * 简化的单 LED 反馈规则：
 * - IDLE（待机）/ ERROR（异常退避）：熄灭
 * - WAKING（唤醒）/ CONNECTING（建连中）：常亮（提示正在进行建连）
 * - ACTIVE（对话中，含聆听/思考/播报子态）：常亮（提示会话进行中）
 *
 * @note 当前为单 LED 方案，不区分子态；substate 仅保留给后续"闪烁/呼吸"等差异化反馈使用。
 */
void indicator_apply_state(chat_state_e state, chat_substate_e substate)
{
    (void)substate;

    if (g_indicator_gpio_num == QOSA_GPIO_MAX)
    {
        return;
    }

    qosa_gpio_level_e level = CHATBOT_LED_INACTIVE_LEVEL;
    switch (state)
    {
        case CHAT_STATE_WAKING:
        case CHAT_STATE_CONNECTING:
        case CHAT_STATE_ACTIVE:
            level = CHATBOT_LED_ACTIVE_LEVEL;
            break;
        case CHAT_STATE_IDLE:
        case CHAT_STATE_ERROR:
        default:
            level = CHATBOT_LED_INACTIVE_LEVEL;
            break;
    }

    (void)qosa_gpio_set_level(g_indicator_gpio_num, level);
}
