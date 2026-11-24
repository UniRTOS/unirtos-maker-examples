#ifndef XIAOZHI_EMOTE_ANIMATION_H
#define XIAOZHI_EMOTE_ANIMATION_H

#include "face.h"
#include <stdint.h>

/* Library/legacy-screen default. Production explicitly selects full animation. */
#ifndef XIAOZHI_EMOTE_ANIMATION
#define XIAOZHI_EMOTE_ANIMATION 0
#endif

#define XZ_EMOTE_FRAME_MS 50U

/* Single display-task owner. Native 360x360 only; caller clears the background
 * once. All source frames are retained, with an eight-color lossy palette.
 * One sequential frame per due update (never skip source frames to catch up).
 * Dirty rectangles include pixels becoming black, and never touch status text.
 * reset() invalidates the displayed history after a page clear; no IO/heap.
 * A failed sink may be retried: the decoded pending frame is retained and will
 * be fully repainted, not incorrectly committed as successfully displayed. */
void xz_emote_animation_reset(void);
int xz_emote_animation_update(robot_canvas_t *canvas, xz_face_t face, uint32_t now);

#endif