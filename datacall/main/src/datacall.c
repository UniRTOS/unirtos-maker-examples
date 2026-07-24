/*****************************************************************/ /**
* @file datacall.c
* @brief
* @author david.deng@quectel.com
* @date 2025-04-23
*
* @copyright Copyright (c) 2023 Quectel Wireless Solution, Co., Ltd.
* All Rights Reserved. Quectel Wireless Solution Proprietary and Confidential.
*
* @par EDIT HISTORY FOR MODULE
* <table>
* <tr><th>Date <th>Version <th>Author <th>Description
* <tr><td>2025-04-23 <td>1.0 <td>david.deng <td> Init
* </table>
**********************************************************************/
#include "qosa_def.h"
#include "qosa_log.h"

#include "qosa_network.h"
#include "qosa_datacall.h"
#include "qosa_platform_cfg.h"
#include "qosa_ip_addr.h"
#include "qosa_event_notify.h"
#include "include.h"
#include "unirtos_app_init_registry.h"

#define QOS_LOG_TAG    LOG_TAG_DEMO

/** Maximum wait time for network attachment (seconds) */
#define DATACALL_DEMO_WAIT_ATTACH_MAX_WAIT_TIME   300
/** Maximum wait time for datacall dial-up establishment (seconds) */
#define DATACALL_DEMO_WAIT_DATACALL_MAX_WAIT_TIME 120

/** datacall demo task handle */
qosa_task_t g_datacall_demo_task = QOSA_NULL;
/** datacall demo message queue handle */
qosa_msgq_t g_datacall_demo_msgq = QOSA_NULL;

/**
 * @enum datacall_demo_msg_e
 * @brief datacall demo enumeration of message types
 *
 */
typedef enum
{
    DATACALL_NW_DEACT_MSG, /*!< datacall network deactivation message */
} datacall_demo_msg_e;

/**
 * @struct datacall_demo_msg_t
 * @brief datacall demo message structure
 *
 */
typedef struct
{
    datacall_demo_msg_e msgid; /*!< datacall demo message type */
    void               *argv;  /*!< datacall demo message argument */
} datacall_demo_msg_t;

/**
 * @struct datacall_demo_pdp_deact_ind_t
 * @brief datacall demo PDP deactivation indicator structure
 *
 */
typedef struct
{
    qosa_uint8_t simid; /*!< SIM identification */
    qosa_uint8_t pdpid; /*!< PDP identification */
} datacall_demo_pdp_deact_ind_t;

/**
 * @brief datacall demo PDP deactivation callback function
 *
 * @param[in] user_argv
 *          - user data pointer
 * @param[in] argv
 *          - datacall demo PDP deactivation indicator structure pointer
 * @return int
 */
int datacall_nw_deact_pdp_cb(void *user_argv, void *argv)
{
    QOSA_UNUSED(user_argv);

    datacall_demo_pdp_deact_ind_t *deact_ptr = QOSA_NULL;
    datacall_demo_msg_t            datacall_nw_deact_msg = {0};

    //get nw deact report params
    qosa_datacall_nw_deact_event_t *pdp_deatch_event = (qosa_datacall_nw_deact_event_t *)argv;

    QLOGI("[datacall]enter,simid=%d,pdpid=%d", pdp_deatch_event->simid, pdp_deatch_event->pdpid);

    // malloc memory
    deact_ptr = (datacall_demo_pdp_deact_ind_t *)qosa_malloc(sizeof(datacall_demo_pdp_deact_ind_t));
    if (deact_ptr == QOSA_NULL)  // if malloc fail ,return
    {
        return 0;
    }

    deact_ptr->simid = pdp_deatch_event->simid;
    deact_ptr->pdpid = pdp_deatch_event->pdpid;

    // Preparing to send messages to the message queue
    datacall_nw_deact_msg.msgid = DATACALL_NW_DEACT_MSG;
    datacall_nw_deact_msg.argv = deact_ptr;

    qosa_msgq_release(g_datacall_demo_msgq, sizeof(datacall_demo_msg_t), (qosa_uint8_t *)&datacall_nw_deact_msg, QOSA_NO_WAIT);
    return 0;
}

/**
 * @brief datacall dial-up PDP status change callback function
 *
 * @param[in] user_argv
 *          - User data pointer
 * @param[in] argv
 *          - PDP status event pointer
 * @return int
 *          - Return value
 */
int datacall_pdp_change_cb(void *user_argv, void *argv)
{
    QOSA_UNUSED(user_argv);
    QOSA_UNUSED(argv);

    // pdp act status event report

    //    qosa_datacall_act_event_t *pdp_act_status = (qosa_datacall_act_event_t *)argv;
    //    QLOGI("enter,pdpid=%d,opt=%d",pdp_act_status->pdpid,pdp_act_status->opt);

    return 0;
}

/**
 * @brief datacall dial-up demo task main function
 *
 * @param[in] arg
 *          - Task parameter (unused)
 */
static void unir_datacall_demo_task(void *arg)
{
    int                     ret = 0;
    int                     retry_count = 0;
    qosa_uint8_t            simid = 0;
    int                     profile_idx = 1;
    qosa_datacall_conn_t    conn;
    qosa_bool_t             datacall_status = QOSA_FALSE;
    qosa_datacall_ip_info_t info = {0};
    datacall_demo_msg_t     datacall_task_msg = {0};
    qosa_pdp_context_t      pdp_ctx = {0};
    qosa_bool_t             is_attached = QOSA_FALSE;
    char                    ip4addr_buf[CONFIG_QOSA_INET_ADDRSTRLEN] = {0};
    char                    ip6addr_buf[CONFIG_QOSA_INET6_ADDRSTRLEN] = {0};
    QOSA_UNUSED(arg);

    // Create message queue
    ret = qosa_msgq_create(&g_datacall_demo_msgq, sizeof(datacall_demo_msg_t), 20);
    QLOGI("[datacall]create msgq result=%d", ret);

    qosa_task_sleep_sec(3);

    // If attach is successful before the max time timeout, it will immediately return QOSA_TRUE and enter second while loop
    // Otherwise, it will block until timeout and returning QOSA_FALSE and enter first while loop
    is_attached = qosa_datacall_wait_attached(simid, DATACALL_DEMO_WAIT_ATTACH_MAX_WAIT_TIME);
    if (!is_attached)
    {
        QLOGI("[datacall]attach fail");
        goto exit;
    }

    // Register network PDN deactivation event callback
    qosa_event_notify_register(QOSA_EVENT_NW_PDN_DEACT, datacall_nw_deact_pdp_cb, QOSA_NULL);

    // Register PDP activation status report event callback
    qosa_event_notify_register(QOSA_EVENT_NET_PDP_ACT, datacall_pdp_change_cb, QOSA_NULL);

    // Configure PDP context: APN, IP type
    // If the operator has restrictions on the APN during registration, needs to be set the APN provided by the operator
    const char *apn_str = "3gnet";
    pdp_ctx.apn_valid = QOSA_TRUE;
    pdp_ctx.pdp_type = QOSA_PDP_TYPE_IP;  //ipv4
    if (pdp_ctx.apn_valid)
    {
        qosa_memcpy(pdp_ctx.apn, apn_str, qosa_strlen(apn_str));
    }

    ret = qosa_datacall_set_pdp_context(simid, profile_idx, &pdp_ctx);
    QLOGI("[datacall]set pdp context, ret=%d", ret);

    // Create datacall object
    conn = qosa_datacall_conn_new(simid, profile_idx, QOSA_DATACALL_CONN_TCPIP);

    // Start execute datacall (sync)
    ret = qosa_datacall_start(conn, DATACALL_DEMO_WAIT_DATACALL_MAX_WAIT_TIME);
    if (ret != QOSA_DATACALL_OK)
    {
        QLOGI("[datacall]datacall fail ,ret=%d", ret);
        goto exit;
    }

    // Get datacall status (0: deactive 1: active)
    datacall_status = qosa_datacall_get_status(conn);
    QLOGI("[datacall]datacall status=%d", datacall_status);

    // Get IP info from datacall
    ret = qosa_datacall_get_ip_info(conn, &info);
    QLOGI("[datacall]pdpid=%d,simid=%d", info.simcid.pdpid, info.simcid.simid);
    QLOGI("[datacall]ip_type=%d", info.ip_type);

    if (info.ip_type == QOSA_PDP_IPV4)
    {
        // IPv4 info
        qosa_memset(ip4addr_buf, 0, sizeof(ip4addr_buf));
        qosa_ip_addr_inet_ntop(QOSA_IP_ADDR_AF_INET, &info.ipv4_ip.addr.ipv4_addr, ip4addr_buf, sizeof(ip4addr_buf));
        QLOGI("[datacall]ipv4 addr:%s", ip4addr_buf);
    }
    else if (info.ip_type == QOSA_PDP_IPV6)
    {
        // IPv6 info
        qosa_memset(ip6addr_buf, 0, sizeof(ip6addr_buf));
        qosa_ip_addr_inet_ntop(QOSA_IP_ADDR_AF_INET6, &info.ipv6_ip.addr.ipv6_addr, ip6addr_buf, sizeof(ip6addr_buf));
        QLOGI("[datacall]ipv6 addr:%s", ip6addr_buf);
    }
    else
    {
        // IPv4 and IPv6 info
        qosa_memset(ip4addr_buf, 0, sizeof(ip4addr_buf));
        qosa_ip_addr_inet_ntop(QOSA_IP_ADDR_AF_INET, &info.ipv4_ip.addr.ipv4_addr, ip4addr_buf, sizeof(ip4addr_buf));
        QLOGI("[datacall]ipv4 addr:%s", ip4addr_buf);
        qosa_memset(ip6addr_buf, 0, sizeof(ip6addr_buf));
        qosa_ip_addr_inet_ntop(QOSA_IP_ADDR_AF_INET6, &info.ipv6_ip.addr.ipv6_addr, ip6addr_buf, sizeof(ip6addr_buf));
        QLOGI("[datacall]ipv6 addr:%s", ip6addr_buf);
    }

    while (1)
    {
        ret = qosa_msgq_wait(g_datacall_demo_msgq, (qosa_uint8_t *)&datacall_task_msg, sizeof(datacall_demo_msg_t), QOSA_WAIT_FOREVER);
        if (ret != 0)
            continue;
        QLOGI("[datacall]enter datacall demo task, msgid=%d", datacall_task_msg.msgid);

        switch (datacall_task_msg.msgid)
        {
            case DATACALL_NW_DEACT_MSG: {
                datacall_demo_pdp_deact_ind_t *deact_ptr = (datacall_demo_pdp_deact_ind_t *)datacall_task_msg.argv;
                QLOGI("[datacall]simid=%d,deact pdpid=%d", deact_ptr->simid, deact_ptr->pdpid);

                // Try reactive 10 times, time interval is 20 seconds
                while (((ret = qosa_datacall_start(conn, DATACALL_DEMO_WAIT_DATACALL_MAX_WAIT_TIME)) != QOSA_DATACALL_OK) && (retry_count < 10))
                {
                    retry_count++;
                    QLOGI("[datacall]datacall fail, the retry count is %d", retry_count);
                    qosa_task_sleep_sec(20);
                }

                if (ret == QOSA_DATACALL_OK)
                {
                    retry_count = 0;

                    // Get datacall status (0: deactive 1: active)
                    datacall_status = qosa_datacall_get_status(conn);
                    QLOGI("[datacall]datacall status=%d", datacall_status);
                    // Get IP info from datacall
                    ret = qosa_datacall_get_ip_info(conn, &info);
                    QLOGI("[datacall]pdpid=%d,simid=%d", info.simcid.pdpid, info.simcid.simid);
                    QLOGI("[datacall]ip type=%d", info.ip_type);
                    char ip4addr_buf[CONFIG_QOSA_INET_ADDRSTRLEN] = {0};
                    char ip6addr_buf[CONFIG_QOSA_INET6_ADDRSTRLEN] = {0};

                    if (info.ip_type == QOSA_PDP_IPV4)
                    {
                        qosa_memset(ip4addr_buf, 0, sizeof(ip4addr_buf));
                        qosa_ip_addr_inet_ntop(QOSA_IP_ADDR_AF_INET, &info.ipv4_ip.addr.ipv4_addr, ip4addr_buf, sizeof(ip4addr_buf));
                        QLOGI("[datacall]ipv4 addr:%s", ip4addr_buf);
                    }
                    else if (info.ip_type == QOSA_PDP_IPV6)
                    {
                        qosa_memset(ip6addr_buf, 0, sizeof(ip6addr_buf));
                        qosa_ip_addr_inet_ntop(QOSA_IP_ADDR_AF_INET6, &info.ipv6_ip.addr.ipv6_addr, ip6addr_buf, sizeof(ip6addr_buf));
                        QLOGI("[datacall]ipv6 addr:%s", ip6addr_buf);
                    }
                    else
                    {
                        qosa_memset(ip4addr_buf, 0, sizeof(ip4addr_buf));
                        qosa_ip_addr_inet_ntop(QOSA_IP_ADDR_AF_INET, &info.ipv4_ip.addr.ipv4_addr, ip4addr_buf, sizeof(ip4addr_buf));
                        QLOGI("[datacall]ipv4 addr:%s", ip4addr_buf);
                        qosa_memset(ip6addr_buf, 0, sizeof(ip6addr_buf));
                        qosa_ip_addr_inet_ntop(QOSA_IP_ADDR_AF_INET6, &info.ipv6_ip.addr.ipv6_addr, ip6addr_buf, sizeof(ip6addr_buf));
                        QLOGI("[datacall]ipv6 addr:%s", ip6addr_buf);
                    }
                }
                else
                {
                    QLOGI("[datacall]datacall fail in nw deact pdn event");
                }
                qosa_free(datacall_task_msg.argv);
            }
            break;

            default:
                break;
        }
    }

exit:
    qosa_event_notify_unregister(QOSA_EVENT_NW_PDN_DEACT, datacall_nw_deact_pdp_cb);
    qosa_event_notify_unregister(QOSA_EVENT_NET_PDP_ACT, datacall_pdp_change_cb);

    qosa_msgq_delete(g_datacall_demo_msgq);
}

/**
 * @brief Initialize datacall dial-up demo program, create demo task
 *
 * This function is responsible for creating the datacall dial-up demo task.
 * After the task is created, it will run automatically and perform demo functions
 * such as datacall dial-up establishment, IP acquisition, network deactivation reconnection, etc.
 *
 * @note Task stack size is 4KB, priority is normal priority
 */
void unir_datacall_demo_init(void)
{
    int err = 0;
    // Create datacall dial-up demo task
    err = qosa_task_create(&g_datacall_demo_task, 4 * 1024, QOSA_PRIORITY_NORMAL, "QDATACALLDEMO", unir_datacall_demo_task, QOSA_NULL);
    if (err != QOSA_OK)
    {
        QLOGD("[datacall]datacall_demo task create error");
        return;
    }
}

UNIRTOS_APP_EXPORT(700, "unir_datacall_demo", unir_datacall_demo_init);