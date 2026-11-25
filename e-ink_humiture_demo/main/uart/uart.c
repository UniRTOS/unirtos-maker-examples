#include "qosa_sys.h"
#include "qosa_def.h"
#include "qosa_log.h"
#include "qosa_gpio.h"
#include "uart.h"
#include "humiture_demo.h"
#include "qosa_pinctrl.h"
#include "unirtos_app_init_registry.h"
#include <stdbool.h>
#include <string.h>

/*===========================================================================
 *  Macro Definition
 ===========================================================================*/

#define QOS_LOG_TAG LOG_TAG_UART_API
#define HUMITURE_LOGE(...) QLOGE("[e-ink_humiture_demo] " __VA_ARGS__)

/*===========================================================================
 *  Variate
 ===========================================================================*/
#define UART_RX_CHUNK_SIZE 128

static char g_uart_rx_line[HUMITURE_UART_TEXT_MAX_LEN + 1];
static size_t g_uart_rx_length;
static bool g_uart_discard_line;

static void unir_uart_process_rx(const unsigned char *data, size_t length)
{
    for (size_t i = 0; i < length; i++)
    {
        unsigned char value = data[i];
        if (value == '\r')
        {
            continue;
        }
        if (value == '\n')
        {
            if (!g_uart_discard_line && g_uart_rx_length > 0)
            {
                g_uart_rx_line[g_uart_rx_length] = '\0';
                humiture_demo_submit_uart_text(g_uart_rx_line);
            }
            g_uart_rx_length = 0;
            g_uart_discard_line = false;
            continue;
        }
        if (value < 0x20 || value > 0x7e)
        {
            g_uart_rx_length = 0;
            g_uart_discard_line = true;
            continue;
        }
        if (g_uart_discard_line)
        {
            continue;
        }
        if (g_uart_rx_length == HUMITURE_UART_TEXT_MAX_LEN)
        {
            g_uart_rx_length = 0;
            g_uart_discard_line = true;
            continue;
        }
        g_uart_rx_line[g_uart_rx_length++] = (char)value;
    }
}

static void unir_uart_ind(qosa_uart_cb_param_t *cb_param)
{
    if (cb_param == NULL)
    {
        return;
    }

    if (cb_param->event_id & QOSA_UART_EVENT_RX_INDICATE)
    {
        unsigned char rx_buf[UART_RX_CHUNK_SIZE];
        int read_len = qosa_uart_read(cb_param->port, rx_buf, sizeof(rx_buf));
        if (read_len > 0)
        {
            qosa_uart_write(cb_param->port, rx_buf, (unsigned int)read_len);
            unir_uart_process_rx(rx_buf, (size_t)read_len);
        }
        else if (read_len < 0)
        {
            HUMITURE_LOGE("UART read failed, error=%d", read_len);
        }
    }
}


static int uart_init()
{
    qosa_uart_status_monitor_t monitor = {0};
    monitor.callback = unir_uart_ind; /* Register callback function */
    monitor.event_mask = QOSA_UART_EVENT_RX_INDICATE;
    monitor.user_data = QOSA_NULL;

    /* Register UART event callback */
    if (qosa_uart_register_cb(UNIR_TEST_UART_PORT, &monitor) != QOSA_UART_SUCCESS)
    {
        HUMITURE_LOGE("UART callback registration failed");
        return -1;
    }

    /* Configure UART communication parameters: baud rate, data bits, stop bits, parity bit, flow control */
    qosa_uart_config_t dcb_config = {0};
    dcb_config.baudrate = QOSA_UART_BAUD_115200;
    dcb_config.data_bit = QOSA_UART_DATABIT_8;
    dcb_config.flow_ctrl = QOSA_FC_NONE;
    dcb_config.parity_bit = QOSA_UART_PARITY_NONE;
    dcb_config.stop_bit = QOSA_UART_STOP_1;

    qosa_uart_error_e ctl_ret = qosa_uart_ioctl(UNIR_TEST_UART_PORT, QOSA_UART_IOCTL_SET_DCB_CFG, (void *)&dcb_config);
    if(ctl_ret != QOSA_UART_SUCCESS)
    {
        HUMITURE_LOGE("UART configuration failed, error=%d", ctl_ret);
        return -1;
    }
    /* Configure UART port pin functions */

    pinctrl_option_t tx_option = {.func = UNIR_TEST_UART_PIN_FUNC, .pull = QOSA_PIN_PULL_UP, .driver = QOSA_PIN_DRIVE_LEVEL_0};
    pinctrl_option_t rx_option = {.func = UNIR_TEST_UART_PIN_FUNC, .pull = QOSA_PIN_PULL_UP, .driver = QOSA_PIN_DRIVE_LEVEL_0};
    if (qosa_pinctrl_set_option(UNIR_TEST_UART_TX_PIN, tx_option) != QOSA_PINCTRL_SUCCESS ||
        qosa_pinctrl_set_option(UNIR_TEST_UART_RX_PIN, rx_option) != QOSA_PINCTRL_SUCCESS)
    {
        HUMITURE_LOGE("UART pin configuration failed");
        return -1;
    }
    /* Open UART port */
    if (qosa_uart_open(UNIR_TEST_UART_PORT) != QOSA_UART_SUCCESS)
    {
        HUMITURE_LOGE("UART port open failed");
        return -1;
    }
    return 0;
}

static void unir_uart_demo_init(void)
{
    if (uart_init() != 0)
    {
        HUMITURE_LOGE("UART demo initialization failed");
    }
}

UNIRTOS_APP_EXPORT(710, "unir_uart_demo", unir_uart_demo_init);