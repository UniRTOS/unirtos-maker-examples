#include "activation_screen.h"

/* Original hand-plotted, purpose-specific simplified Chinese bitmaps.
 * NOT extracted/traced from a font or another project's raster assets.
 * Each row is 16 pixels; bit15 is the leftmost pixel. Drawn at 2x scale.
 * Exactly twelve glyphs: 验证码请在小智控制台输入, not a general font.
 * Structural guides: 马+佥, 讠+正, 石+马, 讠+青, 在, 小, 知+日,
 * 扌+空, 牛/巾+刂, 厶+口, 车+俞, 入 (sloping head, not 人).
 * These original rows may be used/modified under this project's terms;
 * they add no third-party font license or attribution requirement. */
static const uint16_t activation_hanzi[12][16] = {
    {0x0000,0x7820,0x0850,0x4888,0x4904,0x7e02,0x04f8,0x0400,
     0xf4a4,0x0454,0x0408,0x0400,0x1400,0x09fe,0x0000,0x0000}, /* 验 */
    {0x0000,0x2000,0x11fe,0x0010,0x0010,0x7010,0x1010,0x101e,
     0x1090,0x1090,0x1090,0x1290,0x1490,0x1890,0x03fe,0x0000}, /* 证 */
    {0x0000,0x0000,0x7efc,0x1004,0x1044,0x2044,0x407e,0x3c02,
     0x24fa,0x2402,0x2402,0x2402,0x2402,0x3c0a,0x0004,0x0000}, /* 码 */
    {0x0000,0x2020,0x11fe,0x0020,0x00fc,0x7020,0x13fe,0x1000,
     0x10fc,0x1084,0x10fc,0x1284,0x14fc,0x1884,0x008c,0x0000}, /* 请 */
    {0x0000,0x0200,0x0200,0x7ffe,0x0400,0x0800,0x1000,0x2420,
     0x4820,0x8420,0x05fe,0x0420,0x0420,0x0420,0x05fe,0x0000}, /* 在 */
    {0x0000,0x0080,0x0080,0x0080,0x0080,0x0890,0x0890,0x1088,
     0x1088,0x2084,0x0080,0x0080,0x0080,0x0280,0x0100,0x0000}, /* 小 */
    {0x0000,0x1000,0x3e7c,0x2844,0x4844,0x7f44,0x0844,0x147c,
     0x2200,0x0ff8,0x0808,0x0ff8,0x0808,0x0808,0x0ff8,0x0000}, /* 智 */
    {0x0000,0x1020,0x1010,0xfdfe,0x1102,0x1048,0x1484,0x1902,
     0x7000,0x10fc,0x1010,0x1010,0x1010,0x1010,0x71fe,0x0000}, /* 控 */
    {0x0000,0x1004,0x5004,0x7e24,0x9024,0x1024,0xff24,0x1024,
     0x7e24,0x5224,0x5224,0x5224,0x5204,0x1604,0x101c,0x0000}, /* 制 */
    {0x0000,0x0100,0x0200,0x0400,0x0820,0x1010,0x3ff8,0x0004,
     0x0000,0x0ff8,0x0808,0x0808,0x0808,0x0808,0x0ff8,0x0000}, /* 台 */
    {0x0000,0x1020,0xfe50,0x1088,0x2104,0x50fa,0xfe02,0x11ea,
     0x112a,0xffea,0x112a,0x11ea,0x1122,0x1122,0x1166,0x0000}, /* 输 */
    {0x0000,0x0000,0x0200,0x0100,0x0080,0x0080,0x0140,0x0140,
     0x0220,0x0220,0x0410,0x0808,0x1004,0x2002,0x4001,0x0000}  /* 入 */
};

/* Original five-column, seven-row digits; bit4 is the leftmost pixel.
 * Every digit, including 1, owns the same 40-pixel box and 48-pixel pitch. */
static const uint8_t activation_digits[10][7] = {
    {14,17,19,21,25,17,14}, /* 0, diagonal distinguishes it from 8. */
    {4,12,4,4,4,4,14},     /* 1 */
    {14,17,1,2,4,8,31},    /* 2 */
    {30,1,1,14,1,1,30},    /* 3 */
    {2,6,10,18,31,2,2},    /* 4 */
    {31,16,16,30,1,1,30},  /* 5 */
    {14,16,16,30,17,17,14},/* 6 */
    {31,1,2,4,8,8,8},      /* 7 */
    {14,17,17,14,17,17,14},/* 8 */
    {14,17,17,15,1,1,14}   /* 9 */
};

static void activation_hanzi_row(uint16_t *out, unsigned left,
                                 unsigned row, unsigned first, unsigned count)
{
    unsigned g, x;
    for (g = 0; g < count; ++g) {
        uint16_t bits = activation_hanzi[first + g][row / 2U];
        for (x = 0; x < 16U; ++x) {
            if (bits & (0x8000U >> x)) {
                unsigned at = left + g * 32U + x * 2U;
                out[at] = out[at + 1U] = 0xffffU;
            }
        }
    }
}

int xz_activation_screen_render(robot_canvas_t *c, unsigned code)
{
    uint8_t digits[6];
    unsigned i, strip, done = 0;
    size_t available;
    if (!c || !c->buffer || !c->sink ||
        c->width != XZ_ACTIVATION_SCREEN_WIDTH ||
        c->height != XZ_ACTIVATION_SCREEN_HEIGHT ||
        c->capacity < XZ_ACTIVATION_SCREEN_MIN_CAPACITY || code >= 1000000U)
        return ROBOT_RENDER_INVALID;

    /* Clamp as size_t BEFORE narrowing, even for an enormous capacity. */
    available = c->capacity / XZ_ACTIVATION_SCREEN_WIDTH;
    strip = available > XZ_ACTIVATION_SCREEN_STRIP_ROWS
          ? XZ_ACTIVATION_SCREEN_STRIP_ROWS : (unsigned)available;
    for (i = 6U; i > 0U; --i) {
        digits[i - 1U] = (uint8_t)(code % 10U);
        code /= 10U;
    }
    while (done < XZ_ACTIVATION_SCREEN_HEIGHT) {
        unsigned rows = XZ_ACTIVATION_SCREEN_HEIGHT - done, r;
        int rc;
        if (rows > strip) rows = strip;
        for (i = 0; i < rows * XZ_ACTIVATION_SCREEN_WIDTH; ++i)
            c->buffer[i] = 0;
        for (r = 0; r < rows; ++r) {
            unsigned y = done + r;
            uint16_t *out = c->buffer + r * XZ_ACTIVATION_SCREEN_WIDTH;
            if (y >= XZ_ACTIVATION_TITLE_Y &&
                y < XZ_ACTIVATION_TITLE_Y + XZ_ACTIVATION_TITLE_HEIGHT)
                activation_hanzi_row(out, XZ_ACTIVATION_TITLE_X,
                                     y - XZ_ACTIVATION_TITLE_Y, 0U, 3U);
            if (y >= XZ_ACTIVATION_HINT_Y &&
                y < XZ_ACTIVATION_HINT_Y + XZ_ACTIVATION_HINT_HEIGHT)
                activation_hanzi_row(out, XZ_ACTIVATION_HINT_X,
                                     y - XZ_ACTIVATION_HINT_Y, 3U, 9U);
            if (y >= XZ_ACTIVATION_DIGITS_Y &&
                y < XZ_ACTIVATION_DIGITS_Y + XZ_ACTIVATION_DIGITS_HEIGHT) {
                unsigned g, x, dx;
                for (g = 0; g < 6U; ++g) {
                    uint8_t bits = activation_digits[digits[g]]
                                      [(y - XZ_ACTIVATION_DIGITS_Y) / 8U];
                    for (x = 0; x < 5U; ++x) {
                        if (bits & (16U >> x)) {
                            unsigned at = XZ_ACTIVATION_DIGITS_X +
                                          g * XZ_ACTIVATION_DIGIT_STEP + x * 8U;
                            for (dx = 0; dx < 8U; ++dx) out[at + dx] = 0xffffU;
                        }
                    }
                }
            }
        }
        rc = c->sink(c->ctx, 0, (uint16_t)done,
                     XZ_ACTIVATION_SCREEN_WIDTH, (uint16_t)rows, c->buffer);
        if (rc) return rc;
        done += rows;
    }
    return ROBOT_RENDER_OK;
}