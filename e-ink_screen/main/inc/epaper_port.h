#ifndef EPAPER_PORT_H
#define EPAPER_PORT_H

#include "qosa_gpio.h"

int epaper_port_init(void);
int epaper_port_write(int data_mode, const qosa_uint8_t *buffer, unsigned int length);
int epaper_port_reset(qosa_gpio_level_e level);
int epaper_port_busy(qosa_gpio_level_e *level);
void epaper_port_close(int failed);

#endif