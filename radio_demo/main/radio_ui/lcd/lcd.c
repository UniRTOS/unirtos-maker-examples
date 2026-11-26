#include <stdbool.h>
#include <stdint.h>

#include "qosa_def.h"
#include "qosa_sys.h"
#include "qosa_rtc.h"
#include "qosa_gpio.h"
#include "qosa_pinctrl.h"
#include "qosa_log.h"
#include "qosa_dev_eigen.h"
#include "qosa_spi.h"
#include "qosa_pwm.h"
#include "lcd.h"
#define QOS_LOG_TAG LOG_TAG_DEMO



static qosa_gpio_num_e s_gpio_cs;
static qosa_gpio_num_e s_gpio_dc;
static qosa_gpio_num_e s_gpio_rst;
static bool s_lcd_ready = false;
static bool s_bl_ready = false;
static uint8_t s_tx_buf[LCD_TX_BUF_SIZE];

static bool lcd_spi_send(const uint8_t *data, uint32_t len)
{
    if (qosa_spi_write(LCD_SPI_PORT, (void *)data, len) != QOSA_SPI_SUCCESS) {
        QLOGE("[lcd] LCD spi write %u bytes failed", (unsigned int)len);
        return false;
    }
    return true;
}

/* One command byte (DC low) followed by its parameter bytes (DC high), inside one CS frame. */
/**
 * @brief Send a command and its parameters to the ST7789.
 *
 * @param cmd The command byte.
 * @param data Pointer to the parameter bytes.
 * @param len Number of parameter bytes.
 * @return true if the command was sent successfully, false otherwise.
 */
static bool st7789_write(uint8_t cmd, const uint8_t *data, uint32_t len)
{
    bool ok;

    qosa_gpio_set_level(s_gpio_cs, QOSA_GPIO_LEVEL_LOW);
    qosa_gpio_set_level(s_gpio_dc, QOSA_GPIO_LEVEL_LOW);
    ok = lcd_spi_send(&cmd, 1);
    qosa_gpio_set_level(s_gpio_dc, QOSA_GPIO_LEVEL_HIGH);
    if (ok && len > 0) {
        ok = lcd_spi_send(data, len);
    }
    qosa_gpio_set_level(s_gpio_cs, QOSA_GPIO_LEVEL_HIGH);
    return ok;
}

/* Sets the drawing window and leaves CS low / DC high, ready for pixel bytes after 0x2C. */
static bool st7789_begin_pixels(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    
    uint8_t col[] = {(uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(x1 >> 8), (uint8_t)x1};
    uint8_t row[] = {(uint8_t)(y0 >> 8), (uint8_t)y0, (uint8_t)(y1 >> 8), (uint8_t)y1};
    uint8_t ramwr = 0x2C;

    if (!st7789_write(0x2A, col, sizeof(col)) || !st7789_write(0x2B, row, sizeof(row))) {
        return false;
    }
    qosa_gpio_set_level(s_gpio_cs, QOSA_GPIO_LEVEL_LOW);
    qosa_gpio_set_level(s_gpio_dc, QOSA_GPIO_LEVEL_LOW);
    if (!lcd_spi_send(&ramwr, 1)) {
        qosa_gpio_set_level(s_gpio_cs, QOSA_GPIO_LEVEL_HIGH);
        return false;
    }
    qosa_gpio_set_level(s_gpio_dc, QOSA_GPIO_LEVEL_HIGH);
    return true;
}

static bool s_display_on;

static void st7789_end_pixels(void)
{
    qosa_gpio_set_level(s_gpio_cs, QOSA_GPIO_LEVEL_HIGH);
}

/* Init sequence from the Waveshare 2.8" Arduino example (LCD_Driver.cpp). */
static bool st7789_init(void)
{
    static const uint8_t madctl[] = {LCD_MADCTL};
    static const uint8_t colmod[] = {0x05};
    static const uint8_t b2[] = {0x0B, 0x0B, 0x00, 0x33, 0x35};
    static const uint8_t b7[] = {0x11};
    static const uint8_t bb[] = {0x35};
    static const uint8_t c0[] = {0x2C};
    static const uint8_t c2[] = {0x01};
    static const uint8_t c3[] = {0x0D};
    static const uint8_t c4[] = {0x20};
    static const uint8_t c6[] = {0x13};
    static const uint8_t d0[] = {0xA4, 0xA1};
    static const uint8_t d6[] = {0xA1};
    static const uint8_t e0[] = {0xF0, 0x06, 0x0B, 0x0A, 0x09, 0x26, 0x29,
                                 0x33, 0x41, 0x18, 0x16, 0x15, 0x29, 0x2D};
    static const uint8_t e1[] = {0xF0, 0x04, 0x08, 0x08, 0x07, 0x03, 0x28,
                                 0x32, 0x40, 0x3B, 0x19, 0x18, 0x2A, 0x2E};
    bool ok = true;

    qosa_task_sleep_ms(100); /* Give some time before resetting the LCD */
    qosa_gpio_set_level(s_gpio_rst, QOSA_GPIO_LEVEL_LOW);
    qosa_task_sleep_ms(LCD_RESET_LOW_MS);
    qosa_gpio_set_level(s_gpio_rst, QOSA_GPIO_LEVEL_HIGH);
    qosa_task_sleep_ms(LCD_RESET_RECOVERY_MS);

    ok = ok && st7789_write(0x36, madctl, sizeof(madctl));
    ok = ok && st7789_write(0x3A, colmod, sizeof(colmod));
    ok = ok && st7789_write(0xB2, b2, sizeof(b2));
    ok = ok && st7789_write(0xB7, b7, sizeof(b7));
    ok = ok && st7789_write(0xBB, bb, sizeof(bb));
    ok = ok && st7789_write(0xC0, c0, sizeof(c0));
    ok = ok && st7789_write(0xC2, c2, sizeof(c2));
    ok = ok && st7789_write(0xC3, c3, sizeof(c3));
    ok = ok && st7789_write(0xC4, c4, sizeof(c4));
    ok = ok && st7789_write(0xC6, c6, sizeof(c6));
    ok = ok && st7789_write(0xD0, d0, sizeof(d0));
    ok = ok && st7789_write(0xD6, d6, sizeof(d6));
    ok = ok && st7789_write(0xE0, e0, sizeof(e0));
    ok = ok && st7789_write(0xE1, e1, sizeof(e1));
    ok = ok && st7789_write(0x21, QOSA_NULL, 0); /* display inversion on */
    ok = ok && st7789_write(0x11, QOSA_NULL, 0); /* sleep out */
    if (!ok) {
        return false;
    }
    /* Display on (0x29) is deferred to lcd_set_backlight() so random GRAM is never shown. */
    qosa_task_sleep_ms(120);
    return true;
}

static bool lcd_set_pin_func(uint8_t pin, uint8_t func)
{
    if (qosa_pin_set_func(pin, func) != QOSA_PINCTRL_SUCCESS) {
        QLOGE("[lcd] LCD pin %d func %d set failed", (int)pin, (int)func);
        return false;
    }
    return true;
}

static bool lcd_output_gpio(uint8_t pin, qosa_gpio_num_e *gpio, const char *name)
{
    qosa_pin_cfg_t pin_cfg;

    if (qosa_get_pin_default_cfg((qosa_uint8_t)pin, &pin_cfg) != QOSA_GPIO_SUCCESS) {
        QLOGE("[lcd] LCD %s pin %d lookup failed", name, (int)pin);
        return false;
    }
    *gpio = (qosa_gpio_num_e)pin_cfg.gpio_num;
    if (qosa_gpio_init(*gpio, QOSA_GPIO_DIRECTION_OUTPUT, QOSA_GPIO_PULL_NONE,
                       QOSA_GPIO_LEVEL_HIGH) != QOSA_GPIO_SUCCESS) {
        QLOGE("[lcd] LCD %s gpio %d init failed", name, (int)*gpio);
        return false;
    }
    return true;
}

/**
 * @brief  configure spi
 * 
 * @return true 
 * @return false 
 */
static bool lcd_spi_init(void)
{
    qosa_spi_frame_format_e mode = QOSA_SPI_CLK_CPOL0_CPHA0;
    qosa_spi_nss_mode_e nss = QOSA_SPI_NSS_MASTER_SOFTWARE;
    int ret;

    ret = qosa_spi_init(LCD_SPI_PORT, QOSA_SPI_TRANSMIT_POLLING, LCD_SPI_CLK);
    if (ret != QOSA_SPI_SUCCESS) {
        QLOGE("[lcd] LCD spi init failed: 0x%x", (unsigned int)ret);
        return false;
    }
    ret = qosa_spi_ioctl(LCD_SPI_PORT, QOSA_SPI_IOCTL_SET_CLK_POLARITY_PHASE, &mode);
    if (ret != QOSA_SPI_SUCCESS) {
        QLOGE("[lcd] LCD spi set mode failed: 0x%x", (unsigned int)ret);
        return false;
    }
    ret = qosa_spi_ioctl(LCD_SPI_PORT, QOSA_SPI_IOCTL_SET_NSS_MODE, &nss);
    if (ret != QOSA_SPI_SUCCESS) {
        QLOGE("[lcd] LCD spi set nss failed: 0x%x", (unsigned int)ret);
        return false;
    }
    return true;
}

static bool lcd_backlight_init(void)
{
    qosa_pwm_info_t info = {0};

    info.high_one_cycle_duration = 0;
    info.total_one_cycle_duration = LCD_BL_PWM_PERIOD;
    info.pwm_psc = LCD_BL_PWM_PSC;
    info.clk_src = LCD_BL_PWM_CLK_SRC;
    if (qosa_pwm_config(LCD_BL_PWM_SEL, &info) != QOSA_PWM_SUCESS) {
        QLOGE("[lcd] LCD backlight PWM%d config failed", LCD_BL_PWM_SEL);
        return false;
    }
    (void)qosa_pwm_disable(LCD_BL_PWM_SEL);
    return true;
}

bool lcd_init(void)
{
    if (s_lcd_ready) {
        return true;
    }

    /* The ST7789 module uses 3.3 V logic; this switches the module's GPIO power domain. */
    if (qosa_gpio_set_voltage(VOL_3_30V) != QOSA_GPIO_SUCCESS) {
        QLOGW("[lcd] LCD: set GPIO voltage 3.3V failed");
    }

    //spi configuration
    if (!lcd_set_pin_func(LCD_SCLK_PIN, LCD_SCLK_PIN_FUNC) ||
        !lcd_set_pin_func(LCD_MOSI_PIN, LCD_MOSI_PIN_FUNC) ||
        !lcd_set_pin_func(LCD_CS_PIN, LCD_CS_PIN_FUNC) ||
        !lcd_set_pin_func(LCD_DC_PIN, LCD_DC_PIN_FUNC) ||
        !lcd_set_pin_func(LCD_RST_PIN, LCD_RST_PIN_FUNC) ||
        !lcd_set_pin_func(LCD_BL_PIN, LCD_BL_PIN_FUNC)) {
        return false;
    }

    //gpio configuration for control pins (CS, DC, RST)
    if (!lcd_output_gpio(LCD_CS_PIN, &s_gpio_cs, "CS") ||
        !lcd_output_gpio(LCD_DC_PIN, &s_gpio_dc, "DC") ||
        !lcd_output_gpio(LCD_RST_PIN, &s_gpio_rst, "RST")) {
        return false;
    }
    QLOGI("[lcd] LCD gpio: cs=%d dc=%d rst=%d", (int)s_gpio_cs, (int)s_gpio_dc, (int)s_gpio_rst);

    //PWM configuration for backlight
    s_bl_ready = lcd_backlight_init();

    if (!lcd_spi_init()) {
        return false;
    }
    if (!st7789_init()) {
        QLOGE("[lcd] LCD ST7789 init sequence failed");
        return false;
    }

    s_lcd_ready = true;
    QLOGI("[lcd] LCD ready: ST7789 %dx%d on SPI1", LCD_WIDTH, LCD_HEIGHT);
    return true;
}

void lcd_set_backlight(uint8_t percent)
{
    if (percent > 0 && s_lcd_ready && !s_display_on) {
        s_display_on = st7789_write(0x29, QOSA_NULL, 0);
        if (!s_display_on) {
            QLOGE("[lcd] LCD display on failed");
        }
    }
    if (!s_bl_ready) {
        return;
    }
    if (percent > 100) {
        percent = 100;
    }
    if (percent == 0) {
        (void)qosa_pwm_disable(LCD_BL_PWM_SEL);
    } else {
        (void)qosa_pwm_enable(LCD_BL_PWM_SEL, (qosa_uint32_t)percent * LCD_BL_PWM_PERIOD / 100U);
    }
}

bool lcd_draw(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *pixels)
{
    uint32_t total;
    uint32_t done = 0;
    bool ok = true;

    if (!s_lcd_ready || pixels == QOSA_NULL || w == 0 || h == 0 ||
        x + w > LCD_WIDTH || y + h > LCD_HEIGHT) {
        return false;
    }
    if (!st7789_begin_pixels(x, y, x + w - 1, y + h - 1)) {
        return false;
    }
    total = (uint32_t)w * h;
    while (ok && done < total) {
        uint32_t n = total - done;
        uint32_t i;

        if (n > LCD_TX_BUF_SIZE / 2) {
            n = LCD_TX_BUF_SIZE / 2;
        }
        /* The ST7789 expects RGB565 high byte first. */
        for (i = 0; i < n; i++) {
            s_tx_buf[2 * i] = (uint8_t)(pixels[done + i] >> 8);
            s_tx_buf[2 * i + 1] = (uint8_t)pixels[done + i];
        }
        ok = lcd_spi_send(s_tx_buf, n * 2);
        done += n;
    }
    st7789_end_pixels();
    return ok;
}
