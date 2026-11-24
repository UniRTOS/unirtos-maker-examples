#ifndef ROBOT_DISPLAY_BOARD_CONFIG_H
#define ROBOT_DISPLAY_BOARD_CONFIG_H

#include <stddef.h>
#include <stdint.h>

/* Enable only after the actual board, wiring and supplier commands are verified.
 * GPIO values are qosa_gpio_num_e values, NOT FPC or module pin numbers.
 * IM straps, power rails and SPI SCLK/MOSI routing are configured in hardware/BSP.
 * This demo deliberately does not switch panel power, IM straps or touch pins. */
#ifndef ROBOT_LCD_ENABLE
#define ROBOT_LCD_ENABLE 0
#endif
#ifndef ROBOT_LCD_BOARD_VERIFIED
#define ROBOT_LCD_BOARD_VERIFIED 0
#endif
#ifndef ROBOT_LCD_RGB565_VERIFIED
#define ROBOT_LCD_RGB565_VERIFIED 0
#endif
#ifndef ROBOT_LCD_SPI_PORT
#define ROBOT_LCD_SPI_PORT (-1)
#endif
#ifndef ROBOT_LCD_SPI_HZ
#define ROBOT_LCD_SPI_HZ 0
#endif
#ifndef ROBOT_LCD_SPI_MODE
#define ROBOT_LCD_SPI_MODE (-1) /* qosa_spi_frame_format_e, supplier-confirmed */
#endif
#ifndef ROBOT_LCD_CS_GPIO
#define ROBOT_LCD_CS_GPIO (-1)
#endif
#ifndef ROBOT_LCD_DC_GPIO
#define ROBOT_LCD_DC_GPIO (-1)
#endif
#ifndef ROBOT_LCD_RESET_GPIO
#define ROBOT_LCD_RESET_GPIO (-1)
#endif
#ifndef ROBOT_LCD_BACKLIGHT_GPIO
#define ROBOT_LCD_BACKLIGHT_GPIO (-1) /* Driver EN only; NEVER LEDA/LEDK directly */
#endif
#ifndef ROBOT_LCD_BACKLIGHT_ACTIVE_HIGH
#define ROBOT_LCD_BACKLIGHT_ACTIVE_HIGH 1
#endif
#ifndef ROBOT_LCD_EXTERNAL_BACKLIGHT_VERIFIED
#define ROBOT_LCD_EXTERNAL_BACKLIGHT_VERIFIED 0
#endif

#define ROBOT_LCD_WIDTH 360U
#define ROBOT_LCD_HEIGHT 360U
#define ROBOT_LCD_STRIP_ROWS 8U
#define ROBOT_LCD_RESET_LOW_MS 20U
#define ROBOT_LCD_RESET_RECOVERY_MS 120U
#define ROBOT_LCD_COLOR_HOLD_MS 1000U
#define ROBOT_LCD_BLINK_STEP_MS 60U
#define ROBOT_LCD_BLINK_PAUSE_MS 2500U

typedef struct {
    uint8_t command;
    const uint8_t *data;
    size_t size;
    uint32_t delay_ms;
} robot_lcd_init_step_t;

const robot_lcd_init_step_t *robot_lcd_vendor_init_steps(size_t *count);

#endif