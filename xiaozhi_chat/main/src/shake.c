#include "shake.h"

static void clear_history(xz_shake_t *s)
{
    unsigned axis;
    s->have_prev = 0;
    for (axis = 0; axis < 3; ++axis) {
        s->prev_accel_mg[axis] = 0;
        s->first_ms[axis] = 0;
        s->direction[axis] = 0;
        s->count[axis] = 0;
    }
}

void xz_shake_reset(xz_shake_t *s)
{
    if (!s) return;
    clear_history(s);
    s->prev_sampled = 0;
    s->triggered_ms = 0;
    s->have_sample = 0;
    s->cooling = 0;
}

int xz_shake_update(xz_shake_t *s, const xz_mpu6050_t *sensor,
                    uint32_t now)
{
    uint32_t sampled, gap;
    unsigned axis;
    int triggered = 0;
    if (!s) return 0;
    if (s->cooling && (uint32_t)(now - s->triggered_ms) >= XZ_SHAKE_COOLDOWN_MS)
        s->cooling = 0;
    if (!sensor || sensor->view.status != XZ_IMU_LIVE ||
        (uint32_t)(now - sensor->sampled) >= XZ_SHAKE_STALE_MS) {
        clear_history(s);
        return 0;
    }
    sampled = sensor->sampled;
    if (s->have_sample && sampled == s->prev_sampled) return 0;
    gap = (uint32_t)(sampled - s->prev_sampled);
    s->prev_sampled = sampled;
    s->have_sample = 1;
    if (s->cooling) {
        clear_history(s);
        return 0;
    }
    if (s->have_prev && gap > XZ_SHAKE_GAP_MS) clear_history(s);
    for (axis = 0; axis < 3; ++axis) {
        if (s->have_prev) {
            /* Cast BEFORE subtracting: full int32 endpoints differ by 2^32-1. */
            int64_t delta = (int64_t)sensor->view.accel_mg[axis] -
                            (int64_t)s->prev_accel_mg[axis];
            int direction = delta > 0 ? 1 : -1;
            if (delta > -XZ_SHAKE_DELTA_MG && delta < XZ_SHAKE_DELTA_MG) {
                s->count[axis] = 0;
                s->direction[axis] = 0;
                s->first_ms[axis] = 0;
            } else {
                if (!s->count[axis] || s->direction[axis] == direction ||
                    (uint32_t)(sampled - s->first_ms[axis]) > XZ_SHAKE_WINDOW_MS) {
                    s->count[axis] = 1;
                    s->first_ms[axis] = sampled;
                } else {
                    ++s->count[axis];
                }
                s->direction[axis] = direction;
                if (s->count[axis] == 3) triggered = 1;
            }
        }
        s->prev_accel_mg[axis] = sensor->view.accel_mg[axis];
    }
    s->have_prev = 1;
    if (triggered) {
        s->cooling = 1;
        s->triggered_ms = now;
        clear_history(s);
    }
    return triggered;
}
