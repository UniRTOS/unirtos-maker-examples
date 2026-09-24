/**
 * @file chatbot_types.h
 * @brief 事件总线与核心状态机共用的类型定义。
 */
#ifndef __CHATBOT_TYPES_H__
#define __CHATBOT_TYPES_H__

#include "qosa_def.h"

/**
 * @enum chat_state_e
 * @brief 核心状态机状态。持续对话态（ACTIVE）下的"聆听中/思考中/播报中"仅为显示子态，
 *        不是独立的状态机状态，详见 chat_substate_e。
 */
typedef enum
{
    CHAT_STATE_IDLE = 0, /*!< 待机，仅唤醒模块工作 */
    CHAT_STATE_WAKING,   /*!< 命中唤醒词，准备建立会话 */
    CHAT_STATE_CONNECTING, /*!< 正在建立 Coze 实时语音连接 */
    CHAT_STATE_ACTIVE,   /*!< 持续对话态：音频上行/下行任务常驻运行 */
    CHAT_STATE_ERROR,    /*!< 异常态：等待退避重连 */
} chat_state_e;

/**
 * @enum chat_substate_e
 * @brief ACTIVE 状态下的显示子态，仅用于驱动 LED/蜂鸣器反馈。
 */
typedef enum
{
    CHAT_SUBSTATE_LISTENING = 0, /*!< 聆听中 */
    CHAT_SUBSTATE_THINKING,      /*!< 思考中 */
    CHAT_SUBSTATE_SPEAKING,      /*!< 播报中 */
} chat_substate_e;

typedef enum
{
    CHAT_SESSION_MODE_VOICE = 0,
    CHAT_SESSION_MODE_BUTTON,
} chat_session_mode_e;

/**
 * @enum chat_event_type_e
 * @brief 中介模块（事件总线）承载的控制类事件类型，不含音频数据。
 */
typedef enum
{
    CHAT_EVT_WAKE_DETECTED = 0,   /*!< 外设层：唤醒词命中 */
    CHAT_EVT_BUTTON_PRESSED,      /*!< 外设层：按键按下 */
    CHAT_EVT_BUTTON_RELEASED,     /*!< 外设层：按键松开 */
    CHAT_EVT_NET_READY,           /*!< 通信层：网络就绪 */
    CHAT_EVT_NET_LOST,            /*!< 通信层：网络异常 */
    CHAT_EVT_WS_CONNECTED,        /*!< 通信层：Coze 会话建立成功（对应 chat.created） */
    CHAT_EVT_WS_CONNECT_FAILED,   /*!< 通信层：Coze 会话建立失败 */
    CHAT_EVT_WS_DISCONNECTED,     /*!< 通信层：Coze 会话断开 */
    CHAT_EVT_SPEECH_STARTED,      /*!< 通信层：收到服务端 speech_started（仅 server_vad 模式会下发；本工程 client_interrupt 下不触发，保留兜底） */
    CHAT_EVT_SPEECH_STOPPED,      /*!< 通信层：收到服务端 speech_stopped（仅 server_vad 模式会下发；本工程 client_interrupt 下不触发，保留兜底） */
    CHAT_EVT_SERVER_TEXT,         /*!< 通信层：收到云端文本/字幕事件 */
    CHAT_EVT_CHAT_COMPLETED,      /*!< 通信层：一轮对话完成 */
    CHAT_EVT_TOOL_CALL_REQUEST,   /*!< 通信层：收到云端工具调用请求 */
    CHAT_EVT_SERVER_ERROR,        /*!< 通信层：服务端错误事件 */
} chat_event_type_e;

#define CHAT_EVENT_TEXT_MAX 192

/**
 * @struct chat_event_t
 * @brief 事件总线传递的统一事件结构，仅承载控制类小数据，不承载音频帧。
 */
typedef struct
{
    chat_event_type_e type;
    char              text[CHAT_EVENT_TEXT_MAX]; /*!< 附加文本，按 type 解释；未使用时为空串 */
} chat_event_t;

#endif /* __CHATBOT_TYPES_H__ */
