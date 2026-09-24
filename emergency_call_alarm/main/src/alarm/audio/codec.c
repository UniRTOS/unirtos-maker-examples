#include "qosa_def.h"
#include "qosa_log.h"
#include "qosa_sys.h"
#include "qosa_pinctrl.h"
#include "qosa_iic.h"

#define QOS_LOG_TAG LOG_TAG_DEMO

#include "alarm_audio.h"
#include "alarm_es8311_codec.h"

/**
 * @brief 初始化外置音频 Codec（ES8311）。
 *
 * 完成 I2C 引脚复用、总线初始化，并按寄存器序列表逐项下发配置。
 *
 * @return int 0 表示成功，非 0 表示引脚配置、I2C 初始化或寄存器写入失败。
 * @note IMS 通话会切换音频路由，因此每次重新打开 TTS 前都需要重新执行本函数。
 */
int alarm_codec_init(void)
{
#if defined(CONFIG_QOSA_IIC_FUNC)
    qosa_int32_t     ret = 0;
    alarm_codec_reg_t reg_list[] = ALARM_ES8311_INIT_REG;
    int              i = 0;

    (void)qosa_pin_set_func(ALARM_CODEC_IIC_SCL_NUM, ALARM_CODEC_IIC_FUNC);
    (void)qosa_pin_set_func(ALARM_CODEC_IIC_SDA_NUM, ALARM_CODEC_IIC_FUNC);

    ret = qosa_i2c_init(QOSA_I2C_1, QOSA_IIC_STANDARD_MODE);
    if (ret != QOSA_I2C_SUCCESS)
    {
        QLOGW("[alarm] codec i2c init failed ret=%d", ret);
        return ret;
    }

    /* 上电后等待 codec 稳定，再逐项写寄存器。 */
    qosa_task_sleep_ms(ALARM_CODEC_IIC_INIT_DELAY);
    for (i = 0; i < (int)(sizeof(reg_list) / sizeof(reg_list[0])); i++)
    {
        ret = qosa_i2c_write(QOSA_I2C_1,
                             ALARM_ES8311_I2C_ADDR,
                             reg_list[i].regAddr,
                             (qosa_uint8_t *)&reg_list[i].val,
                             1);
        qosa_task_sleep_ms(ALARM_CODEC_REG_CFG_DELAY);
    }

    return ret;
#else
    QLOGW("[alarm] codec init skipped because CONFIG_QOSA_IIC_FUNC is disabled");
    return -1;
#endif
}
