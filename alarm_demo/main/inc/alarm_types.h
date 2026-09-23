#ifndef __ALARM_TYPES_H__
#define __ALARM_TYPES_H__

#include "qosa_def.h"
#include "qosa_sys.h"

#define ALARM_CONTACT_NAME_MAX  (32) /*!< 联系人名最大字节数（含结束符） */
#define ALARM_CONTACT_PHONE_MAX (24) /*!< 联系人号码最大字节数（含结束符） */
#define ALARM_CONTACT_MAX       (3)  /*!< 联系人条目上限 */

/**
 * @enum alarm_trigger_source_e
 * @brief 报警触发来源枚举.
 */
typedef enum
{
    ALARM_TRIGGER_VOICE = 0,   /*!< 语音关键词触发 */
    ALARM_TRIGGER_SOS_BUTTON,  /*!< SOS 按键触发 */
} alarm_trigger_source_e;

/**
 * @enum alarm_state_e
 * @brief 报警会话状态枚举.
 */
typedef enum
{
    ALARM_STATE_IDLE = 0,         /*!< 空闲态 */
    ALARM_STATE_TRIGGER_CONFIRM,  /*!< 按键触发确认态 */
    ALARM_STATE_ALERT_PENDING,    /*!< 告警准备态 */
    ALARM_STATE_CALLING,          /*!< 拨号进行态 */
    ALARM_STATE_CALL_STOPPING,    /*!< 停止当前呼叫态 */
    ALARM_STATE_CONNECTED,        /*!< 呼叫已接通态 */
    ALARM_STATE_FAILED,           /*!< 联系人拨号全部失败态 */
} alarm_state_e;

/**
 * @enum alarm_event_type_e
 * @brief 报警状态机事件类型枚举.
 */
typedef enum
{
    ALARM_EVENT_VOICE_TRIGGER = 0, /*!< 语音触发事件 */
    ALARM_EVENT_SOS_EDGE,          /*!< SOS 中断边沿事件 */
    ALARM_EVENT_CALL_RING,         /*!< 来电振铃事件 */
    ALARM_EVENT_CALL_CONNECTED,    /*!< 呼叫接通事件 */
    ALARM_EVENT_CALL_DISCONNECTED, /*!< 呼叫断开事件 */
    ALARM_EVENT_SMS_RESULT,        /*!< 短信发送结果事件 */
    ALARM_EVENT_LOW_BATTERY,       /*!< 低电量事件 */
    ALARM_EVENT_CONTACTS_UPDATED,  /*!< 紧急联系人配置已更新事件 */
} alarm_event_type_e;

/**
 * @struct alarm_contact_t
 * @brief 紧急联系人信息.
 */
typedef struct
{
    char name[ALARM_CONTACT_NAME_MAX];          /*!< 联系人显示名 */
    char phone_number[ALARM_CONTACT_PHONE_MAX]; /*!< 联系人号码 */
} alarm_contact_t;

/**
 * @struct alarm_event_t
 * @brief 报警总线事件对象.
 */
typedef struct
{
    alarm_event_type_e type; /*!< 事件类型 */
    union
    {
        struct
        {
            qosa_uint8_t contact_index; /*!< 联系人下标 */
            int          err_code;      /*!< 短信结果错误码 */
        } sms;
        struct
        {
            qosa_uint32_t voltage_mv; /*!< 电池毫伏值 */
            qosa_uint8_t  percent;    /*!< 电池百分比 */
        } battery;
    } data;
} alarm_event_t;

/**
 * @struct alarm_session_t
 * @brief 报警会话运行时上下文.
 */
typedef struct
{
    alarm_state_e state;                       /*!< 当前状态机状态 */
    qosa_bool_t   active;                      /*!< 会话是否处于进行中 */
    qosa_bool_t   call_connected;              /*!< 当前是否已接通 */
    qosa_bool_t   button_confirm_pending;      /*!< 是否在按键确认窗口 */
    qosa_uint8_t  current_contact_index;       /*!< 当前拨号联系人下标 */
    qosa_uint8_t  total_contacts;              /*!< 联系人总数 */
    qosa_uint32_t button_confirm_remaining_ms; /*!< 按键确认剩余毫秒 */
    qosa_uint32_t call_remaining_ms;           /*!< 当前呼叫剩余超时毫秒 */
    qosa_uint32_t stop_wait_remaining_ms;      /*!< 停止呼叫后的等待毫秒 */
    qosa_uint32_t battery_poll_remaining_ms;   /*!< 电量轮询剩余毫秒 */
    qosa_uint32_t failed_recover_remaining_ms; /*!< 失败态自动回到空闲态的剩余毫秒 */
} alarm_session_t;

/**
 * @struct alarm_ctx_t
 * @brief 报警应用全局上下文.
 */
typedef struct
{
    qosa_task_t      task;     /*!< 报警主任务句柄 */
    qosa_msgq_t      msgq;     /*!< 事件消息队列句柄 */
    alarm_session_t  session;  /*!< 会话运行状态 */
} alarm_ctx_t;

#endif /* __ALARM_TYPES_H__ */
