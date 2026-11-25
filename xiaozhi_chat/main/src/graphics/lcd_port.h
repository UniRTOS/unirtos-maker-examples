#ifndef ROBOT_DISPLAY_LCD_PORT_H
#define ROBOT_DISPLAY_LCD_PORT_H

#include <stdint.h>

enum {
    ROBOT_LCD_DISABLED = -200,
    ROBOT_LCD_CONFIG_INVALID = -201,
    ROBOT_LCD_NOT_READY = -202
};

/* Pure validation: never initializes GPIO or SPI. */
int robot_lcd_check_config(const char **reason);
int robot_lcd_open(void);
int robot_lcd_close(void);
int robot_lcd_write_pixels(void *ctx, uint16_t x, uint16_t y,
                           uint16_t w, uint16_t h, const uint16_t *pixels);

#endif