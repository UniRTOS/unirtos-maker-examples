/**
 * @file tool_log.c
 * @brief "log_print" 示例工具：把模型传入的文本用 QLOGI 打印到设备日志。
 *
 * 用途：作为智能体工具调用（Function Calling）的最小可用示例——
 *      用户对智能体说"帮我打印日志：你好，我是豆包"，模型决定调用本工具，
 *      设备端在日志中打印对应文本，并将执行结果回传 Coze 让模型播报确认。
 *
 * 入参示例：{"text":"你好，我是豆包"}
 * 出参示例：{"text":"你好，我是豆包","result":"ok"}
 */
#include <string.h>

#include "qosa_def.h"
#include "qosa_log.h"
#include "qosa_cJSON.h"

#define QOS_LOG_TAG LOG_TAG_DEMO

#include "chatbot_tool.h"

#define TOOL_LOG_TEXT_MAX 128

/**
 * @brief "log_print" 工具处理函数：解析 args_json 中的 text 字段并打印。
 */
static int tool_log_print_handler(const char *args_json, char *result_json, qosa_uint32_t result_buf_len)
{
    Q_cJSON *root = Q_cJSON_Parse((args_json != QOSA_NULL) ? args_json : "{}");
    if (root == QOSA_NULL)
    {
        qosa_snprintf(result_json, result_buf_len, "{\"error\":\"invalid args json\"}");
        return -1;
    }

    Q_cJSON *text_item = Q_cJSON_GetObjectItem(root, "text");
    if (!Q_cJSON_IsString(text_item) || (qosa_strlen(text_item->valuestring) == 0))
    {
        Q_cJSON_Delete(root);
        qosa_snprintf(result_json, result_buf_len, "{\"error\":\"missing or empty text\"}");
        return -1;
    }

    char text_buf[TOOL_LOG_TEXT_MAX + 1] = {0};
    qosa_strncpy(text_buf, text_item->valuestring, TOOL_LOG_TEXT_MAX);
    Q_cJSON_Delete(root);

    /* 截断超长文本后打印到设备日志（示例工具核心动作） */
    QLOGI("[chat_tool_log] %s", text_buf);

    qosa_snprintf(result_json, result_buf_len, "{\"text\":\"%s\",\"result\":\"ok\"}", text_buf);
    return 0;
}

/**
 * @brief 注册 log_print 示例工具。
 *
 * @return 0 表示注册成功；返回负值表示注册失败。
 */
int tool_log_init(void)
{
    return tool_registry_register("log_print", tool_log_print_handler);
}
