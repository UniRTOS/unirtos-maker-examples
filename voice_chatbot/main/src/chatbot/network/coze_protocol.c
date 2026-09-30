#include <string.h>

#include "qosa_def.h"
#include "qosa_sys.h"
#include "qosa_log.h"
#include "qosa_cJSON.h"
#include "qcm_base64.h"

#define QOS_LOG_TAG LOG_TAG_DEMO

#include "coze_internal.h"
#include "coze_network.h"
#include "coze_events.h"
#include "chatbot_config.h"
#include "chatbot/audio/g711.h"
#include "chatbot_types.h"
#include "chat_bus.h"

/*!< 服务端是否有一轮对话正在进行（conversation.chat.created/in_progress 置真；
     completed/failed/canceled 置假）。coze_audio_task 用它在按下 PTT / VAD 起声时
     决定是否需要发 conversation.chat.cancel 打断——若 AI 空闲也发 cancel，
     服务端会把当前会话模型终止，导致同连接后续新 chat 报 model has been terminated。 */
static volatile qosa_bool_t s_chat_busy = QOSA_FALSE;

/*!< 打断 cancel 确认门控：本地已发 conversation.chat.cancel、但服务端尚未返回
     conversation.chat.canceled 确认期间置真。cancel 发出后服务端仍会把残余旧回答连同
     其新句子继续下发数秒，直到回 canceled 才停。若此时把 sentence_start 误判为"新一轮开始"
     而解除下行静音门控，残余旧回答会在用户说完话后继续被播完。因此在收到 canceled
     （或一轮自然结束/新一轮开始）之前，sentence_start 一律不得解除 muted。 */
static volatile qosa_bool_t s_cancel_pending = QOSA_FALSE;

/*!< 最近一次发送的 cancel 事件 id（dev_evt_N）。服务端 canceled 回执按官方文档
     回显该 id（id 为客户端自行生成的事件 ID），用于确证服务端是否正确关联、
     执行了本次 cancel。 */
static char s_last_cancel_id[40] = {0};

/*!< 标记一次打断 cancel 已实际发送（send_raw 成功后调用），进入"待服务端确认"窗口。 */
void coze_protocol_mark_cancel_sent(void)
{
    s_cancel_pending = QOSA_TRUE;
    QLOGI("[chat_coze_protocol] cancel sent, awaiting canceled confirm");
}

/*!< cancel 卡死兜底：服务端一直未回 conversation.chat.canceled（cancel 已生效、
     残余已停发但确认事件丢失/迟到），由下行任务心跳在"连续数秒无残余帧到达"时调用，
     强制复位 cancel 待确认与 busy 状态。复位后：
     - 下一个 sentence_start 不会再被 cancel_pending 拦下（可正常 resume）；
     - 下一轮 VAD 起声时 busy=FALSE 不会误发 cancel（避免空闲时误发终止会话模型）。 */
void coze_protocol_cancel_stuck_resolved(void)
{
    s_cancel_pending = QOSA_FALSE;
    s_chat_busy = QOSA_FALSE;
    QLOGW("[chat_coze_protocol] cancel stuck resolved: force reset cancel pending & busy");
}

/**
 * @brief 生成客户端事件 ID（官方文档要求上行事件携带 id 字段，便于服务端定位问题）。
 */
static void coze_protocol_next_event_id(char *buf, int buf_len)
{
    static qosa_uint32_t s_event_seq = 0;
    s_event_seq++;
    qosa_snprintf(buf, buf_len, "dev_evt_%u", s_event_seq);
}

/**
 * @brief 组装会话建立后下发的 chat.update 配置事件。
 *
 * 字段结构以 Coze 官方文档为准：
 * https://docs.coze.cn/developer_guides_streaming_chat_event
 */
int coze_protocol_build_update_event(char *buf, int buf_len, chat_session_mode_e mode)
{
    Q_cJSON *root = Q_cJSON_CreateObject();
    if (root == QOSA_NULL)
    {
        return -1;
    }

    char id_buf[32] = {0};
    coze_protocol_next_event_id(id_buf, sizeof(id_buf));
    Q_cJSON_AddStringToObject(root, COZE_FIELD_ID, id_buf);
    Q_cJSON_AddStringToObject(root, COZE_FIELD_EVENT_TYPE, COZE_EVT_CHAT_UPDATE);
    Q_cJSON *data = Q_cJSON_AddObjectToObject(root, COZE_FIELD_DATA);

    Q_cJSON *chat_config = Q_cJSON_AddObjectToObject(data, COZE_FIELD_CHAT_CONFIG);
    Q_cJSON_AddBoolToObject(chat_config, COZE_FIELD_AUTO_SAVE_HISTORY, 1);
    Q_cJSON_AddStringToObject(chat_config, COZE_FIELD_USER_ID, CHATBOT_COZE_DEVICE_USER_ID);

    Q_cJSON *input_audio = Q_cJSON_AddObjectToObject(data, COZE_FIELD_INPUT_AUDIO);
    Q_cJSON_AddStringToObject(input_audio, COZE_FIELD_FORMAT, COZE_AUDIO_FORMAT_PCM);
#if CHATBOT_AUDIO_CODEC_G711A
    /* g711a/g711u 要求 format 仍为 pcm，采样率固定 8000（由 CHATBOT_AUDIO_SAMPLE_RATE 级联确保） */
    Q_cJSON_AddStringToObject(input_audio, COZE_FIELD_CODEC, COZE_AUDIO_CODEC_G711A);
    Q_cJSON_AddNumberToObject(input_audio, COZE_FIELD_BIT_DEPTH, 8);
#else
    Q_cJSON_AddStringToObject(input_audio, COZE_FIELD_CODEC, COZE_AUDIO_CODEC_PCM);
    Q_cJSON_AddNumberToObject(input_audio, COZE_FIELD_BIT_DEPTH, CHATBOT_AUDIO_BIT_DEPTH);
#endif
    Q_cJSON_AddNumberToObject(input_audio, COZE_FIELD_SAMPLE_RATE, CHATBOT_AUDIO_SAMPLE_RATE);
    Q_cJSON_AddNumberToObject(input_audio, COZE_FIELD_CHANNEL, CHATBOT_AUDIO_CHANNELS);

    Q_cJSON *output_audio = Q_cJSON_AddObjectToObject(data, COZE_FIELD_OUTPUT_AUDIO);
#if CHATBOT_AUDIO_CODEC_G711A
    Q_cJSON_AddStringToObject(output_audio, COZE_FIELD_CODEC, COZE_AUDIO_CODEC_G711A);
#else
    Q_cJSON_AddStringToObject(output_audio, COZE_FIELD_CODEC, COZE_AUDIO_CODEC_PCM);
#endif
    Q_cJSON *pcm_config = Q_cJSON_AddObjectToObject(output_audio, COZE_FIELD_PCM_CONFIG);
    /* codec=g711a 时服务端将 sample_rate 强制固定为 8000（官方文档），与此处一致，无需额外分支 */
    Q_cJSON_AddNumberToObject(pcm_config, COZE_FIELD_SAMPLE_RATE, CHATBOT_AUDIO_SAMPLE_RATE);
    Q_cJSON_AddNumberToObject(pcm_config, COZE_FIELD_FRAME_SIZE_MS, CHATBOT_AUDIO_FRAME_MS);
#if CHATBOT_COZE_DOWNLINK_LIMIT_ENABLE
    /* 下行音频限流（官方字段，且官方明确要求必须同时配置 frame_size_ms 才生效，见上两行）：
       服务端突发下发可能打满本地下行队列导致丢帧，此处通过 chat.update 的
       output_audio.pcm_config.limit_config 主动告知服务端按周期限流下发，
       把下发速率约束到"每 period 秒最多 max_frame_num 个 PCM 包"。 */
    Q_cJSON *limit_config = Q_cJSON_AddObjectToObject(pcm_config, COZE_FIELD_LIMIT_CONFIG);
    Q_cJSON_AddNumberToObject(limit_config, COZE_FIELD_LIMIT_PERIOD, CHATBOT_COZE_DOWNLINK_LIMIT_PERIOD_S);
    Q_cJSON_AddNumberToObject(limit_config, COZE_FIELD_LIMIT_MAX_FRAME_NUM, CHATBOT_COZE_DOWNLINK_LIMIT_FRAMES_PER_PERIOD);
    /* 确认限流参数确实随 chat.update 下发（服务端 chat.updated 会回显同样内容） */
    QLOGI("[chat_coze_protocol] chat.update downlink limit: frame_ms=%d period=%ds max_frame_num=%d (realtime=%d frames/s)",
          CHATBOT_AUDIO_FRAME_MS, CHATBOT_COZE_DOWNLINK_LIMIT_PERIOD_S,
          CHATBOT_COZE_DOWNLINK_LIMIT_FRAMES_PER_PERIOD, 1000 / CHATBOT_AUDIO_FRAME_MS);
#endif

    Q_cJSON_AddBoolToObject(data, COZE_FIELD_NEED_PLAY_PROLOGUE, 1);
#ifdef CHATBOT_COZE_PROLOGUE_CONTENT
    if (qosa_strlen(CHATBOT_COZE_PROLOGUE_CONTENT) > 0)
    {
        Q_cJSON_AddStringToObject(data, COZE_FIELD_PROLOGUE_CONTENT, CHATBOT_COZE_PROLOGUE_CONTENT);
    }
#endif

    Q_cJSON *turn_detection = Q_cJSON_AddObjectToObject(data, COZE_FIELD_TURN_DETECTION);
    (void)mode;
    Q_cJSON_AddStringToObject(turn_detection, COZE_FIELD_TYPE, COZE_TURN_DETECTION_CLIENT_INTERRUPT);

    Q_cJSON     *subs = Q_cJSON_AddArrayToObject(data, COZE_FIELD_EVENT_SUBSCRIPTIONS);
    const char *sub_list[] = COZE_DEFAULT_EVENT_SUBSCRIPTIONS;
    for (qosa_uint32_t i = 0; i < sizeof(sub_list) / sizeof(sub_list[0]); i++)
    {
        Q_cJSON_AddItemToArray(subs, Q_cJSON_CreateString(sub_list[i]));
    }

    int len = -1;
    char *printed = Q_cJSON_PrintUnformatted(root);
    if (printed != QOSA_NULL)
    {
        int printed_len = (int)qosa_strlen(printed);
        if (printed_len < buf_len)
        {
            qosa_memcpy(buf, printed, (qosa_size_t)printed_len + 1);
            len = printed_len;
        }
        else
        {
            QLOGE("[chat_coze_protocol] chat.update buffer too small, need=%d cap=%d", printed_len + 1, buf_len);
        }
        Q_cJSON_free(printed);
    }
    Q_cJSON_Delete(root);
    return len;
}

/**
 * @brief 构造 input_audio_buffer.append 音频上行事件。
 *
 * @param[out] buf     用于接收 JSON 事件的缓冲区。
 * @param[in]  buf_len 缓冲区容量，单位为字节。
 * @param[in]  pcm     待编码的 PCM 数据。
 * @param[in]  pcm_len PCM 数据长度，单位为字节。
 * @return JSON 事件长度；返回负值表示编码、构造或缓冲区失败。
 */
int coze_protocol_build_audio_append_event(char *buf, int buf_len, const qosa_uint8_t *pcm, int pcm_len)
{
    char b64_buf[CHATBOT_AUDIO_FRAME_B64_MAX] = {0};
    int  b64_cap = (int)sizeof(b64_buf) - 1;

    int enc_len = qcm_base64_encode((const char *)pcm, pcm_len, b64_buf, &b64_cap, QOSA_FALSE);
    if (enc_len <= 0)
    {
        QLOGW("[chat_coze_protocol] base64 encode failed, ret=%d", enc_len);
        return -1;
    }
    b64_buf[enc_len] = '\0';

    Q_cJSON *root = Q_cJSON_CreateObject();
    if (root == QOSA_NULL)
    {
        return -1;
    }
    char id_buf[32] = {0};
    coze_protocol_next_event_id(id_buf, sizeof(id_buf));
    Q_cJSON_AddStringToObject(root, COZE_FIELD_ID, id_buf);
    Q_cJSON_AddStringToObject(root, COZE_FIELD_EVENT_TYPE, COZE_EVT_INPUT_AUDIO_BUFFER_APPEND);
    Q_cJSON *data = Q_cJSON_AddObjectToObject(root, COZE_FIELD_DATA);
    Q_cJSON_AddStringToObject(data, COZE_FIELD_DELTA, b64_buf);

    int len = 0;
    char *printed = Q_cJSON_PrintUnformatted(root);
    if (printed != QOSA_NULL)
    {
        len = qosa_snprintf(buf, buf_len, "%s", printed);
        Q_cJSON_free(printed);
    }
    Q_cJSON_Delete(root);
    /* 截断守卫：qosa_snprintf 内容被截断时返回"本应写入长度"，若 >= buf_len 说明被截断，
       按该值外发会把残缺 JSON 发给服务端（服务端将拒绝并返回错误）。
       截断时返回 -1，上游跳过本帧而不是发送坏数据。 */
    if ((len < 0) || (len >= buf_len))
    {
        QLOGE("[chat_coze_protocol] audio append event truncated, need=%d cap=%d", len, buf_len);
        return -1;
    }
    return len;
}

/**
 * @brief 构造 input_audio_buffer.complete 音频提交事件。
 *
 * @param[out] buf     用于接收 JSON 事件的缓冲区。
 * @param[in]  buf_len 缓冲区容量，单位为字节。
 * @return JSON 事件长度；返回负值表示构造失败或缓冲区不足。
 */
int coze_protocol_build_audio_complete_event(char *buf, int buf_len)
{
    char id_buf[32] = {0};
    coze_protocol_next_event_id(id_buf, sizeof(id_buf));
    return qosa_snprintf(buf, buf_len, "{\"" COZE_FIELD_ID "\":\"%s\",\"" COZE_FIELD_EVENT_TYPE "\":\"" COZE_EVT_INPUT_AUDIO_BUFFER_COMPLETE "\"}", id_buf);
}

/**
 * @brief 构造 conversation.chat.cancel 打断事件。
 *
 * @param[out] buf     用于接收 JSON 事件的缓冲区。
 * @param[in]  buf_len 缓冲区容量，单位为字节。
 * @return JSON 事件长度；返回负值表示构造失败或缓冲区不足。
 */
int coze_protocol_build_cancel_event(char *buf, int buf_len)
{
    /* 官方文档（developer_guides_streaming_chat_event "打断智能体输出"）：
       conversation.chat.cancel 只需 id + event_type，无 data 字段，勿加 chat_id。 */
    char id_buf[32] = {0};
    coze_protocol_next_event_id(id_buf, sizeof(id_buf));
    qosa_strncpy(s_last_cancel_id, id_buf, sizeof(s_last_cancel_id) - 1);
    return qosa_snprintf(buf, buf_len,
                         "{\"" COZE_FIELD_ID "\":\"%s\",\"" COZE_FIELD_EVENT_TYPE "\":\"" COZE_EVT_CONVERSATION_CHAT_CANCEL "\"}",
                         id_buf);
}

/**
 * @brief 将 JSON 特殊字符转义后写入目标缓冲，用于把 result_json 作为 String 类型
 *        嵌入 data.tool_outputs[].output 字段（官方文档：output 为 String 类型）。
 *
 * @note 除 " 与 \\ 外，还需转义控制字符（\n \r \t 及其余 <0x20），否则若工具返回值含
 *       换行/制表符会产生非法 JSON，被服务端以 error code 4000 "invalid message" 拒绝。
 *
 * @return 0 成功；-1 目标缓冲不足（发生截断，调用方必须放弃发送，勿外发残缺 JSON）。
 */
static int escape_json_string(const char *src, char *dst, int dst_len)
{
    int di = 0;
    if ((src == QOSA_NULL) || (dst == QOSA_NULL) || (dst_len <= 1))
    {
        if ((dst != QOSA_NULL) && (dst_len > 0))
        {
            dst[0] = '\0';
        }
        return -1;
    }

    for (int si = 0; src[si] != '\0'; si++)
    {
        unsigned char c = (unsigned char)src[si];
        const char   *esc = QOSA_NULL;
        if (c == '"')
        {
            esc = "\\\"";
        }
        else if (c == '\\')
        {
            esc = "\\\\";
        }
        else if (c == '\n')
        {
            esc = "\\n";
        }
        else if (c == '\r')
        {
            esc = "\\r";
        }
        else if (c == '\t')
        {
            esc = "\\t";
        }

        if (esc != QOSA_NULL)
        {
            if (di >= dst_len - 3)
            {
                dst[di] = '\0';
                return -1;
            }
            dst[di++] = esc[0];
            dst[di++] = esc[1];
        }
        else if (c < 0x20)
        {
            /* 其余控制字符：转义为 \u00XX，最多 6 字符 */
            if (di >= dst_len - 7)
            {
                dst[di] = '\0';
                return -1;
            }
            static const char hexd[] = "0123456789abcdef";
            dst[di++] = '\\';
            dst[di++] = 'u';
            dst[di++] = '0';
            dst[di++] = '0';
            dst[di++] = hexd[(c >> 4) & 0x0F];
            dst[di++] = hexd[c & 0x0F];
        }
        else
        {
            if (di >= dst_len - 1)
            {
                dst[di] = '\0';
                return -1;
            }
            dst[di++] = (char)c;
        }
    }
    dst[di] = '\0';
    return 0;
}

/**
 * @brief 构造 conversation.chat.submit_tool_outputs 工具结果事件。
 *
 * @param[out] buf          用于接收 JSON 事件的缓冲区。
 * @param[in]  buf_len      缓冲区容量，单位为字节。
 * @param[in]  chat_id      当前会话 ID。
 * @param[in]  tool_call_id 当前工具调用 ID。
 * @param[in]  result_json  工具执行结果 JSON 文本。
 * @return JSON 事件长度；返回负值表示转义、构造或缓冲区失败。
 */
int coze_protocol_build_tool_result_event(char *buf, int buf_len, const char *chat_id, const char *tool_call_id, const char *result_json)
{
    /* 事件结构：conversation.chat.submit_tool_outputs，data.tool_outputs[].output 为 String 类型，见
       https://docs.coze.cn/developer_guides_streaming_chat_event "提交端插件执行结果" */
    /* escaped_output 需容纳 result_json 的最坏转义膨胀（控制字符最多 6x）。
       result_json 上游最长为 127 字符，此处取 512（栈占用可控，且与 buf_len 匹配）。 */
    char escaped_output[512] = {0};
    if (escape_json_string((result_json != QOSA_NULL) ? result_json : "", escaped_output, sizeof(escaped_output)) != 0)
    {
        QLOGE("[chat_coze_protocol] tool result escape truncated (result too long/control chars?)");
        return -1;
    }

    char id_buf[32] = {0};
    coze_protocol_next_event_id(id_buf, sizeof(id_buf));

    /* chat_id + tool_call_id + 转义后 output + JSON 骨架总长可能超过 256 字节。
       qosa_snprintf 截断时返回"本应写入长度"，若调用方按该值发送，会把残缺 JSON
       发给服务端而被拒绝。故此处显式校验：返回值 <0 或 >= buf_len 即为截断，返回 -1，
       绝不外发非法数据。 */
    int len = qosa_snprintf(
        buf, buf_len,
        "{\"" COZE_FIELD_ID "\":\"%s\",\"" COZE_FIELD_EVENT_TYPE "\":\"" COZE_EVT_CONVERSATION_CHAT_SUBMIT_TOOL_OUTPUTS "\",\"" COZE_FIELD_DATA
        "\":{\"" COZE_FIELD_CHAT_ID "\":\"%s\",\"" COZE_FIELD_TOOL_OUTPUTS "\":[{\"" COZE_FIELD_TOOL_CALL_ID "\":\"%s\",\"" COZE_FIELD_OUTPUT "\":\"%s\"}]}}",
        id_buf, (chat_id != QOSA_NULL) ? chat_id : "", (tool_call_id != QOSA_NULL) ? tool_call_id : "", escaped_output);

    if ((len < 0) || (len >= buf_len))
    {
        QLOGE("[chat_coze_protocol] tool result event truncated, need=%d cap=%d (result too long?)", len, buf_len);
        return -1;
    }
    return len;
}

/**
 * @brief 向聊天事件总线投递一条无文本事件。
 *
 * @param[in] type 要投递的事件类型。
 */
static void post_simple_event(chat_event_type_e type)
{
    chat_event_t event = {0};
    event.type = type;
    (void)chat_bus_post_event(&event);
}

/**
 * @brief 向聊天事件总线投递一条带文本的事件。
 *
 * @param[in] type 事件类型。
 * @param[in] text 事件文本；为空时不填充文本字段。
 */
static void post_text_event(chat_event_type_e type, const char *text)
{
    chat_event_t event = {0};
    event.type = type;
    if (text != QOSA_NULL)
    {
        qosa_snprintf(event.text, sizeof(event.text), "%s", text);
    }
    (void)chat_bus_post_event(&event);
}

/**
 * @brief 处理"conversation.chat.requires_action"下行事件：提取工具调用信息，组装紧凑 JSON
 *        投递给中介模块，由核心逻辑层解析并分发到工具层。
 *
 * @note 字段结构以 Coze 官方文档"端插件请求"章节为准：
 *       data.{id,required_action.submit_tool_outputs.tool_calls[].{id,function.{name,arguments}}}，
 *       其中 data.id 即后续提交结果时需要携带的 chat_id。
 */
static void handle_requires_action(Q_cJSON *root)
{
    Q_cJSON *data = Q_cJSON_GetObjectItem(root, COZE_FIELD_DATA);
    if (data == QOSA_NULL)
    {
        QLOGW("[chat_coze_protocol] requires_action missing data field");
        return;
    }

    Q_cJSON *chat_id_item = Q_cJSON_GetObjectItem(data, COZE_FIELD_ID);
    Q_cJSON *required_action = Q_cJSON_GetObjectItem(data, COZE_FIELD_REQUIRED_ACTION);
    Q_cJSON *submit = (required_action != QOSA_NULL) ? Q_cJSON_GetObjectItem(required_action, COZE_FIELD_SUBMIT_TOOL_OUTPUTS) : QOSA_NULL;
    Q_cJSON *tool_calls = (submit != QOSA_NULL) ? Q_cJSON_GetObjectItem(submit, COZE_FIELD_TOOL_CALLS) : QOSA_NULL;
    Q_cJSON *first_call = (tool_calls != QOSA_NULL) ? Q_cJSON_GetArrayItem(tool_calls, 0) : QOSA_NULL;
    if (first_call == QOSA_NULL)
    {
        QLOGW("[chat_coze_protocol] requires_action: cannot locate tool_calls[0], skip");
        return;
    }

    Q_cJSON *id_item = Q_cJSON_GetObjectItem(first_call, COZE_FIELD_ID);
    Q_cJSON *function_item = Q_cJSON_GetObjectItem(first_call, COZE_FIELD_FUNCTION);
    Q_cJSON *name_item = (function_item != QOSA_NULL) ? Q_cJSON_GetObjectItem(function_item, COZE_FIELD_NAME) : QOSA_NULL;
    Q_cJSON *args_item = (function_item != QOSA_NULL) ? Q_cJSON_GetObjectItem(function_item, COZE_FIELD_ARGUMENTS) : QOSA_NULL;

    Q_cJSON *packed = Q_cJSON_CreateObject();
    Q_cJSON_AddStringToObject(packed, COZE_FIELD_CHAT_ID, Q_cJSON_IsString(chat_id_item) ? chat_id_item->valuestring : "");
    Q_cJSON_AddStringToObject(packed, COZE_FIELD_TOOL_CALL_ID, Q_cJSON_IsString(id_item) ? id_item->valuestring : "");
    Q_cJSON_AddStringToObject(packed, "tool_name", Q_cJSON_IsString(name_item) ? name_item->valuestring : "");

    /* Coze 的 arguments 字段为字符串化的 JSON（见官方示例），也兼容对象形式 */
    if (Q_cJSON_IsString(args_item))
    {
        Q_cJSON *parsed_args = Q_cJSON_Parse(args_item->valuestring);
        Q_cJSON_AddItemToObject(packed, COZE_FIELD_ARGUMENTS, (parsed_args != QOSA_NULL) ? parsed_args : Q_cJSON_CreateObject());
    }
    else if (args_item != QOSA_NULL)
    {
        Q_cJSON_AddItemToObject(packed, COZE_FIELD_ARGUMENTS, Q_cJSON_Duplicate(args_item, 1));
    }
    else
    {
        Q_cJSON_AddObjectToObject(packed, COZE_FIELD_ARGUMENTS);
    }

    char *packed_str = Q_cJSON_PrintUnformatted(packed);
    if (packed_str != QOSA_NULL)
    {
        /* 工具调用请求关键信息打印（低频）：确认服务端下发的
           chat_id/tool_call_id/工具名/入参已被正确解析。 */
        QLOGI("[chat_coze_protocol] requires_action: name=%s chat_id=%s tool_call_id=%s args=%.128s",
              Q_cJSON_IsString(name_item) ? name_item->valuestring : "?",
              Q_cJSON_IsString(chat_id_item) ? chat_id_item->valuestring : "?",
              Q_cJSON_IsString(id_item) ? id_item->valuestring : "?",
              packed_str);
        post_text_event(CHAT_EVT_TOOL_CALL_REQUEST, packed_str);
        Q_cJSON_free(packed_str);
    }
    Q_cJSON_Delete(packed);
}

/**
 * @brief 递归查找下行 JSON 中的音频 Base64 字段。
 *
 * @param[in] node 待搜索的 JSON 节点。
 * @return 找到的 JSON 字符串节点；未找到时返回 QOSA_NULL。
 */
static Q_cJSON *find_audio_b64_field(Q_cJSON *node)
{
    if (node == QOSA_NULL)
    {
        return QOSA_NULL;
    }

    if (Q_cJSON_IsObject(node))
    {
        for (Q_cJSON *child = node->child; child != QOSA_NULL; child = child->next)
        {
            if (Q_cJSON_IsString(child) && (child->string != QOSA_NULL) &&
                ((strcmp(child->string, COZE_FIELD_DELTA) == 0) || (strcmp(child->string, COZE_FIELD_CONTENT) == 0)))
            {
                return child;
            }
        }

        for (Q_cJSON *child = node->child; child != QOSA_NULL; child = child->next)
        {
            Q_cJSON *found = find_audio_b64_field(child);
            if (found != QOSA_NULL)
            {
                return found;
            }
        }
    }
    else if (Q_cJSON_IsArray(node))
    {
        int array_size = Q_cJSON_GetArraySize(node);
        for (int i = 0; i < array_size; i++)
        {
            Q_cJSON *item = Q_cJSON_GetArrayItem(node, i);
            Q_cJSON *found = find_audio_b64_field(item);
            if (found != QOSA_NULL)
            {
                return found;
            }
        }
    }

    return QOSA_NULL;
}

static qosa_uint32_t s_audio_delta_count = 0;

/**
 * @brief 获取服务端当前是否仍处于对话生成状态。
 *
 * @return QOSA_TRUE 表示对话忙；否则返回 QOSA_FALSE。
 */
qosa_bool_t coze_protocol_is_chat_busy(void)
{
    return s_chat_busy;
}

/**
 * @brief 解码下行音频增量并转移给音频播放任务。
 *
 * @param[in] root 已解析的下行事件 JSON 根节点。
 * @note 解码缓冲区所有权在成功入队后转移给下行任务。
 */
static void handle_audio_delta(Q_cJSON *root)
{
    /* 官方文档：增量语音数据在 data.content 中（base64），content_type 固定为 audio；
       data.delta 为第三方参考实现的字段名，仅作兼容兜底。 */
    Q_cJSON *data = Q_cJSON_GetObjectItem(root, COZE_FIELD_DATA);
    Q_cJSON *content_item = (data != QOSA_NULL) ? Q_cJSON_GetObjectItem(data, COZE_FIELD_CONTENT) : QOSA_NULL;
    if (!Q_cJSON_IsString(content_item))
    {
        content_item = (data != QOSA_NULL) ? Q_cJSON_GetObjectItem(data, COZE_FIELD_DELTA) : QOSA_NULL;
    }

    if (!Q_cJSON_IsString(content_item))
    {
        content_item = (data != QOSA_NULL) ? find_audio_b64_field(data) : QOSA_NULL;
    }

    if (!Q_cJSON_IsString(content_item))
    {
        content_item = find_audio_b64_field(root);
    }

    if (!Q_cJSON_IsString(content_item))
    {
        char *raw = Q_cJSON_PrintUnformatted(root);
        QLOGW("[chat_coze_protocol] audio delta payload not found, raw=%.*s",
              (raw != QOSA_NULL) ? (((int)qosa_strlen(raw) > 256) ? 256 : (int)qosa_strlen(raw)) : 0,
              (raw != QOSA_NULL) ? raw : "");
        if (raw != QOSA_NULL)
        {
            Q_cJSON_free(raw);
        }
        return;
    }

    const char *b64 = content_item->valuestring;
    int         b64_len = (int)qosa_strlen(b64);
    if (b64_len <= 0)
    {
        return;
    }

    Q_cJSON *content_type_item = (data != QOSA_NULL) ? Q_cJSON_GetObjectItem(data, COZE_FIELD_CONTENT_TYPE) : QOSA_NULL;
    if (Q_cJSON_IsString(content_type_item) && (strcmp(content_type_item->valuestring, "audio") != 0))
    {
        QLOGW("[chat_coze_protocol] audio delta content_type unexpected: %s", content_type_item->valuestring);
    }

    qosa_uint8_t *pcm_buf = qosa_malloc((qosa_uint32_t)b64_len);
    if (pcm_buf == QOSA_NULL)
    {
        return;
    }

    int decode_cap = b64_len;
    int decode_len = qcm_base64_decode(b64, b64_len, (char *)pcm_buf, &decode_cap);
    if (decode_len <= 0)
    {
        QLOGW("[chat_coze_protocol] audio delta base64 decode failed, b64_len=%d ret=%d", b64_len, decode_len);
        qosa_free(pcm_buf);
        return;
    }

#if CHATBOT_AUDIO_CODEC_G711A
    /* 下行为 g711a 编码字节（1字节/采样），下游播放链路（downlink 队列/audio_player）只认线性 PCM16，
       需在入队前还原，不影响下游任何代码。 */
    qosa_int16_t *pcm16_buf = (qosa_int16_t *)qosa_malloc((qosa_uint32_t)decode_len * sizeof(qosa_int16_t));
    if (pcm16_buf == QOSA_NULL)
    {
        QLOGE("[chat_coze_protocol] g711 decode alloc failed, alaw_len=%d", decode_len);
        qosa_free(pcm_buf);
        return;
    }
    g711_alaw_decode_block(pcm_buf, pcm16_buf, (qosa_uint32_t)decode_len);
    qosa_free(pcm_buf);
    pcm_buf = (qosa_uint8_t *)pcm16_buf;
    decode_len = decode_len * (int)sizeof(qosa_int16_t);
#endif

    s_audio_delta_count++;
    if ((s_audio_delta_count <= 5) || ((s_audio_delta_count % 50) == 0))
    {
        /* 前 5 包强制不节流打印，确保首批音频包在任何日志视图下都可见 */
        QLOGI("[chat_coze_protocol] audio delta decoded=%d count=%u", decode_len, s_audio_delta_count);
    }

    /* pcm_buf 所有权转移给音频下行任务，由其负责 qosa_free */
    coze_audio_downlink_push(pcm_buf, decode_len);
}

/**
 * @brief 解析并分发一条 Coze 下行 JSON 事件。
 *
 * @param[in] raw     原始 JSON 数据。
 * @param[in] raw_len 原始数据长度，单位为字节。
 */
void coze_protocol_dispatch_downlink(const char *raw, int raw_len)
{
    Q_cJSON *root = Q_cJSON_ParseWithLength(raw, (qosa_size_t)raw_len);
    if (root == QOSA_NULL)
    {
        QLOGW("[chat_coze_protocol] downlink json parse failed, len=%d", raw_len);
        return;
    }

    Q_cJSON *type_item = Q_cJSON_GetObjectItem(root, COZE_FIELD_EVENT_TYPE);
    if (!Q_cJSON_IsString(type_item))
    {
        Q_cJSON_Delete(root);
        return;
    }
    const char *event_type = type_item->valuestring;

    /* 仅对状态/边界类事件打印 event 行；高频过程事件（文本增量、识别中间值、
       音频/缓冲区完成回执）不打，避免日志刷屏。 */
    if ((strcmp(event_type, COZE_EVT_CONVERSATION_AUDIO_DELTA) != 0) &&
        (strcmp(event_type, COZE_EVT_CONVERSATION_MESSAGE_DELTA) != 0) &&
        (strcmp(event_type, COZE_EVT_CONVERSATION_AUDIO_TRANSCRIPT_UPDATE) != 0) &&
        (strcmp(event_type, COZE_EVT_CONVERSATION_AUDIO_COMPLETED) != 0) &&
        (strcmp(event_type, COZE_EVT_CONVERSATION_AUDIO_TRANSCRIPT_COMPLETED) != 0) &&
        (strcmp(event_type, COZE_EVT_INPUT_AUDIO_BUFFER_COMPLETED) != 0) &&
        (strcmp(event_type, COZE_EVT_CONVERSATION_MESSAGE_COMPLETED) != 0))
    {
        QLOGI("[chat_coze_protocol] downlink event: %s", event_type);
    }

    if (strcmp(event_type, COZE_EVT_CONVERSATION_AUDIO_DELTA) == 0)
    {
        handle_audio_delta(root);
    }
    else if (strcmp(event_type, COZE_EVT_CONVERSATION_AUDIO_SENTENCE_START) == 0)
    {
        /* 新一轮语音播报开始（开场白/新回答均以本事件起始）。若此前发生过打断(flush)
           导致下行被静音门控，本事件即代表服务端已开始下发新一轮内容，解除门控恢复播放。
           cancel 上行发出后服务端并不会立刻停发——残余 delta 连同旧回答的后继句子
           会继续下发数秒才回 conversation.chat.canceled。此时若仍无条件 resume，残余旧回答
           会在用户说完话后被当作新一轮继续播完。故 cancel 待确认窗口内(s_cancel_pending)的
           sentence_start 一律忽略，保持静音门控丢弃残余；待服务端确认(canceled)或
           真正开始新一轮(chat.created/in_progress)后才允许 resume。 */
        if (s_cancel_pending == QOSA_FALSE)
        {
            coze_audio_downlink_resume();
        }
        else
        {
            QLOGI("[chat_coze_protocol] sentence_start ignored while cancel pending, keep muted");
        }
    }
    else if ((strcmp(event_type, COZE_EVT_CHAT_CREATED) == 0) || (strcmp(event_type, COZE_EVT_CHAT_UPDATED) == 0))
    {
        s_audio_delta_count = 0;
        /* 连接（重）建立/配置成功：取消遗留的 cancel 待确认标记，清除静音门控，允许开场白等播报 */
        s_cancel_pending = QOSA_FALSE;
        coze_audio_downlink_resume();
        if (strcmp(event_type, COZE_EVT_CHAT_UPDATED) == 0)
        {
            QLOGI("[chat_coze_protocol] chat.updated received, post WS_CONNECTED event");
            post_simple_event(CHAT_EVT_WS_CONNECTED);
        }
    }
    else if (strcmp(event_type, COZE_EVT_INPUT_AUDIO_BUFFER_SPEECH_STARTED) == 0)
    {
        coze_audio_flush_downlink();
        post_simple_event(CHAT_EVT_SPEECH_STARTED);
    }
    else if (strcmp(event_type, COZE_EVT_INPUT_AUDIO_BUFFER_SPEECH_STOPPED) == 0)
    {
        post_simple_event(CHAT_EVT_SPEECH_STOPPED);
    }
    else if ((strcmp(event_type, COZE_EVT_CONVERSATION_MESSAGE_COMPLETED) == 0) ||
             (strcmp(event_type, COZE_EVT_CONVERSATION_AUDIO_TRANSCRIPT_COMPLETED) == 0))
    {
        /* 消息/识别完成：携带最终文本，投递 SERVER_TEXT（打印侧已在 chat_core 过滤
           verbose 内部消息）。 */
        Q_cJSON *data = Q_cJSON_GetObjectItem(root, COZE_FIELD_DATA);
        Q_cJSON *content_item = (data != QOSA_NULL) ? Q_cJSON_GetObjectItem(data, COZE_FIELD_CONTENT) : QOSA_NULL;
        post_text_event(CHAT_EVT_SERVER_TEXT, Q_cJSON_IsString(content_item) ? content_item->valuestring : event_type);
    }
    else if (strcmp(event_type, COZE_EVT_CONVERSATION_CHAT_COMPLETED) == 0)
    {
        /* 一轮对话自然结束。注意：此处不关闭 cancel 待确认窗口——若该 completed 恰好
           是"被打断轮"的收尾，残余音频可能仍在途，过早放开会让 sentence_start
           误解除静音门控。窗口只由 canceled（服务端确认）或新一轮 created/in_progress 关闭。 */
        s_chat_busy = QOSA_FALSE;
        post_simple_event(CHAT_EVT_CHAT_COMPLETED);
    }
    else if ((strcmp(event_type, COZE_EVT_CONVERSATION_CHAT_CREATED) == 0) ||
             (strcmp(event_type, COZE_EVT_CONVERSATION_CHAT_IN_PROGRESS) == 0))
    {
        /* 服务端开始执行一轮对话：此后按下 PTT / VAD 起声若想打断才需要发 cancel。
           新一轮开始即意味着上一轮（无论被打断还是自然结束）已彻底结束，关闭
           cancel 待确认窗口，后续 sentence_start 允许恢复播放。 */
        s_cancel_pending = QOSA_FALSE;
        s_chat_busy = QOSA_TRUE;
        /* 极端场景兜底：打断后 canceled 回执丢失/迟到，服务端未发 canceled 直接
           开新一轮 created/in_progress。此时旧轮残余必然已结束，可安全解除残留的
           静音门控；正常轮次本分支 muted=FALSE，resume 为空操作无副作用。 */
        coze_audio_downlink_resume();
    }
    else if ((strcmp(event_type, COZE_EVT_CONVERSATION_CHAT_FAILED) == 0) ||
             (strcmp(event_type, COZE_EVT_CONVERSATION_CHAT_CANCELED) == 0))
    {
        /* canceled：服务端确认打断完成——旧一轮残余已全部发完(不会再有任何该轮 delta)。
           cancel 待确认窗口关闭。此时解除下行静音门控，等待随后开始的新一轮音频。*/
        Q_cJSON *data_obj = Q_cJSON_GetObjectItem(root, COZE_FIELD_DATA);
        Q_cJSON *id_item = Q_cJSON_GetObjectItem(root, COZE_FIELD_ID);
        /* 官方文档字段路径不同：failed 的真实错误码在 data.last_error.code/msg；
           canceled 的中断原因码直接在 data.code/msg（1=被打断 2=主动cancel 3=手动提交）。 */
        Q_cJSON *code_item = QOSA_NULL;
        Q_cJSON *msg_item = QOSA_NULL;
        if (strcmp(event_type, COZE_EVT_CONVERSATION_CHAT_FAILED) == 0)
        {
            Q_cJSON *last_error = (data_obj != QOSA_NULL) ? Q_cJSON_GetObjectItem(data_obj, COZE_FIELD_LAST_ERROR) : QOSA_NULL;
            code_item = (last_error != QOSA_NULL) ? Q_cJSON_GetObjectItem(last_error, COZE_FIELD_CODE) : QOSA_NULL;
            msg_item = (last_error != QOSA_NULL) ? Q_cJSON_GetObjectItem(last_error, COZE_FIELD_MSG) : QOSA_NULL;
        }
        else
        {
            code_item = (data_obj != QOSA_NULL) ? Q_cJSON_GetObjectItem(data_obj, COZE_FIELD_CODE) : QOSA_NULL;
            msg_item = (data_obj != QOSA_NULL) ? Q_cJSON_GetObjectItem(data_obj, COZE_FIELD_MSG) : QOSA_NULL;
        }
        char code_buf[24] = "?";
        if (Q_cJSON_IsNumber(code_item))
        {
            qosa_snprintf(code_buf, sizeof(code_buf), "%d", (int)code_item->valuedouble);
        }
        else if (Q_cJSON_IsString(code_item))
        {
            qosa_snprintf(code_buf, sizeof(code_buf), "%s", code_item->valuestring);
        }
        QLOGI("[chat_coze_protocol] %s: id=%s code=%s msg=%s (last_cancel=%s cancel_pending was %d)",
              event_type,
              Q_cJSON_IsString(id_item) ? id_item->valuestring : "?",
              code_buf,
              Q_cJSON_IsString(msg_item) ? msg_item->valuestring : "?",
              s_last_cancel_id,
              (int)s_cancel_pending);
        if (strcmp(event_type, COZE_EVT_CONVERSATION_CHAT_FAILED) == 0)
        {
            /* 日志行会截断长字符串，failed 事件按 120 字符分片完整回显原始 JSON */
            for (int off = 0; off < raw_len; off += 120)
            {
                int chunk = ((raw_len - off) > 120) ? 120 : (raw_len - off);
                QLOGI("[chat_coze_protocol] failed raw[%d]: %.*s", off, chunk, raw + off);
            }
        }
        s_cancel_pending = QOSA_FALSE;
        s_chat_busy = QOSA_FALSE;
        coze_audio_downlink_resume();
    }
    else if (strcmp(event_type, COZE_EVT_CONVERSATION_CHAT_REQUIRES_ACTION) == 0)
    {
        /* 端插件请求（工具调用）：解析 data.required_action.submit_tool_outputs.tool_calls,
           打包 chat_id/tool_call_id/tool_name/arguments 投递给 chat_core，由其分发到
           工具注册表并把结果经 conversation.chat.submit_tool_outputs 回传服务端。 */
        handle_requires_action(root);
    }
    else if ((strcmp(event_type, COZE_EVT_CONVERSATION_MESSAGE_DELTA) == 0) ||
             (strcmp(event_type, COZE_EVT_CONVERSATION_AUDIO_COMPLETED) == 0) ||
             (strcmp(event_type, COZE_EVT_CONVERSATION_AUDIO_TRANSCRIPT_UPDATE) == 0) ||
             (strcmp(event_type, COZE_EVT_INPUT_AUDIO_BUFFER_COMPLETED) == 0))
    {
        /* 已知高频过程事件：本工程无需业务处理（文本增量、音频完成回执等），
           静默跳过，不打日志。 */
    }
    else
    {
        /* 未知事件：限频打印（前 3 条带 raw，之后每 500 条一条 event_type），
           避免未知事件刷屏且保留排障线索。 */
        static qosa_uint32_t s_unhandled_cnt = 0;
        s_unhandled_cnt++;
        if (s_unhandled_cnt <= 3)
        {
            QLOGD("[chat_coze_protocol] unhandled event_type: %s, raw=%.*s", event_type, (raw_len > 128) ? 128 : raw_len, raw);
        }
        else if ((s_unhandled_cnt % 500) == 0)
        {
            QLOGW("[chat_coze_protocol] unhandled event_type: %s (total=%u, suppress raw)", event_type, s_unhandled_cnt);
        }
    }

    Q_cJSON_Delete(root);
}
