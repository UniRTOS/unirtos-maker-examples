#ifndef __VAD_LITE_H__
#define __VAD_LITE_H__

#include "qosa_def.h"

typedef enum
{
    VAD_LITE_SILENCE = 0,
    VAD_LITE_SPEECH_START,
    VAD_LITE_SPEECH_CONTINUE,
    VAD_LITE_SPEECH_END,
} vad_lite_state_e;

/**
 * @brief 复位 VAD 语音状态和内部计数器。
 */
void vad_lite_reset(void);

/**
 * @brief 处理一帧 PCM16 数据并更新 VAD 状态机。
 *
 * @param[in] pcm     PCM16 音频数据。
 * @param[in] pcm_len 音频数据长度，单位为字节。
 * @return 本帧 VAD 状态。
 */
vad_lite_state_e vad_lite_process(const qosa_uint8_t *pcm, qosa_uint32_t pcm_len);
/**
 * @brief 返回最近一次输入帧内子帧的最大 RMS 能量值（用于观测 mic 能量与阈值余量）。
 */
qosa_uint16_t vad_lite_get_last_max_rms(void);
/**
 * @brief 返回 VAD 内部语音激活状态。
 */
qosa_bool_t vad_lite_is_speech_active(void);

#endif /* __VAD_LITE_H__ */