#include "qosa_def.h"
#include "qosa_sys.h"
#include "qosa_log.h"
#include "qosa_iic.h"
#include "qosa_pinctrl.h"
#include "qosa_audio.h"

#define QOS_LOG_TAG LOG_TAG_DEMO

#include "peripherals.h"
#include "chatbot_config.h"
#include "es8311_codec.h"

#define CHATBOT_CODEC_I2C_WRITE_RETRY_MAX 3

static qosa_bool_t g_codec_initialized = QOSA_FALSE;

/**
 * @brief ES8311 编解码器初始化：先切到外部 I2S 输出路径，再引脚复用为 I2C 功能、
 *        初始化 I2C 主机、逐条下发寄存器配置，最后设置播放音量。
 *
 * 引脚号、I2C 地址与寄存器表沿用现有 ES8311 对接实现（见 es8311_codec.h）；
 * I2S 路径切换与音量设置参照已验证可用的参考实现，缺少这一步会导致音频走内部
 * DAC/ADC 通路而非外部 ES8311，表现为录音/播放异常。
 */
int codec_es8311_init(void)
{
    codec_reg_t reg_list[] = ES8311_INIT_REG;
    qosa_int32_t ret = 0;
    qosa_int32_t last_ret = QOSA_I2C_SUCCESS;
    qosa_uint32_t fail_count = 0;
    qosa_uint32_t first_fail_reg = 0;

    if (g_codec_initialized == QOSA_TRUE)
    {
        return 0;
    }

    QLOGI("[chatbot_codec_es8311] init begin");

    qosa_aud_i2s_cfg_t i2s_cfg = {0, 0, 1, 1};
    ret = qosa_aud_output_ctrl(QOSA_AUDIO_OUTPUT_EXTERNAL, &i2s_cfg);
    if (ret != QOSA_AUD_SUCCESS)
    {
        QLOGE("[chatbot_codec_es8311] output ctrl external failed, ret=%d", ret);
        return -1;
    }

    ret = qosa_pin_set_func(CODEC_IIC_SCL_NUM, CODEC_IIC_FUNC);
    if (ret != QOSA_OK)
    {
        QLOGE("[chatbot_codec_es8311] set SCL pin failed, ret=%d", ret);
        return -1;
    }

    ret = qosa_pin_set_func(CODEC_IIC_SDA_NUM, CODEC_IIC_FUNC);
    if (ret != QOSA_OK)
    {
        QLOGE("[chatbot_codec_es8311] set SDA pin failed, ret=%d", ret);
        return -1;
    }

    ret = qosa_i2c_init(CHATBOT_CODEC_I2C_CHANNEL, QOSA_IIC_STANDARD_MODE);
    if (ret != QOSA_I2C_SUCCESS)
    {
        QLOGE("[chatbot_codec_es8311] i2c init failed, ret=%d", ret);
        return -1;
    }

    qosa_task_sleep_ms(CODEC_IIC_INIT_DELAY);

    for (qosa_uint32_t i = 0; i < sizeof(reg_list) / sizeof(reg_list[0]); i++)
    {
        qosa_uint32_t retry = 0;
        qosa_uint8_t  val = (qosa_uint8_t)reg_list[i].val;

        for (retry = 0; retry < CHATBOT_CODEC_I2C_WRITE_RETRY_MAX; retry++)
        {
            ret = qosa_i2c_write(CHATBOT_CODEC_I2C_CHANNEL, ES8311_I2C_ADDR, (qosa_uint16_t)reg_list[i].regAddr,
                                  &val, 1);
            if (ret == QOSA_I2C_SUCCESS)
            {
                break;
            }
            qosa_task_sleep_ms(CODEC_REG_CFG_DELAY);
        }

        if (ret != QOSA_I2C_SUCCESS)
        {
            if (fail_count == 0)
            {
                first_fail_reg = reg_list[i].regAddr;
            }
            fail_count++;
            QLOGW("[chatbot_codec_es8311] write reg 0x%02x failed after %d retries, ret=%d", reg_list[i].regAddr, CHATBOT_CODEC_I2C_WRITE_RETRY_MAX, ret);
        }
        last_ret = ret;
        qosa_task_sleep_ms(CODEC_REG_CFG_DELAY);
    }

    if (fail_count > 0)
    {
        QLOGE("[chatbot_codec_es8311] init failed, fail_count=%d first_fail_reg=0x%02x last_ret=%d", fail_count, first_fail_reg, last_ret);
        return -1;
    }

    g_codec_initialized = QOSA_TRUE;
    ret = qosa_aud_set_volume(CHATBOT_AUDIO_PLAY_VOLUME);
    QLOGI("[chatbot_codec_es8311] set volume to %d", CHATBOT_AUDIO_PLAY_VOLUME);
    if (ret != QOSA_AUD_SUCCESS)
    {
        QLOGW("[chatbot_codec_es8311] set volume failed, ret=%d", ret);
    }

    QLOGI("[chatbot_codec_es8311] init done");
    return 0;
}
