#include "smart_scale.h"
#include "qosa_gpio.h"
#include "qosa_pinctrl.h"
#include "qosa_spi.h"
#include "qosa_log.h"
#include <stdio.h>
#include <string.h>

#define QOS_LOG_TAG LOG_TAG_DEMO
#define LCD_WIDTH 240
#define LCD_HEIGHT 240
#define STRIPE_HEIGHT 16
#define LCD_CIRCLE_RADIUS 118
#define LCD_SCK_PIN 49
#define LCD_MOSI_PIN 63
#define LCD_MISO_PIN 62
#define LCD_CS_PIN 64
#define LCD_DC_PIN 23
#define LCD_RESET_PIN 20
#define LCD_BACKLIGHT_PIN 22
#define LCD_X_OFFSET 0
#define LCD_Y_OFFSET 0
#define LCD_MADCTL 0x48
#define LCD_INVERTED 1
#define LCD_BACKLIGHT_ACTIVE_HIGH 1
#if LCD_X_OFFSET < 0 || LCD_X_OFFSET + LCD_WIDTH > 240 || LCD_Y_OFFSET < 0 || LCD_Y_OFFSET + LCD_HEIGHT > 240
#error Invalid GC9A01 display offsets
#endif
#define COLOR_BG 0x0000
#define COLOR_FG 0xffff
#define COLOR_MUTED 0xad75
#define COLOR_ACCENT 0x07b4
#define COLOR_WARN 0xfd80

enum { LCD_CS, LCD_DC, LCD_RESET, LCD_BACKLIGHT, LCD_CONTROL_COUNT };
static qosa_pin_cfg_t g_control[LCD_CONTROL_COUNT];
static unsigned int g_control_ready;
static int g_spi_ready;
static int g_backlight_on;
static unsigned char g_bytes[LCD_WIDTH * STRIPE_HEIGHT * 2] __attribute__((aligned(4)));
static uint16_t g_pixels[LCD_WIDTH * STRIPE_HEIGHT];
static int g_stripe;
static int g_ready;
static int g_io_failed;
static int g_first_valid_frame_done;
static int g_trace_valid_frame;

static const unsigned char g_font[][5] = {
    {0x3e,0x51,0x49,0x45,0x3e},{0,0x42,0x7f,0x40,0},{0x42,0x61,0x51,0x49,0x46},
    {0x21,0x41,0x45,0x4b,0x31},{0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},
    {0x3c,0x4a,0x49,0x49,0x30},{1,0x71,9,5,3},{0x36,0x49,0x49,0x49,0x36},
    {6,0x49,0x49,0x29,0x1e},
    {0x7e,0x11,0x11,0x11,0x7e},{0x7f,0x49,0x49,0x49,0x36},{0x3e,0x41,0x41,0x41,0x22},
    {0x7f,0x41,0x41,0x22,0x1c},{0x7f,0x49,0x49,0x49,0x41},{0x7f,9,9,9,1},
    {0x3e,0x41,0x49,0x49,0x7a},{0x7f,8,8,8,0x7f},{0,0x41,0x7f,0x41,0},
    {0x20,0x40,0x41,0x3f,1},{0x7f,8,0x14,0x22,0x41},{0x7f,0x40,0x40,0x40,0x40},
    {0x7f,2,0x0c,2,0x7f},{0x7f,4,8,0x10,0x7f},{0x3e,0x41,0x41,0x41,0x3e},
    {0x7f,9,9,9,6},{0x3e,0x41,0x51,0x21,0x5e},{0x7f,9,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31},{1,1,0x7f,1,1},{0x3f,0x40,0x40,0x40,0x3f},
    {0x1f,0x20,0x40,0x20,0x1f},{0x3f,0x40,0x38,0x40,0x3f},{0x63,0x14,8,0x14,0x63},
    {7,8,0x70,8,7},{0x61,0x51,0x49,0x45,0x43}
};

static int lcd_level(unsigned int control, int high)
{
    if (qosa_gpio_set_level(g_control[control].gpio_num,
        high ? QOSA_GPIO_LEVEL_HIGH : QOSA_GPIO_LEVEL_LOW) != QOSA_GPIO_SUCCESS)
    {
        g_io_failed = 1;
        return 0;
    }
    return 1;
}

static int lcd_write(const unsigned char *data, unsigned int size)
{
    if (g_io_failed) return 0;
    if (size && qosa_spi_write(QOSA_SPI_PORT1, (void *)data, size) != QOSA_SPI_SUCCESS)
    {
        g_io_failed = 1;
        return 0;
    }
    return 1;
}

static int lcd_selected_command(unsigned char command, const unsigned char *data, unsigned int size)
{
    if (!lcd_level(LCD_DC, 0) || !lcd_write(&command, 1)) return 0;
    if (size && (!lcd_level(LCD_DC, 1) || !lcd_write(data, size))) return 0;
    return 1;
}

static void lcd_command(unsigned char command, const unsigned char *data, unsigned int size)
{
    if (!g_io_failed && lcd_level(LCD_CS, 0)) lcd_selected_command(command, data, size);
    lcd_level(LCD_CS, 1);
}

static void lcd_close(void)
{
    if (g_control_ready == LCD_CONTROL_COUNT)
        (void)qosa_gpio_set_level(g_control[LCD_BACKLIGHT].gpio_num,
            LCD_BACKLIGHT_ACTIVE_HIGH ? QOSA_GPIO_LEVEL_LOW : QOSA_GPIO_LEVEL_HIGH);
    if (g_control_ready)
        (void)qosa_gpio_set_level(g_control[LCD_CS].gpio_num, QOSA_GPIO_LEVEL_HIGH);
    if (g_spi_ready) (void)qosa_spi_deinit(QOSA_SPI_PORT1);
    while (g_control_ready) (void)qosa_gpio_deinit(g_control[--g_control_ready].gpio_num);
    g_spi_ready = g_ready = g_backlight_on = 0;
}

static int lcd_output(unsigned int index, unsigned int pad, int high)
{
    qosa_pin_cfg_t *pin = &g_control[index];
    if (qosa_get_pin_default_cfg(pad, pin) != QOSA_GPIO_SUCCESS ||
        qosa_pin_set_func((qosa_pin_num_e)pin->pin_num, pin->gpio_func) != QOSA_PINCTRL_SUCCESS ||
        qosa_gpio_init(pin->gpio_num, QOSA_GPIO_DIRECTION_OUTPUT, QOSA_GPIO_PULL_NONE,
            high ? QOSA_GPIO_LEVEL_HIGH : QOSA_GPIO_LEVEL_LOW) != QOSA_GPIO_SUCCESS) return 0;
    ++g_control_ready;
    QLOGI("scale LCD pad=%u gpio=%u", pad, (unsigned int)pin->gpio_num);
    return 1;
}

static void lcd_panel_init(void)
{
    static const struct {
        unsigned char command;
        unsigned char size;
        unsigned char data[12];
    } setup[] = {
        {0xef,0,{0}},{0xeb,1,{0x14}},{0xfe,0,{0}},{0xef,0,{0}},
        {0xeb,1,{0x14}},{0x84,1,{0x40}},{0x85,1,{0xff}},
        {0x86,1,{0xff}},{0x87,1,{0xff}},{0x88,1,{0x0a}},
        {0x89,1,{0x21}},{0x8a,1,{0x00}},{0x8b,1,{0x80}},
        {0x8c,1,{0x01}},{0x8d,1,{0x01}},{0x8e,1,{0xff}},
        {0x8f,1,{0xff}},{0xb6,2,{0x00,0x00}},{0x3a,1,{0x05}},
        {0x90,4,{0x08,0x08,0x08,0x08}},{0xbd,1,{0x06}},
        {0xbc,1,{0x00}},{0xff,3,{0x60,0x01,0x04}},
        {0xc3,1,{0x13}},{0xc4,1,{0x13}},{0xc9,1,{0x22}},
        {0xbe,1,{0x11}},{0xe1,2,{0x10,0x0e}},{0xdf,3,{0x21,0x0c,0x02}},
        {0xf0,6,{0x45,0x09,0x08,0x08,0x26,0x2a}},
        {0xf1,6,{0x43,0x70,0x72,0x36,0x37,0x6f}},
        {0xf2,6,{0x45,0x09,0x08,0x08,0x26,0x2a}},
        {0xf3,6,{0x43,0x70,0x72,0x36,0x37,0x6f}},
        {0xed,2,{0x1b,0x0b}},{0xae,1,{0x77}},{0xcd,1,{0x63}},
        {0xe8,1,{0x34}},
        {0x62,12,{0x18,0x0d,0x71,0xed,0x70,0x70,0x18,0x0f,0x71,0xef,0x70,0x70}},
        {0x63,12,{0x18,0x11,0x71,0xf1,0x70,0x70,0x18,0x13,0x71,0xf3,0x70,0x70}},
        {0x64,7,{0x28,0x29,0xf1,0x01,0xf1,0x00,0x07}},
        {0x66,10,{0x3c,0x00,0xcd,0x67,0x45,0x45,0x10,0x00,0x00,0x00}},
        {0x67,10,{0x00,0x3c,0x00,0x00,0x00,0x01,0x54,0x10,0x32,0x98}},
        {0x74,7,{0x10,0x85,0x80,0x00,0x00,0x4e,0x00}},{0x98,2,{0x3e,0x07}}
    };
    unsigned int index;
    lcd_level(LCD_RESET, 1);
    qosa_task_sleep_ms(10);
    lcd_level(LCD_RESET, 0);
    qosa_task_sleep_ms(20);
    lcd_level(LCD_RESET, 1);
    qosa_task_sleep_ms(120);
    lcd_command(0x01, 0, 0);
    qosa_task_sleep_ms(150);
    for (index = 0; index < sizeof(setup) / sizeof(setup[0]); ++index)
        lcd_command(setup[index].command, setup[index].data, setup[index].size);
    {
        unsigned char madctl = LCD_MADCTL;
        lcd_command(0x36, &madctl, 1);
    }
    lcd_command(LCD_INVERTED ? 0x21 : 0x20, 0, 0);
    lcd_command(0x11, 0, 0);
    qosa_task_sleep_ms(150);
    lcd_command(0x29, 0, 0);
    qosa_task_sleep_ms(150);
}

int scale_display_init(void)
{
    qosa_spi_nss_mode_e nss = QOSA_SPI_NSS_MASTER_SOFTWARE;
    qosa_spi_frame_format_e format = QOSA_SPI_CLK_CPOL0_CPHA0;
    qosa_spi_data_width_e width = QOSA_SPI_WIDTH_1_BYTES;
    qosa_spi_bit_order_e order = QOSA_SPI_MSB_FIRST;
    if (g_ready) return 1;
    g_io_failed = 0;
    if (!lcd_output(LCD_CS, LCD_CS_PIN, 1) || !lcd_output(LCD_DC, LCD_DC_PIN, 0) ||
        !lcd_output(LCD_RESET, LCD_RESET_PIN, 1) ||
        !lcd_output(LCD_BACKLIGHT, LCD_BACKLIGHT_PIN, !LCD_BACKLIGHT_ACTIVE_HIGH)) goto fail;
    if (qosa_spi_init(QOSA_SPI_PORT1, QOSA_SPI_TRANSMIT_DMA, QOSA_SPI_CLK_6_5MHZ) != QOSA_SPI_SUCCESS) goto fail;
    g_spi_ready = 1;
    if (qosa_spi_ioctl(QOSA_SPI_PORT1, QOSA_SPI_IOCTL_SET_NSS_MODE, &nss) != QOSA_SPI_SUCCESS ||
        qosa_spi_ioctl(QOSA_SPI_PORT1, QOSA_SPI_IOCTL_SET_CLK_POLARITY_PHASE, &format) != QOSA_SPI_SUCCESS ||
        qosa_spi_ioctl(QOSA_SPI_PORT1, QOSA_SPI_IOCTL_SET_DATA_WIDTH, &width) != QOSA_SPI_SUCCESS ||
        qosa_spi_ioctl(QOSA_SPI_PORT1, QOSA_SPI_IOCTL_SET_BIT_ORDER, &order) != QOSA_SPI_SUCCESS ||
        qosa_pin_set_func((qosa_pin_num_e)LCD_SCK_PIN, 1) != QOSA_PINCTRL_SUCCESS ||
        qosa_pin_set_func((qosa_pin_num_e)LCD_MOSI_PIN, 1) != QOSA_PINCTRL_SUCCESS ||
        qosa_pin_set_func((qosa_pin_num_e)LCD_MISO_PIN, 1) != QOSA_PINCTRL_SUCCESS ||
        qosa_pin_set_func((qosa_pin_num_e)g_control[LCD_CS].pin_num, g_control[LCD_CS].gpio_func) != QOSA_PINCTRL_SUCCESS ||
        qosa_gpio_init(g_control[LCD_CS].gpio_num, QOSA_GPIO_DIRECTION_OUTPUT,
            QOSA_GPIO_PULL_NONE, QOSA_GPIO_LEVEL_HIGH) != QOSA_GPIO_SUCCESS) goto fail;
    lcd_panel_init();
    if (g_io_failed) goto fail;
    g_ready = 1;
    QLOGI("scale GC9A01 240x240 SPI1 ready");
    return 1;
fail:
    QLOGE("scale LCD initialization failed; serial output remains active");
    lcd_close();
    return 0;
}

static int lcd_write_stripe(unsigned int top)
{
    unsigned int row;
    unsigned int column;
    unsigned int left = LCD_X_OFFSET;
    unsigned int right = left + LCD_WIDTH - 1;
    unsigned int first = top + LCD_Y_OFFSET;
    unsigned int last = first + STRIPE_HEIGHT - 1;
    unsigned char columns[] = {left >> 8, left & 255, right >> 8, right & 255};
    unsigned char rows[] = {first >> 8, first & 255, last >> 8, last & 255};
    if (top > LCD_HEIGHT - STRIPE_HEIGHT || !lcd_level(LCD_CS, 0)) goto done;
    if (!lcd_selected_command(0x2a, columns, sizeof(columns)) ||
        !lcd_selected_command(0x2b, rows, sizeof(rows)) ||
        !lcd_selected_command(0x2c, 0, 0) || !lcd_level(LCD_DC, 1)) goto done;
    for (row = 0; row < STRIPE_HEIGHT; ++row)
    {
        for (column = 0; column < LCD_WIDTH; ++column)
        {
            uint16_t pixel = g_pixels[row * LCD_WIDTH + column];
            unsigned int offset = (row * LCD_WIDTH + column) * 2;
            g_bytes[offset] = pixel >> 8;
            g_bytes[offset + 1] = pixel & 255;
        }
    }
    if (!lcd_write(g_bytes, sizeof(g_bytes))) goto done;
done:
    lcd_level(LCD_CS, 1);
    return !g_io_failed;
}

static void rectangle(int left, int top, int width, int height, uint16_t color)
{
    int right = left + width;
    int bottom = top + height;
    int row;
    int column;
    if (left < 0) left = 0;
    if (right > LCD_WIDTH) right = LCD_WIDTH;
    if (top < g_stripe) top = g_stripe;
    if (bottom > g_stripe + STRIPE_HEIGHT) bottom = g_stripe + STRIPE_HEIGHT;
    for (row = top; row < bottom; ++row)
        for (column = left; column < right; ++column)
            g_pixels[(row - g_stripe) * LCD_WIDTH + column] = color;
}

static void text(int left, int top, const char *value, int scale, uint16_t color)
{
    int column;
    int row;
    while (*value)
    {
        unsigned char character = *value++;
        int glyph = -1;
        if (character >= 'a' && character <= 'z') character -= 'a' - 'A';
        if (character >= '0' && character <= '9') glyph = character - '0';
        if (character >= 'A' && character <= 'Z') glyph = character - 'A' + 10;
        if (glyph >= 0)
        {
            for (column = 0; column < 5; ++column)
                for (row = 0; row < 7; ++row)
                    if (g_font[glyph][column] & (1 << row))
                        rectangle(left + column * scale, top + row * scale, scale, scale, color);
        }
        else if (character == '-') rectangle(left, top + 3 * scale, 5 * scale, scale, color);
        else if (character == '.') rectangle(left + 2 * scale, top + 6 * scale, scale, scale, color);
        else if (character == '/')
            for (row = 0; row < 7; ++row)
                rectangle(left + (4 - row * 4 / 6) * scale, top + row * scale, scale, scale, color);
        left += 6 * scale;
    }
}

static void text_centered(int top, const char *value, int scale, uint16_t color)
{
    int width = (int)strlen(value) * 6 * scale - scale;
    text((LCD_WIDTH - width) / 2, top, value, scale, color);
}

static void mask_round_stripe(void)
{
    int row;
    int column;
    const int diameter = LCD_CIRCLE_RADIUS * 2;
    const int diameter_squared = diameter * diameter;
    for (row = 0; row < STRIPE_HEIGHT; ++row)
    {
        int y = 2 * (g_stripe + row) - (LCD_HEIGHT - 1);
        for (column = 0; column < LCD_WIDTH; ++column)
        {
            int x = 2 * column - (LCD_WIDTH - 1);
            if (x * x + y * y > diameter_squared)
                g_pixels[row * LCD_WIDTH + column] = 0x0000;
        }
    }
}

static void line(int left, int top, int right, int bottom, uint16_t color)
{
    int dx = right - left;
    int dy = bottom - top;
    int steps = dx > 0 ? dx : -dx;
    int vertical = dy > 0 ? dy : -dy;
    int index;
    if (vertical > steps) steps = vertical;
    if (!steps) steps = 1;
    for (index = 0; index <= steps; ++index)
        rectangle(left + dx * index / steps, top + dy * index / steps, 2, 2, color);
}

static void storage_error_text(char *buffer, unsigned int size, const scale_view_t *view)
{
    snprintf(buffer, size, "SAVE %s %08X",
        view->storage_error_stage ? view->storage_error_stage : "FAIL",
        (unsigned int)view->storage_error_code);
}

static void draw_main(const scale_view_t *view)
{
    char buffer[48];
    unsigned int length;
    int size;
    text_centered(16, "WEIGHT SCALE", 1, COLOR_ACCENT);
    if (view->boot_tare)
    {
        text_centered(43, "INITIALIZING", 2, COLOR_WARN);
        snprintf(buffer, sizeof(buffer), "TARE %u/16", view->tare_samples);
        text_centered(91, buffer, 1, COLOR_MUTED);
        text_centered(119, "KEEP PLATFORM EMPTY", 1, COLOR_FG);
        text_centered(211, "WAIT FOR SENSOR", 1, COLOR_MUTED);
        return;
    }
    text_centered(41, view->valid ? (view->stable ? "STABLE" : "UNSTABLE") : "WAIT", 2,
                  view->stable ? COLOR_ACCENT : COLOR_WARN);
    if (view->valid)
    {
        if (g_trace_valid_frame && g_stripe == 0)
            QLOGI("scale first weight format begin");
        scale_format(buffer, sizeof(buffer), view->grams, view->unit, 1);
        if (g_trace_valid_frame && g_stripe == 0)
            QLOGI("scale first weight format end");
    }
    else snprintf(buffer, sizeof(buffer), "---");
    length = strlen(buffer);
    size = length <= 5 ? 5 : (length <= 7 ? 4 : (length <= 9 ? 3 : 2));
    if (length > 12) { snprintf(buffer, sizeof(buffer), "RANGE"); size = 4; }
    text_centered(76, buffer, size, COLOR_FG);
    snprintf(buffer, sizeof(buffer), "HIST %u/120", view->count);
    text_centered(133, buffer, 2, COLOR_MUTED);
    if (view->storage_error) storage_error_text(buffer, sizeof(buffer), view);
    text_centered(161, view->storage_error ? buffer : (view->tared ? "TARED" : "LOCAL RECORDS"),
               1, view->storage_error ? COLOR_WARN : COLOR_ACCENT);
    text_centered(187, "50 UNIT  51 TARE", 1, COLOR_MUTED);
    text_centered(208, "H50 HISTORY  H51 SAVE", 1, COLOR_MUTED);
}

static void draw_history(const scale_view_t *view, const scale_history_t *history, unsigned int page)
{
    unsigned int first = page * 4;
    unsigned int end = first + 4;
    unsigned int index;
    double minimum = 0;
    double maximum = 0;
    int previous_x = 0;
    int previous_y = 0;
    char buffer[40];
    if (end > history->count) end = history->count;
    text_centered(13, !view->valid ? "HISTORY WAIT" :
        (view->stable ? "HISTORY STABLE" : "HISTORY UNSTABLE"), 1,
        view->stable ? COLOR_ACCENT : COLOR_WARN);
    snprintf(buffer, sizeof(buffer), "PAGE %u/%u", page + 1, history->count ? (history->count + 3) / 4 : 1);
    text_centered(34, buffer, 1, COLOR_MUTED);
    if (!history->count)
    {
        text_centered(110, "NO RECORDS", 2, COLOR_MUTED);
        text_centered(187, "51 TARE  H51 CAL 209G", 1, COLOR_MUTED);
        text_centered(211, "H50 BACK", 1, COLOR_MUTED);
        return;
    }
    if (first < end) minimum = maximum = history->records[first].grams;
    for (index = first; index < end; ++index)
    {
        double grams = history->records[index].grams;
        if (grams < minimum) minimum = grams;
        if (grams > maximum) maximum = grams;
    }
    scale_format(buffer, sizeof(buffer), maximum, view->unit, 1);
    {
        char maximum_label[40];
        snprintf(maximum_label, sizeof(maximum_label), "MAX %s", buffer);
        text_centered(53, maximum_label, 1, COLOR_MUTED);
    }
    rectangle(34, 157, 172, 1, COLOR_MUTED);
    for (index = first; index < end; ++index)
    {
        int center = 50 + (int)(index - first) * 47;
        int point = maximum > minimum ? 145 - (int)((history->records[index].grams - minimum) * 55 / (maximum - minimum)) : 118;
        if (index != first) line(previous_x, previous_y, center, point, COLOR_ACCENT);
        rectangle(center - 3, point - 3, 7, 7, COLOR_ACCENT);
        scale_format(buffer, sizeof(buffer), history->records[index].grams, view->unit, 0);
        if (strlen(buffer) > 8) snprintf(buffer, sizeof(buffer), "RANGE");
        text(center - ((int)strlen(buffer) * 6 - 1) / 2, point - 13, buffer, 1, COLOR_FG);
        snprintf(buffer, sizeof(buffer), "%u", index + 1);
        text(center - ((int)strlen(buffer) * 6 - 1) / 2, 164, buffer, 1, COLOR_MUTED);
        previous_x = center;
        previous_y = point;
    }
    text_centered(187, "51 TARE  H51 CAL 209G", 1, COLOR_MUTED);
    text_centered(211, "50 NEXT  H50 BACK", 1, COLOR_MUTED);
}

void scale_display_draw(const scale_view_t *view, const scale_history_t *history, int page, unsigned int history_page)
{
    unsigned int index;
    if (!g_ready) return;
    g_trace_valid_frame = !g_first_valid_frame_done && !page && view->valid && !view->boot_tare;
    for (g_stripe = 0; g_stripe < LCD_HEIGHT; g_stripe += STRIPE_HEIGHT)
    {
        if (g_trace_valid_frame && (g_stripe == 0 || g_stripe == 64 || g_stripe == 128 || g_stripe == 224))
            QLOGI("scale first weight stripe=%d draw begin", g_stripe);
        for (index = 0; index < sizeof(g_pixels) / sizeof(g_pixels[0]); ++index)
            g_pixels[index] = COLOR_BG;
        if (page) draw_history(view, history, history_page);
        else draw_main(view);
        mask_round_stripe();
        if (g_trace_valid_frame && (g_stripe == 0 || g_stripe == 64 || g_stripe == 128 || g_stripe == 224))
            QLOGI("scale first weight stripe=%d transfer begin", g_stripe);
        if (!lcd_write_stripe(g_stripe))
        {
            QLOGE("scale LCD write failed");
            lcd_close();
            return;
        }
        if (g_trace_valid_frame && (g_stripe == 0 || g_stripe == 64 || g_stripe == 128 || g_stripe == 224))
            QLOGI("scale first weight stripe=%d transfer end", g_stripe);
    }
    if (g_trace_valid_frame) g_first_valid_frame_done = 1;
    if (!g_backlight_on)
    {
        if (!lcd_level(LCD_BACKLIGHT, LCD_BACKLIGHT_ACTIVE_HIGH))
        {
            QLOGE("scale LCD backlight failed");
            lcd_close();
            return;
        }
        g_backlight_on = 1;
    }
}