/**
 * @file chatbot_tool.h
 * @brief 工具层对外接口：大模型工具（Function Calling）注册规范与分发入口。
 *
 * 新增工具的标准步骤：
 * 1. 在 Coze 平台 Bot 配置中新增一个 Function，定义工具名、描述、入参 JSON Schema；
 * 2. 在 main/src/chatbot/tools/ 下新增一个 tool_xxx.c，参照 tool_led.c 实现 tool_handler_t 签名的处理函数；
 * 3. 在 tool_registry_init_all() 中追加一行 tool_xxx_init()；
 * 全程无需修改核心逻辑层、中介模块或通信层代码。
 */
#ifndef __CHATBOT_TOOL_H__
#define __CHATBOT_TOOL_H__

#include "qosa_def.h"

/**
 * @brief 工具处理函数原型.
 *
 * @param[in]  args_json       Coze 下发的工具入参，JSON 字符串.
 * @param[out] result_json     工具执行结果输出缓冲，需写入合法 JSON 字符串.
 * @param[in]  result_buf_len  result_json 缓冲区大小.
 * @return 0 成功；非 0 失败（失败时应向 result_json 写入 {"error":"..."}）.
 */
typedef int (*tool_handler_t)(const char *args_json, char *result_json, qosa_uint32_t result_buf_len);

/**
 * @brief 注册一个工具处理函数，工具名需与 Coze 平台侧 Bot 配置的 Function 名称一致.
 */
int tool_registry_register(const char *tool_name, tool_handler_t handler);

/**
 * @brief 按工具名分发调用。返回 0 且写入 result_json 表示已找到并执行；
 *        未找到工具时返回 -1，并写入 {"error":"tool not found"}.
 */
int tool_registry_dispatch(const char *tool_name, const char *args_json, char *result_json, qosa_uint32_t result_buf_len);

/**
 * @brief 依次初始化工具注册表本身以及全部内置工具（当前仅 LED 开关控制）.
 */
int tool_registry_init_all(void);

/**
 * @brief LED 开关控制工具：初始化 LED GPIO 并完成 "led_control" 工具注册.
 */
int tool_led_init(void);

/**
 * @brief 文本日志打印示例工具：完成 "log_print" 工具注册（模型要求时用 QLOGI 打印入参文本）.
 */
int tool_log_init(void);

#endif /* __CHATBOT_TOOL_H__ */
