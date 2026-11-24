/*****************************************************************/ /**
* @file cloud_loudspeaker.c
* @brief Cloud loudspeaker: play a welcome message at boot, then speak every text pushed over MQTT
*        (iFlytek online TTS, streaming playback through the external ES8311 codec).
*
* @copyright Copyright (c) 2023 Quectel Wireless Solution, Co., Ltd.
* All Rights Reserved. Quectel Wireless Solution Proprietary and Confidential.
**********************************************************************/
#include "qosa_sys.h"
#include "qosa_def.h"
#include "qosa_log.h"
#include "cloud_loudspeaker.h"
#include "qcm_audio.h"
#include "qosa_iic.h"
#include "qosa_pinctrl.h"
#include "qosa_dev_eigen.h"
#include "xf_tts.h"
#include "cloud_mqtt.h"
#include "unirtos_app_init_registry.h"
/*===========================================================================
 *  Macro Definition
 ===========================================================================*/
#define QOS_LOG_TAG LOG_TAG_DEMO

#define UNIR_AUDIO_VOLUME        QOSA_AUD_VOLUME_LEVEL_3 /* 1..11, raise if too quiet, lower if distorted */

/* ---- boot welcome message (see xf_tts.h; credentials live in xf_tts.c) ---- */
#define XF_VOICE                 "x4_yezi"             /* vcn, the voice must be authorized for your app; NULL = x4_yezi */
#define UNIR_TTS_TEXT            "欢迎使用云喇叭"

// codec Register configuration
#define ES8311_INIT_REG                                                                                                                                        \
    {                                                                                                                                                          \
        {0x01, 0x30, 0x00}, {0x02, 0x00, 0x00}, {0x03, 0x20, 0x00}, {0x16, 0x20, 0x00}, {0x04, 0x20, 0x00}, {0x05, 0x00, 0x00}, {0x0B, 0x00, 0x00},            \
            {0x0C, 0x00, 0x00}, {0x0F, 0x44, 0x00}, {0x10, 0x1F, 0x00}, {0x11, 0x7F, 0x00}, {0x00, 0x80, 0x00}, {0x00, 0x80, 0x00}, {0x01, 0x3F, 0x00},        \
            {0x01, 0xBF, 0x00}, {0x02, 0x18, 0x00}, {0x05, 0x00, 0x00}, {0x03, 0x10, 0x00}, {0x04, 0x10, 0x00}, {0x07, 0x00, 0x00}, {0x08, 0xFF, 0x00},        \
            {0x06, 0x03, 0x00}, {0x01, 0xBF, 0x00}, {0x06, 0x03, 0x00}, {0x13, 0x10, 0x00}, {0x1B, 0x0A, 0x00}, {0x1C, 0x6A, 0x00}, {0x09, 0x0D, 0x00},        \
            {0x0A, 0x0D, 0x00}, {0x09, 0x0D, 0x00}, {0x0A, 0x0D, 0x00}, {0x32, 0xBF, 0x00}, {0x09, 0x0D, 0x00}, {0x0A, 0x0D, 0x00}, {0x17, 0xBF, 0x00},        \
            {0x0E, 0x02, 0x00}, {0x12, 0x00, 0x00}, {0x14, 0x1A, 0x00}, {0x14, 0x1A, 0x00}, {0x0D, 0x01, 0x00}, {0x15, 0x20, 0x00}, {0x37, 0x48, 0x00},        \
            {0x45, 0x00, 0x00}, {0x0D, 0x01, 0x00}, {0x45, 0x00, 0x00}, {0x37, 0x08, 0x00}, {0x14, 0x1A, 0x00}, {0x12, 0x00, 0x00}, {0x0E, 0x00, 0x00},        \
            {0x32, 0xBF, 0x00},                                                                                                                                \
    }

typedef struct
{
    qosa_uint32_t regAddr; /*!< Register address */
    qosa_uint16_t val;     /*!< The configured value */
    qosa_uint16_t delay;   /*!< delay time */
} AUD_CODEC_REG_T;

/*===========================================================================
 *  Variate
 ===========================================================================*/
static qosa_task_t g_unir_audio_demo_task = QOSA_NULL;

/*===========================================================================
 *  Function
 ===========================================================================*/
/**
 * @brief Write the ES8311 init register table over I2C (I2C must already be initialized).
 * @return 0 on success, non-zero on failure.
 */
static int unir_codec_write_regs(void)
{
    qosa_int32_t    ret = 0;
    AUD_CODEC_REG_T reg_list[] = ES8311_INIT_REG;

    for (int i = 0; i < sizeof(reg_list) / sizeof(reg_list[0]); i++)
    {
        ret = qosa_i2c_write(QOSA_I2C_1, QCM_ES8311_I2C_ADDR, reg_list[i].regAddr, (qosa_uint8_t *)&(reg_list[i].val), 1);
        if (ret != 0)
        {
            QLOGD("[cloud_loudspeaker] qosa_i2c_write failed, ret=0x%x, index=%d, reg=0x%x, val=0x%x", ret, i, reg_list[i].regAddr, reg_list[i].val);
            return ret;
        }
        qosa_task_sleep_ms(1);
    }
    return ret;
}

/**
 * @brief Initialize the external ES8311 codec over I2C.
 * @return 0 on success, non-zero on failure.
 */
static int unir_codec_init(void)
{
    qosa_int32_t ret = 0;

    qosa_pin_set_func(67, 2);
    qosa_pin_set_func(66, 2);

    ret = qosa_i2c_init(QOSA_I2C_1, QOSA_IIC_STANDARD_MODE);
    if (ret != 0)
    {
        QLOGD("[cloud_loudspeaker] qosa_i2c_init failed, ret=0x%x", ret);
        return ret;
    }

    qosa_task_sleep_ms(1000);
    return unir_codec_write_regs();
}

/**
 * @brief PA control callback invoked by the audio driver.
 */
static void unir_pa_cb(qosa_bool_t is_on)
{
    QLOGD("[cloud_loudspeaker] PA callback is_on=%d", is_on);
}

static void unir_audio_demo_process(void *arg)
{
    qosa_int32_t      ret = 0;
    qosa_aud_i2s_cfg_t i2s_aud_cfg = {0, 0, 1, 1};

    qosa_task_sleep_sec(5);
    QLOGV("[cloud_loudspeaker] audio task start");

    ret = qosa_aud_output_ctrl(QOSA_AUDIO_OUTPUT_EXTERNAL, &i2s_aud_cfg);
    if (ret != 0)
    {
        QLOGD("[cloud_loudspeaker] qosa_aud_output_ctrl external failed, ret=0x%x", ret);
        return;
    }

    ret = unir_codec_init();
    if (ret != 0)
    {
        QLOGD("[cloud_loudspeaker] codec init failed, ret=0x%x", ret);
        return;
    }

    ret = qosa_aud_bind_pa_cb(unir_pa_cb);
    QLOGD("[cloud_loudspeaker] codec init done, bind_pa_cb ret=0x%x", ret);

    ret = qosa_aud_set_volume(UNIR_AUDIO_VOLUME);
    QLOGD("[cloud_loudspeaker] set_volume ret=0x%x", ret);

    /* 1. boot welcome message */
    ret = xf_tts_speak(UNIR_TTS_TEXT, XF_VOICE, -1, -1, -1);
    QLOGD("[cloud_loudspeaker] welcome speak finished, ret=%d", ret);

    /* 2. MQTT service: every message becomes a playback request, played one after another */
    ret = cloud_mqtt_start();
    QLOGD("[cloud_loudspeaker] mqtt start ret=%d", ret);
    if (ret != 0)
    {
        return;
    }

    for (;;)
    {
        cloud_tts_req_t *req = QOSA_NULL;

        if (cloud_mqtt_wait_request(&req, QOSA_WAIT_FOREVER) != 0)
        {
            continue;
        }
        ret = xf_tts_speak(req->text, req->voice, req->speed, req->volume, req->pitch);
        QLOGD("[cloud_loudspeaker] mqtt speak finished, ret=%d", ret);
        cloud_mqtt_free_request(req);
    }
}

void unir_demo_audio_init(void)
{
    /* voice_chatbot (verified working) sets the IO voltage domain to 3.3V before any audio/codec use. */
    if (qosa_gpio_set_voltage(VOL_3_30V) != (qosa_gpio_error_e)QOSA_OK)
    {
        QLOGD("[cloud_loudspeaker] failed to set GPIO voltage 3.3V");
    }
    QLOGV("[cloud_loudspeaker] app init");
    if (g_unir_audio_demo_task == QOSA_NULL)
    {
        qosa_task_create(
            &g_unir_audio_demo_task,
            CONFIG_UNIRTOS_AUDIO_DEMO_TASK_STACK_SIZE,
            UNIR_AUDIO_DEMO_TASK_PRIO,
            "audio_demo",
            unir_audio_demo_process,
            QOSA_NULL,
            1
        );
    }
}

UNIRTOS_APP_EXPORT(320, "audio_demo", unir_demo_audio_init);
