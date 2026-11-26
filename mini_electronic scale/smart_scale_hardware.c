#include "smart_scale.h"

#include "qosa_def.h"
#include "cmsis_gcc.h"
#include "qosa_dev_eigen.h"
#include "qosa_gpio.h"
#include "qosa_log.h"
#include "qosa_pinctrl.h"

#define QOS_LOG_TAG LOG_TAG_DEMO
#define SCALE_KEY1_PAD 50
#define SCALE_KEY2_PAD 51
#define SCALE_KEY1_GPIO QOSA_GPIO_37
#define SCALE_KEY2_GPIO QOSA_GPIO_38
#define SCALE_HX_DOUT_PAD 58
#define SCALE_HX_SCK_PAD 57
#define SCALE_HX_DOUT_GPIO QOSA_GPIO_19
#define SCALE_HX_SCK_GPIO QOSA_GPIO_18
static scale_key_t g_keys[2];
static int g_key_ready;
static int g_hx_ready;

static int scale_setup_input(qosa_pin_num_e pad, qosa_gpio_num_e gpio)
{
    if (qosa_pin_set_func(pad, 0) != QOSA_PINCTRL_SUCCESS ||
        qosa_gpio_init(gpio, QOSA_GPIO_DIRECTION_INPUT, QOSA_GPIO_PULL_NONE,
            QOSA_GPIO_LEVEL_HIGH) != QOSA_GPIO_SUCCESS)
    {
        QLOGE("scale input setup failed pad=%u gpio=%u", (unsigned int)pad, (unsigned int)gpio);
        return 0;
    }
    return 1;
}

int scale_hardware_init(void)
{
    qosa_gpio_level_e level;
    int key1_ok;
    int key2_ok;
    int dout_ok;
    int sck_ok;

    key1_ok = scale_setup_input(SCALE_KEY1_PAD, SCALE_KEY1_GPIO);
    key2_ok = scale_setup_input(SCALE_KEY2_PAD, SCALE_KEY2_GPIO);
    g_key_ready = key1_ok && key2_ok;
    if (key1_ok && qosa_gpio_get_level(SCALE_KEY1_GPIO, &level) == QOSA_GPIO_SUCCESS)
        scale_key_init(&g_keys[0], level == QOSA_GPIO_LEVEL_HIGH);
    else
        scale_key_init(&g_keys[0], 1);
    if (key2_ok && qosa_gpio_get_level(SCALE_KEY2_GPIO, &level) == QOSA_GPIO_SUCCESS)
        scale_key_init(&g_keys[1], level == QOSA_GPIO_LEVEL_HIGH);
    else
        scale_key_init(&g_keys[1], 1);

    dout_ok = qosa_pin_set_func(SCALE_HX_DOUT_PAD, 4) == QOSA_PINCTRL_SUCCESS &&
        qosa_gpio_init(SCALE_HX_DOUT_GPIO, QOSA_GPIO_DIRECTION_INPUT,
            QOSA_GPIO_PULL_NONE, QOSA_GPIO_LEVEL_HIGH) == QOSA_GPIO_SUCCESS;
    sck_ok = qosa_pin_set_func(SCALE_HX_SCK_PAD, 4) == QOSA_PINCTRL_SUCCESS &&
        qosa_gpio_init(SCALE_HX_SCK_GPIO, QOSA_GPIO_DIRECTION_OUTPUT,
            QOSA_GPIO_PULL_NONE, QOSA_GPIO_LEVEL_LOW) == QOSA_GPIO_SUCCESS;
    g_hx_ready = dout_ok && sck_ok;
    if (!g_hx_ready)
        QLOGE("scale HX711 GPIO unavailable; keys remain active");
    QLOGI("scale keys Pin50/GPIO37=%d Pin51/GPIO38=%d HX711 Pin58/GPIO19=%d Pin57/GPIO18=%d",
        key1_ok, key2_ok, dout_ok, sck_ok);
    if (qosa_gpio_set_voltage(VOL_3_30V) != QOSA_GPIO_SUCCESS)
        QLOGE("scale GPIO voltage setup failed");
    return g_key_ready;
}

int scale_hardware_read(int32_t *sample)
{
    qosa_gpio_level_e level;
    uint32_t value = 0;
    unsigned int bit;
    uint32_t primask;
    if (!g_hx_ready || !sample)
        return 0;
    if (qosa_gpio_set_level(SCALE_HX_SCK_GPIO, QOSA_GPIO_LEVEL_LOW) != QOSA_GPIO_SUCCESS ||
        qosa_gpio_get_level(SCALE_HX_DOUT_GPIO, &level) != QOSA_GPIO_SUCCESS)
        return -1;
    if (level != QOSA_GPIO_LEVEL_LOW)
        return 0;
    for (bit = 0; bit < 24; ++bit)
    {
        primask = __get_PRIMASK();
        __disable_irq();
        if (qosa_gpio_set_level(SCALE_HX_SCK_GPIO, QOSA_GPIO_LEVEL_HIGH) != QOSA_GPIO_SUCCESS ||
            qosa_gpio_set_level(SCALE_HX_SCK_GPIO, QOSA_GPIO_LEVEL_LOW) != QOSA_GPIO_SUCCESS)
        {
            __set_PRIMASK(primask);
            return -1;
        }
        __set_PRIMASK(primask);
        if (qosa_gpio_get_level(SCALE_HX_DOUT_GPIO, &level) != QOSA_GPIO_SUCCESS)
            return -1;
        value = (value << 1) | (level == QOSA_GPIO_LEVEL_HIGH);
    }
    primask = __get_PRIMASK();
    __disable_irq();
    if (qosa_gpio_set_level(SCALE_HX_SCK_GPIO, QOSA_GPIO_LEVEL_HIGH) != QOSA_GPIO_SUCCESS ||
        qosa_gpio_set_level(SCALE_HX_SCK_GPIO, QOSA_GPIO_LEVEL_LOW) != QOSA_GPIO_SUCCESS)
    {
        __set_PRIMASK(primask);
        return -1;
    }
    __set_PRIMASK(primask);
    *sample = scale_signed24(value);
    return 1;
}

void scale_hardware_keys(int events[2], uint32_t now_ms)
{
    qosa_gpio_level_e level;
    unsigned int index;
    qosa_gpio_num_e gpios[] = {SCALE_KEY1_GPIO, SCALE_KEY2_GPIO};
    if (!events)
        return;
    events[0] = events[1] = 0;
    if (!g_key_ready)
        return;
    for (index = 0; index < 2; ++index)
        if (qosa_gpio_get_level(gpios[index], &level) == QOSA_GPIO_SUCCESS)
            events[index] = scale_key_scan(&g_keys[index], level == QOSA_GPIO_LEVEL_HIGH, now_ms);
}

