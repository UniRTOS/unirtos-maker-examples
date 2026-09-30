#include "qosa_def.h"
#include "qosa_log.h"
#include "qosa_sdk_version.h"

#define QOS_LOG_TAG LOG_TAG_DEMO

#include "alarm_app.h"
#include "alarm_cloud.h"
#include "alarm_config.h"
#include "alarm_core.h"
#include "alarm_feedback.h"
#include "alarm_machine.h"
#include "alarm_service.h"

static alarm_ctx_t g_alarm_ctx = {0};

/**
 * @brief 报警主任务入口。
 *
 * 负责初始化子模块、消费事件队列并周期驱动状态机。
 *
 * @param[in] ctx 任务入参，实际类型为 alarm_ctx_t*。
 * @return void
 * @note 各模块初始化完成后会等待 ALARM_BOOT_SETTLE_SEC 秒再进入事件循环,
 *       以便网络与语音模块就绪；进入循环后不应阻塞在不可超时接口上。
 */
static void alarm_task(void *ctx)
{
    alarm_ctx_t   *app_ctx = (alarm_ctx_t *)ctx;
    alarm_event_t  event = {0};
    int            ret = 0;
    int            service_ret = 0;
    int            key_ret = 0;
    int            asr_ret = 0;

    /* 先初始化状态机，再初始化反馈/服务/触发模块。 */
    alarm_core_init(app_ctx);
    (void)alarm_contact_store_init();
    feedback_init();
    service_ret = service_init();
    feedback_tts_init();
    key_ret = key_init();
    asr_ret = asr_pro_init();
    (void)alarm_cloud_init();
    feedback_apply_state(ALARM_STATE_IDLE);

    if ((service_ret != 0) || (key_ret != 0) || (asr_ret != 0))
    {
          QLOGW("[alarm] started with degraded capabilities, service_ret=%d key_ret=%d asr_ret=%d",
              service_ret,
              key_ret,
              asr_ret);
    }

    QLOGI("[alarm] task started, sdk=%s", qosa_sdk_get_version());

    /* 预留网络与语音模块就绪时间，避免启动瞬间触发导致不稳定。 */
    qosa_task_sleep_sec(ALARM_BOOT_SETTLE_SEC);

    while (1)
    {
        /* 有事件则优先处理；无事件则执行周期 tick（超时、轮询等）。 */
        ret = qosa_msgq_wait(app_ctx->msgq,
                             (qosa_uint8_t *)&event,
                             sizeof(event),
                             ALARM_LOOP_WAIT_MS);
        if (ret == QOSA_OK)
        {
            alarm_core_handle_event(app_ctx, &event);
        }
        else
        {
            alarm_core_tick(app_ctx);
        }
    }
}

/**
 * @brief 初始化报警应用。
 *
 * 创建消息队列与报警任务，提供状态机运行基础。
 *
 * @return void
 * @note 若重复初始化将直接返回并打印告警日志。
 */
void alarm_init(void)
{
    int ret = 0;

    if (g_alarm_ctx.task != QOSA_NULL)
    {
        QLOGW("[alarm] app already initialized");
        return;
    }

    /* 先创建队列，再创建任务，失败时按相反顺序回滚。 */
    ret = qosa_msgq_create(&g_alarm_ctx.msgq, sizeof(alarm_event_t), ALARM_MSGQ_DEPTH);
    if (ret != QOSA_OK)
    {
        QLOGE("[alarm] msgq create failed, ret=%d", ret);
        return;
    }

    ret = qosa_task_create(&g_alarm_ctx.task,
                           ALARM_TASK_STACK_SIZE,
                           ALARM_TASK_PRIO,
                           "alarm_app",
                           alarm_task,
                           &g_alarm_ctx);
    if (ret != QOSA_OK)
    {
        QLOGE("[alarm] task create failed, ret=%d", ret);
        (void)qosa_msgq_delete(g_alarm_ctx.msgq);
        g_alarm_ctx.msgq = QOSA_NULL;
    }
}

/**
 * @brief 投递报警事件到主任务队列。
 *
 * @param[in] event 事件对象指针。
 * @return int QOSA_OK 表示成功，其他值表示投递失败。
 * @note 使用无等待模式，队列满时会丢弃并输出日志。
 */
int alarm_post_event(const alarm_event_t *event)
{
    int ret = 0;

    if ((event == QOSA_NULL) || (g_alarm_ctx.msgq == QOSA_NULL))
    {
        return -1;
    }

    ret = qosa_msgq_release(g_alarm_ctx.msgq,
                            sizeof(*event),
                            (qosa_uint8_t *)event,
                            QOSA_NO_WAIT);
    if (ret != QOSA_OK)
    {
        QLOGW("[alarm] drop event type=%d ret=%d", event->type, ret);
    }

    return ret;
}

/**
 * @brief 获取联系人数组及数量。
 *
 * @param[out] count 联系人数输出指针，可为 QOSA_NULL。
 * @return const alarm_contact_t* 联系人数组首地址。
 * @note 实际数据由联系人存储模块维护，可被云端下发配置覆盖。
 */
const alarm_contact_t *alarm_get_contacts(qosa_uint8_t *count)
{
    return alarm_contact_store_get(count);
}
