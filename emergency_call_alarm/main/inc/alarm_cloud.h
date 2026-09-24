#ifndef __ALARM_CLOUD_H__
#define __ALARM_CLOUD_H__

#include "alarm_types.h"

/**
 * @brief 初始化紧急联系人存储。
 *
 * 先装载出厂默认联系人，再尝试用 NV 中保存的云端配置覆盖。
 *
 * @return int 0 表示使用了 NV 配置，1 表示回落到出厂默认，-1 表示初始化失败。
 */
int alarm_contact_store_init(void);

/**
 * @brief 获取当前生效的联系人表。
 *
 * @param[out] count 联系人数输出指针，可为 QOSA_NULL。
 * @return const alarm_contact_t* 只读联系人数组首地址，永不为 QOSA_NULL。
 * @note 返回的缓冲区在下次更新时不会被原地改写，读取方无需加锁。
 */
const alarm_contact_t *alarm_contact_store_get(qosa_uint8_t *count);

/**
 * @brief 整体替换联系人表并持久化。
 *
 * @param[in] list 新联系人数组。
 * @param[in] count 新联系人数，取值 1..ALARM_CONTACT_MAX。
 * @return int 0 表示内存与 NV 均更新成功，1 表示内存已更新但 NV 写入失败，-1 表示参数错误。
 */
int alarm_contact_store_update(const alarm_contact_t *list, qosa_uint8_t count);

/**
 * @brief 解析云端下发的联系人 JSON 文本。
 *
 * 期望格式：{"contacts":[{"name":"NAME1","phone":"phone1"}, ...]}，也接受裸数组。
 * 任一条目非法时整包拒绝，不产生部分结果。
 *
 * @param[in] json 以 '\0' 结尾的 JSON 文本。
 * @param[out] out 输出数组，容量需不小于 ALARM_CONTACT_MAX。
 * @param[out] count 解析出的有效联系人数。
 * @param[out] reason 失败原因字符串输出指针，可为 QOSA_NULL。
 * @return int 0 表示成功，-1 表示解析或校验失败。
 */
int alarm_contacts_parse_json(const char *json, alarm_contact_t *out, qosa_uint8_t *count, const char **reason);

/**
 * @brief 将联系人表序列化为规范 JSON 文本。
 *
 * @param[in] list 联系人数组。
 * @param[in] count 联系人数。
 * @param[out] buf 输出缓冲。
 * @param[in] buf_size 输出缓冲字节数。
 * @return int 0 表示成功，-1 表示参数错误或缓冲不足。
 */
int alarm_contacts_build_json(const alarm_contact_t *list, qosa_uint8_t count, char *buf, qosa_uint32_t buf_size);

/**
 * @brief 启动云平台配置下发通道（阿里云 MQTT）。
 *
 * @return int 0 表示任务创建成功，非 0 表示失败。
 * @note 该模块不感知报警状态机内部，仅通过 alarm_post_event 与联系人存储对外交互。
 */
int alarm_cloud_init(void);

#endif /* __ALARM_CLOUD_H__ */
