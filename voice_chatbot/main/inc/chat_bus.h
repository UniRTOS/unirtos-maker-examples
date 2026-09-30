/**
 * @file chat_bus.h
 * @brief 中介模块（事件总线）：核心逻辑层与外设层/通信层之间唯一的低频控制事件通道。
 *
 * 明确不承载音频数据：音频帧的采集/发送、接收/播放走通信层内的独立直连任务
 * （见 coze_network.h），不进入本模块的队列。
 */
#ifndef __CHAT_BUS_H__
#define __CHAT_BUS_H__

#include "chatbot_types.h"

/**
 * @brief 初始化事件总线（创建消息队列），进程内仅需调用一次。
 * @return 0 成功，非 0 失败。
 */
int chat_bus_init(void);

/**
 * @brief 投递一个控制类事件，队列满时直接丢弃并返回失败（不阻塞调用方）。
 * @return 0 成功，非 0 失败。
 */
int chat_bus_post_event(const chat_event_t *event);

/**
 * @brief 等待并取出一个事件。
 *
 * @param[out] event      取出的事件。
 * @param[in]  timeout_ms  等待超时时间（毫秒），QOSA_WAIT_FOREVER 表示一直等待。
 * @return 0 成功取到事件；非 0 超时或失败。
 */
int chat_bus_wait_event(chat_event_t *event, qosa_uint32_t timeout_ms);

#endif /* __CHAT_BUS_H__ */
