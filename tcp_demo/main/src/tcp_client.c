/*****************************************************************/ /**
* @file socket_block_demo.c
* @brief
* @author harry.li@quectel.com
* @date 2025-05-7
*
* @copyright Copyright (c) 2023 Quectel Wireless Solution, Co., Ltd.
* All Rights Reserved. Quectel Wireless Solution Proprietary and Confidential.
*
* @par EDIT HISTORY FOR MODULE
* <table>
* <tr><th>Date <th>Version <th>Author <th>Description
* <tr><td>2025-05-7 <td>1.0 <td>harry.li <td> Init
* </table>
**********************************************************************/
#include "qosa_def.h"
#include "qosa_sys.h"
#include "qosa_log.h"
#include "qosa_asyn_dns.h"

#include "qosa_datacall.h"
#include "unirtos_app_init_registry.h"

#define QOS_LOG_TAG                       LOG_TAG

#define SOCKET_BLOCK_DEMO_TASK_STACK_SIZE 4096
#define SOCKET_BLOCK_DEMO_TASK_PRIO       QOSA_PRIORITY_NORMAL

#define SOCKET_BLOCK_CONNECT_SERVER_ADDR  "101.37.104.185"
#define SOCKET_BLOCK_CONNECT_SERVER_PORT  43033
#define SOCKET_BLOCK_CONNECT_SERVER_NAME  "101.37.104.185"
#define SOCKET_BLOCK_BUFF_MAX_LEN         1024

#define SOCKET_BLOCK_DEMO_SIMID           0
#define SOCKET_BLOCK_DEMO_PDPID           1
#define SOCKET_BLOCK_DEMO_ACTIVE_TIMEOUT  30  // PDP activation timeout 30s

/**
 * @brief Check and activate PDP
 *
 * This function is used to check the data connection status of the specified SIM card and PDP context,
 * and perform synchronous activation if not activated.
 * Uses predefined SIMID and PDPID parameters to establish data connection.
 *
 * @return int Execution result
 * @retval 0  Successfully activated data connection or connection is already active
 * @retval -1 Failed to activate data connection
 */
static int socket_app_datacall_active(void)
{
    /* Define data connection related variables */
    qosa_datacall_conn_t    conn = 0;
    qosa_datacall_ip_info_t info = {0};
    qosa_datacall_errno_e   ret = 0;

    /* Create new data connection object */
    conn = qosa_datacall_conn_new(SOCKET_BLOCK_DEMO_SIMID, SOCKET_BLOCK_DEMO_PDPID, QOSA_DATACALL_CONN_TCPIP);

    /* Check data connection information to determine if PDP is activated */
    if (QOSA_DATACALL_ERR_NO_ACTIVE == qosa_datacall_get_ip_info(conn, &info))
    {
        QLOGI("[TCP DEMO]sim_id=%d,pdp_id=%d", SOCKET_BLOCK_DEMO_SIMID, SOCKET_BLOCK_DEMO_PDPID);

        /* If PDP is not activated, start synchronous activation process */
        ret = qosa_datacall_start(conn, SOCKET_BLOCK_DEMO_ACTIVE_TIMEOUT);
        if (QOSA_DATACALL_OK != ret)
        {
            QLOGE("datacall ret=%x", ret);
            return -1;
        }
    }
    return 0;
}

/**
 * @brief Get IP address corresponding to hostname through DNS resolution
 *
 * @param hostname Pointer to the hostname string to resolve
 * @param ip Pointer to character array for storing the resolved IP address
 * @param ip_len Length of ip buffer
 * @return 0 on success, -1 on failure
 */
static int socket_app_block_dns(char *hostname, char *ip, qosa_uint32_t ip_len)
{
    struct addrinfo     hints = {0};
    struct addrinfo    *result = QOSA_NULL;
    struct sockaddr_in *ipv4 = QOSA_NULL;
    int                 status = 0;

    QLOGI("hostname=%s", hostname);

    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    status = getaddrinfo(hostname, QOSA_NULL, &hints, &result);
    if (status != 0)
    {
        QLOGE("dns err=%d", status);
        return -1;
    }

    ipv4 = (struct sockaddr_in *)result->ai_addr;
    inet_ntop(AF_INET, &(ipv4->sin_addr), ip, ip_len);
    QLOGI("ip=%s", ip);
    freeaddrinfo(result);
    return 0;
}

/**
 * @brief Configure custom socket IO read function
 *
 * @param socket_fd Socket file descriptor
 * @param buf Starting address of buffer to receive data
 * @param len Buffer length
 * @return Returns actual read length on success, <=0 on error
 */
static int socket_app_block_read(int socket_fd, unsigned char *buf, qosa_size_t len)
{
    int ret = 0;

    ret = read(socket_fd, buf, len);
    QLOGI("ret=%d", ret);
    if (ret <= 0)
    {
        QLOGE("read err");
    }

    return ret;
}

/**
 * @brief Configure custom socket IO write function
 *
 * @param socket_fd Socket file descriptor
 * @param buf Starting address of data content to be written
 * @param len Data length
 * @return Returns actual write length on success, <=0 on error
 */
static int socket_app_block_write(int socket_fd, unsigned char *buf, qosa_size_t len)
{
    int ret = 0;

    ret = write(socket_fd, buf, len);
    QLOGI("ret=%d", ret);
    if (ret <= 0)
    {
        QLOGE("write err");
    }

    return ret;
}

/**
 * @brief Create socket and connect, using blocking method to connect to target host
 *
 * @param remote_ip Target host IP address in dotted decimal format
 * @param port Target host port
 * @return Returns connected socket descriptor on success, -1 on failure
 */
static int socket_app_block_create(const char *remote_ip, qosa_uint16_t port)
{
    int                socket_fd = -1;
    struct sockaddr_in server_addr = {0};

    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(remote_ip);
    server_addr.sin_port        = htons(port);

    socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (socket_fd == -1)
    {
        QLOGE("socket create error");
        return -1;
    }

    if (connect(socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        close(socket_fd);
        QLOGE("socket connect error");
        return -1;
    }

    return socket_fd;
}

/**
 * @brief Blocking socket application processing function for executing TCP client communication flow.
 *
 * This function first waits for a period of time for network registration to complete, then activates data connection (such as PDP),
 * then obtains server address through DNS resolution, creates blocking TCP socket and attempts to connect to server.
 * After successful connection, loops to send and receive data, and finally closes the socket.
 *
 * @param argv Unused parameter, reserved to comply with function pointer interface requirements.
 * @return No return value.
 */
static void socket_app_block_process(void *argv)
{
    QOSA_UNUSED(argv);
    int           socket_fd = -1;
    int           ret = 0;
    unsigned char buff[SOCKET_BLOCK_BUFF_MAX_LEN] = {0};
    char          remote_ip[INET_ADDRSTRLEN] = {0};  // Used to store remote server IP address

    // Wait for a period of time before starting demo, convenient for network registration and log capture
    qosa_task_sleep_sec(10);

    // Activate data connection (such as PDP), exit if failed
    if (socket_app_datacall_active() != 0)
    {
        QLOGE("[TCP DEMO]pdp error");
        return;
    }

    // Perform DNS resolution to get server address information
    ret = socket_app_block_dns(SOCKET_BLOCK_CONNECT_SERVER_NAME, remote_ip, INET_ADDRSTRLEN);
    if (ret != 0)
    {
        QLOGE("[TCP DEMO]dns error");
        return;
    }
    else
    {
        QLOGI("[TCP DEMO]dns_syn_getaddrinfo success");

        // Create socket and connect to server
        socket_fd = socket_app_block_create(remote_ip, SOCKET_BLOCK_CONNECT_SERVER_PORT);
        if (socket_fd == -1)
        {
            QLOGE("[TCP DEMO]tcp connect error");
            return;
        }
        QLOGI("[TCP DEMO]socket connect success!!");
    }

    QLOGI("[TCP DEMO]socket_fd=%d", socket_fd);

    // Send and receive data, execute up to 20 times
    while (1)
    {
        static int i = 0;
        i++;
        if (i > 20)
        {
            QLOGI("i = %d", i);
            break;
        }

        // Construct send buffer content
        qosa_memset(buff, i, SOCKET_BLOCK_BUFF_MAX_LEN);
        qosa_snprintf((char *)buff, SOCKET_BLOCK_BUFF_MAX_LEN, "%s,%d", "abcdefg:", i);

        // Send data
        ret = socket_app_block_write(socket_fd, buff, qosa_strlen((const char *)buff));
        if (ret <= 0)
        {
            QLOGE("[TCP DEMO]socket_write error");
            break;
        }
        qosa_task_sleep_sec(1);  // Wait for server response data
        // Receive server response data, can set up own TCP server for send/receive data testing
        qosa_memset(buff, 0, SOCKET_BLOCK_BUFF_MAX_LEN);
        ret = socket_app_block_read(socket_fd, buff, SOCKET_BLOCK_BUFF_MAX_LEN);
        if (ret <= 0)
        {
            QLOGE("[TCP DEMO]socket_read error");
            break;
        }

        QLOGI("[TCP DEMO]recv %d bytes: %s", ret, (char *)buff);
    }

    // Close socket connection
    close(socket_fd);
}

/**
 * @brief Verify socket connection using blocking socket
 */
void unir_tcp_demo_init(void)
{
    int         err = 0;
    qosa_task_t app_task = QOSA_NULL;

    err = qosa_task_create(&app_task, SOCKET_BLOCK_DEMO_TASK_STACK_SIZE, SOCKET_BLOCK_DEMO_TASK_PRIO, "app_block", socket_app_block_process, QOSA_NULL);
    if (err != QOSA_OK)
    {
        QLOGE("[TCP DEMO]app_task task create error");
        return;
    }
}
UNIRTOS_APP_EXPORT(700, "unir_tcp_demo", unir_tcp_demo_init);