/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "cpu_808x_test_harness.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#define FLAG_ZF 0x0040U

typedef struct cpu_result {
    uint64_t cx;
    uint64_t si;
    uint64_t di;
    uint64_t flags;
    uint64_t halted;
    bm_808x_trace_t prefixed_trace;
    int saw_prefixed_trace;
} cpu_result_t;

typedef struct memory_region {
    uint64_t address;
    const uint8_t *data;
    size_t size;
} memory_region_t;

static void
capture_trace(void *context, const bm_808x_trace_t *trace)
{
    cpu_result_t *result = context;
    if (trace->prefix_count != 0U) {
        result->prefixed_trace = *trace;
        result->saw_prefixed_trace = 1;
    }
}

static cpu_result_t
run_program(const uint8_t *program,
            size_t program_size,
            const memory_region_t *regions,
            size_t region_count)
{
    cpu_result_t result = { 0 };
    cpu_808x_test_config_t config = { 0 };
    cpu_808x_test_machine_t machine;
    size_t index;

    config.trace = capture_trace;
    config.trace_context = &result;
    cpu_808x_test_machine_create(&machine, &config, program, program_size);
    for (index = 0U; index < region_count; ++index) {
        cpu_808x_test_write(&machine, regions[index].address,
                            regions[index].data, regions[index].size);
    }
    assert(cpu_808x_test_run(&machine, 64U) == BM_STATUS_OK);
    result.cx = cpu_808x_test_inspect(&machine, "cx");
    result.si = cpu_808x_test_inspect(&machine, "si");
    result.di = cpu_808x_test_inspect(&machine, "di");
    result.flags = cpu_808x_test_inspect(&machine, "flags");
    result.halted = cpu_808x_test_inspect(&machine, "halted");
    cpu_808x_test_machine_destroy(&machine);
    return result;
}

static void
test_repe_stops_on_mismatch(void)
{
    static const uint8_t program[] = {
        0xbe, 0x00, 0x01, 0xbf, 0x00, 0x02, 0xb9, 0x04, 0x00,
        0xf3, 0xa6, 0xf4
    };
    static const uint8_t source[] = { 0x11U, 0x22U, 0x33U };
    static const uint8_t destination[] = { 0x11U, 0x22U, 0x44U };
    static const memory_region_t regions[] = {
        { 0x0100U, source, sizeof(source) },
        { 0x0200U, destination, sizeof(destination) }
    };
    cpu_result_t result = run_program(program, sizeof(program), regions,
                                      sizeof(regions) / sizeof(regions[0]));

    assert(result.halted == 1U);
    assert(result.cx == 1U);
    assert(result.si == 0x0103U && result.di == 0x0203U);
    assert((result.flags & FLAG_ZF) == 0U);
}

static void
test_repne_stops_on_equality(void)
{
    static const uint8_t program[] = {
        0xbe, 0x00, 0x01, 0xbf, 0x00, 0x02, 0xb9, 0x03, 0x00,
        0xf2, 0xa6, 0xf4
    };
    static const uint8_t source[] = { 0x11U, 0x22U };
    static const uint8_t destination[] = { 0x33U, 0x22U };
    static const memory_region_t regions[] = {
        { 0x0100U, source, sizeof(source) },
        { 0x0200U, destination, sizeof(destination) }
    };
    cpu_result_t result = run_program(program, sizeof(program), regions,
                                      sizeof(regions) / sizeof(regions[0]));

    assert(result.halted == 1U);
    assert(result.cx == 1U);
    assert(result.si == 0x0102U && result.di == 0x0202U);
    assert((result.flags & FLAG_ZF) != 0U);
}

static void
test_direction_and_word_width(void)
{
    static const uint8_t reverse_program[] = {
        0xbe, 0x01, 0x01, 0xbf, 0x01, 0x02, 0xb9, 0x02, 0x00,
        0xb8, 0x02, 0x04, 0x50, 0x9d, 0xf3, 0xa6, 0xf4
    };
    static const uint8_t word_program[] = {
        0xbe, 0x00, 0x01, 0xbf, 0x00, 0x02, 0xa7, 0xf4
    };
    static const uint8_t reverse_data[] = { 0x11U, 0x22U };
    static const uint8_t word_data[] = { 0x34U, 0x12U };
    static const memory_region_t reverse_regions[] = {
        { 0x0100U, reverse_data, sizeof(reverse_data) },
        { 0x0200U, reverse_data, sizeof(reverse_data) }
    };
    static const memory_region_t word_regions[] = {
        { 0x0100U, word_data, sizeof(word_data) },
        { 0x0200U, word_data, sizeof(word_data) }
    };
    cpu_result_t result = run_program(
        reverse_program, sizeof(reverse_program), reverse_regions,
        sizeof(reverse_regions) / sizeof(reverse_regions[0]));

    assert(result.halted == 1U);
    assert(result.cx == 0U);
    assert(result.si == 0x00ffU && result.di == 0x01ffU);
    assert((result.flags & FLAG_ZF) != 0U);

    result = run_program(word_program, sizeof(word_program), word_regions,
                         sizeof(word_regions) / sizeof(word_regions[0]));
    assert(result.halted == 1U);
    assert(result.si == 0x0102U && result.di == 0x0202U);
    assert((result.flags & FLAG_ZF) != 0U);
}

static void
test_source_override_and_zero_count(void)
{
    static const uint8_t override_program[] = {
        0xb8, 0x10, 0x00, 0x8e, 0xd8, 0xb8, 0x20, 0x00, 0x8e, 0xc0,
        0xbe, 0x00, 0x01, 0xbf, 0x00, 0x02, 0xb9, 0x01, 0x00,
        0x2e, 0xf3, 0xa6, 0xf4
    };
    static const uint8_t zero_program[] = {
        0xbe, 0x00, 0x01, 0xbf, 0x00, 0x02, 0xb9, 0x00, 0x00,
        0xb8, 0x00, 0x40, 0x9e, 0xf2, 0xa6, 0xf4
    };
    static const uint8_t cs_source[] = { 0x5aU };
    static const uint8_t ds_source[] = { 0x99U };
    static const uint8_t destination[] = { 0x5aU };
    static const uint8_t unused_destination[] = { 0x00U };
    static const memory_region_t regions[] = {
        { 0xf0100U, cs_source, sizeof(cs_source) },
        { 0x0200U, ds_source, sizeof(ds_source) },
        { 0x0400U, destination, sizeof(destination) },
        { 0xf0200U, unused_destination, sizeof(unused_destination) }
    };
    cpu_result_t result = run_program(override_program,
                                      sizeof(override_program), regions,
                                      sizeof(regions) / sizeof(regions[0]));

    assert(result.halted == 1U);
    assert(result.cx == 0U);
    assert(result.si == 0x0101U && result.di == 0x0201U);
    assert((result.flags & FLAG_ZF) != 0U);
    assert(result.saw_prefixed_trace);
    assert(result.prefixed_trace.opcode == 0x2eU);
    assert(result.prefixed_trace.effective_opcode == 0xa6U);
    assert(result.prefixed_trace.prefix_count == 2U);

    result = run_program(zero_program, sizeof(zero_program), NULL, 0U);
    assert(result.halted == 1U);
    assert(result.cx == 0U);
    assert(result.si == 0x0100U && result.di == 0x0200U);
    assert((result.flags & FLAG_ZF) != 0U);
}

int
main(void)
{
    test_repe_stops_on_mismatch();
    test_repne_stops_on_equality();
    test_direction_and_word_width();
    test_source_override_and_zero_count();
    return 0;
}
