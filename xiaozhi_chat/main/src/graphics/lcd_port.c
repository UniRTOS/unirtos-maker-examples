#include "board_config.h"
#include "lcd_port.h"
#include "qosa_def.h"

#if ROBOT_LCD_ENABLE && defined(CONFIG_QOSA_SPI_FUNC)
#define ROBOT_LCD_HAS_SPI 1
#include "qosa_gpio.h"
#include "qosa_spi.h"
#include "qosa_sys.h"
#include "qosa_log.h"
#define QOS_LOG_TAG LOG_TAG_DEMO
#else
#define ROBOT_LCD_HAS_SPI 0
#endif

int robot_lcd_check_config(const char **reason)
{
    const char *why = NULL;
    int ret = ROBOT_LCD_CONFIG_INVALID;
    size_t count = 0, i;
    const robot_lcd_init_step_t *steps;
    if (!ROBOT_LCD_ENABLE) {
        why = "ROBOT_LCD_ENABLE=0: display hardware is untouched";
        ret = ROBOT_LCD_DISABLED;
    } else if (!ROBOT_LCD_HAS_SPI) {
        why = "Enable CONFIG_QOSA_SPI_FUNC in SDK menuconfig first";
    } else if (!ROBOT_LCD_BOARD_VERIFIED || !ROBOT_LCD_RGB565_VERIFIED) {
        why = "Confirm board wiring, 4-wire SPI mode and RGB565 support first";
    } else if (ROBOT_LCD_SPI_PORT < 0 || ROBOT_LCD_SPI_PORT > 2 ||
               ROBOT_LCD_SPI_HZ == 0 || ROBOT_LCD_SPI_MODE < 0 || ROBOT_LCD_SPI_MODE > 3) {
        why = "Fill in supplier-confirmed SPI port, clock and CPOL/CPHA";
    } else if (ROBOT_LCD_CS_GPIO < 0 || ROBOT_LCD_DC_GPIO < 0 || ROBOT_LCD_RESET_GPIO < 0 ||
               ROBOT_LCD_CS_GPIO >= 128 || ROBOT_LCD_DC_GPIO >= 128 || ROBOT_LCD_RESET_GPIO >= 128 ||
               ROBOT_LCD_BACKLIGHT_GPIO < -1 || ROBOT_LCD_BACKLIGHT_GPIO >= 128 ||
               (ROBOT_LCD_BACKLIGHT_GPIO < 0 && !ROBOT_LCD_EXTERNAL_BACKLIGHT_VERIFIED)) {
        why = "Fill in valid control GPIOs and a verified backlight driver";
    } else if (ROBOT_LCD_CS_GPIO == ROBOT_LCD_DC_GPIO || ROBOT_LCD_CS_GPIO == ROBOT_LCD_RESET_GPIO ||
               ROBOT_LCD_DC_GPIO == ROBOT_LCD_RESET_GPIO ||
               (ROBOT_LCD_BACKLIGHT_GPIO >= 0 &&
                (ROBOT_LCD_BACKLIGHT_GPIO == ROBOT_LCD_CS_GPIO ||
                 ROBOT_LCD_BACKLIGHT_GPIO == ROBOT_LCD_DC_GPIO ||
                 ROBOT_LCD_BACKLIGHT_GPIO == ROBOT_LCD_RESET_GPIO))) {
        why = "Control GPIOs must be distinct";
    } else {
        steps = robot_lcd_vendor_init_steps(&count);
        if (steps == NULL || count == 0) {
            why = "Supplier initialization command table is empty";
        } else {
            ret = 0;
            for (i = 0; i < count; ++i) {
                if (steps[i].size > 255 || (steps[i].size != 0 && steps[i].data == NULL)) {
                    why = "Invalid supplier initialization command entry";
                    ret = ROBOT_LCD_CONFIG_INVALID;
                    break;
                }
            }
        }
    }
    if (reason != NULL) {
        *reason = why;
    }
    return ret;
}

#if ROBOT_LCD_HAS_SPI
static int gpio_open[4];
static int spi_open;
static int ready;
static uint8_t wire_buffer[ROBOT_LCD_WIDTH * ROBOT_LCD_STRIP_ROWS * 2U];
static const int control_pins[4] = {
    ROBOT_LCD_CS_GPIO, ROBOT_LCD_DC_GPIO, ROBOT_LCD_RESET_GPIO, ROBOT_LCD_BACKLIGHT_GPIO
};

static int level(int pin, int high)
{
    return (int)qosa_gpio_set_level((qosa_gpio_num_e)pin,
                                    high ? QOSA_GPIO_LEVEL_HIGH : QOSA_GPIO_LEVEL_LOW);
}

static int backlight(int on)
{
    if (!gpio_open[3]) {
        return 0;
    }
    return level(ROBOT_LCD_BACKLIGHT_GPIO, on ? ROBOT_LCD_BACKLIGHT_ACTIVE_HIGH :
                                              !ROBOT_LCD_BACKLIGHT_ACTIVE_HIGH);
}

static int command(uint8_t cmd, const uint8_t *data, size_t size)
{
    int ret, release_ret;
    ret = level(ROBOT_LCD_CS_GPIO, 0);
    if (ret == 0) ret = level(ROBOT_LCD_DC_GPIO, 0);
    if (ret == 0) ret = (int)qosa_spi_write((qosa_spi_port_e)ROBOT_LCD_SPI_PORT, &cmd, 1);
    if (ret == 0 && size != 0) {
        ret = level(ROBOT_LCD_DC_GPIO, 1);
        if (ret == 0) {
            ret = (int)qosa_spi_write((qosa_spi_port_e)ROBOT_LCD_SPI_PORT, (void *)data, (uint32_t)size);
        }
    }
    release_ret = level(ROBOT_LCD_CS_GPIO, 1);
    return ret != 0 ? ret : release_ret;
}

int robot_lcd_close(void)
{
    int first = 0, ret, i;
    ready = 0;
    ret = backlight(0);
    if (ret != 0) first = ret;
    if (gpio_open[0]) {
        ret = level(ROBOT_LCD_CS_GPIO, 1);
        if (first == 0) first = ret;
    }
    if (gpio_open[2]) {
        ret = level(ROBOT_LCD_RESET_GPIO, 0);
        if (first == 0) first = ret;
    }
    if (spi_open) {
        ret = (int)qosa_spi_deinit((qosa_spi_port_e)ROBOT_LCD_SPI_PORT);
        if (first == 0) first = ret;
        if (ret == 0) spi_open = 0;
    }
    for (i = 3; i >= 0; --i) {
        if (gpio_open[i]) {
            ret = (int)qosa_gpio_deinit((qosa_gpio_num_e)control_pins[i]);
            if (first == 0) first = ret;
            if (ret == 0) gpio_open[i] = 0;
        }
    }
    return first;
}

int robot_lcd_open(void)
{
    const char *reason;
    size_t count, j;
    const robot_lcd_init_step_t *steps;
    int ret = robot_lcd_check_config(&reason);
    int i;
    if (ret != 0) {
        QLOGE("display config blocked: %s", reason);
        return ret;
    }
    if (ready || spi_open || gpio_open[0] || gpio_open[1] || gpio_open[2] || gpio_open[3]) {
        return ROBOT_LCD_NOT_READY;
    }
    for (i = 0; i < 4; ++i) {
        int high = i == 0 || i == 1 || (i == 3 && !ROBOT_LCD_BACKLIGHT_ACTIVE_HIGH);
        if (control_pins[i] < 0) continue;
        ret = (int)qosa_gpio_init((qosa_gpio_num_e)control_pins[i], QOSA_GPIO_DIRECTION_OUTPUT,
                                  QOSA_GPIO_PULL_NONE, high ? QOSA_GPIO_LEVEL_HIGH : QOSA_GPIO_LEVEL_LOW);
        if (ret != 0) goto fail;
        gpio_open[i] = 1;
    }
    /* Polling writes intentionally consume the strip synchronously. */
    ret = (int)qosa_spi_init((qosa_spi_port_e)ROBOT_LCD_SPI_PORT, QOSA_SPI_TRANSMIT_POLLING,
                             (qosa_spi_clk_e)ROBOT_LCD_SPI_HZ);
    if (ret != 0) goto fail;
    spi_open = 1;
    {
        qosa_spi_nss_mode_e nss = QOSA_SPI_NSS_MASTER_SOFTWARE;
        qosa_spi_frame_format_e mode = (qosa_spi_frame_format_e)ROBOT_LCD_SPI_MODE;
        qosa_spi_data_width_e width = QOSA_SPI_WIDTH_1_BYTES;
        qosa_spi_bit_order_e order = QOSA_SPI_MSB_FIRST;
        ret = (int)qosa_spi_ioctl((qosa_spi_port_e)ROBOT_LCD_SPI_PORT, QOSA_SPI_IOCTL_SET_NSS_MODE, &nss);
        if (ret == 0) ret = (int)qosa_spi_ioctl((qosa_spi_port_e)ROBOT_LCD_SPI_PORT, QOSA_SPI_IOCTL_SET_CLK_POLARITY_PHASE, &mode);
        if (ret == 0) ret = (int)qosa_spi_ioctl((qosa_spi_port_e)ROBOT_LCD_SPI_PORT, QOSA_SPI_IOCTL_SET_DATA_WIDTH, &width);
        if (ret == 0) ret = (int)qosa_spi_ioctl((qosa_spi_port_e)ROBOT_LCD_SPI_PORT, QOSA_SPI_IOCTL_SET_BIT_ORDER, &order);
        if (ret != 0) goto fail;
    }
    qosa_task_sleep_ms(ROBOT_LCD_RESET_LOW_MS);
    ret = level(ROBOT_LCD_RESET_GPIO, 1);
    if (ret != 0) goto fail;
    qosa_task_sleep_ms(ROBOT_LCD_RESET_RECOVERY_MS);
    steps = robot_lcd_vendor_init_steps(&count);
    for (j = 0; j < count; ++j) {
        ret = command(steps[j].command, steps[j].data, steps[j].size);
        if (ret != 0) {
            QLOGE("panel init failed: step=%u cmd=0x%02x rc=%d", (unsigned)j, steps[j].command, ret);
            goto fail;
        }
        if (steps[j].delay_ms != 0) qosa_task_sleep_ms(steps[j].delay_ms);
    }
    ret = backlight(1);
    if (ret != 0) goto fail;
    ready = 1;
    return 0;
fail:
    QLOGE("panel open failed rc=%d", ret);
    {
        int cleanup_ret = robot_lcd_close();
        if (cleanup_ret != 0) QLOGE("panel cleanup failed rc=%d", cleanup_ret);
    }
    return ret;
}

int robot_lcd_write_pixels(void *ctx, uint16_t x, uint16_t y,
                           uint16_t w, uint16_t h, const uint16_t *pixels)
{
    uint16_t end_x, end_y;
    uint8_t bounds[4];
    size_t i, count = (size_t)w * h;
    int ret;
    (void)ctx;
    if (!ready) return ROBOT_LCD_NOT_READY;
    if (pixels == NULL || w == 0 || h == 0 ||
        (uint32_t)x + w > ROBOT_LCD_WIDTH || (uint32_t)y + h > ROBOT_LCD_HEIGHT ||
        count > sizeof(wire_buffer) / 2) return ROBOT_LCD_CONFIG_INVALID;
    end_x = (uint16_t)(x + w - 1);
    end_y = (uint16_t)(y + h - 1);
    bounds[0] = (uint8_t)(x >> 8); bounds[1] = (uint8_t)x;
    bounds[2] = (uint8_t)(end_x >> 8); bounds[3] = (uint8_t)end_x;
    ret = command(0x2A, bounds, sizeof(bounds));
    if (ret != 0) return ret;
    bounds[0] = (uint8_t)(y >> 8); bounds[1] = (uint8_t)y;
    bounds[2] = (uint8_t)(end_y >> 8); bounds[3] = (uint8_t)end_y;
    ret = command(0x2B, bounds, sizeof(bounds));
    if (ret != 0) return ret;
    for (i = 0; i < count; ++i) {
        wire_buffer[2 * i] = (uint8_t)(pixels[i] >> 8);
        wire_buffer[2 * i + 1] = (uint8_t)pixels[i];
    }
    return command(0x2C, wire_buffer, count * 2);
}
#else
int robot_lcd_open(void)
{
    return robot_lcd_check_config(NULL);
}
int robot_lcd_close(void)
{
    return 0;
}
int robot_lcd_write_pixels(void *ctx, uint16_t x, uint16_t y,
                           uint16_t w, uint16_t h, const uint16_t *pixels)
{
    (void)ctx; (void)x; (void)y; (void)w; (void)h; (void)pixels;
    return ROBOT_LCD_NOT_READY;
}
#endif