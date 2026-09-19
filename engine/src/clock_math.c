/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "clock_math.h"

#include <limits.h>

static uint64_t
greatest_common_divisor(uint64_t left, uint64_t right)
{
    while (right != 0U) {
        uint64_t remainder = left % right;

        left = right;
        right = remainder;
    }
    return left;
}

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
bm_clock_position_init(bm_clock_position_t *clock, const bm_clock_rate_t *rate)
{
    uint64_t cycles_numerator;
    uint64_t cycles_denominator;
    uint64_t divisor;
    uint64_t nanoseconds_numerator;

    if ((clock == NULL) || (rate == NULL) ||
        (rate->cycles_per_second_numerator == 0U) ||
        (rate->cycles_per_second_denominator == 0U))
        return BM_STATUS_INVALID_ARGUMENT;
    divisor = greatest_common_divisor(rate->cycles_per_second_numerator,
                                      rate->cycles_per_second_denominator);
    cycles_numerator = rate->cycles_per_second_numerator / divisor;
    cycles_denominator = rate->cycles_per_second_denominator / divisor;
    divisor = greatest_common_divisor(cycles_numerator,
                                      UINT64_C(1000000000));
    cycles_numerator /= divisor;
    nanoseconds_numerator = UINT64_C(1000000000) / divisor;
    if (cycles_denominator > (UINT64_MAX / nanoseconds_numerator))
        return BM_STATUS_CAPACITY_EXCEEDED;
    clock->nanoseconds = 0U;
    clock->phase = 0U;
    clock->phase_denominator = cycles_numerator;
    clock->nanoseconds_per_cycle_numerator =
        nanoseconds_numerator * cycles_denominator;
    return BM_STATUS_OK;
}

bm_status_t
bm_clock_position_advance(bm_clock_position_t *clock, uint64_t cycles)
{
    uint64_t numerator;
    uint64_t whole;

    if ((clock == NULL) || (clock->phase_denominator == 0U) ||
        (clock->nanoseconds_per_cycle_numerator == 0U) ||
        (clock->phase >= clock->phase_denominator))
        return BM_STATUS_INVALID_ARGUMENT;
    if (cycles > ((UINT64_MAX - clock->phase) /
                  clock->nanoseconds_per_cycle_numerator))
        return BM_STATUS_CAPACITY_EXCEEDED;
    numerator = cycles * clock->nanoseconds_per_cycle_numerator + clock->phase;
    whole = numerator / clock->phase_denominator;
    if (clock->nanoseconds > (UINT64_MAX - whole))
        return BM_STATUS_CAPACITY_EXCEEDED;
    clock->nanoseconds += whole;
    clock->phase = numerator % clock->phase_denominator;
    return BM_STATUS_OK;
}

int
bm_clock_position_compare(const bm_clock_position_t *left,
                          const bm_clock_position_t *right)
{
    if (left->nanoseconds != right->nanoseconds)
        return left->nanoseconds < right->nanoseconds ? -1 : 1;
    return compare_fractions(left->phase, left->phase_denominator,
                             right->phase, right->phase_denominator);
}
