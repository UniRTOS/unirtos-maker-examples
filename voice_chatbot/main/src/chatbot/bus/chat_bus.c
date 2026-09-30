#include "qosa_def.h"
#include "qosa_sys.h"
#include "qosa_log.h"

#define QOS_LOG_TAG LOG_TAG_DEMO

#include "chat_bus.h"
#include "chatbot_config.h"

static qosa_msgq_t g_chat_bus_msgq = QOSA_NULL;

/**
 * @brief 创建聊天控制事件总线队列。
 *
 * @return 0 表示成功；返回负值表示队列创建失败。
 */
int chat_bus_init(void)
{
    int ret = qosa_msgq_create(&g_chat_bus_msgq, sizeof(chat_event_t), CHATBOT_BUS_QUEUE_DEPTH);
    if (ret != QOSA_ERROR_OK)
    {
        QLOGE("[chat_bus] msgq create failed, ret=%d", ret);
        return -1;
    }
    return 0;
}

/**
 * @brief 向聊天控制事件总线投递一条事件。
 *
 * @param[in] event 待投递的事件。
 * @return 0 表示成功；返回负值表示参数无效、队列未初始化或已满。
 */
int chat_bus_post_event(const chat_event_t *event)
{
    if ((event == QOSA_NULL) || (g_chat_bus_msgq == QOSA_NULL))
    {
        return -1;
    }
    QLOGV("[chatbot] post event(type=%d)", event->type);
    /* 队列满时直接丢弃，不阻塞投递方（控制事件低频，正常不会积压）。 */
    int ret = qosa_msgq_release(g_chat_bus_msgq, sizeof(chat_event_t), (qosa_uint8_t *)event, QOSA_NO_WAIT);
    if (ret != QOSA_ERROR_OK)
    {
        QLOGW("[chat_bus] post event(type=%d) failed, ret=%d", event->type, ret);
        return -1;
    }
    return 0;
}

/**
 * @brief 等待并取出一条聊天控制事件。
 *
 * @param[out] event      用于接收事件的结构体。
 * @param[in]  timeout_ms 最大等待时间，单位为毫秒。
 * @return 0 表示成功；返回负值表示参数无效、队列未初始化或等待失败。
 */
int chat_bus_wait_event(chat_event_t *event, qosa_uint32_t timeout_ms)
{
    if ((event == QOSA_NULL) || (g_chat_bus_msgq == QOSA_NULL))
    {
        return -1;
    }

    return qosa_msgq_wait(g_chat_bus_msgq, (qosa_uint8_t *)event, sizeof(chat_event_t), timeout_ms);
}
