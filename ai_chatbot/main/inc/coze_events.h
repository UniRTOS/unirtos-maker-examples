/**
 * @file coze_events.h
 * @brief Coze 实时语音平台（双向流式对话）事件类型、JSON 字段名、协议默认取值的集中定义。
 *
 * 字符串取值以 Coze 官方文档为准：
 * - 上行事件：https://docs.coze.cn/developer_guides_streaming_chat_event
 * - 下行事件：https://docs.coze.cn/developer_guides_streaming_chat_downlink_event
 *
 * 后续如需调整 Coze 协议相关的事件名、JSON 字段名或默认取值，只需修改本文件，
 * 不需要改动 coze_protocol.c / coze_client.c / coze_session.c 的业务逻辑。
 */
#ifndef __COZE_EVENTS_H__
#define __COZE_EVENTS_H__

/* ============================== 上行事件（device -> Coze，官方"双向流式对话上行事件"） ============================== */
#define COZE_EVT_CHAT_UPDATE                             "chat.update"                           /*!< 更新对话配置 */
#define COZE_EVT_INPUT_AUDIO_BUFFER_APPEND               "input_audio_buffer.append"              /*!< 流式上传音频片段 */
#define COZE_EVT_INPUT_AUDIO_BUFFER_COMPLETE             "input_audio_buffer.complete"            /*!< 提交音频（server_vad 模式下无效） */
#define COZE_EVT_INPUT_AUDIO_BUFFER_CLEAR                "input_audio_buffer.clear"               /*!< 清除缓冲区音频（server_vad 模式下无效） */
#define COZE_EVT_CONVERSATION_MESSAGE_CREATE             "conversation.message.create"            /*!< 手动提交对话内容 */
#define COZE_EVT_CONVERSATION_CLEAR                      "conversation.clear"                     /*!< 清除上下文 */
#define COZE_EVT_CONVERSATION_CHAT_SUBMIT_TOOL_OUTPUTS   "conversation.chat.submit_tool_outputs"  /*!< 提交端插件执行结果 */
#define COZE_EVT_CONVERSATION_CHAT_CANCEL                "conversation.chat.cancel"                /*!< 打断智能体输出 */
#define COZE_EVT_INPUT_TEXT_GENERATE_AUDIO               "input_text.generate_audio"               /*!< 提交文本做语音合成 */

/* ============================== 下行事件（Coze -> device，官方"双向流式对话下行事件"） ============================== */
#define COZE_EVT_CHAT_CREATED                            "chat.created"                           /*!< 对话连接成功 */
#define COZE_EVT_CHAT_UPDATED                            "chat.updated"                           /*!< 对话配置成功 */
#define COZE_EVT_CONVERSATION_CHAT_CREATED               "conversation.chat.created"              /*!< 对话开始 */
#define COZE_EVT_CONVERSATION_CHAT_IN_PROGRESS           "conversation.chat.in_progress"           /*!< 对话正在处理（注意是下划线，不是点） */
#define COZE_EVT_CONVERSATION_CHAT_COMPLETED             "conversation.chat.completed"             /*!< 对话完成 */
#define COZE_EVT_CONVERSATION_CHAT_FAILED                "conversation.chat.failed"                /*!< 对话失败 */
#define COZE_EVT_CONVERSATION_CHAT_CANCELED              "conversation.chat.canceled"              /*!< 智能体输出中断完成（单 L，区别于上行的 .cancel） */
#define COZE_EVT_CONVERSATION_CHAT_REQUIRES_ACTION       "conversation.chat.requires_action"       /*!< 端插件请求 */
#define COZE_EVT_CONVERSATION_MESSAGE_DELTA              "conversation.message.delta"              /*!< 增量消息 */
#define COZE_EVT_CONVERSATION_MESSAGE_COMPLETED          "conversation.message.completed"          /*!< 消息完成 */
#define COZE_EVT_CONVERSATION_AUDIO_SENTENCE_START       "conversation.audio.sentence_start"       /*!< 增量语音字幕（新句起始） */
#define COZE_EVT_CONVERSATION_AUDIO_DELTA                "conversation.audio.delta"                /*!< 增量语音 */
#define COZE_EVT_CONVERSATION_AUDIO_COMPLETED            "conversation.audio.completed"            /*!< 语音回复完成 */
#define COZE_EVT_CONVERSATION_AUDIO_TRANSCRIPT_UPDATE    "conversation.audio_transcript.update"    /*!< 用户语音识别字幕（中间值） */
#define COZE_EVT_CONVERSATION_AUDIO_TRANSCRIPT_COMPLETED "conversation.audio_transcript.completed" /*!< 用户语音识别完成 */
#define COZE_EVT_INPUT_AUDIO_BUFFER_SPEECH_STARTED       "input_audio_buffer.speech_started"       /*!< 用户开始说话（仅 server_vad 模式） */
#define COZE_EVT_INPUT_AUDIO_BUFFER_SPEECH_STOPPED       "input_audio_buffer.speech_stopped"       /*!< 用户结束说话（仅 server_vad 模式） */
#define COZE_EVT_INPUT_AUDIO_BUFFER_COMPLETED            "input_audio_buffer.completed"            /*!< input_audio_buffer 提交成功 */
#define COZE_EVT_INPUT_AUDIO_BUFFER_CLEARED              "input_audio_buffer.cleared"              /*!< input_audio_buffer 清除成功 */
#define COZE_EVT_CONVERSATION_CLEARED                    "conversation.cleared"                    /*!< 上下文清除完成 */
#define COZE_EVT_SERVER_ERROR                            "error"                                   /*!< 发生错误 */

/* ============================== JSON 字段名 ============================== */
#define COZE_FIELD_ID                     "id"
#define COZE_FIELD_EVENT_TYPE             "event_type"
#define COZE_FIELD_DATA                   "data"
#define COZE_FIELD_DETAIL                 "detail"
#define COZE_FIELD_DELTA                  "delta"
#define COZE_FIELD_CONTENT                "content"
#define COZE_FIELD_CONTENT_TYPE           "content_type"
#define COZE_FIELD_CODE                   "code"
#define COZE_FIELD_MSG                    "msg"
#define COZE_FIELD_LAST_ERROR             "last_error"
#define COZE_FIELD_CHAT_ID                "chat_id"
#define COZE_FIELD_CHAT_CONFIG            "chat_config"
#define COZE_FIELD_AUTO_SAVE_HISTORY      "auto_save_history"
#define COZE_FIELD_USER_ID                "user_id"
#define COZE_FIELD_INPUT_AUDIO            "input_audio"
#define COZE_FIELD_OUTPUT_AUDIO           "output_audio"
#define COZE_FIELD_FORMAT                 "format"
#define COZE_FIELD_CODEC                  "codec"
#define COZE_FIELD_SAMPLE_RATE            "sample_rate"
#define COZE_FIELD_CHANNEL                "channel"
#define COZE_FIELD_BIT_DEPTH              "bit_depth"
#define COZE_FIELD_PCM_CONFIG             "pcm_config"
#define COZE_FIELD_FRAME_SIZE_MS          "frame_size_ms"
#define COZE_FIELD_TURN_DETECTION         "turn_detection"
#define COZE_FIELD_TYPE                   "type"
#define COZE_FIELD_EVENT_SUBSCRIPTIONS    "event_subscriptions"
#define COZE_FIELD_NEED_PLAY_PROLOGUE     "need_play_prologue"
#define COZE_FIELD_PROLOGUE_CONTENT       "prologue_content"
#define COZE_FIELD_REQUIRED_ACTION        "required_action"
#define COZE_FIELD_SUBMIT_TOOL_OUTPUTS    "submit_tool_outputs"
#define COZE_FIELD_TOOL_CALLS             "tool_calls"
#define COZE_FIELD_TOOL_CALL_ID           "tool_call_id"
#define COZE_FIELD_TOOL_OUTPUTS           "tool_outputs"
#define COZE_FIELD_OUTPUT                 "output"
#define COZE_FIELD_FUNCTION               "function"
#define COZE_FIELD_NAME                   "name"
#define COZE_FIELD_ARGUMENTS              "arguments"
#define COZE_FIELD_LIMIT_CONFIG           "limit_config"
#define COZE_FIELD_LIMIT_PERIOD           "period"
#define COZE_FIELD_LIMIT_MAX_FRAME_NUM    "max_frame_num"

/* ============================== 协议默认取值 ============================== */
/*!< 默认使用线性 PCM16（详见设计报告 1.4 节确认结论）；G711A 编码由
     CHATBOT_AUDIO_CODEC_G711A 宏控启用，见 chatbot_config.h。 */
#define COZE_AUDIO_FORMAT_PCM             "pcm"
#define COZE_AUDIO_CODEC_PCM              "pcm"
/*!< g711a 编码要求 format 仍填 pcm（官方文档：codec 为 g711a/g711u 时 format 请设置为 pcm） */
#define COZE_AUDIO_CODEC_G711A            "g711a"
/*!< 端点检测方式：本工程实际使用 client_interrupt（客户端控制语音开始/结束）——
     BUTTON 模式 = 按键 PTT；VOICE 模式 = 设备侧本地 vad_lite 判定语音起止，检测到
     SPEECH_END 才发 input_audio_buffer.complete。不使用 server_vad（服务端 VAD）,
     server_vad 模式下 complete 事件无效且打断依赖服务端 speech_started 下行。
     常量保留作参考。 */
#define COZE_TURN_DETECTION_SERVER_VAD    "server_vad"
#define COZE_TURN_DETECTION_CLIENT_INTERRUPT "client_interrupt"

/*!< chat.update 下发时默认订阅的服务端事件列表（官方文档：不设置或为空则订阅全部下行事件） */
#define COZE_DEFAULT_EVENT_SUBSCRIPTIONS                       \
    {                                                           \
        COZE_EVT_SERVER_ERROR,                                   \
        COZE_EVT_CHAT_CREATED,                                   \
        COZE_EVT_CHAT_UPDATED,                                   \
        COZE_EVT_CONVERSATION_CHAT_CREATED,                      \
        COZE_EVT_CONVERSATION_CHAT_IN_PROGRESS,                  \
        COZE_EVT_CONVERSATION_MESSAGE_DELTA,                     \
        COZE_EVT_CONVERSATION_AUDIO_SENTENCE_START,              \
        /* COZE_EVT_CONVERSATION_AUDIO_TRANSCRIPT_UPDATE,        */ \
        COZE_EVT_CONVERSATION_AUDIO_TRANSCRIPT_COMPLETED,        \
        COZE_EVT_CONVERSATION_MESSAGE_COMPLETED,                 \
        COZE_EVT_CONVERSATION_AUDIO_DELTA,                       \
        COZE_EVT_CONVERSATION_AUDIO_COMPLETED,                   \
        COZE_EVT_INPUT_AUDIO_BUFFER_SPEECH_STARTED,              \
        COZE_EVT_INPUT_AUDIO_BUFFER_SPEECH_STOPPED,              \
        COZE_EVT_INPUT_AUDIO_BUFFER_COMPLETED,                   \
        COZE_EVT_CONVERSATION_CHAT_COMPLETED,                    \
        COZE_EVT_CONVERSATION_CHAT_REQUIRES_ACTION,              \
        COZE_EVT_CONVERSATION_CHAT_FAILED,                       \
        COZE_EVT_CONVERSATION_CHAT_CANCELED,                     \
    }

#endif /* __COZE_EVENTS_H__ */
