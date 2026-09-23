#ifndef __ALARM_CORE_H__
#define __ALARM_CORE_H__

/**
 * @file alarm_core.h
 * @brief 状态机层接口：会话管理、事件处理、联系人轮询与超时恢复。
 *
 * 本层不感知 MQTT、按键、串口等具体通道，只消费 alarm_event_t；
 * 触发出参统一由 alarm_post_event() 回流，保证各层解耦。
 */

#include "alarm_types.h"

/**
 * @brief 初始化状态机会话数据。
 *
 * @param[in,out] ctx 报警上下文。
 */
void alarm_core_init(alarm_ctx_t *ctx);

/**
 * @brief 处理单个事件。
 *
 * @param[in,out] ctx 报警上下文。
 * @param[in] event 输入事件。
 */
void alarm_core_handle_event(alarm_ctx_t *ctx, const alarm_event_t *event);

/**
 * @brief 状态机周期驱动函数。
 *
 * @param[in,out] ctx 报警上下文。
 * @note 负责按键确认倒计时、呼叫超时推进与低电量轮询。
 */
void alarm_core_tick(alarm_ctx_t *ctx);

#endif /* __ALARM_CORE_H__ */
