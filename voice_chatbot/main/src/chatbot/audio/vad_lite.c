#include "qosa_def.h"
#include "qosa_log.h"

#define QOS_LOG_TAG LOG_TAG_DEMO

#include "vad_lite.h"

#define VAD_LITE_FRAME_SAMPLES       160
/*!< 起声/延续 RMS 阈值（PCM16，满幅 32767）。
     低于起声阈值，形成迟滞，防止检测到起声后在弱语音间反复横跳。 */
#define VAD_LITE_START_RMS           200
#define VAD_LITE_CONTINUE_RMS        95
/*!< 起声需连续 N 个"检测帧"都判定有语音才置 SPEECH_START，避免单次环境杂音
     尖峰/短暂碰撞声误触发。注意：这里的"帧"= 一次 vad_lite_process() 调用，
     其时长由上行读块大小决定，不是固定毫秒数；起声确认窗口内的语音帧由上行侧
     预滚缓存回溯补发（见 coze_audio_task.c 的 CHATBOT_UPLINK_PREROLL_FRAMES）。 */
#define VAD_LITE_START_FRAMES        3
/*!< 判定一句话结束所需连续静音帧数（每调用一次 vad_lite_process、且整帧无语音子帧
     才 +1；语音活跃期 uplink 每帧后 sleep≈100ms，故 END_FRAMES×100ms≈静音容忍时长）。
     静音时长 15≈1.5s：说话中间停顿/换气（<1.5s）不会被提前截断，
     但说完后约 1.5s 静音即发 complete 提交，避免整句拖太久。 */
#define VAD_LITE_END_FRAMES          15

static qosa_bool_t  g_speech_active = QOSA_FALSE;
static qosa_uint8_t g_speech_run = 0;
static qosa_uint8_t g_silence_run = 0;
/*!< 最近一次 vad_lite_process 输入帧中各子帧的最大 RMS，供上层观测 mic 能量。 */
static qosa_uint16_t g_last_max_rms = 0;

/**
 * @brief 计算一个 VAD 子帧的 RMS 能量。
 *
 * @param[in] samples 指向 PCM16 采样数据的指针，至少包含一个子帧。
 * @return 子帧 RMS 值。
 */
static qosa_uint16_t vad_lite_frame_rms(const qosa_int16_t *samples)
{
    qosa_uint64_t sum = 0;
    qosa_uint32_t bit = (qosa_uint32_t)1 << 30;
    qosa_uint32_t result = 0;

    for (int i = 0; i < VAD_LITE_FRAME_SAMPLES; i++)
    {
        qosa_int32_t sample = samples[i];
        sum += (qosa_uint64_t)(sample * sample);
    }
    sum /= VAD_LITE_FRAME_SAMPLES;

    while ((qosa_uint64_t)bit > sum)
    {
        bit >>= 2;
    }
    while (bit != 0)
    {
        if (sum >= ((qosa_uint64_t)result + bit))
        {
            sum -= (qosa_uint64_t)result + bit;
            result = (result >> 1) + bit;
        }
        else
        {
            result >>= 1;
        }
        bit >>= 2;
    }
    return (qosa_uint16_t)result;
}

/**
 * @brief 复位 VAD 语音状态、连续帧计数和能量观测值。
 */
void vad_lite_reset(void)
{
    g_speech_active = QOSA_FALSE;
    g_speech_run = 0;
    g_silence_run = 0;
    g_last_max_rms = 0;
}

/**
 * @brief 返回最近一次 vad_lite_process 输入帧内子帧的最大 RMS。
 *
 * 约 0 表示 mic 可能无信号/全静音；明显大于 0 且接近起声阈值说明有能量但未达阈值。
 */
qosa_uint16_t vad_lite_get_last_max_rms(void)
{
    return g_last_max_rms;
}

/**
 * @brief 返回 VAD 内部语音激活状态（当前是否处于语音段内）。
 */
qosa_bool_t vad_lite_is_speech_active(void)
{
    return g_speech_active;
}

/**
 * @brief 处理一帧输入音频，返回当前 VAD 判定状态。
 *
 * @param[in] pcm     线性 PCM16 数据。
 * @param[in] pcm_len 数据长度，单位为字节。
 * @return VAD_LITE_SILENCE / VAD_LITE_SPEECH_START / VAD_LITE_SPEECH_CONTINUE / VAD_LITE_SPEECH_END。
 */
vad_lite_state_e vad_lite_process(const qosa_uint8_t *pcm, qosa_uint32_t pcm_len)
{
    const qosa_int16_t *samples = (const qosa_int16_t *)pcm;
    qosa_uint32_t sample_count = pcm_len / sizeof(qosa_int16_t);
    qosa_bool_t speech_now = QOSA_FALSE;

    if ((pcm == QOSA_NULL) || (sample_count < VAD_LITE_FRAME_SAMPLES))
    {
        return g_speech_active ? VAD_LITE_SPEECH_CONTINUE : VAD_LITE_SILENCE;
    }

    qosa_uint16_t max_rms = 0;
    for (qosa_uint32_t offset = 0; (offset + VAD_LITE_FRAME_SAMPLES) <= sample_count; offset += VAD_LITE_FRAME_SAMPLES)
    {
        qosa_uint16_t rms = vad_lite_frame_rms(samples + offset);
        if (rms > max_rms)
        {
            max_rms = rms;
        }
        qosa_uint16_t threshold = g_speech_active ? VAD_LITE_CONTINUE_RMS : VAD_LITE_START_RMS;
        if (rms >= threshold)
        {
            speech_now = QOSA_TRUE;
            break;
        }
    }
    g_last_max_rms = max_rms;

    if (speech_now == QOSA_TRUE)
    {
        if (g_speech_run < 255)
        {
            g_speech_run++;
        }
        g_silence_run = 0;
        if ((g_speech_active == QOSA_FALSE) && (g_speech_run >= VAD_LITE_START_FRAMES))
        {
            g_speech_active = QOSA_TRUE;
            return VAD_LITE_SPEECH_START;
        }
    }
    else
    {
        g_speech_run = 0;
        if (g_speech_active == QOSA_TRUE)
        {
            if (g_silence_run < 255)
            {
                g_silence_run++;
            }
            if (g_silence_run >= VAD_LITE_END_FRAMES)
            {
                g_speech_active = QOSA_FALSE;
                g_silence_run = 0;
                return VAD_LITE_SPEECH_END;
            }
        }
    }

    return g_speech_active ? VAD_LITE_SPEECH_CONTINUE : VAD_LITE_SILENCE;
}