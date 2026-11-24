/**
 * @file xorshift.h
 * @brief Reproducible pseudo-random numbers (xorshift32).
 *
 * @details
 * The spectrum mode uses a fixed seed so that every boot of the same firmware
 * produces the same rhythm. That makes visual regressions reproducible and lets
 * tools/preview_frames.py replay the exact same sequence in Python.
 */

#ifndef XORSHIFT_H
#define XORSHIFT_H

#include <stdint.h>

/** xorshift32 generator state. Treat as opaque; use the xr_* functions. */
typedef struct
{
    uint32_t state;
} xorshift_t;

/**
 * @brief Seed the generator.
 *
 * @param rng  Generator state to initialise. NULL is ignored.
 * @param seed Seed value. Zero is remapped to a non-zero constant because zero
 *             is a fixed point of xorshift.
 */
void xr_seed(xorshift_t *rng, uint32_t seed);

/**
 * @brief Advance the generator and return the next 32-bit value.
 *
 * @param rng Generator state.
 * @return Pseudo-random 32-bit value.
 */
uint32_t xr_next(xorshift_t *rng);

/**
 * @brief Next value in [0, bound).
 *
 * @param rng   Generator state.
 * @param bound Exclusive upper bound. Zero returns zero.
 * @return Pseudo-random value in [0, bound).
 *
 * @note Uses modulo, so the distribution is slightly biased when @p bound does
 *       not divide 2^32. Acceptable here because the values only drive visuals.
 */
uint32_t xr_range(xorshift_t *rng, uint32_t bound);

/**
 * @brief Next value in [-scale, +scale].
 *
 * @param rng   Generator state.
 * @param scale Half-width of the range. Values <= 0 return zero.
 * @return Pseudo-random value in [-scale, +scale].
 */
int32_t xr_symmetric(xorshift_t *rng, int32_t scale);

#endif /* XORSHIFT_H */
