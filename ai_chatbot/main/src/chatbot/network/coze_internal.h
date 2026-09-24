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
     completed/failed/canceled）。仅在该状态为真时才应发送 conversation.chat.cancel 打断；
     AI 空闲（如开场白播完）时按下 PTT 只是开启新一轮输入，发 cancel 会把会话模型终止，
     导致后续同一连接内新 chat 报 "model has been terminated"。 */
qosa_bool_t coze_protocol_is_chat_busy(void);
/*!< 标记一次打断 cancel 已实际发送成功（发送端调用）：进入"待服务端 canceled 确认"
     窗口，窗口内下行的 sentence_start 不得解除静音门控（cancel 后服务端仍会把残余旧回答
     连同其新句子继续下发数秒，直到回 canceled 才停）。 */
void coze_protocol_mark_cancel_sent(void);
/*!< cancel 卡死兜底：服务端一直未回 canceled、且本地已判定残余停发时，
     由 audio_task 兜底心跳调用，强制复位 cancel 待确认与 busy 状态。 */
void coze_protocol_cancel_stuck_resolved(void);

/* ------------------------------ coze_audio_task.c ------------------------------ */
int  coze_audio_tasks_start(void);
void coze_audio_tasks_stop(void);
void coze_audio_set_session_mode(chat_session_mode_e mode);
void coze_audio_set_ptt_pressed(qosa_bool_t pressed);
/*!< pcm 所有权转移给下行任务，由下行任务负责 qosa_free */
void coze_audio_downlink_push(qosa_uint8_t *pcm, int len);
/*!< 打断时调用（本地 VAD SPEECH_START / PTT 按下 / 服务端 speech_started 兜底）：
     清空本地待播队列、停播放流并置下行静音门控 */
void coze_audio_flush_downlink(void);
/*!< 服务端开始新一轮回答（sentence_start / chat.created/in_progress）时调用：
     解除打断(flush)后的下行静音门控，恢复新一轮音频播放 */
void coze_audio_downlink_resume(void);

/* ------------------------------ coze_session.c ------------------------------ */
chat_session_mode_e coze_session_get_mode(void);

#endif /* __COZE_INTERNAL_H__ */
