/**
 * @file audio_pipeline.h
 * @brief 音频层：屏蔽底层编解码器差异，向通信层提供"直接读取一帧采集音频 / 直接写入一帧
 *        待播放音频"的同步接口，不引入中间队列转发。音频层始终使用线性 PCM16 格式。
 */
#ifndef __AUDIO_PIPELINE_H__
#define __AUDIO_PIPELINE_H__

#include "qosa_def.h"

/* ------------------------------ 采集（麦克风上行） ------------------------------ */
/**
 * @brief 初始化音频采集模块。
 *
 * @return 0 表示成功；返回负值表示初始化失败。
 */
int audio_capture_init(void);

/**
 * @brief 打开 PCM 音频采集流。
 *
 * @return 0 表示成功或已经打开；返回负值表示打开失败。
 */
int audio_capture_start(void);

/**
 * @brief 停止并关闭音频采集流。
 *
 * @return 0 表示处理完成。
 */
int audio_capture_stop(void);

/**
 * @brief 阻塞读取一帧采集数据，由通信层的音频上行任务直接调用。
 * @return 实际读取字节数，>=0 成功；<0 失败。
 */
int audio_capture_read_frame(qosa_uint8_t *buf, qosa_uint32_t buf_len);

/* ------------------------------ 播放（扬声器下行） ------------------------------ */
/**
 * @brief 初始化音频播放模块。
 *
 * @return 0 表示成功；返回负值表示初始化失败。
 */
int audio_player_init(void);

/**
 * @brief 打开 PCM 音频播放流。
 *
 * @return 0 表示成功或已经打开；返回负值表示打开失败。
 */
int audio_player_start(void);

/**
 * @brief 写入一帧解码后的播放数据，由通信层的音频下行任务直接调用。
 * @return 实际写入字节数，>=0 成功；<0 失败。
 */
int audio_player_write_frame(const qosa_uint8_t *pcm, qosa_uint32_t len);

/**
 * @brief 打断时立即停止播放并清空缓冲（强制关闭播放流；下一帧到达时重新打开）。
 *        调用方：本地 VAD SPEECH_START / PTT 按下 / 服务端 speech_started 兜底。
 */
int audio_player_stop_and_flush(void);

#endif /* __AUDIO_PIPELINE_H__ */
