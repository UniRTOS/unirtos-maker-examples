#ifndef XIAOZHI_ACTIVATION_CLOCK_H
#define XIAOZHI_ACTIVATION_CLOCK_H
#include "qosa_rtc.h"

/* Plausibility only, NOT authenticated time or proof that NITZ arrived.
 * SDK1.0.15 UTCsecs is Unix seconds, NOT seconds since 2000. */
static inline int xz_activation_utc_valid(qosa_time_t utc)
{
    return utc >= (qosa_time_t)1704067200LL &&
           utc < (qosa_time_t)4102444800LL; /* 2024 <= UTC < 2100 */
}
#endif