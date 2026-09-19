/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "clock_math.h"

#include <assert.h>
#include <stdint.h>

static void
test_distinct_domains_and_exact_rendezvous(void)
{
    bm_clock_position_t two_hz;
    bm_clock_position_t three_hz;
    const bm_clock_rate_t two_hz_rate = { 2U, 1U };
    const bm_clock_rate_t three_hz_rate = { 3U, 1U };

    assert(bm_clock_position_init(&two_hz, &two_hz_rate) == BM_STATUS_OK);
    assert(bm_clock_position_init(&three_hz, &three_hz_rate) == BM_STATUS_OK);
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
    const bm_clock_rate_t rate = { UINT64_C(3579545), 1U };
    uint64_t index;

    assert(bm_clock_position_init(&incremental, &rate) == BM_STATUS_OK);
    assert(bm_clock_position_init(&bulk, &rate) == BM_STATUS_OK);
    for (index = 0U; index < UINT64_C(10000); ++index)
        assert(bm_clock_position_advance(&incremental, 1U) == BM_STATUS_OK);
    assert(bm_clock_position_advance(&bulk, UINT64_C(10000)) == BM_STATUS_OK);
    assert(bm_clock_position_compare(&incremental, &bulk) == 0);
    assert(bm_clock_position_advance(&bulk, UINT64_C(3569545)) == BM_STATUS_OK);
    assert(bulk.nanoseconds == UINT64_C(1000000000));
    assert(bulk.phase == 0U);
}

static void
test_rational_crystal_divider_has_no_drift(void)
{
    bm_clock_position_t incremental;
    bm_clock_position_t bulk;
    const bm_clock_rate_t divided_crystal = {
        UINT64_C(14318180), 3U
    };
    uint64_t index;

    assert(bm_clock_position_init(&incremental, &divided_crystal) ==
           BM_STATUS_OK);
    assert(bm_clock_position_init(&bulk, &divided_crystal) == BM_STATUS_OK);
    for (index = 0U; index < UINT64_C(10000); ++index)
        assert(bm_clock_position_advance(&incremental, 1U) == BM_STATUS_OK);
    assert(bm_clock_position_advance(&bulk, UINT64_C(10000)) == BM_STATUS_OK);
    assert(bm_clock_position_compare(&incremental, &bulk) == 0);

    assert(bm_clock_position_init(&bulk, &divided_crystal) == BM_STATUS_OK);
    assert(bm_clock_position_advance(&bulk, UINT64_C(14318180)) ==
           BM_STATUS_OK);
    assert(bulk.nanoseconds == UINT64_C(3000000000));
    assert(bulk.phase == 0U);
}

static void
test_fraction_comparison_without_overflow(void)
{
    bm_clock_position_t left = {
        7U, UINT64_MAX - 2U, UINT64_MAX - 1U, 1U
    };
    bm_clock_position_t right = {
        7U, UINT64_MAX - 3U, UINT64_MAX - 1U, 1U
    };
    bm_clock_position_t equivalent = { 7U, 1U, 2U, 1U };
    bm_clock_position_t half = { 7U, 2U, 4U, 1U };

    assert(bm_clock_position_compare(&left, &right) > 0);
    assert(bm_clock_position_compare(&right, &left) < 0);
    assert(bm_clock_position_compare(&equivalent, &half) == 0);
}

static void
test_export_normalizes_public_fraction(void)
{
    bm_clock_position_t clock = { 7U, 3U, 9U, 1U };
    bm_time_point_t time_point;

    bm_clock_position_export(&clock, &time_point);
    assert(time_point.nanoseconds == 7U);
    assert(time_point.subnanosecond_numerator == 1U);
    assert(time_point.subnanosecond_denominator == 3U);
    clock.phase = 0U;
    bm_clock_position_export(&clock, &time_point);
    assert(time_point.subnanosecond_numerator == 0U);
    assert(time_point.subnanosecond_denominator == 1U);
}

static void
test_next_domain_edge_is_strict_and_exact(void)
{
    const bm_clock_rate_t three_hz = { 3U, 1U };
    const bm_clock_rate_t one_ghz = { UINT64_C(1000000000), 1U };
    bm_clock_position_t target = { 0U, 0U, 1U, 1U };
    bm_clock_position_t next;

    assert(bm_clock_position_next_after(&three_hz, &target, &next) ==
           BM_STATUS_OK);
    assert(next.nanoseconds == UINT64_C(333333333));
    assert(next.phase == 1U);
    assert(next.phase_denominator == 3U);

    target = next;
    assert(bm_clock_position_next_after(&three_hz, &target, &next) ==
           BM_STATUS_OK);
    assert(next.nanoseconds == UINT64_C(666666666));
    assert(next.phase == 2U);
    assert(next.phase_denominator == 3U);

    target.nanoseconds = UINT64_C(500000000);
    target.phase = 0U;
    target.phase_denominator = 1U;
    assert(bm_clock_position_next_after(&three_hz, &target, &next) ==
           BM_STATUS_OK);
    assert(next.nanoseconds == UINT64_C(666666666));
    assert(next.phase == 2U);

    target.nanoseconds = UINT64_MAX;
    assert(bm_clock_position_next_after(&one_ghz, &target, &next) ==
           BM_STATUS_CAPACITY_EXCEEDED);
    target.nanoseconds = 0U;
    target.phase_denominator = 0U;
    assert(bm_clock_position_next_after(&three_hz, &target, &next) ==
           BM_STATUS_INVALID_ARGUMENT);
}

static void
test_cycle_cursor_is_exact_across_fractional_and_large_ranges(void)
{
    const bm_clock_rate_t three_hz = { 3U, 1U };
    const bm_clock_rate_t twenty_ghz = { UINT64_C(20000000000), 1U };
    bm_clock_position_t target = {
        UINT64_C(666666666), 2U, 3U, 1U
    };
    bm_clock_position_t clock;
    uint64_t cycles = UINT64_MAX;

    assert(bm_clock_cycles_at_or_before(&three_hz, &target, &cycles) ==
           BM_STATUS_OK);
    assert(cycles == 2U);
    target.phase = 1U;
    assert(bm_clock_cycles_at_or_before(&three_hz, &target, &cycles) ==
           BM_STATUS_OK);
    assert(cycles == 1U);

    target.nanoseconds = 0U;
    target.phase = 1U;
    target.phase_denominator = 2U;
    assert(bm_clock_cycles_at_or_before(&twenty_ghz, &target, &cycles) ==
           BM_STATUS_OK);
    assert(cycles == 10U);
    target.phase = 499U;
    target.phase_denominator = 1000U;
    assert(bm_clock_cycles_at_or_before(&twenty_ghz, &target, &cycles) ==
           BM_STATUS_OK);
    assert(cycles == 9U);

    target.nanoseconds = UINT64_MAX;
    target.phase = 0U;
    target.phase_denominator = 1U;
    assert(bm_clock_cycles_at_or_before(&three_hz, &target, &cycles) ==
           BM_STATUS_OK);
    assert(cycles == UINT64_C(55340232221));

    assert(bm_clock_position_init(&clock, &three_hz) == BM_STATUS_OK);
    assert(bm_clock_position_advance(&clock, cycles) == BM_STATUS_OK);
    assert(clock.nanoseconds == UINT64_C(18446744073666666666));
    assert(clock.phase == 2U);
    assert(bm_clock_position_advance(&clock, 1U) ==
           BM_STATUS_CAPACITY_EXCEEDED);
    assert(clock.nanoseconds == UINT64_C(18446744073666666666));
    assert(clock.phase == 2U);
}

static void
test_invalid_and_overflow_are_atomic(void)
{
    bm_clock_position_t clock;
    const bm_clock_rate_t two_hz = { 2U, 1U };
    const bm_clock_rate_t zero_numerator = { 0U, 1U };
    const bm_clock_rate_t zero_denominator = { 1U, 0U };
    const bm_clock_rate_t unrepresentable = { 1U, UINT64_MAX };

    assert(bm_clock_position_init(NULL, &two_hz) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_clock_position_init(&clock, NULL) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_clock_position_init(&clock, &zero_numerator) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_clock_position_init(&clock, &zero_denominator) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_clock_position_init(&clock, &unrepresentable) ==
           BM_STATUS_CAPACITY_EXCEEDED);
    assert(bm_clock_position_init(&clock, &two_hz) == BM_STATUS_OK);
    assert(bm_clock_cycles_at_or_before(&two_hz, &clock, NULL) ==
           BM_STATUS_INVALID_ARGUMENT);
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
    test_rational_crystal_divider_has_no_drift();
    test_fraction_comparison_without_overflow();
    test_export_normalizes_public_fraction();
    test_next_domain_edge_is_strict_and_exact();
    test_cycle_cursor_is_exact_across_fractional_and_large_ranges();
    test_invalid_and_overflow_are_atomic();
    return 0;
}
