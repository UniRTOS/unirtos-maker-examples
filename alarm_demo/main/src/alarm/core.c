#include "qosa_def.h"
#include "qosa_log.h"

#define QOS_LOG_TAG LOG_TAG_DEMO

#include "alarm_app.h"
#include "alarm_config.h"
#include "alarm_core.h"
#include "alarm_feedback.h"
#include "alarm_service.h"

/**
 * @brief 更新状态机状态并触发反馈层.
 *
 * @param[in,out] ctx 报警上下文.
 * @param[in] state 目标状态.
 * @return void
 */
static void set_state(alarm_ctx_t *ctx, alarm_state_e state)
{
    if (ctx->session.state == state)
    {
        return;
    }

    ctx->session.state = state;
    feedback_apply_state(state);
}

/**
 * @brief 将会话置为失败态.
 *
 * @param[in,out] ctx 报警上下文.
 * @return void
 * @note 用于联系人轮询全部失败后的统一收敛.
 */
static void fail_session(alarm_ctx_t *ctx)
{
    ctx->session.active = QOSA_FALSE;
    ctx->session.call_connected = QOSA_FALSE;
    ctx->session.button_confirm_pending = QOSA_FALSE;
    ctx->session.call_remaining_ms = 0;
    ctx->session.stop_wait_remaining_ms = 0;
    /* 失败态停留一段时间后由 tick 自动收敛回空闲态。 */
    ctx->session.failed_recover_remaining_ms = ALARM_FAILED_RECOVER_MS;
    set_state(ctx, ALARM_STATE_FAILED);
}

/**
 * @brief 按联系人顺序发起下一次呼叫.
 *
 * @param[in,out] ctx 报警上下文.
 * @return void
 * @note 若当前联系人发起失败，会递归尝试下一个联系人.
 */
static void start_next_call(alarm_ctx_t *ctx)
{
    const alarm_contact_t *contacts = QOSA_NULL;
    qosa_uint8_t           contact_count = 0;
    int                    ret = 0;

    contacts = alarm_get_contacts(&contact_count);
    ctx->session.total_contacts = contact_count;

    if (ctx->session.current_contact_index >= contact_count)
    {
        QLOGW("[alarm] all emergency contacts exhausted");
        fail_session(ctx);
        return;
    }

    /* 先切换到拨号态，再调用语音接口，方便日志与状态对齐。 */
    set_state(ctx, ALARM_STATE_CALLING);
    ret = service_start_call(contacts[ctx->session.current_contact_index].phone_number);
    if (ret != 0)
    {
          QLOGE("[alarm] start voice call failed, contact=%d ret=%d",
              ctx->session.current_contact_index,
              ret);
        /* 当前联系人拨号失败，立即轮询下一联系人。 */
        ctx->session.current_contact_index++;
        start_next_call(ctx);
        return;
    }

    ctx->session.call_remaining_ms = ALARM_CALL_TIMEOUT_MS;
    ctx->session.stop_wait_remaining_ms = 0;
    QLOGI("[alarm] dial contact[%d]=%s",
          ctx->session.current_contact_index,
          contacts[ctx->session.current_contact_index].name);
}

/**
 * @brief 开始一次新的报警会话.
 *
 * @param[in,out] ctx 报警上下文.
 * @param[in] trigger 触发来源.
 * @return void
 * @note 会话启动时仅群发一次短信，随后进入联系人拨号轮询.
 */
static void begin_alert(alarm_ctx_t *ctx, alarm_trigger_source_e trigger)
{
    const alarm_contact_t *contacts = QOSA_NULL;
    qosa_uint8_t           contact_count = 0;

    if (ctx->session.active == QOSA_TRUE)
    {
        QLOGW("[alarm] ignore trigger=%d while alarm active", trigger);
        return;
    }

    contacts = alarm_get_contacts(&contact_count);
    ctx->session.active = QOSA_TRUE;
    ctx->session.call_connected = QOSA_FALSE;
    ctx->session.button_confirm_pending = QOSA_FALSE;
    ctx->session.current_contact_index = 0;
    ctx->session.total_contacts = contact_count;
    ctx->session.call_remaining_ms = 0;
    ctx->session.stop_wait_remaining_ms = 0;

    set_state(ctx, ALARM_STATE_ALERT_PENDING);

    /* 短信为并行通知能力，不阻塞后续拨号；每次会话仅群发一次。 */
    service_start_sms_fanout(contacts, contact_count);

    /* 触发提示与 IMS 通话共享外部音频链路，先等提示结束再开始拨号。 */
    if (feedback_wait_voice_finish() != QOSA_OK)
    {
        QLOGW("[alarm] trigger tts wait timed out, continue dialing");
    }

    start_next_call(ctx);
}

/**
 * @brief 初始化状态机会话数据.
 *
 * @param[in,out] ctx 报警上下文.
 * @return void
 */
void alarm_core_init(alarm_ctx_t *ctx)
{
    qosa_memset(&ctx->session, 0, sizeof(ctx->session));
    ctx->session.state = ALARM_STATE_IDLE;
    ctx->session.battery_poll_remaining_ms = ALARM_BATTERY_POLL_MS;
}

/**
 * @brief 处理单个报警事件.
 *
 * @param[in,out] ctx 报警上下文.
 * @param[in] event 输入事件.
 * @return void
 */
void alarm_core_handle_event(alarm_ctx_t *ctx, const alarm_event_t *event)
{
    if ((ctx == QOSA_NULL) || (event == QOSA_NULL))
    {
        return;
    }

    switch (event->type)
    {
        case ALARM_EVENT_VOICE_TRIGGER:
            begin_alert(ctx, ALARM_TRIGGER_VOICE);
            break;

        case ALARM_EVENT_SOS_EDGE:
            if ((ctx->session.active == QOSA_TRUE) ||
                (ctx->session.button_confirm_pending == QOSA_TRUE))
            {
                QLOGD("[alarm] ignore repeated sos edge during debounce window");
                break;
            }

            /* 按键触发进入确认窗口，避免抖动或误触。 */
            ctx->session.button_confirm_pending = QOSA_TRUE;
            ctx->session.button_confirm_remaining_ms = ALARM_BUTTON_CONFIRM_MS;
            set_state(ctx, ALARM_STATE_TRIGGER_CONFIRM);
            break;

        case ALARM_EVENT_CALL_RING:
            QLOGI("[alarm] voice call ring event");
            break;

        case ALARM_EVENT_CALL_CONNECTED:
            if (ctx->session.active == QOSA_TRUE)
            {
                ctx->session.call_connected = QOSA_TRUE;
                ctx->session.call_remaining_ms = 0;
                ctx->session.stop_wait_remaining_ms = 0;
                set_state(ctx, ALARM_STATE_CONNECTED);
            }
            break;

        case ALARM_EVENT_CALL_DISCONNECTED:
            if (ctx->session.active == QOSA_FALSE)
            {
                set_state(ctx, ALARM_STATE_IDLE);
                break;
            }

            if (ctx->session.call_connected == QOSA_TRUE)
            {
                ctx->session.active = QOSA_FALSE;
                ctx->session.call_connected = QOSA_FALSE;
                set_state(ctx, ALARM_STATE_IDLE);
                break;
            }

            /* 未接通即断开，认为当前联系人失败并轮询下一位。 */
            ctx->session.current_contact_index++;
            ctx->session.stop_wait_remaining_ms = 0;
            start_next_call(ctx);
            break;

        case ALARM_EVENT_SMS_RESULT:
            QLOGI("[alarm] sms result contact=%d err=%x",
                  event->data.sms.contact_index,
                  event->data.sms.err_code);
            break;

        case ALARM_EVENT_LOW_BATTERY:
            feedback_notify_low_battery(event->data.battery.voltage_mv,
                                        event->data.battery.percent);
            break;

        case ALARM_EVENT_CONTACTS_UPDATED:
            if (ctx->session.active == QOSA_TRUE)
            {
                /* 报警进行中不换表，避免拨号轮询下标与联系人错位。 */
                QLOGW("[alarm] contacts updated during active session, apply after recovery");
                break;
            }

            (void)alarm_get_contacts(&ctx->session.total_contacts);
            ctx->session.current_contact_index = 0;
            QLOGI("[alarm] contacts applied, total=%d", ctx->session.total_contacts);
            break;

        default:
            break;
    }
}

/**
 * @brief 状态机周期驱动.
 *
 * @param[in,out] ctx 报警上下文.
 * @return void
 * @note 负责按键确认倒计时、呼叫超时推进、低电量轮询.
 */
void alarm_core_tick(alarm_ctx_t *ctx)
{
    if (ctx == QOSA_NULL)
    {
        return;
    }

    if (ctx->session.button_confirm_pending == QOSA_TRUE)
    {
        if (ctx->session.button_confirm_remaining_ms > ALARM_LOOP_WAIT_MS)
        {
            ctx->session.button_confirm_remaining_ms -= ALARM_LOOP_WAIT_MS;
        }
        else
        {
            ctx->session.button_confirm_pending = QOSA_FALSE;
            ctx->session.button_confirm_remaining_ms = 0;
            /* 仅使用队列事件驱动：收到 SOS 边沿后到期直接进入报警流程。 */
            begin_alert(ctx, ALARM_TRIGGER_SOS_BUTTON);
        }
    }

    if ((ctx->session.active == QOSA_TRUE) &&
        (ctx->session.state == ALARM_STATE_CALLING) &&
        (ctx->session.call_connected == QOSA_FALSE))
    {
        if (ctx->session.call_remaining_ms > ALARM_LOOP_WAIT_MS)
        {
            ctx->session.call_remaining_ms -= ALARM_LOOP_WAIT_MS;
        }
        else
        {
            ctx->session.call_remaining_ms = 0;
            /* 单联系人呼叫超时后先请求停止，再等待 IMS 断开事件。 */
            set_state(ctx, ALARM_STATE_CALL_STOPPING);
            ctx->session.stop_wait_remaining_ms = ALARM_CALL_STOP_WAIT_MS;
            if (service_stop_call() != 0)
            {
                /* 若停止呼叫请求失败，直接进入下一个联系人，避免卡死。 */
                ctx->session.current_contact_index++;
                ctx->session.stop_wait_remaining_ms = 0;
                start_next_call(ctx);
            }
        }
    }

    if ((ctx->session.active == QOSA_TRUE) &&
        (ctx->session.state == ALARM_STATE_CALL_STOPPING) &&
        (ctx->session.call_connected == QOSA_FALSE) &&
        (ctx->session.stop_wait_remaining_ms > 0))
    {
        if (ctx->session.stop_wait_remaining_ms > ALARM_LOOP_WAIT_MS)
        {
            ctx->session.stop_wait_remaining_ms -= ALARM_LOOP_WAIT_MS;
        }
        else
        {
            /* 停呼等待超时仍未收到断开事件，兜底推进到下一个联系人。 */
            ctx->session.stop_wait_remaining_ms = 0;
            ctx->session.current_contact_index++;
            start_next_call(ctx);
        }
    }

    if (ctx->session.battery_poll_remaining_ms > ALARM_LOOP_WAIT_MS)
    {
        ctx->session.battery_poll_remaining_ms -= ALARM_LOOP_WAIT_MS;
    }
    else
    {
        /* 到达轮询周期后触发一次电量采样。 */
        ctx->session.battery_poll_remaining_ms = ALARM_BATTERY_POLL_MS;
        feedback_poll_battery();
    }

    if ((ctx->session.state == ALARM_STATE_FAILED) &&
        (ctx->session.failed_recover_remaining_ms > 0))
    {
        if (ctx->session.failed_recover_remaining_ms > ALARM_LOOP_WAIT_MS)
        {
            ctx->session.failed_recover_remaining_ms -= ALARM_LOOP_WAIT_MS;
        }
        else
        {
            /* 失败提示到期，复位联系人轮询状态并回到空闲态等待下一次触发。 */
            ctx->session.failed_recover_remaining_ms = 0;
            ctx->session.current_contact_index = 0;
            ctx->session.total_contacts = 0;
            set_state(ctx, ALARM_STATE_IDLE);
        }
    }
}
