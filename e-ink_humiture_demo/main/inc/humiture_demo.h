
#ifndef _HUMITURE_DEMO_H_
#define _HUMITURE_DEMO_H_

// aht20 humiture demo configuration

#define UNIR_AHT20_COLLECT_TASK_STACK_SIZE 4096
#define UNIR_EINK_DISPLAY_TASK_STACK_SIZE 4096

#define HUMITURE_DATA_COLLECT_INTERVAL_MS 3000
#define HUMITURE_DATA_COLLECT_WAIT_MS 200
#define HUMITURE_UART_TEXT_MAX_LEN 27

void humiture_demo_submit_uart_text(const char *text);

#endif
