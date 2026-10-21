/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Per-instruction 8085 timing. The expected T-states are transcribed
 * independently from the OKI MSM80C85A Table 8 (OKI 1986 Microprocessor
 * Data Book, pp. 30-32), which matches Intel MCS-80/85 §5.6. Conditional
 * entries are not-taken/taken. Zero marks undocumented opcodes. */
#include "cpu_i8085_test_harness.h"

#include <blumach/engine/engine.h>

#include <assert.h>
#include <string.h>

static const uint8_t not_taken_t[256] = {
/*       0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F */
/* 0 */  4, 10,  7,  6,  4,  4,  7,  4,  0, 10,  7,  6,  4,  4,  7,  4,
/* 1 */  0, 10,  7,  6,  4,  4,  7,  4,  0, 10,  7,  6,  4,  4,  7,  4,
/* 2 */  4, 10, 16,  6,  4,  4,  7,  4,  0, 10, 16,  6,  4,  4,  7,  4,
/* 3 */  4, 10, 13,  6, 10, 10, 10,  4,  0, 10, 13,  6,  4,  4,  7,  4,
/* 4 */  4,  4,  4,  4,  4,  4,  7,  4,  4,  4,  4,  4,  4,  4,  7,  4,
/* 5 */  4,  4,  4,  4,  4,  4,  7,  4,  4,  4,  4,  4,  4,  4,  7,  4,
/* 6 */  4,  4,  4,  4,  4,  4,  7,  4,  4,  4,  4,  4,  4,  4,  7,  4,
/* 7 */  7,  7,  7,  7,  7,  7,  5,  7,  4,  4,  4,  4,  4,  4,  7,  4,
/* 8 */  4,  4,  4,  4,  4,  4,  7,  4,  4,  4,  4,  4,  4,  4,  7,  4,
/* 9 */  4,  4,  4,  4,  4,  4,  7,  4,  4,  4,  4,  4,  4,  4,  7,  4,
/* A */  4,  4,  4,  4,  4,  4,  7,  4,  4,  4,  4,  4,  4,  4,  7,  4,
/* B */  4,  4,  4,  4,  4,  4,  7,  4,  4,  4,  4,  4,  4,  4,  7,  4,
/* C */  6, 10,  7, 10,  9, 12,  7, 12,  6, 10,  7,  0,  9, 18,  7, 12,
/* D */  6, 10,  7, 10,  9, 12,  7, 12,  6,  0,  7, 10,  9,  0,  7, 12,
/* E */  6, 10,  7, 16,  9, 12,  7, 12,  6,  6,  7,  4,  9,  0,  7, 12,
/* F */  6, 10,  7,  4,  9, 12,  7, 12,  6,  6,  7,  4,  9,  0,  7, 12,
};

static int
is_conditional(uint8_t op)
{
    return ((op & 0xc7U) == 0xc0U) || ((op & 0xc7U) == 0xc2U) ||
           ((op & 0xc7U) == 0xc4U);
}

static uint8_t
taken_t(uint8_t op)
{
    if ((op & 0xc7U) == 0xc0U)
        return 12U;
    if ((op & 0xc7U) == 0xc2U)
        return 10U;
    return 18U;
}

typedef struct observer {
    unsigned int count;
    bm_i8085_timing_observation_t last;
} observer_t;

static void
observe(void *context, const bm_i8085_timing_observation_t *observation)
{
    observer_t *observer = context;
    ++observer->count;
    observer->last = *observation;
}

/* Flags for which condition (op >> 3) & 7 is true or false. */
static uint8_t
flags_for(uint8_t op, int take)
{
    static const uint8_t flag[4] = {
        BM_I8085_FLAG_Z, BM_I8085_FLAG_CY, BM_I8085_FLAG_P, BM_I8085_FLAG_S
    };
    unsigned int condition = (op >> 3U) & 7U;
    int set_when_true = (condition & 1U) != 0U;
    return (take == set_when_true) ? flag[condition >> 1U] : 0U;
}

static void
measure(uint8_t op, int take, uint32_t wait_states)
{
    static const uint8_t operands[] = { 0x34, 0x12 };
    i8085_test_machine_t machine;
    i8085_test_config_t config = { 0 };
    observer_t observer = { 0 };
    bm_i8085_arch_state_t state;
    uint8_t expected = take ? taken_t(op) : not_taken_t[op];
    uint8_t expected_cycles;
    uint64_t cycles = 0U;

    config.timing = observe;
    config.timing_context = &observer;
    i8085_test_create(&machine, &config);
    machine.memory[0x1000U] = op;
    i8085_test_load(&machine, 0x1001U, operands, sizeof(operands));
    machine.memory_wait_states = wait_states;
    machine.io_wait_states = wait_states;
    state = i8085_test_state(&machine);
    state.pc = 0x1000U;
    state.sp = 0x8000U;
    state.h = 0x90U;
    state.f = is_conditional(op) ? flags_for(op, take) : 0U;
    i8085_test_set_state(&machine, &state);
    assert(bm_i8085_step_clocked(machine.cpu.context, 0U, &cycles) ==
           BM_STATUS_OK);
    assert(observer.count == 1U);
    assert(observer.last.size == sizeof(observer.last));
    assert(observer.last.version == BM_I8085_TIMING_OBSERVATION_VERSION);
    assert(observer.last.kind == BM_I8085_BOUNDARY_INSTRUCTION);
    assert(observer.last.opcode == op);
    assert(observer.last.clock_kind == BM_I8085_CLOCKS_EXACT);
    assert(observer.last.t_states == expected);
    assert(observer.last.branch_taken == (is_conditional(op) ? take : 0));
    assert(observer.last.reported_wait_states ==
           observer.last.logical_bus_transactions * wait_states);
    assert(cycles == expected + observer.last.reported_wait_states);
    /* Every machine cycle is a bus transfer except the two Bus Idle cycles
     * of DAD ([MCS85] §2.3.5) and the HALT machine cycle. */
    expected_cycles = (uint8_t) observer.last.logical_bus_transactions;
    if ((op & 0xcfU) == 0x09U)
        expected_cycles = (uint8_t) (expected_cycles + 2U);
    if (op == 0x76U)
        expected_cycles = (uint8_t) (expected_cycles + 1U);
    assert(observer.last.machine_cycles == expected_cycles);
    i8085_test_destroy(&machine);
}

static void
test_every_documented_opcode(void)
{
    unsigned int op;

    for (op = 0U; op < 256U; ++op) {
        if (not_taken_t[op] == 0U) {
            assert(bm_i8085_classify_opcode((uint8_t) op) ==
                   BM_I8085_OPCODE_UNDOCUMENTED);
            continue;
        }
        measure((uint8_t) op, 0, 0U);
        measure((uint8_t) op, 0, 2U);
        if (is_conditional((uint8_t) op)) {
            measure((uint8_t) op, 1, 0U);
            measure((uint8_t) op, 1, 1U);
        }
    }
}

/* The 8085 skips the third byte of a false conditional ([ASM85] p. 1-25)
 * and CALL writes PCH to SP-1 before PCL to SP-2 ([MCS85] Figure 2-18). */
static void
test_conditional_and_call_bus_order(void)
{
    static const uint8_t program[] = {
        0xc2, 0x00, 0x20,   /* JNZ 2000h with Z set: not taken */
        0xcc, 0x00, 0x30,   /* CZ 3000h: taken */
    };
    i8085_test_machine_t machine;
    bm_i8085_arch_state_t state;

    i8085_test_create(&machine, NULL);
    i8085_test_load(&machine, 0x0000U, program, sizeof(program));
    state = i8085_test_state(&machine);
    state.f = BM_I8085_FLAG_Z;
    state.sp = 0x8000U;
    i8085_test_set_state(&machine, &state);
    i8085_test_clear_log(&machine);
    i8085_test_step(&machine);
    assert(machine.log_count == 2U);
    assert(machine.log[0].operation == BM_BUS_FETCH && machine.log[0].address == 0U);
    assert(machine.log[1].operation == BM_BUS_READ && machine.log[1].address == 1U);
    assert(i8085_test_state(&machine).pc == 0x0003U);
    i8085_test_clear_log(&machine);
    i8085_test_step(&machine);
    assert(machine.log_count == 5U);
    assert(machine.log[3].operation == BM_BUS_WRITE &&
           machine.log[3].address == 0x7fffU && machine.log[3].value == 0x00U);
    assert(machine.log[4].operation == BM_BUS_WRITE &&
           machine.log[4].address == 0x7ffeU && machine.log[4].value == 0x06U);
    assert(i8085_test_state(&machine).pc == 0x3000U);
    i8085_test_destroy(&machine);
}

/* In a clocked engine at 2.4576 MHz each NOP lasts 4 CLK = 1627.6 ns. */
static void
test_clocked_engine_advances_virtual_time(void)
{
    bm_engine_config_t engine_config = { 1U, 1U, 0U };
    bm_clock_rate_t rate = { 2457600U, 1U };
    i8085_test_machine_t machine;
    bm_engine_t *engine = NULL;

    i8085_test_create(&machine, NULL);
    assert(bm_engine_create_clocked(&machine.host, &engine_config, &engine) ==
           BM_STATUS_OK);
    assert(bm_engine_add_clocked_cpu(engine, &machine.cpu,
                                     bm_i8085_step_clocked, &rate, NULL) ==
           BM_STATUS_OK);
    assert(bm_engine_run_for(engine, 6000U) == BM_STATUS_OK);
    assert(i8085_test_state(&machine).pc == 4U);
    assert(bm_engine_run_for(engine, 1000U) == BM_STATUS_OK);
    assert(i8085_test_state(&machine).pc == 5U);
    bm_engine_destroy(engine);
    machine.cpu.ops.destroy = NULL;
    i8085_test_destroy(&machine);
}

int
main(void)
{
    test_every_documented_opcode();
    test_conditional_and_call_bus_order();
    test_clocked_engine_advances_virtual_time();
    return 0;
}
