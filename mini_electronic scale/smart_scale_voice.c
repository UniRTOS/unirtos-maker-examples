#include "smart_scale.h"
#include "xf_tts.h"

#include "qosa_def.h"
#include "qosa_audio.h"
#include "qosa_dev_eigen.h"
#include "qosa_gpio.h"
#include "qosa_iic.h"
#include "qosa_log.h"
#include "qosa_pinctrl.h"
#include "qosa_sys.h"

#include <stdio.h>

#define QOS_LOG_TAG LOG_TAG_DEMO
#define SCALE_VOICE_TASK_STACK 12288
#define SCALE_CODEC_ADDRESS 0x18

typedef struct
{
    double grams;
    unsigned int unit;
} scale_voice_event_t;

typedef struct
{
    qosa_uint8_t address;
    qosa_uint8_t value;
} scale_codec_reg_t;

static const scale_codec_reg_t g_codec_registers[] = {
    {0x01,0x30},{0x02,0x00},{0x03,0x20},{0x16,0x20},{0x04,0x20},{0x05,0x00},
    {0x0b,0x00},{0x0c,0x00},{0x0f,0x44},{0x10,0x1f},{0x11,0x7f},{0x00,0x80},
    {0x00,0x80},{0x01,0x3f},{0x01,0xbf},{0x02,0x18},{0x05,0x00},{0x03,0x10},
    {0x04,0x10},{0x07,0x00},{0x08,0xff},{0x06,0x03},{0x01,0xbf},{0x06,0x03},
    {0x13,0x10},{0x1b,0x0a},{0x1c,0x6a},{0x09,0x0d},{0x0a,0x0d},{0x09,0x0d},
    {0x0a,0x0d},{0x32,0xbf},{0x09,0x0d},{0x0a,0x0d},{0x17,0xbf},{0x0e,0x02},
    {0x12,0x00},{0x14,0x1a},{0x14,0x1a},{0x0d,0x01},{0x15,0x20},{0x37,0x48},
    {0x45,0x00},{0x0d,0x01},{0x45,0x00},{0x37,0x08},{0x14,0x1a},{0x12,0x00},
    {0x0e,0x00},{0x32,0xbf}
};

static qosa_msgq_t g_voice_queue;
static qosa_task_t g_voice_task;
static qosa_gpio_num_e g_pa_gpio;

static void scale_voice_pa(qosa_bool_t on)
{
    (void)qosa_gpio_set_level(g_pa_gpio, on ? QOSA_GPIO_LEVEL_HIGH : QOSA_GPIO_LEVEL_LOW);
}

static int scale_voice_route_init(void)
{
    qosa_aud_i2s_cfg_t config = {0, 0, 1, 1};
    qosa_pin_cfg_t pin = {0};
    unsigned int index;
    if (qosa_get_pin_default_cfg(54, &pin) != QOSA_GPIO_SUCCESS ||
        qosa_pin_set_func((qosa_pin_num_e)pin.pin_num, pin.gpio_func) != QOSA_PINCTRL_SUCCESS ||
        qosa_gpio_init(pin.gpio_num, QOSA_GPIO_DIRECTION_OUTPUT, QOSA_GPIO_PULL_NONE,
            QOSA_GPIO_LEVEL_LOW) != QOSA_GPIO_SUCCESS)
        return 0;
    g_pa_gpio = pin.gpio_num;
    if (qosa_aud_bind_pa_cb(scale_voice_pa) != QOSA_AUD_SUCCESS ||
        qosa_aud_output_ctrl(QOSA_AUDIO_OUTPUT_EXTERNAL, &config) != QOSA_AUD_SUCCESS ||
        qosa_pin_set_func(67, 2) != QOSA_PINCTRL_SUCCESS ||
        qosa_pin_set_func(66, 2) != QOSA_PINCTRL_SUCCESS ||
        qosa_i2c_init(QOSA_I2C_1, QOSA_IIC_STANDARD_MODE) != QOSA_I2C_SUCCESS)
        return 0;
    qosa_task_sleep_ms(1000);
    for (index = 0; index < sizeof(g_codec_registers) / sizeof(g_codec_registers[0]); ++index)
    {
        qosa_uint8_t value = g_codec_registers[index].value;
        if (qosa_i2c_write(QOSA_I2C_1, SCALE_CODEC_ADDRESS,
                g_codec_registers[index].address, &value, 1) != QOSA_I2C_SUCCESS)
            return 0;
        qosa_task_sleep_ms(1);
    }
    return qosa_aud_set_volume(QOSA_AUD_VOLUME_LEVEL_3) == QOSA_AUD_SUCCESS;
}

static void scale_voice_format(char *message, unsigned int size, const scale_voice_event_t *event)
{
    char number[48];
    const char *unit_text;
    scale_format(number, sizeof(number), event->grams, event->unit, 0);
    unit_text = event->unit == SCALE_UNIT_KG ? "\xe5\x8d\x83\xe5\x85\x8b" :
        event->unit == SCALE_UNIT_LB ? "\xe7\xa3\x85" : "\xe5\x85\x8b";
    (void)snprintf(message, size, "%s%s\xe5\xb7\xb2\xe4\xbf\x9d\xe5\xad\x98", number, unit_text);
}

static void scale_voice_process(void *argument)
{
    scale_voice_event_t event;
    char message[96];
    (void)argument;
    while (!scale_voice_route_init())
    {
        QLOGE("scale voice route initialization failed");
        qosa_task_sleep_ms(5000);
    }
    while (xf_tts_speak("\xe6\xac\xa2\xe8\xbf\x8e\xe4\xbd\xbf\xe7\x94\xa8\xe8\xbf\xb7\xe4\xbd\xa0\xe7\x94\xb5\xe5\xad\x90\xe7\xa7\xa4", QOSA_NULL, -1, -1, -1) != 0)
    {
        QLOGE("scale voice welcome failed");
        qosa_task_sleep_ms(5000);
    }
    for (;;)
    {
        if (qosa_msgq_wait(g_voice_queue, (qosa_uint8_t *)&event, sizeof(event), QOSA_WAIT_FOREVER) != QOSA_ERROR_OK)
            continue;
        scale_voice_format(message, sizeof(message), &event);
        if (xf_tts_speak(message, QOSA_NULL, -1, -1, -1) != 0)
            QLOGE("scale voice record failed");
    }
}

void scale_voice_init(void)
{
    if (g_voice_task)
        return;
    if (!g_voice_queue && qosa_msgq_create(&g_voice_queue, sizeof(scale_voice_event_t),
            SCALE_HISTORY_CAPACITY) != QOSA_ERROR_OK)
    {
        QLOGE("scale voice queue initialization failed");
        return;
    }
    if (qosa_task_create(&g_voice_task, SCALE_VOICE_TASK_STACK, QOSA_PRIORITY_NORMAL,
            "scale_voice", scale_voice_process, QOSA_NULL) != QOSA_ERROR_OK)
        QLOGE("scale voice task creation failed");
}

void scale_voice_record(double grams, unsigned int unit)
{
    scale_voice_event_t event = {grams, unit};
    if (!g_voice_task || qosa_msgq_release(g_voice_queue, sizeof(event),
            (qosa_uint8_t *)&event, QOSA_NO_WAIT) != QOSA_ERROR_OK)
        QLOGE("scale voice queue unavailable or full");
}