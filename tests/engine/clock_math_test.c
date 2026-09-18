/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "clock_math.h"

#include <assert.h>
#include <stdint.h>

static void
test_distinct_domains_and_exact_rendezvous(void)
{
    bm_clock_position_t two_hz;
    bm_clock_position_t three_hz;

    assert(bm_clock_position_init(&two_hz, 2U) == BM_STATUS_OK);
    assert(bm_clock_position_init(&three_hz, 3U) == BM_STATUS_OK);
    assert(bm_clock_position_compare(&two_hz, &three_hz) == 0);
    assert(bm_clock_position_advance(&two_hz, 1U) == BM_STATUS_OK);
    assert(bm_clock_position_advance(&three_hz, 1U) == BM_STATUS_OK);
    assert(bm_clock_position_compare(&three_hz, &two_hz) < 0);
    assert(bm_clock_position_advance(&three_hz, 1U) == BM_STATUS_OK);
    assert(bm_clock_position_compare(&three_hz, &two_hz) > 0);
    assert(bm_clock_position_advance(&two_hz, 1U) == BM_STATUS_OK);
    assert(bm_clock_position_advance(&three_hz, 1U) == BM_STATUS_OK);
    assert(two_hz.nanoseconds == UINT64_C(1000000000));
    assert(three_hz.nanoseconds == UINT64_C(1000000000));
    assert(bm_clock_position_compare(&two_hz, &three_hz) == 0);
}

static void
test_no_accumulated_rounding(void)
{
    bm_clock_position_t incremental;
    bm_clock_position_t bulk;
    uint64_t index;

    assert(bm_clock_position_init(&incremental, UINT64_C(3579545)) == BM_STATUS_OK);
    assert(bm_clock_position_init(&bulk, UINT64_C(3579545)) == BM_STATUS_OK);
    for (index = 0U; index < UINT64_C(10000); ++index)
        assert(bm_clock_position_advance(&incremental, 1U) == BM_STATUS_OK);
    assert(bm_clock_position_advance(&bulk, UINT64_C(10000)) == BM_STATUS_OK);
    assert(bm_clock_position_compare(&incremental, &bulk) == 0);
    assert(bm_clock_position_advance(&bulk, UINT64_C(3569545)) == BM_STATUS_OK);
    assert(bulk.nanoseconds == UINT64_C(1000000000));
    assert(bulk.phase == 0U);
}

static void
test_fraction_comparison_without_overflow(void)
{
    bm_clock_position_t left = { 7U, UINT64_MAX - 2U, UINT64_MAX - 1U };
    bm_clock_position_t right = { 7U, UINT64_MAX - 3U, UINT64_MAX - 1U };
    bm_clock_position_t equivalent = { 7U, 1U, 2U };
    bm_clock_position_t half = { 7U, 2U, 4U };

    assert(bm_clock_position_compare(&left, &right) > 0);
    assert(bm_clock_position_compare(&right, &left) < 0);
    assert(bm_clock_position_compare(&equivalent, &half) == 0);
}

static void
test_invalid_and_overflow_are_atomic(void)
{
    bm_clock_position_t clock;

    assert(bm_clock_position_init(NULL, 1U) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_clock_position_init(&clock, 0U) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_clock_position_init(&clock, 2U) == BM_STATUS_OK);
    clock.nanoseconds = UINT64_MAX;
    assert(bm_clock_position_advance(&clock, 1U) == BM_STATUS_CAPACITY_EXCEEDED);
    assert(clock.nanoseconds == UINT64_MAX);
    assert(clock.phase == 0U);
    assert(bm_clock_position_advance(&clock, UINT64_MAX) == BM_STATUS_CAPACITY_EXCEEDED);
    assert(clock.nanoseconds == UINT64_MAX);
    assert(clock.phase == 0U);
}

int
main(void)
{
    test_distinct_domains_and_exact_rendezvous();
    test_no_accumulated_rounding();
    test_fraction_comparison_without_overflow();
    test_invalid_and_overflow_are_atomic();
    return 0;
}
