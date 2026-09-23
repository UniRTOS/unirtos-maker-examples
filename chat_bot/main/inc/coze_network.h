/**
 * @file coze_network.h
 * @brief 通信层对外接口：注网状态监听、Coze 实时语音会话生命周期管理。
 *
 * 通信层内部还包含两个专职的音频数据面任务（音频上行/下行任务），它们只在
 * chat_core 通过本头文件提供的会话生命周期接口驱动下启动/停止，音频数据本身
 * 不经过事件总线（见 chat_bus.h）。
 */
#ifndef __COZE_NETWORK_H__
#define __COZE_NETWORK_H__

#include "qosa_def.h"
#include "chatbot_types.h"

/* ------------------------------ 注网状态监听 ------------------------------ */
/**
 * @brief 初始化网络管理并注册 PDP 状态事件。
 *
 * @return 0 表示成功；返回负值表示初始化失败。
 */
int net_mgr_init(void);

/**
 * @brief 查询 Coze 数据连接是否已就绪。
 *
 * @return QOSA_TRUE 表示已就绪；否则返回 QOSA_FALSE。
 */
qosa_bool_t net_mgr_is_ready(void);

/**
 * @brief 请求激活 Coze 使用的数据连接。
 *
 * @return 0 表示已就绪或请求已提交；返回负值表示请求失败。
 */
int net_mgr_request_ready(void);

/* ------------------------------ 会话生命周期（供 chat_core 调用） ------------------------------ */

/**
 * @brief 初始化通信层（网络监听 + WebSocket 客户端 + 协议编解码），进程内调用一次。
 */
int coze_network_init(void);

/**
 * @brief 幂等地请求建立一次 Coze 实时语音会话：若网络未就绪则等待网络就绪事件驱动的
 *        再次调用；若已连接或正在连接则直接返回。建连结果通过事件总线的
 *        CHAT_EVT_WS_CONNECTED / CHAT_EVT_WS_CONNECT_FAILED 通知核心状态机。
 */
int coze_session_request_start(chat_session_mode_e mode);

/**
 * @brief 在收到 chat.created 后，由核心任务上下文调用以启动音频上/下行任务。
 *
 * @note 不应在 WebSocket 回调上下文直接执行音频任务启动，避免阻塞回调线程。
 */
int coze_session_on_connected(void);

/**
 * @brief 停止当前会话：停止音频上行/下行任务并关闭 WebSocket 连接。
 */
void coze_session_stop(void);

/**
 * @brief 主动发送打断/取消当前回复事件（conversation.chat.cancel）。
 *        用于本地判定用户开口打断（本地 VAD SPEECH_START / PTT 按下）且服务端
 *        有对话进行中时，取消 AI 正在进行的回复生成。
 */
int coze_session_send_cancel(void);

/**
 * @brief 提交当前客户端控制的语音段，通知 Coze 开始处理本轮输入。
 */
int coze_session_send_audio_complete(void);

/**
 * @brief 按键会话中设置 PTT 按住/松开状态；按下会触发本地打断并打开上行门控。
 */
void coze_audio_set_ptt_pressed(qosa_bool_t pressed);

/**
 * @brief 供工具层在完成工具调用后，将结果通过控制面回传云端。
 *
 * @param[in] chat_id       触发本次工具调用的对话 ID（对应 requires_action 事件 data.id）。
 * @param[in] tool_call_id  工具调用 ID（对应 tool_calls[].id）。
 * @param[in] result_json   工具执行结果 JSON 字符串。
 */
int coze_session_send_tool_result(const char *chat_id, const char *tool_call_id, const char *result_json);

#endif /* __COZE_NETWORK_H__ */
