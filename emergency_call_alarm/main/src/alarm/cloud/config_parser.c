#include <string.h>

#include "qosa_def.h"
#include "qosa_log.h"
#include "qosa_cJSON.h"

#define QOS_LOG_TAG LOG_TAG_DEMO

#include "alarm_cloud.h"
#include "alarm_config.h"

/**
 * @brief 校验号码字符串是否合法。
 *
 * 允许一个前导 '+'，其余必须为数字，长度需在有效区间内。
 *
 * @param[in] phone 号码字符串。
 * @return qosa_bool_t QOSA_TRUE 表示合法。
 */
static qosa_bool_t phone_is_valid(const char *phone)
{
    size_t len = 0;
    size_t i = 0;

    if (phone == QOSA_NULL)
    {
        return QOSA_FALSE;
    }

    len = strlen(phone);
    if ((len >= ALARM_CONTACT_PHONE_MAX) || (len < ALARM_CONTACT_PHONE_MIN_LEN))
    {
        return QOSA_FALSE;
    }

    i = (phone[0] == '+') ? 1u : 0u;
    if (i >= len)
    {
        return QOSA_FALSE;
    }

    for (; i < len; i++)
    {
        if ((phone[i] < '0') || (phone[i] > '9'))
        {
            return QOSA_FALSE;
        }
    }

    return QOSA_TRUE;
}

/**
 * @brief 校验联系人名是否合法。
 *
 * 序列化时采用手工拼接 JSON，因此拒绝引号、反斜杠与控制字符。
 *
 * @param[in] name 联系人名。
 * @return qosa_bool_t QOSA_TRUE 表示合法。
 */
static qosa_bool_t name_is_valid(const char *name)
{
    size_t len = 0;
    size_t i = 0;

    if (name == QOSA_NULL)
    {
        return QOSA_FALSE;
    }

    len = strlen(name);
    if ((len == 0) || (len >= ALARM_CONTACT_NAME_MAX))
    {
        return QOSA_FALSE;
    }

    for (i = 0; i < len; i++)
    {
        if ((name[i] == '"') || (name[i] == '\\') || ((unsigned char)name[i] < 0x20u))
        {
            return QOSA_FALSE;
        }
    }

    return QOSA_TRUE;
}

/**
 * @brief 设置失败原因输出。
 *
 * @param[out] reason 原因输出指针，可为 QOSA_NULL。
 * @param[in] text 原因文本。
 * @return int 固定返回 -1，便于调用处直接 return。
 */
static int set_reason(const char **reason, const char *text)
{
    if (reason != QOSA_NULL)
    {
        *reason = text;
    }
    return -1;
}

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
int alarm_contacts_parse_json(const char *json, alarm_contact_t *out, qosa_uint8_t *count, const char **reason)
{
    Q_cJSON *root = QOSA_NULL;
    Q_cJSON *array = QOSA_NULL;
    Q_cJSON *item = QOSA_NULL;
    Q_cJSON *name = QOSA_NULL;
    Q_cJSON *phone = QOSA_NULL;
    int      total = 0;
    int      i = 0;

    if ((json == QOSA_NULL) || (out == QOSA_NULL) || (count == QOSA_NULL))
    {
        return set_reason(reason, "bad_param");
    }

    root = Q_cJSON_Parse(json);
    if (root == QOSA_NULL)
    {
        return set_reason(reason, "invalid_json");
    }

    array = Q_cJSON_IsArray(root) ? root : Q_cJSON_GetObjectItemCaseSensitive(root, "contacts");
    if ((array == QOSA_NULL) || (!Q_cJSON_IsArray(array)))
    {
        Q_cJSON_Delete(root);
        return set_reason(reason, "contacts_not_array");
    }

    total = Q_cJSON_GetArraySize(array);
    if ((total <= 0) || (total > ALARM_CONTACT_MAX))
    {
        Q_cJSON_Delete(root);
        return set_reason(reason, "bad_contact_count");
    }

    qosa_memset(out, 0, sizeof(alarm_contact_t) * (size_t)ALARM_CONTACT_MAX);

    /* 先整体校验再落盘，任一条目非法即整包拒绝。 */
    for (i = 0; i < total; i++)
    {
        item = Q_cJSON_GetArrayItem(array, i);
        name = Q_cJSON_GetObjectItemCaseSensitive(item, "name");
        phone = Q_cJSON_GetObjectItemCaseSensitive(item, "phone");

        if ((!Q_cJSON_IsString(name)) || (name_is_valid(name->valuestring) == QOSA_FALSE))
        {
            Q_cJSON_Delete(root);
            return set_reason(reason, "invalid_name");
        }

        if ((!Q_cJSON_IsString(phone)) || (phone_is_valid(phone->valuestring) == QOSA_FALSE))
        {
            Q_cJSON_Delete(root);
            return set_reason(reason, "invalid_phone");
        }

        strncpy(out[i].name, name->valuestring, ALARM_CONTACT_NAME_MAX - 1);
        strncpy(out[i].phone_number, phone->valuestring, ALARM_CONTACT_PHONE_MAX - 1);
    }

    Q_cJSON_Delete(root);
    *count = (qosa_uint8_t)total;

    if (reason != QOSA_NULL)
    {
        *reason = "ok";
    }
    return 0;
}

/**
 * @brief 将联系人表序列化为规范 JSON 文本。
 *
 * @param[in] list 联系人数组。
 * @param[in] count 联系人数。
 * @param[out] buf 输出缓冲。
 * @param[in] buf_size 输出缓冲字节数。
 * @return int 0 表示成功，-1 表示参数错误或缓冲不足。
 */
int alarm_contacts_build_json(const alarm_contact_t *list, qosa_uint8_t count, char *buf, qosa_uint32_t buf_size)
{
    qosa_uint32_t used = 0;
    int           written = 0;
    qosa_uint8_t  i = 0;

    if ((list == QOSA_NULL) || (buf == QOSA_NULL) || (count == 0) || (count > ALARM_CONTACT_MAX) || (buf_size == 0))
    {
        return -1;
    }

    written = qosa_snprintf(buf, buf_size, "{\"contacts\":[");
    if ((written < 0) || ((qosa_uint32_t)written >= buf_size))
    {
        return -1;
    }
    used = (qosa_uint32_t)written;

    for (i = 0; i < count; i++)
    {
        written = qosa_snprintf(buf + used,
                                buf_size - used,
                                "%s{\"name\":\"%s\",\"phone\":\"%s\"}",
                                (i == 0) ? "" : ",",
                                list[i].name,
                                list[i].phone_number);
        if ((written < 0) || ((qosa_uint32_t)written >= (buf_size - used)))
        {
            return -1;
        }
        used += (qosa_uint32_t)written;
    }

    written = qosa_snprintf(buf + used, buf_size - used, "]}");
    if ((written < 0) || ((qosa_uint32_t)written >= (buf_size - used)))
    {
        return -1;
    }

    return 0;
}
