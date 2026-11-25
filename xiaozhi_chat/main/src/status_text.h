#ifndef XIAOZHI_STATUS_TEXT_H
#define XIAOZHI_STATUS_TEXT_H
#include "display.h"

/* W180-specific text region, outside both eye windows, cleared on every
 * label change. Uses caller's existing strip buffer, no heap/full frame. */
#define XZ_STATUS_X 84U
#define XZ_STATUS_Y 300U
#define XZ_STATUS_WIDTH 192U
#define XZ_STATUS_HEIGHT 24U

/* Exactly six public words; auxiliary/internal states map to Offline or
 * Connecting. No font library, cloud text, formatted strings or new assets. */
const char *xz_status_word(xz_display_state_t state);
int xz_status_render(robot_canvas_t *canvas, xz_display_state_t state);
#endif