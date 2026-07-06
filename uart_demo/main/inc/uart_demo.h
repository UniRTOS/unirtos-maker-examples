/*****************************************************************/ /**
* @file uart_demo.h
* @brief
* @author bronson.zhan@quectel.com
* @date 2025-04-23
*
* @copyright Copyright (c) 2023 Quectel Wireless Solution, Co., Ltd.
* All Rights Reserved. Quectel Wireless Solution Proprietary and Confidential.
*
* @par EDIT HISTORY FOR MODULE
* <table>
* <tr><th>Date <th>Version <th>Author <th>Description"
* <tr><td>2025-04-23 <td>1.0 <td>Bronson.Zhan <td> Init
* </table>
**********************************************************************/
#ifndef __UART_DEMO_H__
#define __UART_DEMO_H__

#include "qosa_def.h"
#include "qosa_sys.h"


/*===========================================================================
 *  Macro Definition
 ===========================================================================*/

#define CONFIG_UNIRTOS_UART_DEMO_TASK_STACK_SIZE 4096
#define UNIR_UART_DEMO_TASK_PRIO                 QOSA_PRIORITY_NORMAL
#define UNIR_TEST_UART_PORT                      QOSA_UART_PORT_1
#define UNIR_TEST_UART_TX_PIN                    UNIR_UART1_TX_PIN
#define UNIR_TEST_UART_RX_PIN                    UNIR_UART1_RX_PIN
#define UNIR_TEST_UART_PIN_FUNC                  UNIR_UART1_PIN_FUNC
/* Pin definitions need to be configured according to the PINMUX table of the actual platform, as there are differences in definitions among different platforms
  The pin definitions need to be configured according to the PINMUX table of the actual platform.
  There are differences in the definitions for each platform.*/

/*These config only for EG800Z platform*/
#define UNIR_UART0_TX_PIN                        (39)
#define UNIR_UART0_RX_PIN                        (38)
#define UNIR_UART0_PIN_FUNC                      (1)

#define UNIR_UART1_TX_PIN                        (18)
#define UNIR_UART1_RX_PIN                        (17)
#define UNIR_UART1_PIN_FUNC                      (1)

#define UNIR_UART2_TX_PIN                        (29)
#define UNIR_UART2_RX_PIN                        (28)
#define UNIR_UART2_PIN_FUNC                      (3)


void unir_uart_demo_init(void);

#endif /* __UART_DEMO_H__ */
