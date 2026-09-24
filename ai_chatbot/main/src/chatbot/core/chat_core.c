#include <string.h>

#include "qosa_def.h"
#include "qosa_sys.h"
#include "qosa_log.h"
#include "qosa_cJSON.h"

#define QOS_LOG_TAG LOG_TAG_DEMO

#include "chat_core.h"
#include "chat_bus.h"
#include "chatbot_types.h"
#include "chatbot_config.h"
#include "chatbot_tool.h"
#include "peripherals.h"
#include "coze_network.h"

static qosa_task_t     g_core_task = QOSA_NULL;
static chat_state_e    g_state = CHAT_STATE_IDLE;
static chat_substate_e g_substate = CHAT_SUBSTATE_LISTENING;
static qosa_uint32_t   g_idle_elapsed_ms = 0;
static qosa_uint32_t   g_error_backoff_elapsed_ms = 0;
static chat_session_mode_e g_session_mode = CHAT_SESSION_MODE_VOICE;
static qosa_bool_t     g_button_pressed = QOSA_FALSE;
static qosa_bool_t     g_button_wake_wait_release = QOSA_FALSE;
static qosa_uint32_t   g_button_press_elapsed_ms = 0;

/**
 * @brief 处理一次云端工具调用请求：解析 evt->text 中的紧凑 JSON，分发到工具层，
 *        并将结果通过控制面回传云端。
 *
 * @note evt->text 受 CHAT_EVENT_TEXT_MAX 限制，仅适用于入参较小的工具（如 LED 开关）；
 *       如后续新增大入参工具，需要评估改用堆内存传递。
 */
static void chat_core_process_tool_call(const char *packed_json)
{
    char        result_json[128] = {0};
    const char *chat_id = "";
    const char *tool_call_id = "";
    const char *tool_name = "";
    char        args_json[128] = {0};

    Q_cJSON *root = Q_cJSON_Parse(packed_json);
    if (root == QOSA_NULL)
    {
        QLOGW("[chat_core] tool call json parse failed: %s", packed_json);
        return;
    }

    Q_cJSON *chat_id_item = Q_cJSON_GetObjectItem(root, "chat_id");
    Q_cJSON *id_item = Q_cJSON_GetObjectItem(root, "tool_call_id");
    Q_cJSON *name_item = Q_cJSON_GetObjectItem(root, "tool_name");
    Q_cJSON *args_item = Q_cJSON_GetObjectItem(root, "arguments");

    if (Q_cJSON_IsString(chat_id_item))
    {
        chat_id = chat_id_item->valuestring;
    }
    if (Q_cJSON_IsString(id_item))
    {
        tool_call_id = id_item->valuestring;
    }
    if (Q_cJSON_IsString(name_item))
    {
        tool_name = name_item->valuestring;
    }
    if (args_item != QOSA_NULL)
    {
        char *args_str = Q_cJSON_PrintUnformatted(args_item);
        if (args_str != QOSA_NULL)
        {
            qosa_snprintf(args_json, sizeof(args_json), "%s", args_str);
            Q_cJSON_free(args_str);
        }
    }

    QLOGI("[chat_core] tool call: name=%s args=%s", tool_name, args_json);
    (void)tool_registry_dispatch(tool_name, args_json, result_json, sizeof(result_json));
    /* 打印工具执行结果（每次调用一条，低频），确认本地工具已执行并拿到返回值。 */
    QLOGI("[chat_core] tool result: name=%s result=%.128s", tool_name, result_json);
    (void)coze_session_send_tool_result(chat_id, tool_call_id, result_json);

    Q_cJSON_Delete(root);
}

/**
 * @brief 消费一条事件并推进聊天核心状态机。
 *
 * @param[in] evt 待处理的聊天事件，事件文本由事件总线管理其存储。
 */
static void chat_core_handle_event(const chat_event_t *evt)
{
    switch (evt->type)
    {
        case CHAT_EVT_WAKE_DETECTED:
            if (g_state == CHAT_STATE_IDLE)
            {
                QLOGI("[chat_core] wake detected, requesting session start");
                g_state = CHAT_STATE_CONNECTING;
                g_session_mode = CHAT_SESSION_MODE_VOICE;
                g_button_wake_wait_release = QOSA_FALSE;
                indicator_apply_state(g_state, g_substate);
                (void)coze_session_request_start(g_session_mode);
            }
            break;

        case CHAT_EVT_BUTTON_PRESSED:
            g_button_pressed = QOSA_TRUE;
            g_button_press_elapsed_ms = 0;
            if (g_state == CHAT_STATE_ACTIVE && (g_session_mode == CHAT_SESSION_MODE_BUTTON) && (g_button_wake_wait_release == QOSA_FALSE))
            {
                g_idle_elapsed_ms = 0;
                coze_audio_set_ptt_pressed(QOSA_TRUE);
                g_substate = CHAT_SUBSTATE_LISTENING;
                indicator_apply_state(g_state, g_substate);
            }
            break;

        case CHAT_EVT_BUTTON_RELEASED:
            if (g_button_wake_wait_release == QOSA_TRUE)
            {
                QLOGI("[chat_core] button wake press released, ptt ready");
                g_button_wake_wait_release = QOSA_FALSE;
                g_idle_elapsed_ms = 0;
            }
            else if (g_state == CHAT_STATE_ACTIVE && (g_session_mode == CHAT_SESSION_MODE_BUTTON) && (g_button_pressed == QOSA_TRUE))
            {
                g_idle_elapsed_ms = 0;
                coze_audio_set_ptt_pressed(QOSA_FALSE);
                (void)coze_session_send_audio_complete();
                g_substate = CHAT_SUBSTATE_THINKING;
                indicator_apply_state(g_state, g_substate);
            }
            g_button_pressed = QOSA_FALSE;
            g_button_press_elapsed_ms = 0;
            break;

        case CHAT_EVT_NET_READY:
            if (g_state == CHAT_STATE_CONNECTING)
            {
                (void)coze_session_request_start(g_session_mode);
            }
            break;

        case CHAT_EVT_NET_LOST:
            if (g_state != CHAT_STATE_IDLE)
            {
                coze_session_stop();
                g_state = CHAT_STATE_ERROR;
                g_error_backoff_elapsed_ms = 0;
                indicator_apply_state(g_state, g_substate);
            }
            break;

        case CHAT_EVT_WS_CONNECTED:
            QLOGI("[chat_core] ws connected event, start audio tasks");
            if (coze_session_on_connected() != 0)
            {
                QLOGE("[chat_core] ws connected but audio tasks start failed");
                coze_session_stop();
                g_state = CHAT_STATE_ERROR;
                g_error_backoff_elapsed_ms = 0;
                indicator_apply_state(g_state, g_substate);
                break;
            }
            QLOGI("[chat_core] audio tasks start ok, enter active state");
            g_state = CHAT_STATE_ACTIVE;
            g_substate = CHAT_SUBSTATE_LISTENING;
            g_idle_elapsed_ms = 0;
            indicator_apply_state(g_state, g_substate);
            break;

        case CHAT_EVT_WS_CONNECT_FAILED:
        case CHAT_EVT_WS_DISCONNECTED:
            coze_session_stop();
            g_state = CHAT_STATE_ERROR;
            g_error_backoff_elapsed_ms = 0;
            g_button_wake_wait_release = QOSA_FALSE;
            indicator_apply_state(g_state, g_substate);
            break;

        case CHAT_EVT_SPEECH_STARTED:
            if (g_state == CHAT_STATE_ACTIVE)
            {
                g_substate = CHAT_SUBSTATE_LISTENING;
                g_idle_elapsed_ms = 0;
                indicator_apply_state(g_state, g_substate);
            }
            break;

        case CHAT_EVT_SPEECH_STOPPED:
            if (g_state == CHAT_STATE_ACTIVE)
            {
                g_substate = CHAT_SUBSTATE_THINKING;
                /* 有语音交互即视为活跃，重置空闲计时（否则 AI 答话期间 idle 一直累加,
                   长回复/工具调用耗时接近超时上限时会被误判空闲断开） */
                g_idle_elapsed_ms = 0;
                indicator_apply_state(g_state, g_substate);
            }
            break;

        case CHAT_EVT_SERVER_TEXT:
            /* 过滤 verbose 内部消息（多 answer 场景的收尾标记/空结果，无业务价值），
               仅打印真实 answer 文本并截断。 */
            if ((strstr(evt->text, "generate_answer_finish") == QOSA_NULL) &&
                (strstr(evt->text, "empty result") == QOSA_NULL))
            {
                QLOGI("[chat_core] server text: %.128s", evt->text);
            }
            if (g_state == CHAT_STATE_ACTIVE)
            {
                /* 回复文本到达，随后云端会陆续下发 audio.delta，进入播报显示子态。 */
                g_substate = CHAT_SUBSTATE_SPEAKING;
                g_idle_elapsed_ms = 0;
                indicator_apply_state(g_state, g_substate);
            }
            break;

        case CHAT_EVT_CHAT_COMPLETED:
            if (g_state == CHAT_STATE_ACTIVE)
            {
                g_substate = CHAT_SUBSTATE_LISTENING;
                g_idle_elapsed_ms = 0;
                indicator_apply_state(g_state, g_substate);
            }
            break;

        case CHAT_EVT_TOOL_CALL_REQUEST:
            chat_core_process_tool_call(evt->text);
            break;

        case CHAT_EVT_SERVER_ERROR:
            QLOGW("[chat_core] server error: %s", evt->text);
            break;

        default:
            break;
    }
}

/**
 * @brief 上报数据面活跃心跳，重置会话空闲计时（仅 ACTIVE 状态生效）。
 */
void chat_core_ping_activity(void)
{
    /* 数据面活跃心跳：仅 ACTIVE 会话态重置空闲计时，其余状态无效。 */
    if (g_state == CHAT_STATE_ACTIVE)
    {
        g_idle_elapsed_ms = 0;
    }
}

/**
 * @brief 执行核心状态机的周期性超时和退避处理。
 */
static void chat_core_tick(void)
{
    switch (g_state)
    {
        case CHAT_STATE_ERROR:
            g_error_backoff_elapsed_ms += CHATBOT_CORE_TICK_PERIOD_MS;
            if (g_error_backoff_elapsed_ms >= CHATBOT_RECONNECT_BACKOFF_MS)
            {
                QLOGI("[chat_core] backoff finished, back to IDLE waiting next wake word");
                g_state = CHAT_STATE_IDLE;
                g_error_backoff_elapsed_ms = 0;
                indicator_apply_state(g_state, g_substate);
            }
            break;

        case CHAT_STATE_ACTIVE:
            g_idle_elapsed_ms += CHATBOT_CORE_TICK_PERIOD_MS;
            if (g_idle_elapsed_ms >= CHATBOT_SESSION_IDLE_TIMEOUT_MS)
            {
                QLOGI("[chat_core] session idle timeout, stop session");
                coze_session_stop();
                g_state = CHAT_STATE_IDLE;
                indicator_apply_state(g_state, g_substate);
            }
            break;

        case CHAT_STATE_IDLE:
            if (g_button_pressed == QOSA_TRUE)
            {
                g_button_press_elapsed_ms += CHATBOT_CORE_TICK_PERIOD_MS;
                if (g_button_press_elapsed_ms >= CHATBOT_TALK_BUTTON_LONG_PRESS_MS)
                {
                    QLOGI("[chat_core] button long press, requesting button session");
                    g_state = CHAT_STATE_CONNECTING;
                    g_session_mode = CHAT_SESSION_MODE_BUTTON;
                    g_button_wake_wait_release = QOSA_TRUE;
                    g_button_press_elapsed_ms = 0;
                    indicator_apply_state(g_state, g_substate);
                    (void)coze_session_request_start(g_session_mode);
                }
            }
            break;

        default:
            break;
    }
}

/**
 * @brief 核心状态机任务入口：循环消费事件，无事件时按周期驱动超时与退避处理。
 *
 * @param[in] ctx 任务上下文，未使用。
 */
static void chat_core_task_process(void *ctx)
{
    chat_event_t evt = {0};

    while (1)
    {
        int ret = chat_bus_wait_event(&evt, CHATBOT_CORE_TICK_PERIOD_MS);
        if (ret == 0)
        {
            chat_core_handle_event(&evt);
        }
        else
        {
            chat_core_tick();
        }
    }
}

/**
 * @brief 初始化聊天核心：创建事件总线与核心状态机任务，并应用初始指示灯状态。
 *
 * @return 0 表示成功；返回负值表示事件总线或任务创建失败。
 */
int chat_core_init(void)
{
    {
        QLOGE("[chat_core] chat bus init failed");
        return -1;
    }

    int ret = qosa_task_create(
        &g_core_task,
        CHATBOT_CORE_TASK_STACK_SIZE,
        CHATBOT_CORE_TASK_PRIORITY,
        "chatbot_core",
        chat_core_task_process,
        QOSA_NULL);
    if (ret != 0)
    {
        QLOGE("[chat_core] task create failed, ret=%d", ret);
        return -1;
    }

    indicator_apply_state(g_state, g_substate);
    return 0;
}
