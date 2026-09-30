#include "qosa_def.h"
#include "qosa_log.h"
#include "qosa_event_notify.h"
#include "qosa_datacall.h"

#define QOS_LOG_TAG LOG_TAG_DEMO

#include "coze_network.h"
#include "chatbot_config.h"
#include "chatbot_types.h"
#include "chat_bus.h"

static qosa_bool_t g_net_ready = QOSA_FALSE;
static qosa_bool_t g_net_activating = QOSA_FALSE;

/**
 * @brief 向聊天核心投递网络已就绪事件。
 */
static void net_mgr_post_ready_event(void)
{
    chat_event_t event = {0};

    event.type = CHAT_EVT_NET_READY;
    (void)chat_bus_post_event(&event);
}

/**
 * @brief 查询指定 SIM/PDP 的当前 TCP/IP 数据连接状态。
 *
 * @return QOSA_TRUE 表示网络已就绪；否则返回 QOSA_FALSE。
 */
static qosa_bool_t net_mgr_refresh_ready(void)
{
    qosa_datacall_conn_t conn = qosa_datacall_conn_new(CHATBOT_COZE_SIM_CID, CHATBOT_COZE_PDP_CID, QOSA_DATACALL_CONN_TCPIP);
    qosa_bool_t          ready = QOSA_FALSE;

    if (conn == QOSA_DATACALL_CONN_INVALID)
    {
        QLOGW("[chat_net_mgr] invalid datacall conn, sim=%d pdp=%d", CHATBOT_COZE_SIM_CID, CHATBOT_COZE_PDP_CID);
        return QOSA_FALSE;
    }

    ready = qosa_datacall_get_status(conn);
    if (ready != g_net_ready)
    {
        g_net_ready = ready;
        QLOGI("[chat_net_mgr] network %s by status query, sim=%d pdp=%d", ready ? "ready" : "not ready", CHATBOT_COZE_SIM_CID, CHATBOT_COZE_PDP_CID);
    }
    return g_net_ready;
}

/**
 * @brief 处理异步 PDP 激活确认并更新网络状态。
 *
 * @param[in] user_argv 注册回调时提供的用户上下文，当前未使用。
 * @param[in] act_info PDP 激活结果，回调期间由数据连接模块提供。
 */
static void net_mgr_datacall_act_cb(void *user_argv, qosa_datacall_act_cnf_t *act_info)
{
    (void)user_argv;

    g_net_activating = QOSA_FALSE;

    if (act_info == QOSA_NULL)
    {
        QLOGW("[chat_net_mgr] datacall activate cb null");
        return;
    }

    if ((act_info->simid != CHATBOT_COZE_SIM_CID) || (act_info->pdpid != CHATBOT_COZE_PDP_CID))
    {
        return;
    }

    if ((act_info->opt == QOSA_PDP_OPT_ACTIVE) && (act_info->status == QOSA_TRUE))
    {
        g_net_ready = QOSA_TRUE;
        QLOGI("[chat_net_mgr] datacall active ok, sim=%d pdp=%d", act_info->simid, act_info->pdpid);
        net_mgr_post_ready_event();
        return;
    }

    g_net_ready = QOSA_FALSE;
    QLOGW("[chat_net_mgr] datacall active failed, sim=%d pdp=%d err=%d status=%d opt=%d", act_info->simid, act_info->pdpid, act_info->err_code, act_info->status, act_info->opt);
}

/**
 * @brief PDP 激活状态变化回调，网络就绪后置位并通知核心状态机。
 *
 * @note 若 PDP 在本模块注册事件之前已经激活完成，将无法收到本次事件，
 *       核心状态机会在下一次唤醒时通过 coze_session_request_start() 重新尝试，
 *       实际项目如需更强健的就绪检测，可结合 qosa_datacall 状态查询接口补充轮询。
 */
static int net_pdp_event_cb(void *user_argv, void *argv)
{
    qosa_datacall_act_event_t *act_event = (qosa_datacall_act_event_t *)argv;

    (void)user_argv;

    if (act_event != QOSA_NULL)
    {
        if ((act_event->simid != CHATBOT_COZE_SIM_CID) || (act_event->pdpid != CHATBOT_COZE_PDP_CID))
        {
            return 0;
        }

        if (act_event->opt == QOSA_PDP_OPT_DEACTIVE)
        {
            g_net_ready = QOSA_FALSE;
            QLOGW("[chat_net_mgr] network deactive event, sim=%d pdp=%d", act_event->simid, act_event->pdpid);
            return 0;
        }
    }

    if ((act_event == QOSA_NULL) || (act_event->opt == QOSA_PDP_OPT_ACTIVE))
    {
        g_net_ready = QOSA_TRUE;
        QLOGI("[chat_net_mgr] network ready event, sim=%d pdp=%d", CHATBOT_COZE_SIM_CID, CHATBOT_COZE_PDP_CID);
        net_mgr_post_ready_event();
    }
    return 0;
}

/**
 * @brief 请求异步激活 Coze 使用的 SIM/PDP 数据连接。
 *
 * @return 0 表示已就绪或激活请求已提交；返回负值表示当前无法发起请求。
 */
int net_mgr_request_ready(void)
{
    qosa_datacall_conn_t  conn = QOSA_DATACALL_CONN_INVALID;
    qosa_datacall_errno_e ret = QOSA_DATACALL_OK;

    if (net_mgr_refresh_ready() == QOSA_TRUE)
    {
        return 0;
    }

    if (g_net_activating == QOSA_TRUE)
    {
        QLOGI("[chat_net_mgr] datacall activating, sim=%d pdp=%d", CHATBOT_COZE_SIM_CID, CHATBOT_COZE_PDP_CID);
        return 0;
    }

    if (qosa_datacall_wait_attached(CHATBOT_COZE_SIM_CID, 0) == QOSA_FALSE)
    {
        QLOGW("[chat_net_mgr] network not attached, sim=%d", CHATBOT_COZE_SIM_CID);
        return -1;
    }

    conn = qosa_datacall_conn_new(CHATBOT_COZE_SIM_CID, CHATBOT_COZE_PDP_CID, QOSA_DATACALL_CONN_TCPIP);
    if (conn == QOSA_DATACALL_CONN_INVALID)
    {
        QLOGW("[chat_net_mgr] invalid datacall conn, sim=%d pdp=%d", CHATBOT_COZE_SIM_CID, CHATBOT_COZE_PDP_CID);
        return -1;
    }

    ret = qosa_datacall_start_async(conn, 60, (datacall_callback_cb_ptr)net_mgr_datacall_act_cb, QOSA_NULL);
    if (ret != QOSA_DATACALL_OK)
    {
        QLOGW("[chat_net_mgr] datacall start async failed, ret=%d", ret);
        return -1;
    }

    g_net_activating = QOSA_TRUE;
    QLOGI("[chat_net_mgr] datacall activating start, sim=%d pdp=%d", CHATBOT_COZE_SIM_CID, CHATBOT_COZE_PDP_CID);
    return 0;
}

/**
 * @brief 注册 PDP 状态事件并初始化网络就绪状态。
 *
 * @return 0 表示初始化成功；返回负值表示事件注册失败。
 */
int net_mgr_init(void)
{
    if (qosa_event_notify_register(QOSA_EVENT_NET_PDP_ACT, net_pdp_event_cb, QOSA_NULL) != QOSA_EVENT_NOTIFY_OK)
    {
        QLOGW("[chat_net_mgr] register PDP event failed");
        return -1;
    }
    (void)net_mgr_refresh_ready();
    return 0;
}

/**
 * @brief 获取当前网络是否已就绪。
 *
 * @return QOSA_TRUE 表示网络已就绪；否则返回 QOSA_FALSE。
 */
qosa_bool_t net_mgr_is_ready(void)
{
    return net_mgr_refresh_ready();
}
