#ifndef XIAOZHI_FACE_H
#define XIAOZHI_FACE_H

#include "graphics/renderer.h"

typedef enum {
    XZ_FACE_NEUTRAL, XZ_FACE_HAPPY, XZ_FACE_SAD, XZ_FACE_ANGRY,
    XZ_FACE_SURPRISED, XZ_FACE_SLEEPY, XZ_FACE_THINKING,
    XZ_FACE_LISTENING, XZ_FACE_SPEAKING, XZ_FACE_CONNECTING,
    XZ_FACE_ERROR, XZ_FACE_COUNT,
    XZ_FACE_DIZZY = 64 /* Local feedback only; not an asset or protocol slot. */
} xz_face_t;

/* Pure bounded rendering: no SDK, GPIO, allocation, fonts or runtime files.
 * Embedded ESP Emote original keyframes; source/license in emote_assets/.
 * At least 240x240; caller must clear the background once before first use.
 * Update only two fixed eye windows, including their black pixels to erase old
 * poses. Other pixels are untouched. Native 360-wide output: 2 x 125x160.
 * phase wraps modulo 16. Original faces: bit2 selects one of two poses;
 * 14/15 use original shared blink 22. DIZZY reuses the original confused.eaf
 * spiral-eye poses from THINKING; bit2 selects the pose, without a blink.
 * COUNT remains invalid. Timing/temporary feedback belongs to the caller. */
int xz_face_render(robot_canvas_t *canvas, xz_face_t face, unsigned phase);
/* Whitelisted protocol names only; NULL/unknown names become neutral. */
xz_face_t xz_face_from_emotion(const char *name);

#endif