#include "qosa_def.h"
#include "qosa_sys.h"
#include "qosa_log.h"
#include "qcm_audio.h"

#define QOS_LOG_TAG LOG_TAG_DEMO

#include "audio_pipeline.h"
#include "chatbot_config.h"
#include "peripherals.h"

static qosa_aud_handle_t g_player_handle = QOSA_NULL;

/**
 * @brief 初始化音频播放模块。
 *
 * @return 0 表示成功；返回负值表示初始化失败。
 */
int audio_player_init(void)
{
    /* 预留：当前平台无需在会话建立前提前占用播放资源，实际打开延迟到 audio_player_start()。 */
    return 0;
}

/**
 * @brief 打开 PCM 音频播放流。
 *
 * @return 0 表示成功或已经打开；返回负值表示打开失败。
 */
int audio_player_start(void)
{
    if (g_player_handle != QOSA_NULL)
    {
        return 0; /* 已打开，幂等返回 */
    }

    qosa_aud_stream_cfg_t cfg = {0};
    cfg.is_record = QOSA_FALSE;
    cfg.samprate = CHATBOT_AUDIO_SAMPLE_RATE;
    cfg.channels = CHATBOT_AUDIO_CHANNELS;
    cfg.sync_mode = QOSA_FALSE; /* 异步模式：避免与采集流同时以同步模式打开导致 I2S 数字接口永久阻塞 */
    cfg.format = QOSA_AUD_FMT_PCM;
    cfg.callback = QOSA_NULL;
    cfg.ctx = QOSA_NULL;

    qosa_aud_errcode_e ret = qcm_aud_stream_open(&cfg, &g_player_handle);
    if (ret != QOSA_AUD_SUCCESS)
    {
        QLOGE("[chat_audio_player] open failed, ret=%d", ret);
        g_player_handle = QOSA_NULL;
        return -1;
    }
    qosa_aud_set_volume(CHATBOT_AUDIO_PLAY_VOLUME);
    /* 播放流实际打开一次打一条日志，配合 downlink idle 关流日志可量化"关流→重开"次数与时机，
       用于诊断下行卡顿。 */
    QLOGI("[chat_audio_player] opened (stream start)");
    return 0;
}

/**
 * @brief 关闭当前播放流并释放播放句柄。
 *
 * @return 0 表示处理完成。
 */
static int audio_player_stop(void)
{
    if (g_player_handle != QOSA_NULL)
    {
        (void)qcm_aud_stream_close(g_player_handle, QOSA_FALSE);
        g_player_handle = QOSA_NULL;
        QLOGI("[chat_audio_player] closed (stream stop)");
    }
    return 0;
}

/**
 * @brief 向播放流写入一帧已解码的 PCM 数据。
 *
 * @param[in] pcm 待播放的 PCM 数据。
 * @param[in] len PCM 数据长度，单位为字节。
 * @return 实际写入字节数；返回负值表示播放流未打开、参数无效或写入超时。
 */
int audio_player_write_frame(const qosa_uint8_t *pcm, qosa_uint32_t len)
{
    if ((g_player_handle == QOSA_NULL) || (pcm == QOSA_NULL))
    {
        return -1;
    }

    /* 异步模式下 qcm_aud_stream_write 可能返回 0（缓冲区已满，需重试）或小于 len 的部分写入，
       需循环补写直至写完；持续 2 秒无法写入则判定异常并放弃本帧，避免下行任务被永久阻塞。 */
    qosa_uint32_t written = 0;
    qosa_uint32_t full_retry = 0;
    while (written < len)
    {
        qosa_int32_t ret = qcm_aud_stream_write(g_player_handle, (qosa_uint8_t *)(pcm + written), len - written);
        if (ret < 0)
        {
            QLOGW("[chat_audio_player] write failed, ret=%d", ret);
            return -1;
        }
        if (ret == 0)
        {
            full_retry++;
            if (full_retry >= 400) /* 400 * 5ms ≈ 2s */
            {
                QLOGW("[chat_audio_player] write timeout: sink full for 2s");
                return -1;
            }
            qosa_task_sleep_ms(5);
            continue;
        }
        full_retry = 0;
        written += (qosa_uint32_t)ret;
    }
    return (int)written;
}

/**
 * @brief 停止播放并清空缓冲（强制关闭播放流，下一帧到达时由调用方重新打开）。
 *
 * @return 0 表示成功；返回负值表示关闭失败。
 */
int audio_player_stop_and_flush(void)
{
    /* 强制关闭并重开播放流，达到"立即停止播放并清空缓冲"的效果；
       下一帧下行音频到达时，音频下行任务会重新调用 audio_player_start()。 */
    return audio_player_stop();
}
