#ifndef XIAOZHI_CLOCK_H
#define XIAOZHI_CLOCK_H
#include "graphics/renderer.h"

/* Center (180,180), symmetric dial margins; bottom status starts at y300. */
#define XZ_CLOCK_X 60U
#define XZ_CLOCK_Y 60U
#define XZ_CLOCK_SIZE 240U
#define XZ_CLOCK_UNKNOWN 86400U
#define XZ_CLOCK_FIRST 86401U

/* Mirror of the bottom status region (84,300,192,24) about screen centre. */
#define XZ_CLOCK_TIME_X 84U
#define XZ_CLOCK_TIME_Y 36U
#define XZ_CLOCK_TIME_WIDTH 192U
#define XZ_CLOCK_TIME_HEIGHT 24U

/* Seconds of local day, or UNKNOWN: ticks only, never fake hands/time.
 * UTC is read-only and displayed as Beijing time (UTC+8), no RTC/NV writes. */
unsigned xz_clock_local_seconds(int64_t utc, int read_ok);
/* FIRST forces the initial region. Subsequent calls compare old/new geometry
 * and only send changed strip spans. Caller owns the existing RGB565 buffer.
 * No frame buffer, heap, libm, date conversion or hardware access here. */
int xz_clock_render(robot_canvas_t *canvas, unsigned seconds, unsigned previous);
/* Top digital 24-hour HH:MM:SS, or --:--:-- when RTC is unavailable.
 * Same seconds => no writes. Entire text region is cleared on changes.
 * Shares the same local-day seconds/previous value as the dial; no RTC read. */
int xz_clock_time_render(robot_canvas_t *canvas, unsigned seconds, unsigned previous);
#endif