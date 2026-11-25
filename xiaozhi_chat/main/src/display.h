#ifndef XIAOZHI_DISPLAY_H
#define XIAOZHI_DISPLAY_H

#include "face.h"

typedef enum {
    XZ_DISPLAY_BOOT, XZ_DISPLAY_ACTIVATING, XZ_DISPLAY_IDLE,
    XZ_DISPLAY_CONNECTING, XZ_DISPLAY_LISTENING, XZ_DISPLAY_THINKING,
    XZ_DISPLAY_SPEAKING, XZ_DISPLAY_CLOSING, XZ_DISPLAY_ERROR,
    XZ_DISPLAY_READY,
    XZ_DISPLAY_STATE_COUNT
} xz_display_state_t;

/* Call init once from the app task. Disabled returns ROBOT_LCD_DISABLED;
 * display failures do not gate legacy S2; touch-talk refuses/ends recording
 * when display is unavailable, so its mic state cannot silently disappear.
 * Setters are nonblocking atomic mailboxes: no SPI, locks or allocation. */
int xz_display_init(void);
void xz_display_set_state(xz_display_state_t state);
void xz_display_set_emotion(const char *name);
/* Reset emotion at session boundaries, not at every TTS sentence. */
void xz_display_reset_emotion(void);
int xz_display_status(void);
/* Cycle the persistent manual face; never wakes PTT or sends audio. */
unsigned xz_display_touch_next(void);
/* direction must be +1 (next/left swipe) or -1 (previous/right swipe).
 * Manual selection cancels temporary feedback. No audio or hardware access. */
unsigned xz_display_touch_step(int direction);
void xz_display_touch_happy(void);
/* Explicit page setters for app/test control. Mailboxes only.
 * Horizontal/hold interactions also return to the face they select. */
void xz_display_show_clock(unsigned show);
void xz_display_show_xyz(void);
/* Relative vertical navigation: +1=down, -1=up; other values do nothing.
 * Face: down->clock, up->XYZ. Clock: up->face, down stays.
 * XYZ: down->face, up stays. Never skip the face or wrap at outer edges.
 * Atomic mailbox only, no rendering/audio/bus IO; activation still wins. */
void xz_display_touch_vertical(int direction);
/* Six ASCII digits, including leading zeroes; NULL/invalid clears the page.
 * One packed atomic command prevents torn digits across tasks. Activation
 * owns publication; the display task alone draws. Fresh confirmations renew
 * the local stale-code guard (not a claim about server-side expiry). */
#define XZ_DISPLAY_CODE_TTL_MS 60000U
void xz_display_set_activation_code(const char *code);
/* Single display-consumer selection; also consumes the feedback mailbox and
 * expires temporary happy using the system tick. Cloud never changes manual. */
xz_face_t xz_display_select_face(xz_display_state_t state, xz_face_t emotion);

#endif