#include "qosa_def.h"
#include "qosa_log.h"
#include "qcm_websocket.h"

#define QOS_LOG_TAG LOG_TAG_DEMO

#include "coze_internal.h"
#include "coze_network.h"

static chat_session_mode_e g_session_mode = CHAT_SESSION_MODE_VOICE;

/**
 * @brief 初始化网络管理与 Coze 会话依赖。
 *
 * @return 0 表示初始化流程已完成；网络就绪由异步事件继续通知。
 */
int coze_network_init(void)
{
    if (net_mgr_init() != 0)
    {
        QLOGW("[chat_coze_session] net_mgr_init failed, network readiness event unavailable");
    }
    return 0;
}

/**
 * @brief 请求建立指定模式的 Coze 会话。
 *
 * @param[in] mode 会话输入模式，语音模式或按键 PTT 模式。
 * @return 0 表示请求已受理；返回负值表示连接请求失败。
 */
int coze_session_request_start(chat_session_mode_e mode)
{
    g_session_mode = mode;
    if (net_mgr_is_ready() == QOSA_FALSE)
    {
        (void)net_mgr_request_ready();
        QLOGI("[chat_coze_session] network not ready yet, requested PDP activate and will retry on CHAT_EVT_NET_READY");
        return 0;
    }

    return coze_client_connect();
}

/**
 * @brief 获取当前会话输入模式。
 *
 * @return 当前会话模式。
 */
chat_session_mode_e coze_session_get_mode(void)
{
    return g_session_mode;
}

/**
 * @brief WebSocket 建连成功后的会话拉起入口：设置会话模式并启动音频上/下行任务。
 *
 * @return 0 表示音频任务已启动；返回负值表示启动失败。
 */
int coze_session_on_connected(void)
{
    QLOGI("[chat_coze_session] ws connected, start audio tasks");
    coze_audio_set_session_mode(g_session_mode);
    int ret = coze_audio_tasks_start();
    QLOGI("[chat_coze_session] audio tasks start result=%d", ret);
    return ret;
}

/**
 * @brief 停止音频任务并关闭当前 Coze WebSocket 会话。
 */
void coze_session_stop(void)
{
    coze_audio_tasks_stop();
    coze_client_close();
}

/**
 * @brief 发送 conversation.chat.cancel 打断事件，并在发送成功后进入待确认窗口。
 *
 * @return 0 表示发送成功；返回负值表示构造或发送失败。
 */
int coze_session_send_cancel(void)
{
    char buf[128] = {0};
    int  len = coze_protocol_build_cancel_event(buf, sizeof(buf));
    if (len <= 0)
    {
        return -1;
    }
    QLOGI("[chat_coze_session] send cancel: %s", buf);
    int ret = coze_client_send_raw(buf, len, QCM_WEB_OPCODE_TEXT);
    if (ret == 0)
    {
        /* 发送成功才进入"待服务端 canceled 确认"窗口：窗口内 sentence_start
           不会解除下行静音门控，防止 cancel 后服务端仍下发的残余旧回答被当作新一轮播出。 */
        coze_protocol_mark_cancel_sent();
    }
    return ret;
}

/**
 * @brief 发送 input_audio_buffer.complete 事件，通知服务端当前语音段结束。
 *
 * @return 0 表示发送成功；返回负值表示构造或发送失败。
 */
int coze_session_send_audio_complete(void)
{
    char buf[128] = {0};
    int  len = coze_protocol_build_audio_complete_event(buf, sizeof(buf));
    if (len <= 0)
    {
        return -1;
    }
    QLOGI("[chat_coze_session] send audio complete: %s", buf);
    return coze_client_send_raw(buf, len, QCM_WEB_OPCODE_TEXT);
}

/**
 * @brief 发送 conversation.chat.submit_tool_outputs 事件，回传本地工具执行结果。
 *
 * @param[in] chat_id      当前会话 ID（requires_action 事件 data.id）。
 * @param[in] tool_call_id 当前工具调用 ID。
 * @param[in] result_json  工具执行结果 JSON 文本。
 * @return 0 表示发送成功；返回负值表示构造、转义或发送失败。
 */
int coze_session_send_tool_result(const char *chat_id, const char *tool_call_id, const char *result_json)
{
    /* 缓冲区 768：submit_tool_outputs 需容纳 chat_id + tool_call_id + 转义后的
       output + JSON 骨架，超出缓冲会导致截断（构造函数会拒绝并返回负值）。 */
    char buf[768] = {0};
    int  len = coze_protocol_build_tool_result_event(buf, sizeof(buf), chat_id, tool_call_id, result_json);
    if ((len <= 0) || (len >= (int)sizeof(buf)))
    {
        QLOGE("[chat_coze_session] build tool result failed/truncated, len=%d cap=%d", len, (int)sizeof(buf));
        return -1;
    }
    /* 与 send cancel / send audio complete 一致打印上行事件（低频）。 */
    QLOGI("[chat_coze_session] send tool result: %s", buf);
    return coze_client_send_raw(buf, len, QCM_WEB_OPCODE_TEXT);
}
