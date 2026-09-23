#include <string.h>

#include "qosa_def.h"
#include "qosa_log.h"

#define QOS_LOG_TAG LOG_TAG_DEMO

#include "chatbot_tool.h"

#define TOOL_REGISTRY_MAX_COUNT 8
#define TOOL_NAME_MAX_LEN       32

typedef struct
{
    char           name[TOOL_NAME_MAX_LEN];
    tool_handler_t handler;
} tool_entry_t;

static tool_entry_t  g_tool_entries[TOOL_REGISTRY_MAX_COUNT] = {0};
static qosa_uint32_t g_tool_entry_count = 0;

/**
 * @brief 向工具注册表追加一个工具处理函数。
 *
 * @param[in] tool_name 工具名称。
 * @param[in] handler   工具调用处理函数。
 * @return 0 表示注册成功；返回负值表示参数无效或注册表已满。
 */
int tool_registry_register(const char *tool_name, tool_handler_t handler)
{
    if ((tool_name == QOSA_NULL) || (handler == QOSA_NULL))
    {
        return -1;
    }

    if (g_tool_entry_count >= TOOL_REGISTRY_MAX_COUNT)
    {
        QLOGE("[tool_registry] registry full, cannot register \"%s\"", tool_name);
        return -1;
    }

    qosa_snprintf(g_tool_entries[g_tool_entry_count].name, TOOL_NAME_MAX_LEN, "%s", tool_name);
    g_tool_entries[g_tool_entry_count].handler = handler;
    g_tool_entry_count++;

    QLOGI("[tool_registry] registered tool \"%s\"", tool_name);
    return 0;
}

/**
 * @brief 按工具名称查找并执行工具处理函数。
 *
 * @param[in]  tool_name       工具名称。
 * @param[in]  args_json       工具参数 JSON 文本。
 * @param[out] result_json     用于接收工具结果 JSON 文本的缓冲区。
 * @param[in]  result_buf_len 结果缓冲区容量，单位为字节。
 * @return 工具处理函数返回值；工具不存在或参数无效时返回负值。
 */
int tool_registry_dispatch(const char *tool_name, const char *args_json, char *result_json, qosa_uint32_t result_buf_len)
{
    if ((tool_name == QOSA_NULL) || (result_json == QOSA_NULL) || (result_buf_len == 0))
    {
        return -1;
    }

    for (qosa_uint32_t i = 0; i < g_tool_entry_count; i++)
    {
        if (strcmp(g_tool_entries[i].name, tool_name) == 0)
        {
            return g_tool_entries[i].handler(args_json, result_json, result_buf_len);
        }
    }

    QLOGW("[tool_registry] tool \"%s\" not found", tool_name);
    qosa_snprintf(result_json, result_buf_len, "{\"error\":\"tool not found\"}");
    return -1;
}

/**
 * @brief 清空工具注册表并初始化所有内置工具。
 *
 * @return 0 表示初始化流程完成；单个工具失败时保留可用工具并记录告警。
 */
int tool_registry_init_all(void)
{
    g_tool_entry_count = 0;

    /* 新增工具时，在此追加一行 tool_xxx_init()，无需改动其余代码。 */
    if (tool_led_init() != 0)
    {
        QLOGW("[tool_registry] tool_led_init failed, LED tool degraded/unavailable");
    }
    if (tool_log_init() != 0)
    {
        QLOGW("[tool_registry] tool_log_init failed, log_print tool unavailable");
    }

    return 0;
}
