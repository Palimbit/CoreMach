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

/* Compute floor(left * right / denominator) and its remainder without a
 * wider integer type. This keeps the portable engine usable on MSVC and on
 * 32-bit hosts while still accepting the complete representable time range. */
static bm_status_t
multiply_divide(uint64_t left, uint64_t right, uint64_t denominator,
                uint64_t *quotient, uint64_t *remainder)
{
    uint64_t whole;
    uint64_t residual_quotient = 0U;
    uint64_t residual_remainder = 0U;
    uint64_t residual;
    unsigned int bit;

    if ((denominator == 0U) || (quotient == NULL) || (remainder == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    whole = left / denominator;
    residual = left % denominator;
    if ((whole != 0U) && (right > (UINT64_MAX / whole)))
        return BM_STATUS_CAPACITY_EXCEEDED;
    whole *= right;
    /* Integral domains are common (for example, a 10 MHz CPU advances by
     * exactly 100 ns per cycle). Once left has no remainder, the general
     * bitwise multiply/divide cannot contribute another quotient bit or a
     * fractional remainder. Avoid paying its fixed 64-iteration cost at every
     * instruction boundary while preserving the same overflow decision. */
    if ((residual == 0U) || (right == 0U)) {
        *quotient = whole;
        *remainder = 0U;
        return BM_STATUS_OK;
    }

    /* Instruction-sized advances usually fit in one native multiplication.
     * Keep the general overflow-safe path for genuinely large operands. */
    if (residual <= (UINT64_MAX / right)) {
        uint64_t product = residual * right;
        uint64_t residual_whole = product / denominator;

        if (whole > (UINT64_MAX - residual_whole))
            return BM_STATUS_CAPACITY_EXCEEDED;
        *quotient = whole + residual_whole;
        *remainder = product % denominator;
        return BM_STATUS_OK;
    }

    for (bit = 64U; bit-- > 0U;) {
        uint64_t carry = 0U;

        if (residual_remainder >= (denominator - residual_remainder)) {
            residual_remainder -= denominator - residual_remainder;
            carry = 1U;
        } else {
            residual_remainder += residual_remainder;
        }
        if (((right >> bit) & UINT64_C(1)) != 0U) {
            if (residual_remainder >= (denominator - residual)) {
                residual_remainder -= denominator - residual;
                ++carry;
            } else {
                residual_remainder += residual;
            }
        }
        if (residual_quotient > ((UINT64_MAX - carry) / 2U))
            return BM_STATUS_CAPACITY_EXCEEDED;
        residual_quotient = residual_quotient * 2U + carry;
    }
    if (whole > (UINT64_MAX - residual_quotient))
        return BM_STATUS_CAPACITY_EXCEEDED;
    *quotient = whole + residual_quotient;
    *remainder = residual_remainder;
    return BM_STATUS_OK;
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
    uint64_t whole;
    uint64_t phase;
    uint64_t nanoseconds;
    bm_status_t status;

    if ((clock == NULL) || (clock->phase_denominator == 0U) ||
        (clock->nanoseconds_per_cycle_numerator == 0U) ||
        (clock->phase >= clock->phase_denominator))
        return BM_STATUS_INVALID_ARGUMENT;
    status = multiply_divide(cycles,
                             clock->nanoseconds_per_cycle_numerator,
                             clock->phase_denominator, &whole, &phase);
    if (status != BM_STATUS_OK)
        return status;
    if (clock->phase >= (clock->phase_denominator - phase)) {
        phase = clock->phase - (clock->phase_denominator - phase);
        if (whole == UINT64_MAX)
            return BM_STATUS_CAPACITY_EXCEEDED;
        ++whole;
    } else {
        phase += clock->phase;
    }
    if (clock->nanoseconds > (UINT64_MAX - whole))
        return BM_STATUS_CAPACITY_EXCEEDED;
    nanoseconds = clock->nanoseconds + whole;
    clock->nanoseconds = nanoseconds;
    clock->phase = phase;
    return BM_STATUS_OK;
}

static bm_status_t
position_after_cycles(const bm_clock_rate_t *rate, uint64_t cycles,
                      bm_clock_position_t *position)
{
    bm_status_t status = bm_clock_position_init(position, rate);
    uint64_t nanoseconds;
    uint64_t phase;

    if (status != BM_STATUS_OK)
        return status;
    status = multiply_divide(cycles,
                             position->nanoseconds_per_cycle_numerator,
                             position->phase_denominator,
                             &nanoseconds, &phase);
    if (status != BM_STATUS_OK)
        return status;
    position->nanoseconds = nanoseconds;
    position->phase = phase;
    return BM_STATUS_OK;
}

bm_status_t
bm_clock_cycles_at_or_before(const bm_clock_rate_t *rate,
                             const bm_clock_position_t *target,
                             uint64_t *cycles)
{
    bm_clock_position_t candidate;
    uint64_t lower = 0U;
    uint64_t upper = 1U;
    bm_status_t status;

    if ((rate == NULL) || (target == NULL) || (cycles == NULL) ||
        (target->phase_denominator == 0U) ||
        (target->phase >= target->phase_denominator))
        return BM_STATUS_INVALID_ARGUMENT;

    status = position_after_cycles(rate, 0U, &candidate);
    if (status != BM_STATUS_OK)
        return status;

    /* Find an upper source-cycle index whose position is strictly after the
     * target. An overflowing position is still a valid binary-search upper
     * bound; the final selected position must itself remain representable. */
    for (;;) {
        status = position_after_cycles(rate, upper, &candidate);
        if ((status == BM_STATUS_CAPACITY_EXCEEDED) ||
            ((status == BM_STATUS_OK) &&
             (bm_clock_position_compare(&candidate, target) > 0)))
            break;
        if (status != BM_STATUS_OK)
            return status;
        lower = upper;
        if (upper > (UINT64_MAX / 2U)) {
            upper = UINT64_MAX;
            status = position_after_cycles(rate, upper, &candidate);
            if ((status == BM_STATUS_OK) &&
                (bm_clock_position_compare(&candidate, target) <= 0)) {
                *cycles = UINT64_MAX;
                return BM_STATUS_OK;
            }
            if ((status != BM_STATUS_OK) &&
                (status != BM_STATUS_CAPACITY_EXCEEDED))
                return status;
            break;
        }
        upper *= 2U;
    }

    while ((upper - lower) > 1U) {
        uint64_t middle = lower + ((upper - lower) / 2U);

        status = position_after_cycles(rate, middle, &candidate);
        if ((status == BM_STATUS_CAPACITY_EXCEEDED) ||
            ((status == BM_STATUS_OK) &&
             (bm_clock_position_compare(&candidate, target) > 0)))
            upper = middle;
        else if (status == BM_STATUS_OK)
            lower = middle;
        else
            return status;
    }

    *cycles = lower;
    return BM_STATUS_OK;
}

bm_status_t
bm_clock_position_next_after(const bm_clock_rate_t *rate,
                             const bm_clock_position_t *target,
                             bm_clock_position_t *next)
{
    uint64_t cycles;
    bm_status_t status;

    if (next == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    status = bm_clock_cycles_at_or_before(rate, target, &cycles);
    if (status != BM_STATUS_OK)
        return status;
    if (cycles == UINT64_MAX)
        return BM_STATUS_CAPACITY_EXCEEDED;
    return position_after_cycles(rate, cycles + 1U, next);
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

void
bm_clock_position_export(const bm_clock_position_t *clock,
                         bm_time_point_t *time_point)
{
    uint64_t divisor;

    time_point->nanoseconds = clock->nanoseconds;
    if (clock->phase == 0U) {
        time_point->subnanosecond_numerator = 0U;
        time_point->subnanosecond_denominator = 1U;
        return;
    }
    divisor = greatest_common_divisor(clock->phase,
                                      clock->phase_denominator);
    time_point->subnanosecond_numerator = clock->phase / divisor;
    time_point->subnanosecond_denominator =
        clock->phase_denominator / divisor;
}
