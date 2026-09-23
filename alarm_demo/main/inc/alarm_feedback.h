#ifndef __ALARM_FEEDBACK_H__
#define __ALARM_FEEDBACK_H__

/**
 * @file alarm_feedback.h
 * @brief 反馈层接口：状态灯、蜂鸣器、震动、TTS 语音与低电量提示。
 *
 * 本层依赖音频模块（alarm_audio.h）建立 Codec 通路，
 * 但不再依赖服务层，保证"上层依赖下层"的单向关系。
 */

#include "alarm_types.h"

/**
 * @brief 初始化反馈外设（状态灯 PWM、蜂鸣器、震动马达、LED 定时器）。
 */
void feedback_init(void);

/**
 * @brief 创建 TTS 播放完成通知信号量（音频通路在首次播放时建立）。
 */
void feedback_tts_init(void);

/**
 * @brief 按状态更新灯/蜂鸣器/震动反馈。
 *
 * @param[in] state 目标状态。
 */
void feedback_apply_state(alarm_state_e state);

/**
 * @brief 异步播放一条中文反馈语音。
 *
 * @param[in] text UTF-8 文本。
 */
void feedback_notify_voice(const char *text);

/**
 * @brief 等待当前反馈语音播放完成或超时。
 *
 * @return int QOSA_OK 表示播放完成/中断，其他值表示超时或未初始化。
 */
int feedback_wait_voice_finish(void);

/**
 * @brief 轮询电池状态并上报低电量事件。
 */
void feedback_poll_battery(void);

/**
 * @brief 输出低电量提示。
 *
 * @param[in] voltage_mv 当前电压（mV）。
 * @param[in] percent 当前电量百分比。
 */
void feedback_notify_low_battery(qosa_uint32_t voltage_mv, qosa_uint8_t percent);

#endif /* __ALARM_FEEDBACK_H__ */
