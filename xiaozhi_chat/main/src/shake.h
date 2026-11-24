#ifndef XIAOZHI_SHAKE_H
#define XIAOZHI_SHAKE_H

#include "mpu6050.h"

enum {
    XZ_SHAKE_DELTA_MG = 900,
    XZ_SHAKE_GAP_MS = 900,
    XZ_SHAKE_WINDOW_MS = 1800,
    XZ_SHAKE_STALE_MS = 1000,
    XZ_SHAKE_COOLDOWN_MS = 4000
};

/* Caller-owned, zero-initializable, single-owner state; no SDK/heap/IO.
 * have_sample distinguishes an unseen sample from the valid timestamp zero.
 * prev_sampled is the last consumed burst token, even without motion history.
 * Each axis tracks its own consecutive above-threshold alternating deltas. */
typedef struct xz_shake_t {
    int32_t prev_accel_mg[3];
    uint32_t prev_sampled, first_ms[3], triggered_ms;
    int direction[3];
    unsigned count[3];
    unsigned have_sample, have_prev, cooling;
} xz_shake_t;

/* Explicit reset clears EVERYTHING, including cooldown. NULL is a no-op. */
void xz_shake_reset(xz_shake_t *shake);

/* Returns 1 only on the third qualifying change of ONE acceleration axis:
 * |delta| >=900mg, alternating signs, first-to-last <=1800ms, sample gaps
 * <=900ms. A subthreshold delta breaks that axis's run; a repeated sign or
 * expired window starts a new one-change run. Gyro/model do not participate.
 * Only LIVE bursts with (uint32_t)(now - sensor->sampled) <1000 are usable.
 * Equal sampled timestamps are always ignored, even if values changed (the
 * driver cannot expose two distinct bursts stamped in the same millisecond).
 * Non-LIVE/stale/NULL sensor clears motion history, NOT the consumed token or
 * cooldown. A NULL shake returns 0. First usable burst establishes baseline.
 * Trigger starts 4000ms cooldown at now (already includes any 2000ms display).
 * During cooldown fresh tokens are consumed but ALL motion history is empty;
 * the first fresh burst at/after expiry is baseline, never a deferred trigger.
 * Unsigned elapsed times handle uint32 wrap; caller supplies monotonic modulo
 * 2^32 millisecond clocks and calls at intervals shorter than a full wrap.
 * Future-dated samples are rejected as stale by the unsigned age check. */
int xz_shake_update(xz_shake_t *shake, const xz_mpu6050_t *sensor,
                    uint32_t now);

#endif
