/**
 * @file chatbot_app.h
 * @brief AI 语音聊天机器人应用对外初始化入口。
 */
#ifndef __CHATBOT_APP_H__
#define __CHATBOT_APP_H__

/**
 * @brief 初始化并启动 AI 语音聊天机器人应用。
 *
 * 依次初始化外设层（唤醒模块/编解码器/LED）、工具层、音频层、通信层与核心状态机，
 * 任一模块初始化失败仅记录降级日志，不阻塞后续模块启动。
 *
 * @return void
 */
void chatbot_init(void);

#endif /* __CHATBOT_APP_H__ */
