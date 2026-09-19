/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_ENGINE_CLOCK_MATH_H
#define BLUMACH_ENGINE_CLOCK_MATH_H

#include <stdint.h>
#include <blumach/engine/engine.h>
#include <blumach/engine/types.h>

/* Internal, exact-phase clock representation. The whole part is virtual
 * nanoseconds; phase/phase_denominator is the remaining fraction of one
 * nanosecond. Advancing by a whole number of cycles never accumulates
 * rounding error. */
typedef struct bm_clock_position {
    uint64_t nanoseconds;
    uint64_t phase;
    uint64_t phase_denominator;
    uint64_t nanoseconds_per_cycle_numerator;
} bm_clock_position_t;

bm_status_t bm_clock_position_init(bm_clock_position_t *clock,
                                   const bm_clock_rate_t *rate);
bm_status_t bm_clock_position_advance(bm_clock_position_t *clock, uint64_t cycles);
int bm_clock_position_compare(const bm_clock_position_t *left,
                              const bm_clock_position_t *right);

#endif
