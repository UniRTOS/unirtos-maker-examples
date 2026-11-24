#ifndef XIAOZHI_TOUCH_POLICY_H
#define XIAOZHI_TOUCH_POLICY_H
#include <stdint.h>

typedef enum {
    XZ_TOUCH_NONE, XZ_TOUCH_TAP, XZ_TOUCH_SWIPE_LEFT,
    XZ_TOUCH_SWIPE_RIGHT, XZ_TOUCH_LONG_PRESS,
    XZ_TOUCH_SWIPE_DOWN, XZ_TOUCH_SWIPE_UP
} xz_touch_event_t;

typedef struct {
    uint32_t released_at, pressed_at, sampled_at;
    unsigned tracking_release, armed, sampled, contact, consumed;
    unsigned start_x, start_y, last_x, last_y, max_dx, max_dy;
    xz_touch_event_t pending;
} xz_touch_policy_t;

void xz_touch_policy_reset(xz_touch_policy_t *policy);
/* One event per single-finger contact. Tap/swipe commit after stable release;
 * long press fires while held. Invalid/busy I/O, multi-touch and sample gaps
 * cancel the contact. Booting while held never generates an event. */
xz_touch_event_t xz_touch_policy_sample(xz_touch_policy_t *policy, uint32_t now,
                                       int valid, unsigned fingers,
                                       unsigned x, unsigned y);
#endif