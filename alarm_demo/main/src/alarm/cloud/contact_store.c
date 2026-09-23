#include <string.h>

#include "qosa_def.h"
#include "qosa_log.h"
#include "qosa_nvitem.h"

#define QOS_LOG_TAG LOG_TAG_DEMO

#include "alarm_cloud.h"
#include "alarm_config.h"

/* 双缓冲：写方填充非活动缓冲后翻转下标，读方无锁读取活动缓冲。 */
static alarm_contact_t       g_contacts[2][ALARM_CONTACT_MAX] = {0};
static qosa_uint8_t          g_counts[2] = {0};
static volatile qosa_uint8_t g_active_idx = 0;
static qosa_mutex_t          g_store_lock = QOSA_NULL;
static char                  g_nv_json[ALARM_CONTACT_JSON_MAX] = {0};

/**
 * @brief 拷贝一条联系人字段并保证结尾有 '\0'。
 *
 * @param[out] dst 目标联系人。
 * @param[in] name 联系人名。
 * @param[in] phone 联系人号码。
 * @return void
 */
static void contact_assign(alarm_contact_t *dst, const char *name, const char *phone)
{
    qosa_memset(dst, 0, sizeof(*dst));
    if (name != QOSA_NULL)
    {
        strncpy(dst->name, name, ALARM_CONTACT_NAME_MAX - 1);
    }
    if (phone != QOSA_NULL)
    {
        strncpy(dst->phone_number, phone, ALARM_CONTACT_PHONE_MAX - 1);
    }
}

/**
 * @brief 把出厂默认联系人写入指定缓冲。
 *
 * @param[in] idx 目标缓冲下标。
 * @return void
 */
static void load_defaults(qosa_uint8_t idx)
{
    contact_assign(&g_contacts[idx][0], ALARM_CONTACT_1_NAME, ALARM_CONTACT_1_NUMBER);
    contact_assign(&g_contacts[idx][1], ALARM_CONTACT_2_NAME, ALARM_CONTACT_2_NUMBER);
    contact_assign(&g_contacts[idx][2], ALARM_CONTACT_3_NAME, ALARM_CONTACT_3_NUMBER);
    g_counts[idx] = ALARM_CONTACT_MAX;
}

/**
 * @brief 从 NV 读取联系人 JSON 文本。
 *
 * @param[out] json 输出缓冲，大小需为 ALARM_CONTACT_JSON_MAX。
 * @return int 0 表示读取到有效文本，非 0 表示无记录或读取失败。
 */
static int nv_read_contacts(char *json)
{
    qosa_nv_cfg_item_data_t item = {0};
    qosa_nvm_error_e        ret = QOSA_NVM_CFG_OK;

    qosa_memset(json, 0, ALARM_CONTACT_JSON_MAX);
    item.key = ALARM_CONTACT_NV_KEY;
    item.value = json;
    item.value_size = ALARM_CONTACT_JSON_MAX;

    ret = qosa_nv_item_cfg_read(ALARM_CONTACT_NV_NODE, &item, 1);
    if (ret != QOSA_NVM_CFG_OK)
    {
        QLOGW("[store] nv read failed, node=%s ret=%x", ALARM_CONTACT_NV_NODE, ret);
        return -1;
    }

    json[ALARM_CONTACT_JSON_MAX - 1] = '\0';
    QLOGI("[store] nv read ok, json=%s", json);
    return (json[0] == '\0') ? -1 : 0;
}

/**
 * @brief 将联系人 JSON 文本写入 NV。
 *
 * @param[in] json 以 '\0' 结尾的 JSON 文本。
 * @return int 0 表示成功，非 0 表示写入失败。
 */
static int nv_write_contacts(const char *json)
{
    qosa_nv_cfg_item_data_t item = {0};
    qosa_nvm_error_e        ret = QOSA_NVM_CFG_OK;

    qosa_memset(g_nv_json, 0, sizeof(g_nv_json));
    strncpy(g_nv_json, json, sizeof(g_nv_json) - 1);

    item.key = ALARM_CONTACT_NV_KEY;
    item.value = g_nv_json;
    item.value_size = sizeof(g_nv_json);

    ret = qosa_nv_item_json_write(ALARM_CONTACT_NV_NODE, &item, 1);
    if (ret != QOSA_NVM_CFG_OK)
    {
        QLOGE("[store] nv write failed, node=%s ret=%x", ALARM_CONTACT_NV_NODE, ret);
        return -1;
    }

    QLOGI("[store] nv write ok, json=%s", g_nv_json);
    return 0;
}

/**
 * @brief 把新联系人表写入非活动缓冲并翻转生效。
 *
 * @param[in] list 联系人数组。
 * @param[in] count 联系人数。
 * @return void
 */
static void publish_to_standby(const alarm_contact_t *list, qosa_uint8_t count)
{
    qosa_uint8_t standby = (qosa_uint8_t)(g_active_idx ^ 1u);

    qosa_memset(g_contacts[standby], 0, sizeof(g_contacts[standby]));
    qosa_memcpy(g_contacts[standby], list, sizeof(alarm_contact_t) * count);
    g_counts[standby] = count;
    g_active_idx = standby;
}

/**
 * @brief 初始化紧急联系人存储。
 *
 * 先装载出厂默认联系人，再尝试用 NV 中保存的云端配置覆盖。
 *
 * @return int 0 表示使用了 NV 配置，1 表示回落到出厂默认，-1 表示初始化失败。
 */
int alarm_contact_store_init(void)
{
    alarm_contact_t parsed[ALARM_CONTACT_MAX] = {0};
    qosa_uint8_t    parsed_count = 0;
    char            json[ALARM_CONTACT_JSON_MAX] = {0};

    if (g_store_lock == QOSA_NULL)
    {
        if (qosa_mutex_create(&g_store_lock) != QOSA_OK)
        {
            QLOGE("[alarm] contact store mutex create failed");
            return -1;
        }
    }

    load_defaults(g_active_idx);

    if (nv_read_contacts(json) != 0)
    {
        QLOGI("[store] no contacts in nv, use factory defaults");
        return 1;
    }

    if (alarm_contacts_parse_json(json, parsed, &parsed_count, QOSA_NULL) != 0)
    {
        QLOGW("[store] contacts in nv are invalid, use factory defaults");
        return 1;
    }

    publish_to_standby(parsed, parsed_count);
    QLOGI("[store] contacts restored from nv, count=%d", parsed_count);
    return 0;
}

/**
 * @brief 获取当前生效的联系人表。
 *
 * @param[out] count 联系人数输出指针，可为 QOSA_NULL。
 * @return const alarm_contact_t* 只读联系人数组首地址，永不为 QOSA_NULL。
 * @note 返回的缓冲区在下次更新时不会被原地改写，读取方无需加锁。
 */
const alarm_contact_t *alarm_contact_store_get(qosa_uint8_t *count)
{
    qosa_uint8_t idx = g_active_idx;

    if (count != QOSA_NULL)
    {
        *count = g_counts[idx];
    }

    return g_contacts[idx];
}

/**
 * @brief 整体替换联系人表并持久化。
 *
 * @param[in] list 新联系人数组。
 * @param[in] count 新联系人数，取值 1..ALARM_CONTACT_MAX。
 * @return int 0 表示内存与 NV 均更新成功，1 表示内存已更新但 NV 写入失败，-1 表示参数错误。
 */
int alarm_contact_store_update(const alarm_contact_t *list, qosa_uint8_t count)
{
    char json[ALARM_CONTACT_JSON_MAX] = {0};
    int  ret = 0;

    if ((list == QOSA_NULL) || (count == 0) || (count > ALARM_CONTACT_MAX))
    {
        return -1;
    }

    if (alarm_contacts_build_json(list, count, json, sizeof(json)) != 0)
    {
        QLOGE("[alarm] contacts serialize failed");
        return -1;
    }

    if ((g_store_lock != QOSA_NULL) && (qosa_mutex_lock(g_store_lock, QOSA_WAIT_FOREVER) != QOSA_OK))
    {
        return -1;
    }

    publish_to_standby(list, count);
    ret = (nv_write_contacts(json) == 0) ? 0 : 1;

    if (g_store_lock != QOSA_NULL)
    {
        (void)qosa_mutex_unlock(g_store_lock);
    }

    QLOGI("[store] contacts updated, count=%d nv_ret=%d", count, ret);
    return ret;
}
