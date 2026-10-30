#include "qosa_def.h"
#include "qosa_lcd.h"
#include "qosa_log.h"
#include "qosa_spi.h"
#include "qosa_sys.h"

#define ST7796S_WIDTH                 480
#define ST7796S_HEIGHT                320
#define ST7796S_LSPI_INIT_FREQ        QOSA_SPI_CLK_3_25MHZ
#define ST7796S_RESET_LOW_TIME_MS     100
#define ST7796S_RESET_RECOVERY_MS     50
#define ST7796S_MADCTL_LANDSCAPE_RGB  0x20U

static void unir_lcd_drv_st7796s_init(qosa_lcd_channel_e lcd_no);
static void unir_lcd_drv_st7796s_deinit(qosa_lcd_channel_e lcd_no);
static void unir_lcd_drv_st7796s_enter_sleep(qosa_lcd_channel_e lcd_no, qosa_bool_t is_sleep);
static void unir_lcd_drv_st7796s_set_window(qosa_lcd_channel_e lcd_no, qosa_uint16_t left, qosa_uint16_t top, qosa_uint16_t right, qosa_uint16_t bottom);
static qosa_uint32_t unir_lcd_drv_st7796s_read_id(qosa_lcd_channel_e lcd_no);

static qosa_lcd_operations_t g_unir_lcd_drv_st7796s_operations = {
    .init = unir_lcd_drv_st7796s_init,
    .deInit = unir_lcd_drv_st7796s_deinit,
    .enterSleep = unir_lcd_drv_st7796s_enter_sleep,
    .setDisplayWindow = unir_lcd_drv_st7796s_set_window,
    .readId = unir_lcd_drv_st7796s_read_id,
};

qosa_lcd_drv_cfg_t g_unir_lcd_drv_st7796s_info = {
    .lcd_channel = QOSA_LCD_SPECIAL_PORT,
    .lcd_name = (qosa_uint8_t *)"ST7796S",
    .lcd_device_id = 0x7796,
    .lcd_width = ST7796S_WIDTH,
    .lcd_height = ST7796S_HEIGHT,
    .bus_mode = QOSA_LCD_BUS_MODE_4_WIRE_I_SPI,
    .lcd_input_format = QOSA_LCD_INPUT_FORMAT_RGB565,
    .lcd_output_format = QOSA_LCD_OUTPUT_FORMAT_RGB565,
    .operation = &g_unir_lcd_drv_st7796s_operations,
    .lcd_write_frequence = ST7796S_LSPI_INIT_FREQ,
    .lcd_read_frequence = ST7796S_LSPI_INIT_FREQ,
    .lcd_reset_func = {QOSA_GPIO_15, ST7796S_RESET_LOW_TIME_MS},
    .lcd_spi_func = {QOSA_SPI_CLK_CPOL0_CPHA0,
                     QOSA_SPI_MSB_FIRST,
                     QOSA_SPI_TRANSMIT_DMA,
                     QOSA_SPI_NSS_MASTER_SOFTWARE,
                     QOSA_GPIO_MAX,
                     QOSA_GPIO_MAX},
};

static void unir_lcd_write(qosa_lcd_channel_e lcd_no, qosa_uint8_t command, const qosa_uint8_t *data, qosa_uint32_t length)
{
    qosa_uint32_t i;
    qosa_lcd_write_cmd(lcd_no, command);
    for (i = 0; i < length; ++i) {
        qosa_lcd_write_cmd_data(lcd_no, data[i]);
    }
    qosa_lcd_flush_cmd(lcd_no);
}

static void unir_lcd_drv_st7796s_init(qosa_lcd_channel_e lcd_no)
{
    static const qosa_uint8_t f0_c3[] = {0xC3};
    static const qosa_uint8_t f0_96[] = {0x96};
    static const qosa_uint8_t madctl[] = {ST7796S_MADCTL_LANDSCAPE_RGB};
    static const qosa_uint8_t pixel_format[] = {0x05};
    static const qosa_uint8_t b0[] = {0x80};
    static const qosa_uint8_t b6[] = {0x00, 0x02};
    static const qosa_uint8_t b5[] = {0x02, 0x03, 0x00, 0x04};
    static const qosa_uint8_t b1[] = {0x80, 0x10};
    static const qosa_uint8_t b4[] = {0x00};
    static const qosa_uint8_t b7[] = {0xC6};
    static const qosa_uint8_t c5[] = {0x1C};
    static const qosa_uint8_t e4[] = {0x31};
    static const qosa_uint8_t e8[] = {0x40, 0x8A, 0x00, 0x00, 0x29, 0x19, 0xA5, 0x33};
    static const qosa_uint8_t e0[] = {0xF0, 0x09, 0x13, 0x12, 0x12, 0x2B, 0x3C, 0x44, 0x4B, 0x1B, 0x18, 0x17, 0x1D, 0x21};
    static const qosa_uint8_t e1[] = {0xF0, 0x09, 0x13, 0x0C, 0x0D, 0x27, 0x3B, 0x44, 0x4D, 0x0B, 0x17, 0x17, 0x1D, 0x21};
    static const qosa_uint8_t f0_3c[] = {0x3C};
    static const qosa_uint8_t f0_69[] = {0x69};

    qosa_task_sleep_ms(ST7796S_RESET_RECOVERY_MS);
    unir_lcd_write(lcd_no, 0xF0, f0_c3, sizeof(f0_c3));
    unir_lcd_write(lcd_no, 0xF0, f0_96, sizeof(f0_96));
    unir_lcd_write(lcd_no, 0x36, madctl, sizeof(madctl));
    unir_lcd_write(lcd_no, 0x3A, pixel_format, sizeof(pixel_format));
    unir_lcd_write(lcd_no, 0xB0, b0, sizeof(b0));
    unir_lcd_write(lcd_no, 0xB6, b6, sizeof(b6));
    unir_lcd_write(lcd_no, 0xB5, b5, sizeof(b5));
    unir_lcd_write(lcd_no, 0xB1, b1, sizeof(b1));
    unir_lcd_write(lcd_no, 0xB4, b4, sizeof(b4));
    unir_lcd_write(lcd_no, 0xB7, b7, sizeof(b7));
    unir_lcd_write(lcd_no, 0xC5, c5, sizeof(c5));
    unir_lcd_write(lcd_no, 0xE4, e4, sizeof(e4));
    unir_lcd_write(lcd_no, 0xE8, e8, sizeof(e8));
    unir_lcd_write(lcd_no, 0xC2, QOSA_NULL, 0);
    unir_lcd_write(lcd_no, 0xA7, QOSA_NULL, 0);
    unir_lcd_write(lcd_no, 0xE0, e0, sizeof(e0));
    unir_lcd_write(lcd_no, 0xE1, e1, sizeof(e1));
    unir_lcd_write(lcd_no, 0xF0, f0_3c, sizeof(f0_3c));
    unir_lcd_write(lcd_no, 0xF0, f0_69, sizeof(f0_69));
    unir_lcd_write(lcd_no, 0x13, QOSA_NULL, 0);
    unir_lcd_write(lcd_no, 0x11, QOSA_NULL, 0);
    qosa_task_sleep_ms(120);
    unir_lcd_write(lcd_no, 0x29, QOSA_NULL, 0);
    qosa_task_sleep_ms(20);
    QOSA_LOG_I(LOG_TAG_LCD_API, "ST7796S initialized");
}

static void unir_lcd_drv_st7796s_deinit(qosa_lcd_channel_e lcd_no)
{
    (void)lcd_no;
}

static void unir_lcd_drv_st7796s_enter_sleep(qosa_lcd_channel_e lcd_no, qosa_bool_t is_sleep)
{
    if (is_sleep) {
        unir_lcd_write(lcd_no, 0x28, QOSA_NULL, 0);
        qosa_task_sleep_ms(20);
        unir_lcd_write(lcd_no, 0x10, QOSA_NULL, 0);
        qosa_task_sleep_ms(120);
    } else {
        unir_lcd_write(lcd_no, 0x11, QOSA_NULL, 0);
        qosa_task_sleep_ms(120);
        unir_lcd_write(lcd_no, 0x29, QOSA_NULL, 0);
    }
}

static void unir_lcd_drv_st7796s_set_window(qosa_lcd_channel_e lcd_no, qosa_uint16_t left, qosa_uint16_t top, qosa_uint16_t right, qosa_uint16_t bottom)
{
    qosa_uint8_t column[] = {(qosa_uint8_t)(left >> 8), (qosa_uint8_t)left, (qosa_uint8_t)(right >> 8), (qosa_uint8_t)right};
    qosa_uint8_t page[] = {(qosa_uint8_t)(top >> 8), (qosa_uint8_t)top, (qosa_uint8_t)(bottom >> 8), (qosa_uint8_t)bottom};
    unir_lcd_write(lcd_no, 0x2A, column, sizeof(column));
    unir_lcd_write(lcd_no, 0x2B, page, sizeof(page));
    qosa_lcd_write_cmd(lcd_no, 0x2C);
}

static qosa_uint32_t unir_lcd_drv_st7796s_read_id(qosa_lcd_channel_e lcd_no)
{
    qosa_uint8_t id[3] = {0};
    qosa_lcd_read_cmd_data(lcd_no, 0x04, id, sizeof(id), 1);
    return ((qosa_uint32_t)id[0] << 16) | ((qosa_uint32_t)id[1] << 8) | id[2];
}