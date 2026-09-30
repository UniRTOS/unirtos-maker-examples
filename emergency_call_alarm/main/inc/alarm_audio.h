#ifndef __ALARM_AUDIO_H__
#define __ALARM_AUDIO_H__

/**
 * @file alarm_audio.h
 * @brief 外置音频 Codec（ES8311）初始化接口。
 *
 * 该模块属于音频基础设施，被服务层（IMS 通话）与反馈层（TTS 播报）共同依赖，
 * 因此独立于两者存在，避免反馈层反向依赖服务层。
 */

/**
 * @brief 初始化外置音频 Codec（I2C 引脚复用、总线初始化与寄存器序列写入）。
 *
 * @return int 0 表示成功，非 0 表示引脚配置、I2C 初始化或寄存器写入失败。
 * @note 依赖 SDK 的 IIC 能力开关（CONFIG_QOSA_IIC_FUNC）：未开启时直接返回 -1。
 */
int alarm_codec_init(void);

#endif /* __ALARM_AUDIO_H__ */
