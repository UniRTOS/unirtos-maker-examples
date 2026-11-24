#include "renderer.h"

static int valid_canvas(const robot_canvas_t *canvas)
{
    return canvas != NULL && canvas->buffer != NULL && canvas->sink != NULL &&
           canvas->width > 0 && canvas->height > 0 &&
           canvas->capacity >= canvas->width;
}

static uint16_t chunk_rows(const robot_canvas_t *canvas, uint16_t remaining)
{
    size_t rows = canvas->capacity / canvas->width;
    return (uint16_t)(rows < remaining ? rows : remaining);
}

int robot_render_fill(robot_canvas_t *canvas, uint16_t color)
{
    uint16_t y = 0;
    if (!valid_canvas(canvas)) {
        return ROBOT_RENDER_INVALID;
    }
    while (y < canvas->height) {
        uint16_t rows = chunk_rows(canvas, (uint16_t)(canvas->height - y));
        size_t count = (size_t)canvas->width * rows;
        size_t i;
        int ret;
        for (i = 0; i < count; ++i) {
            canvas->buffer[i] = color;
        }
        ret = canvas->sink(canvas->ctx, 0, y, canvas->width, rows, canvas->buffer);
        if (ret != 0) {
            return ret;
        }
        y = (uint16_t)(y + rows);
    }
    return ROBOT_RENDER_OK;
}

static int inside_eye(int x, int y, int left, int top, int w, int h)
{
    int radius = h / 2 < 12 ? h / 2 : 12;
    int dx = 0;
    int dy = 0;
    if (x < left || x >= left + w || y < top || y >= top + h) {
        return 0;
    }
    if (x < left + radius) {
        dx = left + radius - x;
    } else if (x >= left + w - radius) {
        dx = x - (left + w - radius - 1);
    }
    if (y < top + radius) {
        dy = top + radius - y;
    } else if (y >= top + h - radius) {
        dy = y - (top + h - radius - 1);
    }
    return dx * dx + dy * dy <= radius * radius;
}

int robot_render_eyes(robot_canvas_t *canvas, uint16_t eye_height)
{
    uint16_t band_y;
    uint16_t done = 0;
    int top, left, right;
    /* Fixed 48px eye width; centers at 1/3 and 2/3 of the canvas. */
    if (!valid_canvas(canvas) || canvas->width < 160 || canvas->height < 120 ||
        eye_height == 0 || eye_height > ROBOT_EYE_MAX_HEIGHT) {
        return ROBOT_RENDER_INVALID;
    }
    band_y = (uint16_t)((canvas->height - ROBOT_EYE_MAX_HEIGHT) / 2);
    top = (canvas->height - eye_height) / 2;
    left = canvas->width / 3 - 24;
    right = 2 * canvas->width / 3 - 24;
    /* Redraw the entire maximum-height band, clearing remnants of open eyes.
     * The rest of the screen stays untouched; clear the screen once beforehand. */
    while (done < ROBOT_EYE_MAX_HEIGHT) {
        uint16_t rows = chunk_rows(canvas, (uint16_t)(ROBOT_EYE_MAX_HEIGHT - done));
        uint16_t row, x;
        int ret;
        for (row = 0; row < rows; ++row) {
            int y = band_y + done + row;
            for (x = 0; x < canvas->width; ++x) {
                int lit = inside_eye(x, y, left, top, 48, eye_height) ||
                          inside_eye(x, y, right, top, 48, eye_height);
                canvas->buffer[(size_t)row * canvas->width + x] = lit ? 0xFFFF : 0;
            }
        }
        ret = canvas->sink(canvas->ctx, 0, (uint16_t)(band_y + done),
                           canvas->width, rows, canvas->buffer);
        if (ret != 0) {
            return ret;
        }
        done = (uint16_t)(done + rows);
    }
    return ROBOT_RENDER_OK;
}