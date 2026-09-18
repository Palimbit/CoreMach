/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "clock_math.h"

#include <limits.h>

/* Compare non-negative fractions without cross-multiplication overflow. Each
 * reciprocal reverses the ordering, hence the alternating sign. */
static int
compare_fractions(uint64_t left_numerator, uint64_t left_denominator,
                  uint64_t right_numerator, uint64_t right_denominator)
{
    int direction = 1;

    for (;;) {
        uint64_t left_whole = left_numerator / left_denominator;
        uint64_t right_whole = right_numerator / right_denominator;
        uint64_t swap;

        if (left_whole != right_whole)
            return direction * (left_whole < right_whole ? -1 : 1);
        left_numerator %= left_denominator;
        right_numerator %= right_denominator;
        if ((left_numerator == 0U) || (right_numerator == 0U)) {
            if (left_numerator == right_numerator)
                return 0;
            return direction * (left_numerator == 0U ? -1 : 1);
        }
        swap = left_numerator;
        left_numerator = left_denominator;
        left_denominator = swap;
        swap = right_numerator;
        right_numerator = right_denominator;
        right_denominator = swap;
        direction = -direction;
    }
}

bm_status_t
bm_clock_position_init(bm_clock_position_t *clock, uint64_t frequency_hz)
{
    if ((clock == NULL) || (frequency_hz == 0U))
        return BM_STATUS_INVALID_ARGUMENT;
    clock->nanoseconds = 0U;
    clock->phase = 0U;
    clock->frequency_hz = frequency_hz;
    return BM_STATUS_OK;
}

bm_status_t
bm_clock_position_advance(bm_clock_position_t *clock, uint64_t cycles)
{
    uint64_t numerator;
    uint64_t whole;

    if ((clock == NULL) || (clock->frequency_hz == 0U) ||
        (clock->phase >= clock->frequency_hz))
        return BM_STATUS_INVALID_ARGUMENT;
    if (cycles > ((UINT64_MAX - clock->phase) / UINT64_C(1000000000)))
        return BM_STATUS_CAPACITY_EXCEEDED;
    numerator = cycles * UINT64_C(1000000000) + clock->phase;
    whole = numerator / clock->frequency_hz;
    if (clock->nanoseconds > (UINT64_MAX - whole))
        return BM_STATUS_CAPACITY_EXCEEDED;
    clock->nanoseconds += whole;
    clock->phase = numerator % clock->frequency_hz;
    return BM_STATUS_OK;
}

int
bm_clock_position_compare(const bm_clock_position_t *left,
                          const bm_clock_position_t *right)
{
    if (left->nanoseconds != right->nanoseconds)
        return left->nanoseconds < right->nanoseconds ? -1 : 1;
    return compare_fractions(left->phase, left->frequency_hz,
                             right->phase, right->frequency_hz);
}
