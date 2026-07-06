/*****************************************************************/ /**
* @file test_demo.c
* @brief
* The demo demonstrates the use of UART interface, including UART port initialization, event callback registration, data transmission 
* and reception, and communication parameter configuration.
* @author lysander.li@quectel.com
* @date 2026-03-30
*
**********************************************************************/
#include "qosa_sys.h"
#include "qosa_uart.h"
#include "qosa_def.h"
#include "qosa_log.h"
#include "qosa_gpio.h"
#include "uart_demo.h"
#include "qosa_pinctrl.h"
#include "unirtos_app_init_registry.h"

/*===========================================================================
 *  Macro Definition
 ===========================================================================*/

#define QOS_LOG_TAG LOG_TAG_UART_API

/*===========================================================================
 *  Variate
 ===========================================================================*/

static qosa_task_t  g_unir_uart_demo_task = QOSA_NULL;
static qosa_uint8_t g_uart_data[1024] = {0};


/*===========================================================================
 *  Static API Functions
 ===========================================================================*/
/**
 * @brief UART event callback processing function
 *
 * This function is used to handle various event indications of UART ports, including receive data, transmit completion, and transmit buffer low water level events.
 * When an event occurs, the event information will be sent out via UART.
 *
 * @param cb_param UART callback parameter structure pointer, contains port number, event ID, user data and other information
 */
static void unir_uart_callback(qosa_uart_cb_param_t *cb_param)
{
    qosa_uart_port_number_e port = cb_param->port;
    qosa_uint32_t           event_id = cb_param->event_id;
    char                    data[] = "UART event callback invoked!\r\n";
    // qosa_snprintf(data, sizeof(data), "port=%d, event_id=%d, user_data=%s", port, event_id, (unsigned char *)cb_param->user_data);
    // Process accordingly based on different UART event types
    if (event_id & QOSA_UART_EVENT_RX_INDICATE)
    {
        qosa_uart_write(port, (unsigned char *)data, sizeof(data));
    }
}

/**
 * @brief UART function processing function, used to configure and test various functions of UART interface.
 *
 * This function initializes UART port, registers callback functions, and tests UART data transmission and reception functions. 
 * It also configures UART communication parameters such as baud rate, data bits, stop bits, parity bit, and flow control.
 *
 */
static void unir_uart_demo_process(void *ctx)
{
    int ret = 0;

    qosa_uart_status_monitor_t monitor = {0};
    monitor.callback = unir_uart_callback; /* Register callback function */
    monitor.event_mask = QOSA_UART_EVENT_RX_INDICATE;
    monitor.user_data = "Hello, Uart!";

    /* Register UART event callback */
    qosa_uart_register_cb(UNIR_TEST_UART_PORT, &monitor);

    /* Configure UART communication parameters: baud rate, data bits, stop bits, parity bit, flow control */
    qosa_uart_config_t dcb_config = {0};
    dcb_config.baudrate = QOSA_UART_BAUD_115200;
    dcb_config.data_bit = QOSA_UART_DATABIT_8;
    dcb_config.flow_ctrl = QOSA_FC_NONE;
    dcb_config.parity_bit = QOSA_UART_PARITY_NONE;
    dcb_config.stop_bit = QOSA_UART_STOP_1;

    qosa_uart_ioctl(UNIR_TEST_UART_PORT, QOSA_UART_IOCTL_SET_DCB_CFG, (void *)&dcb_config);
    /* Configure UART port pin functions */
    qosa_pin_set_func(UNIR_TEST_UART_TX_PIN,UNIR_TEST_UART_PIN_FUNC);
    qosa_pin_set_func(UNIR_TEST_UART_RX_PIN,UNIR_TEST_UART_PIN_FUNC);
    /* Open UART port */
    qosa_uart_open(UNIR_TEST_UART_PORT);

    while (1)
    {
        qosa_task_sleep_sec(1);
        if (qosa_uart_read_available(UNIR_TEST_UART_PORT) > 0)
        {
            qosa_uart_read(UNIR_TEST_UART_PORT, (unsigned char *)&g_uart_data, 1024);
            QLOGI("[TEST Demo]]recv uart data %s", g_uart_data);
            ret = qosa_uart_write(UNIR_TEST_UART_PORT, (unsigned char *)&g_uart_data, 1024);
            QLOGI("[TEST Demo]qosa_uart_write ret = %d", ret);
            qosa_memset(g_uart_data, 0, sizeof(g_uart_data));
        }
    }
}

/*===========================================================================
 *  Public API Functions
 ===========================================================================*/
void unir_uart_demo_init(void)
{
    QLOGI("enter UniRTOS UART DEMO !!!");
    if (g_unir_uart_demo_task == QOSA_NULL)
    {
        qosa_task_create(
            &g_unir_uart_demo_task,
            CONFIG_UNIRTOS_UART_DEMO_TASK_STACK_SIZE,
            UNIR_UART_DEMO_TASK_PRIO,
            "uart_demo",
            unir_uart_demo_process,
            QOSA_NULL,
            1
        );
    }
}
UNIRTOS_APP_EXPORT(700, "unir_uart_demo", unir_uart_demo_init);