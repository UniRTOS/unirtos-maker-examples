#ifndef UART_H
#define UART_H

#include "qosa_uart.h"

#ifndef UNIR_TEST_UART_PORT
#define UNIR_TEST_UART_PORT QOSA_UART_PORT_1
#endif
#ifndef UNIR_TEST_UART_TX_PIN
#define UNIR_TEST_UART_TX_PIN 18
#endif
#ifndef UNIR_TEST_UART_RX_PIN
#define UNIR_TEST_UART_RX_PIN 17
#endif
#ifndef UNIR_TEST_UART_PIN_FUNC
#define UNIR_TEST_UART_PIN_FUNC 1
#endif

#endif // UART_H