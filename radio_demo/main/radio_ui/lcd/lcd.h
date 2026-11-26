#ifndef LCD_H
#define LCD_H

#include <stdbool.h>
#include <stdint.h>

/* Physical module pin numbers, used with qosa_pin_set_func(). */
#define LCD_MOSI_PIN 63
#define LCD_SCLK_PIN 49
#define LCD_CS_PIN   64
#define LCD_DC_PIN   67
#define LCD_RST_PIN  66
#define LCD_BL_PIN   62

#define LCD_MOSI_PIN_FUNC 1 /* SPI1_MOSI */
#define LCD_SCLK_PIN_FUNC 1 /* SPI1_SCLK */
#define LCD_CS_PIN_FUNC   0 /* GPIO12, chip select driven in software */
#define LCD_DC_PIN_FUNC   0 /* GPIO9 */
#define LCD_RST_PIN_FUNC  0 /* GPIO8 */
#define LCD_BL_PIN_FUNC   5 /* PWM0 */

#define LCD_BL_PWM_SEL 0

/* Landscape, 320 x 240 (see LCD_MADCTL). */
#define LCD_WIDTH  320
#define LCD_HEIGHT 240

/*
 * The EG800ZCN_LA base firmware (SDK v1.0.5) does not export the qosa_lcd API
 * (qosa_lcd_ioctl returns 0x801f0001 "NOT SUPPORT"), so the ST7789 is driven
 * directly: SPI1 carries the bytes, CS / DC / RST are plain GPIOs.
 */
#define LCD_SPI_PORT           QOSA_SPI_PORT1
#define LCD_SPI_CLK            QOSA_SPI_CLK_6_5MHZ
#define LCD_RESET_LOW_MS       20
#define LCD_RESET_RECOVERY_MS  120
#define LCD_MADCTL             0x60 /* MV|MX: landscape 320x240; 0x00 = portrait (then swap LCD_WIDTH/HEIGHT) */

/* 26 MHz / (25 + 1) = 1 MHz counter, 1000 counts = 1 kHz backlight PWM. */
#define LCD_BL_PWM_CLK_SRC     QOSA_FCLK_SEL_26M
#define LCD_BL_PWM_PSC         25
#define LCD_BL_PWM_PERIOD      1000

/* Pixel bytes sent per qosa_spi_write() call (two screen rows). */
#define LCD_TX_BUF_SIZE        (LCD_WIDTH * 2 * 2)

/* Initialise pins, backlight PWM and the ST7789. Display and backlight stay off until
 * lcd_set_backlight() with a non-zero value, so draw the first frame before that. Safe to call twice. */
bool lcd_init(void);

/* 0 = off, 100 = full brightness. */
void lcd_set_backlight(uint8_t percent);

/* Copy w * h RGB565 pixels (row by row) to the screen at (x, y). */
bool lcd_draw(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *pixels);

#endif /* LCD_H */