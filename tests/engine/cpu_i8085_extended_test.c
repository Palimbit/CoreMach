/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Extended (undocumented) 8085 instructions. A strict host keeps rejecting
 * them; a tolerant host executes them as [DS79] describes, with the timing
 * of [DS79]/[TUN97], and receives one report per opcode. Flag cases that
 * [DS79] and [KS1302] do not establish are checked as implemented and
 * documented as unconfirmed in doc/architecture/intel-8085-cpu.md. */
#include "cpu_i8085_test_harness.h"

#include <assert.h>
#include <string.h>

typedef struct report_log {
    unsigned int count;
    bm_tolerance_event_t last;
    unsigned int per_opcode[256];
} report_log_t;

static void
record_report(void *context, const bm_tolerance_event_t *event)
{
    report_log_t *log = context;
    ++log->count;
    log->last = *event;
    ++log->per_opcode[event->opcode & 0xffU];
}

static bm_i8085_timing_observation_t last_timing;

static void
record_timing(void *context, const bm_i8085_timing_observation_t *observation)
{
    (void) context;
    last_timing = *observation;
}

static report_log_t reports;
static const bm_host_tolerance_services_t tolerance = { record_report, &reports };

static void
create(i8085_test_machine_t *machine, bm_i8085_model_t model, int tolerant)
{
    i8085_test_config_t config;
    memset(&config, 0, sizeof(config));
    config.model = model;
    config.timing = record_timing;
    config.tolerance = tolerant ? &tolerance : NULL;
    memset(&reports, 0, sizeof(reports));
    i8085_test_create(machine, &config);
}

static bm_i8085_arch_state_t
prepare(i8085_test_machine_t *machine, const uint8_t *program, size_t size)
{
    bm_i8085_arch_state_t state;
    i8085_test_load(machine, 0x0100U, program, size);
    state = i8085_test_state(machine);
    state.pc = 0x0100U;
    state.sp = 0xf000U;
    state.f = 0x00U;
    return state;
}

static void
expect_timing(uint32_t t_states, uint8_t machine_cycles, int taken)
{
    assert(last_timing.clock_kind == BM_I8085_CLOCKS_EXACT);
    assert(last_timing.t_states == t_states);
    assert(last_timing.machine_cycles == machine_cycles);
    assert(last_timing.branch_taken == (taken ? 1U : 0U));
}

static void
test_strict_host_rejects(void)
{
    static const uint8_t opcodes[10] = {
        0x08U, 0x10U, 0x18U, 0x28U, 0x38U, 0xcbU, 0xd9U, 0xddU, 0xedU, 0xfdU
    };
    unsigned int index;

    for (index = 0U; index < 10U; ++index) {
        i8085_test_machine_t machine;
        bm_i8085_arch_state_t before;
        bm_i8085_arch_state_t after;
        bm_tick_t consumed = 0U;

        create(&machine, BM_I8085_INTEL_8085A, 0);
        before = prepare(&machine, &opcodes[index], 1U);
        before.f = BM_I8085_FLAG_V | BM_I8085_FLAG_K;
        i8085_test_set_state(&machine, &before);
        assert(bm_i8085_step(&machine.cpu, &consumed) == BM_STATUS_UNSUPPORTED);
        after = i8085_test_state(&machine);
        assert(after.pc == 0x0100U);
        assert(after.f == before.f);
        assert(reports.count == 0U);
        i8085_test_destroy(&machine);
    }
}

static void
test_dsub(void)
{
    static const uint8_t program[] = { 0x08U, 0x08U, 0x08U };
    i8085_test_machine_t machine;
    bm_i8085_arch_state_t state;

    create(&machine, BM_I8085_INTEL_8085A, 1);
    state = prepare(&machine, program, sizeof(program));
    state.h = 0x12U; state.l = 0x34U; state.b = 0x02U; state.c = 0x35U;
    i8085_test_set_state(&machine, &state);
    i8085_test_step(&machine);                      /* 1234h - 0235h */
    state = i8085_test_state(&machine);
    assert((state.h == 0x0fU) && (state.l == 0xffU));
    assert((state.f & (BM_I8085_FLAG_CY | BM_I8085_FLAG_Z | BM_I8085_FLAG_S)) == 0U);
    expect_timing(10U, 3U, 0);

    state.b = 0x0fU; state.c = 0xffU;               /* equal: zero */
    i8085_test_set_state(&machine, &state);
    i8085_test_step(&machine);
    state = i8085_test_state(&machine);
    assert((state.h == 0x00U) && (state.l == 0x00U));
    assert((state.f & BM_I8085_FLAG_Z) != 0U);
    assert((state.f & BM_I8085_FLAG_CY) == 0U);

    state.b = 0x00U; state.c = 0x01U;               /* 0000h - 0001h */
    i8085_test_set_state(&machine, &state);
    i8085_test_step(&machine);
    state = i8085_test_state(&machine);
    assert((state.h == 0xffU) && (state.l == 0xffU));
    assert((state.f & BM_I8085_FLAG_CY) != 0U);
    assert((state.f & BM_I8085_FLAG_S) != 0U);
    assert((state.f & BM_I8085_FLAG_Z) == 0U);
    assert(reports.per_opcode[0x08U] == 1U);        /* once per CPU */
    i8085_test_destroy(&machine);
}

static void
test_shifts_and_rotates(void)
{
    static const uint8_t program[] = { 0x10U, 0x18U, 0x18U, 0x18U };
    i8085_test_machine_t machine;
    bm_i8085_arch_state_t state;

    create(&machine, BM_I8085_INTEL_8085A, 1);
    state = prepare(&machine, program, sizeof(program));
    state.h = 0x80U; state.l = 0x03U;
    state.f = BM_I8085_FLAG_Z | BM_I8085_FLAG_V;
    i8085_test_set_state(&machine, &state);
    i8085_test_step(&machine);                      /* ARHL: only CY */
    state = i8085_test_state(&machine);
    assert((state.h == 0xc0U) && (state.l == 0x01U));
    assert(state.f == (BM_I8085_FLAG_Z | BM_I8085_FLAG_V | BM_I8085_FLAG_CY));
    expect_timing(7U, 2U, 0);

    state.d = 0x80U; state.e = 0x01U;               /* RDEL with CY set */
    i8085_test_set_state(&machine, &state);
    i8085_test_step(&machine);
    state = i8085_test_state(&machine);
    assert((state.d == 0x00U) && (state.e == 0x03U));
    assert((state.f & BM_I8085_FLAG_CY) != 0U);
    assert((state.f & BM_I8085_FLAG_V) != 0U);      /* bit 15 changed */
    assert((state.f & BM_I8085_FLAG_Z) != 0U);      /* not affected */
    expect_timing(10U, 3U, 0);

    state.d = 0x10U; state.e = 0x00U; state.f = 0x00U;
    i8085_test_set_state(&machine, &state);
    i8085_test_step(&machine);
    state = i8085_test_state(&machine);
    assert((state.d == 0x20U) && (state.e == 0x00U));
    assert((state.f & (BM_I8085_FLAG_CY | BM_I8085_FLAG_V)) == 0U);

    state.d = 0x40U; state.e = 0x00U;
    i8085_test_set_state(&machine, &state);
    i8085_test_step(&machine);
    state = i8085_test_state(&machine);
    assert((state.d == 0x80U) && ((state.f & BM_I8085_FLAG_V) != 0U));
    i8085_test_destroy(&machine);
}

static void
test_offsets_and_indirect(void)
{
    static const uint8_t program[] = {
        0x28U, 0x05U,                               /* LDHI 05h */
        0x38U, 0x02U,                               /* LDSI 02h */
        0xd9U,                                      /* SHLX */
        0xedU                                       /* LHLX */
    };
    i8085_test_machine_t machine;
    bm_i8085_arch_state_t state;

    create(&machine, BM_I8085_INTEL_8085A, 1);
    state = prepare(&machine, program, sizeof(program));
    state.h = 0x10U; state.l = 0xfeU; state.f = BM_I8085_FLAG_S;
    i8085_test_set_state(&machine, &state);
    i8085_test_step(&machine);
    state = i8085_test_state(&machine);
    assert((state.d == 0x11U) && (state.e == 0x03U) && (state.pc == 0x0102U));
    assert(state.f == BM_I8085_FLAG_S);
    expect_timing(10U, 3U, 0);

    i8085_test_step(&machine);                      /* SP F000h + 2 */
    state = i8085_test_state(&machine);
    assert((state.d == 0xf0U) && (state.e == 0x02U));

    state.d = 0x20U; state.e = 0x00U; state.h = 0xabU; state.l = 0xcdU;
    i8085_test_set_state(&machine, &state);
    i8085_test_clear_log(&machine);
    i8085_test_step(&machine);                      /* SHLX */
    assert((machine.memory[0x2000U] == 0xcdU) && (machine.memory[0x2001U] == 0xabU));
    assert(machine.log_count == 3U);                /* fetch, L, H */
    expect_timing(10U, 3U, 0);

    machine.memory[0x2000U] = 0x34U;
    machine.memory[0x2001U] = 0x12U;
    i8085_test_step(&machine);                      /* LHLX */
    state = i8085_test_state(&machine);
    assert((state.h == 0x12U) && (state.l == 0x34U));
    assert(state.f == BM_I8085_FLAG_S);
    expect_timing(10U, 3U, 0);
    i8085_test_destroy(&machine);
}

static void
test_conditional_forms(void)
{
    static const uint8_t program[] = {
        0xcbU,                                      /* RSTV, V clear */
        0xcbU,                                      /* RSTV, V set */
    };
    static const uint8_t jumps[] = {
        0xfdU, 0x00U, 0x30U,                        /* JK 3000h */
        0xddU, 0x00U, 0x40U,                        /* JNK 4000h */
    };
    i8085_test_machine_t machine;
    bm_i8085_arch_state_t state;

    create(&machine, BM_I8085_INTEL_8085A, 1);
    state = prepare(&machine, program, sizeof(program));
    i8085_test_set_state(&machine, &state);
    i8085_test_step(&machine);
    state = i8085_test_state(&machine);
    assert((state.pc == 0x0101U) && (state.sp == 0xf000U));
    expect_timing(6U, 1U, 0);

    state.f = BM_I8085_FLAG_V;
    i8085_test_set_state(&machine, &state);
    i8085_test_step(&machine);
    state = i8085_test_state(&machine);
    assert((state.pc == 0x0040U) && (state.sp == 0xeffeU));
    assert((machine.memory[0xeffeU] == 0x02U) && (machine.memory[0xefffU] == 0x01U));
    expect_timing(12U, 3U, 1);
    i8085_test_destroy(&machine);

    /* JK and JNK: a false condition does not read the third byte. */
    create(&machine, BM_I8085_INTEL_8085A, 1);
    state = prepare(&machine, jumps, sizeof(jumps));
    i8085_test_set_state(&machine, &state);         /* K clear */
    i8085_test_clear_log(&machine);
    i8085_test_step(&machine);                      /* JK not taken */
    state = i8085_test_state(&machine);
    assert(state.pc == 0x0103U);
    assert(machine.log_count == 2U);
    expect_timing(7U, 2U, 0);
    i8085_test_step(&machine);                      /* JNK taken */
    state = i8085_test_state(&machine);
    assert(state.pc == 0x4000U);
    expect_timing(10U, 3U, 1);

    state.pc = 0x0100U;
    state.f = BM_I8085_FLAG_K;
    i8085_test_set_state(&machine, &state);
    i8085_test_step(&machine);                      /* JK taken */
    state = i8085_test_state(&machine);
    assert(state.pc == 0x3000U);
    expect_timing(10U, 3U, 1);
    state.pc = 0x0103U;
    i8085_test_set_state(&machine, &state);
    i8085_test_step(&machine);                      /* JNK not taken */
    state = i8085_test_state(&machine);
    assert(state.pc == 0x0106U);
    expect_timing(7U, 2U, 0);
    i8085_test_destroy(&machine);
}

static void
test_reports(void)
{
    static const uint8_t program[] = { 0x10U, 0x10U, 0xedU };
    i8085_test_machine_t machine;
    bm_i8085_arch_state_t state;

    create(&machine, BM_I8085_INTEL_8085A, 1);
    state = prepare(&machine, program, sizeof(program));
    i8085_test_set_state(&machine, &state);
    i8085_test_step(&machine);
    assert(reports.count == 1U);
    assert(reports.last.size == sizeof(bm_tolerance_event_t));
    assert(reports.last.kind == BM_TOLERANCE_UNDOCUMENTED_INSTRUCTION);
    assert(reports.last.flags == BM_TOLERANCE_FLAG_HAS_PC);
    assert((reports.last.pc == 0x0100U) && (reports.last.opcode == 0x10U));
    assert(strcmp(reports.last.device, "Intel 8085A") == 0);
    i8085_test_step(&machine);                      /* same opcode again */
    assert(reports.count == 1U);
    i8085_test_step(&machine);                      /* another opcode */
    assert((reports.count == 2U) && (reports.last.opcode == 0xedU));
    i8085_test_destroy(&machine);

    /* The OKI part executes the same semantics, marked as assumed. */
    create(&machine, BM_I8085_OKI_MSM80C85A, 1);
    state = prepare(&machine, program, 1U);
    state.h = 0x00U; state.l = 0x02U;
    i8085_test_set_state(&machine, &state);
    i8085_test_step(&machine);
    state = i8085_test_state(&machine);
    assert(state.l == 0x01U);
    assert(reports.count == 1U);
    assert(reports.last.flags ==
           (BM_TOLERANCE_FLAG_HAS_PC | BM_TOLERANCE_FLAG_ASSUMED_BEHAVIOUR));
    assert(strcmp(reports.last.device, "OKI MSM80C85A") == 0);
    i8085_test_destroy(&machine);
}

int
main(void)
{
    test_strict_host_rejects();
    test_dsub();
    test_shifts_and_rotates();
    test_offsets_and_indirect();
    test_conditional_forms();
    test_reports();
    return 0;
}
