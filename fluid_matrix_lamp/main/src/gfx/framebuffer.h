/**
 * @file framebuffer.h
 * @brief 16x16 RGB frame buffer and primitive drawing.
 *
 * @details
 * Coordinate convention: x=0 is the left edge, y=0 is the **top** row and y
 * grows downwards, matching what the viewer sees. The mapping from logical
 * pixel to physical LED index (serpentine wiring, first-LED corner) is
 * controlled by board_config.h.
 *
 * Pixels are stored in physical LED order, so the transmit path can read the
 * buffer linearly. This layer knows nothing about WS2812 or SPI.
 */

#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <stdint.h>
#include <stdbool.h>

#include "config/board_config.h"

#define FB_W        BOARD_MATRIX_W
#define FB_H        BOARD_MATRIX_H
#define FB_PIXELS   (FB_W * FB_H)

/** 8-bit-per-channel colour. */
typedef struct
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
} fb_rgb_t;

/** A full frame, stored in physical LED order. */
typedef struct
{
    fb_rgb_t px[FB_PIXELS];
} fb_canvas_t;

/**
 * @brief Map logical coordinates to a physical LED index.
 *
 * @param x Column, 0 = left.
 * @param y Row, 0 = top.
 * @return Index into fb_canvas_t::px. Out-of-range inputs are clamped.
 */
int fb_index(int x, int y);

/**
 * @brief Write one pixel, ignoring out-of-range coordinates.
 *
 * @param fb Canvas to draw into.
 * @param x  Column, 0 = left.
 * @param y  Row, 0 = top.
 * @param c  Colour to write.
 */
static inline void fb_set(fb_canvas_t *fb, int x, int y, fb_rgb_t c)
{
    if ((unsigned)x >= (unsigned)FB_W || (unsigned)y >= (unsigned)FB_H)
    {
        return;
    }
    fb->px[fb_index(x, y)] = c;
}

/**
 * @brief Fill the whole canvas with black (all channels zero).
 *
 * @param fb Canvas to clear. NULL is ignored.
 */
void fb_clear(fb_canvas_t *fb);

/**
 * @brief Fill the whole canvas with one colour.
 *
 * @param fb Canvas to fill.
 * @param c  Fill colour.
 */
void fb_fill(fb_canvas_t *fb, fb_rgb_t c);

/**
 * @brief dst = dst*(1-t) + src*t, per channel.
 *
 * @param dst Destination canvas, updated in place.
 * @param src Source canvas.
 * @param t   Blend weight, 0..256. Values <= 0 leave @p dst unchanged;
 *            values >= 256 copy @p src verbatim.
 */
void fb_blend(fb_canvas_t *dst, const fb_canvas_t *src, int t);

/**
 * @brief Draw a horizontal line, both endpoints inclusive.
 *
 * @param fb Canvas to draw into.
 * @param x0 Start column.
 * @param x1 End column. Reversed input is handled.
 * @param y  Row.
 * @param c  Colour.
 */
void fb_hline(fb_canvas_t *fb, int x0, int x1, int y, fb_rgb_t c);

/**
 * @brief Draw a filled rectangle.
 *
 * @param fb Canvas to draw into.
 * @param x  Left column.
 * @param y  Top row.
 * @param w  Width in pixels.
 * @param h  Height in pixels.
 * @param c  Colour.
 */
void fb_rect(fb_canvas_t *fb, int x, int y, int w, int h, fb_rgb_t c);

/**
 * @brief Draw a filled disc.
 *
 * @param fb     Canvas to draw into.
 * @param cx     Centre column.
 * @param cy     Centre row.
 * @param radius Radius in pixels. Negative values are ignored.
 * @param c      Colour.
 */
void fb_disc(fb_canvas_t *fb, int cx, int cy, int radius, fb_rgb_t c);

/**
 * @brief Build a colour from channel values.
 *
 * @param r Red, 0..255.
 * @param g Green, 0..255.
 * @param b Blue, 0..255.
 * @return The composed colour.
 */
fb_rgb_t fb_rgb(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Build a greyscale colour.
 *
 * @param level Brightness, 0..255, applied to all three channels.
 * @return The composed colour.
 */
fb_rgb_t fb_gray(uint8_t level);

/**
 * @brief Convert HSV to RGB using integer arithmetic.
 *
 * @param h Hue in degrees; any value is wrapped into 0..359.
 * @param s Saturation, 0..255. Zero yields grey.
 * @param v Value (brightness), 0..255.
 * @return The composed colour.
 */
fb_rgb_t fb_hsv(int h, uint8_t s, uint8_t v);

#endif /* FRAMEBUFFER_H */
