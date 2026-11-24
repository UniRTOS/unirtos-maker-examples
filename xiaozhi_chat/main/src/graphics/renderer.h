#ifndef ROBOT_DISPLAY_RENDERER_H
#define ROBOT_DISPLAY_RENDERER_H

#include <stddef.h>
#include <stdint.h>

/* Synchronous sink: consume/copy pixels before returning. x/y/w/h describe
 * a rectangle; pixels are host-endian RGB565, in row-major order. */
typedef int (*robot_pixel_sink_t)(void *ctx, uint16_t x, uint16_t y,
                                uint16_t w, uint16_t h, const uint16_t *pixels);

typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t *buffer;
    size_t capacity; /* pixels, not bytes */
    robot_pixel_sink_t sink;
    void *ctx;
} robot_canvas_t;

enum {
    ROBOT_RENDER_OK = 0,
    ROBOT_RENDER_INVALID = -100,
    ROBOT_EYE_MAX_HEIGHT = 96
};

int robot_render_fill(robot_canvas_t *canvas, uint16_t color);
int robot_render_eyes(robot_canvas_t *canvas, uint16_t eye_height);

#endif