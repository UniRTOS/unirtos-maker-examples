/**
 * @file rtime.c
 * @brief Monotonic millisecond time base implementation.
 */

#include "util/rtime.h"
#include "config/app_config.h"

#include "qosa_def.h"
#include "qosa_sys.h"
#include "qosa_rtc.h"

/**
 * @brief Calibration window length in milliseconds.
 *
 * A longer window improves accuracy but delays start-up. 200 ms already keeps
 * the error below 1 % (the sleep itself rounds by one tick).
 */
#define RTIME_CAL_WINDOW_MS     (200)

/** Tick-to-millisecond ratio measured by rtime_init(). */
static uint32_t s_ticks_per_ms = 1;

void rtime_init(void)
{
    const uint32_t t0 = qosa_get_system_tick_cnt();
    qosa_task_sleep_ms(RTIME_CAL_WINDOW_MS);
    const uint32_t t1 = qosa_get_system_tick_cnt();
    const uint32_t span = t1 - t0;

    uint32_t tpm = span / RTIME_CAL_WINDOW_MS;
    if (tpm == 0)
    {
        /* Defensive fallback: treat one tick as one millisecond, which keeps
         * the division safe even if the platform reports an unexpected value. */
        tpm = 1;
    }
    s_ticks_per_ms = tpm;

    LD_LOGI("tick calibration: %u ticks / %u ms -> %u tick per ms",
            (unsigned int)span, (unsigned int)RTIME_CAL_WINDOW_MS,
            (unsigned int)tpm);
}

uint32_t rtime_now_ms(void)
{
    return qosa_get_system_tick_cnt() / s_ticks_per_ms;
}
