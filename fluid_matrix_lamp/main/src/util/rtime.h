/**
 * @file rtime.h
 * @brief Monotonic millisecond time base.
 *
 * @details
 * The platform function @c qosa_get_system_tick_cnt() returns an internal tick
 * counter, not milliseconds. The tick-to-millisecond ratio is not documented in
 * the SDK, so this module derives it at boot by sleeping for a known duration
 * and measuring how many ticks elapsed.
 *
 * Every timing decision in this application (frame pacing, physics time step,
 * key debounce, watchdog margins) must go through rtime_now_ms() instead of
 * assuming a tick unit. Skipping this calibration silently scales all durations
 * by an unknown factor.
 */

#ifndef RTIME_H
#define RTIME_H

#include <stdint.h>

/**
 * @brief Calibrate the tick-to-millisecond ratio.
 *
 * @details Sleeps for a fixed window (about 200 ms) and derives the ratio from
 * the observed tick delta. Call once during start-up, before any other module
 * reads time. The result is logged so a wrong ratio is visible in the field.
 *
 * @note Not thread safe. Call from a single initialisation context.
 */
void rtime_init(void);

/**
 * @brief Milliseconds since boot, derived from the internal tick counter.
 *
 * @return Monotonic time in milliseconds. Wraps after about 49 days.
 */
uint32_t rtime_now_ms(void);

#endif /* RTIME_H */
