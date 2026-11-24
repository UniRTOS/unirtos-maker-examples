/*****************************************************************/ /**
 * @file lcd_gc9a01.c
 * @brief GC9A01 圆形 SPI 屏驱动（引脚与初始化命令表来自 small-screen-demo，已在实机验证）
 **********************************************************************/
#include "qosa_def.h"
#include "qosa_sys.h"
#include "qosa_log.h"
#include "qosa_spi.h"
#include "qosa_gpio.h"
#include "qosa_pinctrl.h"
#include "lcd_gc9a01.h"

#define QOS_LOG_TAG   LOG_TAG_DEMO

/* ------------- LCD 接线配置 ------------- */
#define LCD_SPI_PORT        (1)
#define LCD_SPI_CS_PIN      (64)
#define LCD_SPI_MOSI_PIN    (63)
#define LCD_SPI_MISO_PIN    (62)
#define LCD_SPI_CLK_PIN     (49)

#define LCD_RST_PIN         (22)
#define LCD_DC_PIN          (23)
#define LCD_BL_PIN          (19)

#define LCD_SPI_CS_FUNC     1
#define LCD_SPI_MOSI_FUNC   1
#define LCD_SPI_MISO_FUNC   1
#define LCD_SPI_CLK_FUNC    1

#define LCD_RST_FUNC        0
#define LCD_DC_FUNC         0
#define LCD_BL_FUNC         0

/* MADCTL(0x36): bit3=1 为 BGR 顺序，用于修正红蓝互换；方向位保持 0 */
#define LCD_ROTATION        0x08

typedef struct
{
    uint8_t        cmd;
    uint8_t        len;
    const uint8_t *param;
    uint16_t       delay_ms;
} lcd_cmd_t;

#define LCD_CMD(cmd)             { (uint8_t)(cmd), 0, QOSA_NULL, 0 }
#define LCD_CMD_DATA(cmd, ...)                                             \
    {                                                                      \
        (uint8_t)(cmd), (uint8_t)sizeof((const uint8_t[]){__VA_ARGS__}),   \
            (const uint8_t[]){__VA_ARGS__}, 0                              \
    }
#define LCD_CMD_D(cmd, delay_ms) { (uint8_t)(cmd), 0, QOSA_NULL, (uint16_t)(delay_ms) }

static const lcd_cmd_t gc9a01_init_cmds[] =
{
    LCD_CMD_DATA(0x36, LCD_ROTATION),
    LCD_CMD(0xEF),
    LCD_CMD_DATA(0xEB, 0x14),
    LCD_CMD(0xFE),
    LCD_CMD(0xEF),
    LCD_CMD_DATA(0xEB, 0x14),
    LCD_CMD_DATA(0x84, 0x40),
    LCD_CMD_DATA(0x85, 0xFF),
    LCD_CMD_DATA(0x86, 0xFF),
    LCD_CMD_DATA(0x87, 0xFF),
    LCD_CMD_DATA(0x88, 0x0A),
    LCD_CMD_DATA(0x89, 0x21),
    LCD_CMD_DATA(0x8A, 0x00),
    LCD_CMD_DATA(0x8B, 0x80),
    LCD_CMD_DATA(0x8C, 0x01),
    LCD_CMD_DATA(0x8D, 0x01),
    LCD_CMD_DATA(0x8E, 0xFF),
    LCD_CMD_DATA(0x8F, 0xFF),
    LCD_CMD_DATA(0xB6, 0x00, 0x20),
    LCD_CMD_DATA(0x3A, 0x05),
    LCD_CMD_DATA(0x90, 0x08, 0x08, 0x08, 0x08),
    LCD_CMD_DATA(0xBD, 0x06),
    LCD_CMD_DATA(0xBC, 0x00),
    LCD_CMD_DATA(0xFF, 0x60, 0x01, 0x04),
    LCD_CMD_DATA(0xC3, 0x13),
    LCD_CMD_DATA(0xC4, 0x13),
    LCD_CMD_DATA(0xC9, 0x22),
    LCD_CMD_DATA(0xBE, 0x11),
    LCD_CMD_DATA(0xE1, 0x10, 0x0E),
    LCD_CMD_DATA(0xDF, 0x21, 0x0C, 0x02),
    LCD_CMD_DATA(0xF0, 0x45, 0x09, 0x08, 0x08, 0x26, 0x2A),
    LCD_CMD_DATA(0xF1, 0x43, 0x70, 0x72, 0x36, 0x37, 0x6F),
    LCD_CMD_DATA(0xF2, 0x45, 0x09, 0x08, 0x08, 0x26, 0x2A),
    LCD_CMD_DATA(0xF3, 0x43, 0x70, 0x72, 0x36, 0x37, 0x6F),
    LCD_CMD_DATA(0xED, 0x1B, 0x0B),
    LCD_CMD_DATA(0xAE, 0x77),
    LCD_CMD_DATA(0xCD, 0x63),
    LCD_CMD_DATA(0x70, 0x07, 0x07, 0x04, 0x0E, 0x0F, 0x09, 0x07, 0x08, 0x03),
    LCD_CMD_DATA(0xE8, 0x34),
    LCD_CMD_DATA(0x62, 0x18, 0x0D, 0x71, 0xED, 0x70, 0x70, 0x18, 0x0F, 0x71, 0xEF, 0x70, 0x70),
    LCD_CMD_DATA(0x63, 0x18, 0x11, 0x71, 0xF1, 0x70, 0x70, 0x18, 0x13, 0x71, 0xF3, 0x70, 0x70),
    LCD_CMD_DATA(0x64, 0x28, 0x29, 0xF1, 0x01, 0xF1, 0x00, 0x07),
    LCD_CMD_DATA(0x66, 0x3C, 0x00, 0xCD, 0x67, 0x45, 0x45, 0x10, 0x00, 0x00, 0x00),
    LCD_CMD_DATA(0x67, 0x00, 0x3C, 0x00, 0x00, 0x00, 0x01, 0x54, 0x10, 0x32, 0x98),
    LCD_CMD_DATA(0x74, 0x10, 0x85, 0x80, 0x00, 0x00, 0x4E, 0x00),
    LCD_CMD_DATA(0x98, 0x3E, 0x07),
    LCD_CMD(0x35),
    LCD_CMD(0x21),
    LCD_CMD_D(0x11, 80),
};

static qosa_gpio_num_e g_lcd_dc_gpio  = QOSA_GPIO_MAX;
static qosa_gpio_num_e g_lcd_rst_gpio = QOSA_GPIO_MAX;
static qosa_gpio_num_e g_lcd_bl_gpio  = QOSA_GPIO_MAX;

/* 写一条 MIPI DCS 命令：DC=0 发命令，DC=1 发参数 */
static void lcd_write(uint8_t cmd, const uint8_t *data, int len)
{
    qosa_gpio_set_level(g_lcd_dc_gpio, 0);
    qosa_spi_write(LCD_SPI_PORT, &cmd, 1);

    if (data != QOSA_NULL && len > 0)
    {
        qosa_gpio_set_level(g_lcd_dc_gpio, 1);
        qosa_spi_write(LCD_SPI_PORT, (void *)data, len);
    }
}

static void lcd_set_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    uint8_t col[4] = {0x00, (uint8_t)x1, 0x00, (uint8_t)x2};
    uint8_t row[4] = {0x00, (uint8_t)y1, 0x00, (uint8_t)y2};

    lcd_write(0x2A, col, 4);
    lcd_write(0x2B, row, 4);
    lcd_write(0x2C, QOSA_NULL, 0);
}

void lcd_display(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint8_t *data, int len)
{
    lcd_set_window(x1, y1, x2, y2);

    qosa_gpio_set_level(g_lcd_dc_gpio, 1);
    qosa_spi_write(LCD_SPI_PORT, (void *)data, len);
}

static void lcd_clear(void)
{
    uint8_t chunk[512] = {0};
    int     total      = LCD_WIDTH * LCD_HEIGHT * 2;

    lcd_set_window(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1);
    qosa_gpio_set_level(g_lcd_dc_gpio, 1);

    for (int sent = 0; sent < total; sent += (int)sizeof(chunk))
    {
        int len = ((total - sent) < (int)sizeof(chunk)) ? (total - sent) : (int)sizeof(chunk);
        qosa_spi_write(LCD_SPI_PORT, chunk, len);
    }
}

static void lcd_send_cmd_list(const lcd_cmd_t *list, unsigned int count)
{
    for (unsigned int i = 0; i < count; i++)
    {
        lcd_write(list[i].cmd, list[i].param, list[i].len);

        if (list[i].delay_ms > 0)
        {
            qosa_task_sleep_ms(list[i].delay_ms);
        }
    }
}

static qosa_gpio_num_e lcd_gpio_output_init(int pin, int func, qosa_gpio_level_e level)
{
    qosa_pin_cfg_t    cfg = {0};
    qosa_gpio_error_e ret;

    qosa_get_pin_default_cfg(pin, &cfg);
    qosa_pin_set_func(cfg.pin_num, func);
    ret = qosa_gpio_init(cfg.gpio_num, QOSA_GPIO_DIRECTION_OUTPUT, QOSA_GPIO_PULL_NONE, level);

    QLOGI("[smart_thermos_cup] lcd gpio pin=%d -> pin_num=%d gpio_num=%d func=%d init ret=0x%x",
          pin, cfg.pin_num, cfg.gpio_num, func, (unsigned int)ret);

    return cfg.gpio_num;
}

void lcd_init(void)
{
    qosa_spi_nss_mode_e nss_mode = QOSA_SPI_NSS_MASTER_HARDWARE;
    int                 ret;

    QLOGI("[smart_thermos_cup] lcd init start, spi port=%d", LCD_SPI_PORT);

    qosa_pin_set_func(LCD_SPI_MOSI_PIN, LCD_SPI_MOSI_FUNC);
    qosa_pin_set_func(LCD_SPI_MISO_PIN, LCD_SPI_MISO_FUNC);
    qosa_pin_set_func(LCD_SPI_CLK_PIN, LCD_SPI_CLK_FUNC);
    qosa_pin_set_func(LCD_SPI_CS_PIN, LCD_SPI_CS_FUNC);

    ret = qosa_spi_init(LCD_SPI_PORT, QOSA_SPI_TRANSMIT_POLLING, QOSA_SPI_CLK_13MHZ);
    if (ret != QOSA_SPI_SUCCESS)
    {
        QLOGE("[smart_thermos_cup] lcd spi init failed, ret=0x%x", (unsigned int)ret);
        return;
    }
    QLOGI("[smart_thermos_cup] lcd spi init ok");

    ret = qosa_spi_ioctl(LCD_SPI_PORT, QOSA_SPI_IOCTL_SET_NSS_MODE, &nss_mode);
    QLOGI("[smart_thermos_cup] lcd spi set nss ret=0x%x", (unsigned int)ret);

    g_lcd_dc_gpio  = lcd_gpio_output_init(LCD_DC_PIN, LCD_DC_FUNC, QOSA_GPIO_LEVEL_LOW);
    g_lcd_rst_gpio = lcd_gpio_output_init(LCD_RST_PIN, LCD_RST_FUNC, QOSA_GPIO_LEVEL_LOW);
    g_lcd_bl_gpio  = lcd_gpio_output_init(LCD_BL_PIN, LCD_BL_FUNC, QOSA_GPIO_LEVEL_HIGH);

    qosa_gpio_set_level(g_lcd_rst_gpio, 1);
    qosa_task_sleep_ms(100);
    qosa_gpio_set_level(g_lcd_rst_gpio, 0);
    qosa_task_sleep_ms(100);
    qosa_gpio_set_level(g_lcd_rst_gpio, 1);
    qosa_task_sleep_ms(100);
    QLOGI("[smart_thermos_cup] lcd reset done");

    lcd_send_cmd_list(gc9a01_init_cmds, (unsigned int)(sizeof(gc9a01_init_cmds) / sizeof(gc9a01_init_cmds[0])));
    QLOGI("[smart_thermos_cup] lcd init cmds sent");

    lcd_clear();

    lcd_write(0x29, QOSA_NULL, 0);
    qosa_gpio_set_level(g_lcd_bl_gpio, 1);
    QLOGI("[smart_thermos_cup] lcd display on, backlight on");
}
