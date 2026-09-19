/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "pit8253_exact.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static void
program_channel(bm_pit_exact_device_t *pit, unsigned int channel,
                unsigned int mode, int bcd, uint16_t count)
{
    uint8_t control = (uint8_t) ((channel << 6U) | 0x30U |
                                 (mode << 1U) | (bcd ? 1U : 0U));

    bm_pit_exact_control_write(pit, control);
    bm_pit_exact_data_write(pit, channel, (uint8_t) count);
    bm_pit_exact_data_write(pit, channel, (uint8_t) (count >> 8U));
    bm_pit_exact_set_gate(pit, channel, true);
}

static int
outputs_differ(const bm_pit_exact_device_t *left,
               const bm_pit_exact_device_t *right)
{
    unsigned int channel;

    for (channel = 0U; channel < 3U; ++channel) {
        if (bm_pit_exact_get_output(left, channel) !=
            bm_pit_exact_get_output(right, channel))
            return 1;
    }
    return 0;
}

static uint32_t
reference_cycles_until_output_change(const bm_pit_exact_device_t *source)
{
    bm_pit_exact_device_t reference = *source;
    uint32_t cycles;

    for (cycles = 1U; cycles <= 0x10001U; ++cycles) {
        bm_pit_exact_device_t previous = reference;

        bm_pit_exact_tick(&reference);
        if (outputs_differ(&previous, &reference))
            return cycles;
    }
    return 0U;
}

static void
assert_batch_matches_reference(const bm_pit_exact_device_t *initial,
                               uint32_t total_ticks)
{
    bm_pit_exact_device_t reference = *initial;
    bm_pit_exact_device_t batched = *initial;
    uint32_t remaining = total_ticks;
    uint32_t iteration = 0U;

    while (remaining != 0U) {
        uint32_t expected =
            reference_cycles_until_output_change(&reference);
        uint32_t actual =
            bm_pit_exact_cycles_until_output_change(&batched);
        uint32_t limit = ((iteration * 7919U) % 70000U) + 1U;
        uint32_t consumed;
        uint32_t tick;

        assert(actual == expected);
        if (limit > remaining)
            limit = remaining;
        consumed = bm_pit_exact_advance_until_output_change(&batched, limit);
        assert((consumed != 0U) && (consumed <= limit));
        for (tick = 0U; tick < consumed; ++tick)
            bm_pit_exact_tick(&reference);
        assert(memcmp(&batched, &reference, sizeof(batched)) == 0);
        remaining -= consumed;
        ++iteration;
    }
}

static void
test_each_mode_matches_edge_reference(void)
{
    static const uint16_t binary_counts[6] = {
        0x1234U, 0x0031U, 0x1235U, 0x1235U, 0x0234U, 0x0031U
    };
    static const uint16_t bcd_counts[6] = {
        0x0123U, 0x0031U, 0x0125U, 0x0125U, 0x0234U, 0x0031U
    };
    unsigned int mode;

    for (mode = 0U; mode < 6U; ++mode) {
        bm_pit_exact_device_t pit;

        bm_pit_exact_reset(&pit);
        program_channel(&pit, 0U, mode, 0, binary_counts[mode]);
        assert_batch_matches_reference(&pit, 200000U);

        bm_pit_exact_reset(&pit);
        program_channel(&pit, 0U, mode, 1, bcd_counts[mode]);
        assert_batch_matches_reference(&pit, 30000U);
    }
}

static void
test_even_odd_and_zero_periods_match(void)
{
    static const uint16_t counts[] = { 0U, 1U, 2U, 3U, 4U, 0xffffU };
    size_t index;

    for (index = 0U; index < (sizeof(counts) / sizeof(counts[0])); ++index) {
        bm_pit_exact_device_t pit;

        bm_pit_exact_reset(&pit);
        program_channel(&pit, 0U, 2U, 0, counts[index]);
        program_channel(&pit, 1U, 3U, 0, counts[index]);
        assert_batch_matches_reference(&pit, 150000U);
    }
}

static void
test_mixed_channels_and_gate_states_match(void)
{
    bm_pit_exact_device_t pit;

    bm_pit_exact_reset(&pit);
    program_channel(&pit, 0U, 2U, 0, 0x1235U);
    program_channel(&pit, 1U, 3U, 0, 0x0400U);
    program_channel(&pit, 2U, 4U, 1, 0x0999U);
    assert_batch_matches_reference(&pit, 250000U);

    bm_pit_exact_set_gate(&pit, 0U, false);
    bm_pit_exact_set_gate(&pit, 1U, false);
    bm_pit_exact_set_gate(&pit, 2U, false);
    assert_batch_matches_reference(&pit, 250000U);
}

static void
test_latches_partial_writes_and_unusual_bcd_match(void)
{
    bm_pit_exact_device_t pit;
    unsigned int tick;

    bm_pit_exact_reset(&pit);
    program_channel(&pit, 0U, 2U, 0, 0x1235U);
    for (tick = 0U; tick < 20U; ++tick)
        bm_pit_exact_tick(&pit);
    bm_pit_exact_control_write(&pit, 0x00U);
    assert_batch_matches_reference(&pit, 100000U);

    bm_pit_exact_reset(&pit);
    bm_pit_exact_set_gate(&pit, 0U, true);
    bm_pit_exact_control_write(&pit, 0x30U);
    bm_pit_exact_data_write(&pit, 0U, 0x34U);
    assert_batch_matches_reference(&pit, 70000U);
    bm_pit_exact_data_write(&pit, 0U, 0x12U);
    assert_batch_matches_reference(&pit, 100000U);

    bm_pit_exact_reset(&pit);
    program_channel(&pit, 0U, 2U, 1, 0xfa3cU);
    program_channel(&pit, 1U, 3U, 1, 0x9a17U);
    assert_batch_matches_reference(&pit, 50000U);
}

static void
test_simultaneous_transitions_match(void)
{
    bm_pit_exact_device_t pit;

    bm_pit_exact_reset(&pit);
    program_channel(&pit, 0U, 2U, 0, 4U);
    program_channel(&pit, 1U, 3U, 0, 6U);
    assert(bm_pit_exact_cycles_until_output_change(&pit) == 4U);
    assert_batch_matches_reference(&pit, 100000U);
    assert(bm_pit_exact_advance_until_output_change(NULL, 1U) == 0U);
    assert(bm_pit_exact_advance_until_output_change(&pit, 0U) == 0U);
    assert(bm_pit_exact_cycles_until_output_change(NULL) == 0U);
}

static void
test_mode3_low_half_wrap_boundary_matches(void)
{
    bm_pit_exact_device_t pit;
    bm_pit_exact_channel_t *channel;

    bm_pit_exact_reset(&pit);
    channel = &pit.channel[0];
    channel->mode = 3U;
    channel->count_register = 3U;
    channel->counting_element = 1U;
    channel->gate = true;
    channel->output = false;
    channel->state = BM_PIT_COUNTING;
    assert(bm_pit_exact_cycles_until_output_change(&pit) == 0x8000U);
    assert_batch_matches_reference(&pit, 70000U);
}

int
main(void)
{
    test_each_mode_matches_edge_reference();
    test_even_odd_and_zero_periods_match();
    test_mixed_channels_and_gate_states_match();
    test_latches_partial_writes_and_unusual_bcd_match();
    test_simultaneous_transitions_match();
    test_mode3_low_half_wrap_boundary_matches();
    return 0;
}
