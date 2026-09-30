#include "qosa_def.h"
#include "qosa_log.h"
#include "qcm_audio.h"

#define QOS_LOG_TAG LOG_TAG_DEMO

#include "audio_pipeline.h"
#include "chatbot_config.h"
#include "peripherals.h"

static qosa_aud_handle_t g_capture_handle = QOSA_NULL;

/**
 * @brief 初始化音频采集模块。
 *
 * @return 0 表示成功；返回负值表示初始化失败。
 */
int audio_capture_init(void)
{
    /* 预留：当前平台无需在会话建立前提前占用采集资源，实际打开延迟到 audio_capture_start()。 */
    return 0;
}

/**
 * @brief 打开 PCM 音频采集流。
 *
 * @return 0 表示成功或已经打开；返回负值表示打开失败。
 */
int audio_capture_start(void)
{
    if (g_capture_handle != QOSA_NULL)
    {
        return 0; /* 已打开，幂等返回 */
    }

    qosa_aud_stream_cfg_t cfg = {0};
    cfg.is_record = QOSA_TRUE;
    cfg.samprate = CHATBOT_AUDIO_SAMPLE_RATE;
    cfg.channels = CHATBOT_AUDIO_CHANNELS;
    cfg.sync_mode = QOSA_FALSE; /* 异步模式：无数据时立即返回 0，配合上行任务的重试循环，
                                   避免与播放流同时以同步模式打开导致 I2S 数字接口永久阻塞 */
    cfg.format = QOSA_AUD_FMT_PCM;
    cfg.callback = QOSA_NULL;
    cfg.ctx = QOSA_NULL;

    qosa_aud_errcode_e ret = qcm_aud_stream_open(&cfg, &g_capture_handle);
    if (ret != QOSA_AUD_SUCCESS)
    {
        QLOGE("[chat_audio_capture] open failed, ret=%d", ret);
        g_capture_handle = QOSA_NULL;
        return -1;
    }
    qosa_aud_set_volume(CHATBOT_AUDIO_PLAY_VOLUME);
    return 0;
}

/**
 * @brief 停止并关闭音频采集流。
 *
 * @return 0 表示处理完成。
 */
int audio_capture_stop(void)
{
    if (g_capture_handle != QOSA_NULL)
    {
        (void)qcm_aud_stream_close(g_capture_handle, QOSA_FALSE);
        g_capture_handle = QOSA_NULL;
    }
    return 0;
}

/**
 * @brief 从采集流读取一帧 PCM 数据。
 *
 * @param[out] buf     用于接收音频数据的缓冲区。
 * @param[in]  buf_len 缓冲区容量，单位为字节。
 * @return 实际读取字节数；返回负值表示参数无效或采集流未打开。
 */
int audio_capture_read_frame(qosa_uint8_t *buf, qosa_uint32_t buf_len)
{
    if ((g_capture_handle == QOSA_NULL) || (buf == QOSA_NULL))
    {
        return -1;
    }
    return (int)qcm_aud_stream_read(g_capture_handle, buf, buf_len);
}
