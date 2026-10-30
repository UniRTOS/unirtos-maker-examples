/**
 * @file coze_internal.h
 * @brief 通信层（network/）内部模块间共享的声明，不对外（chatbot core/tools）暴露。
 */
#ifndef __COZE_INTERNAL_H__
#define __COZE_INTERNAL_H__

#include "qosa_def.h"
#include "qcm_websocket.h"
#include "chatbot_types.h"

/* ------------------------------ coze_client.c ------------------------------ */
int  coze_client_connect(void);
int  coze_client_send_raw(const char *data, int len, qcm_web_opcode_e opcode);
void coze_client_close(void);
/*!< 主动轮询排空接收水位线：兜底 SDK 内部 QCM_WEB_MSG_TCP_RECV_DATA_REPORT 通知
     在消息队列拥塞时被静默丢弃导致的下行数据卡死，需由高频任务周期性调用 */
void coze_client_poll_recv(void);

/* ------------------------------ coze_protocol.c ------------------------------ */
int  coze_protocol_build_update_event(char *buf, int buf_len, chat_session_mode_e mode);
int  coze_protocol_build_audio_append_event(char *buf, int buf_len, const qosa_uint8_t *pcm, int pcm_len);
int  coze_protocol_build_audio_complete_event(char *buf, int buf_len);
int  coze_protocol_build_cancel_event(char *buf, int buf_len);
int  coze_protocol_build_tool_result_event(char *buf, int buf_len, const char *chat_id, const char *tool_call_id, const char *result_json);
void coze_protocol_dispatch_downlink(const char *raw, int raw_len);
/*!< 查询服务端是否有一轮对话正在进行（chat.created/in_progress 后未收到
     completed/failed/canceled）。仅按键 PTT 模式需要据此决定是否发
     conversation.chat.cancel 打断；AI 空闲（如开场白播完）时按下 PTT 只是开启新一轮
     输入，发 cancel 会把会话模型终止，导致后续同一连接内新 chat 报
     "model has been terminated"。server_vad（VOICE）模式不使用该判定——打断由服务端完成。 */
qosa_bool_t coze_protocol_is_chat_busy(void);

/* ------------------------------ coze_audio_task.c ------------------------------ */
int  coze_audio_tasks_start(void);
void coze_audio_tasks_stop(void);
void coze_audio_set_session_mode(chat_session_mode_e mode);
void coze_audio_set_ptt_pressed(qosa_bool_t pressed);
/*!< pcm 所有权转移给下行任务，由下行任务负责 qosa_free */
void coze_audio_downlink_push(qosa_uint8_t *pcm, int len);
/*!< 用户打断：立即关闭本地播放并清除待播尾音，并在随后一个时间有界的窗口内
     丢弃服务端在途的旧回复残余帧。调用方：
       - VOICE（server_vad）模式：收到服务端 input_audio_buffer.speech_started
         （服务端判定用户起声，即打断发生的那一刻）；
       - BUTTON（PTT）模式：按下按键准备说话时。
     窗口到点自动过期（不依赖任何对端事件解除），故不会永久静音或永久丢弃下行；
     队列空且播放流未打开时为空操作；两次清尾间隔受冷却限制。 */
void coze_audio_flush_downlink(void);

/* ------------------------------ coze_session.c ------------------------------ */
chat_session_mode_e coze_session_get_mode(void);

#endif /* __COZE_INTERNAL_H__ */
