#ifndef XIAOZHI_ACTIVATION_SCREEN_H
#define XIAOZHI_ACTIVATION_SCREEN_H

#include "graphics/renderer.h"

#define XZ_ACTIVATION_SCREEN_WIDTH 360U
#define XZ_ACTIVATION_SCREEN_HEIGHT 360U
#define XZ_ACTIVATION_SCREEN_STRIP_ROWS 8U
#define XZ_ACTIVATION_SCREEN_MIN_CAPACITY 360U /* RGB565 pixels, not bytes. */

#define XZ_ACTIVATION_TITLE_X 132U
#define XZ_ACTIVATION_TITLE_Y 88U
#define XZ_ACTIVATION_TITLE_WIDTH 96U
#define XZ_ACTIVATION_TITLE_HEIGHT 32U
#define XZ_ACTIVATION_DIGITS_X 40U
#define XZ_ACTIVATION_DIGITS_Y 152U
#define XZ_ACTIVATION_DIGITS_WIDTH 280U
#define XZ_ACTIVATION_DIGITS_HEIGHT 56U
#define XZ_ACTIVATION_DIGIT_WIDTH 40U
#define XZ_ACTIVATION_DIGIT_STEP 48U
#define XZ_ACTIVATION_HINT_X 36U
#define XZ_ACTIVATION_HINT_Y 236U
#define XZ_ACTIVATION_HINT_WIDTH 288U
#define XZ_ACTIVATION_HINT_HEIGHT 32U

#ifdef __cplusplus
extern "C" {
#endif

/* Render a COMPLETE black 360x360 page with white text and exactly six digits.
 * code: 0..999999, including leading zeroes (0 -> 000000; 1234 -> 001234).
 * Uses caller-owned canvas->buffer only, at most 360*8 pixels per strip;
 * capacity >= 360 pixels is sufficient. The sink must consume synchronously.
 * Returns ROBOT_RENDER_INVALID before any writes for invalid inputs, otherwise
 * ROBOT_RENDER_OK or the first nonzero sink result unchanged. No rollback:
 * after sink failure the panel may be partially updated; retry the full page.
 * No allocation, retained state, hardware calls, or lifecycle management. */
int xz_activation_screen_render(robot_canvas_t *canvas, unsigned code);

#ifdef __cplusplus
}
#endif
#endif