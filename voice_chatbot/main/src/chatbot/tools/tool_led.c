#include <string.h>

#include "qosa_def.h"
#include "qosa_log.h"
#include "qosa_gpio.h"
#include "qosa_pinctrl.h"
#include "qosa_cJSON.h"

#define QOS_LOG_TAG LOG_TAG_DEMO

#include "chatbot_tool.h"
#include "chatbot_config.h"

static qosa_gpio_num_e g_led_tool_gpio_num = QOSA_GPIO_MAX;

/* 极性与状态反馈 LED 一致，统一取自 chatbot_config.h，便于整板一次性对调。 */
#define TOOL_LED_LEVEL_ON     CHATBOT_LED_ACTIVE_LEVEL
#define TOOL_LED_LEVEL_OFF    CHATBOT_LED_INACTIVE_LEVEL

/**
 * @brief "led_control" 工具处理函数.
 *
 * 入参示例：{"led_id":"status","action":"on"}
 * 出参示例：{"led_id":"status","action":"on","result":"ok"}
 */
static int tool_led_control_handler(const char *args_json, char *result_json, qosa_uint32_t result_buf_len)
{
    qosa_bool_t turn_on = QOSA_FALSE;

    if (g_led_tool_gpio_num == QOSA_GPIO_MAX)
    {
        qosa_snprintf(result_json, result_buf_len, "{\"error\":\"led not initialized\"}");
        return -1;
    }

    Q_cJSON *root = Q_cJSON_Parse((args_json != QOSA_NULL) ? args_json : "{}");
    if (root == QOSA_NULL)
    {
        qosa_snprintf(result_json, result_buf_len, "{\"error\":\"invalid args json\"}");
        return -1;
    }

    Q_cJSON *action_item = Q_cJSON_GetObjectItem(root, "action");
    if (Q_cJSON_IsString(action_item) && (strcmp(action_item->valuestring, "on") == 0))
    {
        turn_on = QOSA_TRUE;
    }
    else if (Q_cJSON_IsString(action_item) && (strcmp(action_item->valuestring, "off") == 0))
    {
        turn_on = QOSA_FALSE;
    }
    else
    {
        Q_cJSON_Delete(root);
        qosa_snprintf(result_json, result_buf_len, "{\"error\":\"invalid action\"}");
        return -1;
    }
    Q_cJSON_Delete(root);

    if (qosa_gpio_set_level(g_led_tool_gpio_num, turn_on ? TOOL_LED_LEVEL_ON : TOOL_LED_LEVEL_OFF) != QOSA_GPIO_SUCCESS)
    {
        qosa_snprintf(result_json, result_buf_len, "{\"error\":\"gpio set level failed\"}");
        return -1;
    }

    qosa_snprintf(result_json, result_buf_len, "{\"led_id\":\"status\",\"action\":\"%s\",\"result\":\"ok\"}", turn_on ? "on" : "off");
    return 0;
}

/**
 * @brief 初始化工具示例 LED，并注册 led_control 工具。
 *
 * @return 0 表示初始化并注册成功；返回负值表示 GPIO 或注册失败。
 */
int tool_led_init(void)
{
    qosa_pin_cfg_t pin_cfg = {0};

    if (qosa_get_pin_default_cfg(TOOL_LED_NUM_DEFAULT, &pin_cfg) != QOSA_GPIO_SUCCESS)
    {
        QLOGE("[tool_led] get pin default cfg failed, pin_num=%d", TOOL_LED_NUM_DEFAULT);
        return -1;
    }

    (void)qosa_pin_set_func(pin_cfg.pin_num, pin_cfg.gpio_func);
    /* 上电默认熄灭（低电平点亮 -> 熄灭为 HIGH） */
    if (qosa_gpio_init(pin_cfg.gpio_num, QOSA_GPIO_DIRECTION_OUTPUT, QOSA_GPIO_PULL_NONE, TOOL_LED_LEVEL_OFF) != QOSA_GPIO_SUCCESS)
    {
        QLOGE("[tool_led] gpio init failed");
        return -1;
    }
    g_led_tool_gpio_num = pin_cfg.gpio_num;
    QLOGI("[tool_led] init done, pin_num=%d gpio_num=%d off_level=%d",
          pin_cfg.pin_num, pin_cfg.gpio_num, TOOL_LED_LEVEL_OFF);

    return tool_registry_register("led_control", tool_led_control_handler);
}
