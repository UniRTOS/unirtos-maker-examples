/**
 * @file framebuffer.c
 * @brief 16x16 RGB frame buffer and primitive drawing.
 */

#include "gfx/framebuffer.h"

#include <string.h>

int fb_index(int x, int y)
{
    int row;
    int col;

    if (x < 0) x = 0;
    if (x >= FB_W) x = FB_W - 1;
    if (y < 0) y = 0;
    if (y >= FB_H) y = FB_H - 1;

    /* Reverse the row order when the first LED sits at the bottom-left corner. */
    row = BOARD_MATRIX_ORIGIN_TOP ? y : (FB_H - 1 - y);

#if BOARD_MATRIX_SERPENTINE
    col = ((row & 1) == 0) ? x : (FB_W - 1 - x);
#else
    col = x;
#endif
    return row * FB_W + col;
}

void fb_clear(fb_canvas_t *fb)
{
    if (fb != NULL)
    {
        memset(fb->px, 0, sizeof(fb->px));
    }
}

void fb_fill(fb_canvas_t *fb, fb_rgb_t c)
{
    if (fb == NULL)
    {
        return;
    }
    for (int i = 0; i < FB_PIXELS; i++)
    {
        fb->px[i] = c;
    }
}

void fb_blend(fb_canvas_t *dst, const fb_canvas_t *src, int t)
{
    if (dst == NULL || src == NULL || t <= 0)
    {
        return;
    }
    if (t >= 256)
    {
        memcpy(dst->px, src->px, sizeof(dst->px));
        return;
    }
    const int inv = 256 - t;
    for (int i = 0; i < FB_PIXELS; i++)
    {
        dst->px[i].r = (uint8_t)((dst->px[i].r * inv + src->px[i].r * t) >> 8);
        dst->px[i].g = (uint8_t)((dst->px[i].g * inv + src->px[i].g * t) >> 8);
        dst->px[i].b = (uint8_t)((dst->px[i].b * inv + src->px[i].b * t) >> 8);
    }
}

void fb_hline(fb_canvas_t *fb, int x0, int x1, int y, fb_rgb_t c)
{
    if (x0 > x1)
    {
        int t = x0; x0 = x1; x1 = t;
    }
    for (int x = x0; x <= x1; x++)
    {
        fb_set(fb, x, y, c);
    }
}

void fb_rect(fb_canvas_t *fb, int x, int y, int w, int h, fb_rgb_t c)
{
    for (int j = 0; j < h; j++)
    {
        for (int i = 0; i < w; i++)
        {
            fb_set(fb, x + i, y + j, c);
        }
    }
}

void fb_disc(fb_canvas_t *fb, int cx, int cy, int radius, fb_rgb_t c)
{
    if (fb == NULL || radius < 0)
    {
        return;
    }
    const int r2 = radius * radius;
    for (int dy = -radius; dy <= radius; dy++)
    {
        for (int dx = -radius; dx <= radius; dx++)
        {
            if (dx * dx + dy * dy <= r2)
            {
                fb_set(fb, cx + dx, cy + dy, c);
            }
        }
    }
}

fb_rgb_t fb_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    fb_rgb_t c;
    c.r = r;
    c.g = g;
    c.b = b;
    return c;
}

fb_rgb_t fb_gray(uint8_t level)
{
    return fb_rgb(level, level, level);
}

fb_rgb_t fb_hsv(int h, uint8_t s, uint8_t v)
{
    if (h < 0)
    {
        h = 0;
    }
    h %= 360;

    if (s == 0)
    {
        return fb_rgb(v, v, v);
    }

    /* Integer HSV -> RGB, one 60-degree sector per case. */
    const int region = h / 60;
    const int frac = (h % 60) * 255 / 60;   /* 0..255 */
    const int p = (int)v * (255 - (int)s) / 255;
    const int q = (int)v * (255 - ((int)s * frac) / 255) / 255;
    const int t = (int)v * (255 - ((int)s * (255 - frac)) / 255) / 255;

    switch (region)
    {
    case 0:  return fb_rgb((uint8_t)v, (uint8_t)t, (uint8_t)p);
    case 1:  return fb_rgb((uint8_t)q, (uint8_t)v, (uint8_t)p);
    case 2:  return fb_rgb((uint8_t)p, (uint8_t)v, (uint8_t)t);
    case 3:  return fb_rgb((uint8_t)p, (uint8_t)q, (uint8_t)v);
    case 4:  return fb_rgb((uint8_t)t, (uint8_t)p, (uint8_t)v);
    default: return fb_rgb((uint8_t)v, (uint8_t)p, (uint8_t)q);
    }
}
