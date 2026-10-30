/**
 * @file xorshift.c
 * @brief Reproducible pseudo-random numbers (xorshift32).
 */

#include "util/xorshift.h"

void xr_seed(xorshift_t *rng, uint32_t seed)
{
    if (rng == 0)
    {
        return;
    }
    /* Zero is a fixed point of xorshift, so substitute a non-zero constant. */
    rng->state = (seed != 0) ? seed : 0x9E3779B9u;
}

uint32_t xr_next(xorshift_t *rng)
{
    uint32_t x = rng->state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng->state = x;
    return x;
}

uint32_t xr_range(xorshift_t *rng, uint32_t bound)
{
    if (bound == 0)
    {
        return 0;
    }
    return xr_next(rng) % bound;
}

int32_t xr_symmetric(xorshift_t *rng, int32_t scale)
{
    if (scale <= 0)
    {
        return 0;
    }
    int32_t span = 2 * scale + 1;
    return (int32_t)xr_range(rng, (uint32_t)span) - scale;
}
