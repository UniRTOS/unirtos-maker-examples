#include "touch_policy.h"
#include "touch_config.h"

void xz_touch_policy_reset(xz_touch_policy_t *p)
{
    if (p) {
        /* Explicit reset avoids a runtime memset dependency in bare fixtures. */
        p->released_at = p->pressed_at = p->sampled_at = 0;
        p->tracking_release = p->armed = p->sampled = 0;
        p->contact = p->consumed = 0;
        p->start_x = p->start_y = p->last_x = p->last_y = p->max_dx = p->max_dy = 0;
        p->pending = XZ_TOUCH_NONE;
    }
}

static unsigned distance(unsigned a, unsigned b)
{ return a > b ? a - b : b - a; }

static xz_touch_event_t released_event(const xz_touch_policy_t *p, uint32_t now)
{
    uint32_t held = now - p->pressed_at;
    unsigned dx = distance(p->last_x, p->start_x);
    unsigned dy = distance(p->last_y, p->start_y);
    if (p->consumed) return XZ_TOUCH_NONE;
    if (p->max_dx <= XIAOZHI_TOUCH_SLOP_PX &&
        p->max_dy <= XIAOZHI_TOUCH_SLOP_PX) {
        if (held >= XIAOZHI_TOUCH_LONG_MS) return XZ_TOUCH_LONG_PRESS;
        return held >= XIAOZHI_TOUCH_TAP_MIN_MS ? XZ_TOUCH_TAP : XZ_TOUCH_NONE;
    }
    if (held <= XIAOZHI_TOUCH_SWIPE_MAX_MS && dx >= XIAOZHI_TOUCH_SWIPE_PX &&
        p->max_dy <= XIAOZHI_TOUCH_SWIPE_Y_PX && dx >= p->max_dy * 2U)
        return p->last_x < p->start_x ? XZ_TOUCH_SWIPE_LEFT : XZ_TOUCH_SWIPE_RIGHT;
    if (held <= XIAOZHI_TOUCH_SWIPE_MAX_MS && dy >= XIAOZHI_TOUCH_SWIPE_PX &&
        p->max_dx <= XIAOZHI_TOUCH_SWIPE_Y_PX && dy >= p->max_dx * 2U)
        return p->last_y > p->start_y ? XZ_TOUCH_SWIPE_DOWN : XZ_TOUCH_SWIPE_UP;
    return XZ_TOUCH_NONE;
}

xz_touch_event_t xz_touch_policy_sample(xz_touch_policy_t *p, uint32_t now,
                                       int valid, unsigned fingers,
                                       unsigned x, unsigned y)
{
    if (!p) return XZ_TOUCH_NONE;
    if (!valid || fingers > 1U || (fingers && (x >= 360U || y >= 360U))) {
        xz_touch_policy_reset(p);
        return XZ_TOUCH_NONE;
    }
    if (p->sampled && (uint32_t)(now - p->sampled_at) > XIAOZHI_TOUCH_SAMPLE_GAP_MS)
        xz_touch_policy_reset(p);
    p->sampled = 1;
    p->sampled_at = now;
    if (!fingers) {
        if (!p->tracking_release) {
            p->released_at = now;
            p->tracking_release = 1;
            p->pending = p->contact ? released_event(p, now) : XZ_TOUCH_NONE;
        }
        if ((uint32_t)(now - p->released_at) >= XIAOZHI_TOUCH_RELEASE_MS) {
            xz_touch_event_t event = p->pending;
            p->pending = XZ_TOUCH_NONE;
            p->contact = 0;
            p->armed = 1;
            return event;
        }
        return XZ_TOUCH_NONE;
    }
    if (p->tracking_release && p->contact) {
        /* A short release/bounce cancels instead of making a second action. */
        p->contact = p->armed = 0;
        p->pending = XZ_TOUCH_NONE;
    }
    p->tracking_release = 0;
    if (p->armed) {
        p->armed = 0;
        p->contact = 1;
        p->consumed = 0;
        p->pressed_at = now;
        p->start_x = p->last_x = x;
        p->start_y = p->last_y = y;
        p->max_dx = p->max_dy = 0;
    }
    if (p->contact && !p->consumed) {
        unsigned dx = distance(x, p->start_x), dy = distance(y, p->start_y);
        p->last_x = x;
        p->last_y = y;
        if (dx > p->max_dx) p->max_dx = dx;
        if (dy > p->max_dy) p->max_dy = dy;
        if ((uint32_t)(now - p->pressed_at) >= XIAOZHI_TOUCH_LONG_MS &&
            p->max_dx <= XIAOZHI_TOUCH_SLOP_PX && p->max_dy <= XIAOZHI_TOUCH_SLOP_PX) {
            p->consumed = 1;
            return XZ_TOUCH_LONG_PRESS;
        }
    }
    return XZ_TOUCH_NONE;
}