/**
 * @file chat_core.h
 * @brief 核心逻辑层：以状态机形式编排对话生命周期，是全局唯一的业务决策中心。
 */
#ifndef __CHAT_CORE_H__
#define __CHAT_CORE_H__

/**
 * @brief 初始化核心状态机并创建其事件消费任务。
 * @return 0 成功，非 0 失败。
 */
int chat_core_init(void);

/**
 * @brief 活跃心跳：数据面（如下行音频播放帧）发生活动时调用,
 *        重置会话空闲计时，避免"AI 播放中/持续下行"被误判为空闲断开。
 *        仅在 ACTIVE 态生效。
 */
void chat_core_ping_activity(void);

#endif /* __CHAT_CORE_H__ */
