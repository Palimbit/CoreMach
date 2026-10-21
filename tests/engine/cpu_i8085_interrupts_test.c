/* SPDX-License-Identifier: GPL-2.0-or-later */
/* 8085 interrupt system, RIM/SIM, SID/SOD, HALT, RESET IN and READY.
 * References: Intel MCS-80/85 Family User's Manual §2.3 and pp. 5-17..5-18;
 * Intel 8080/8085 Assembly Language Programming Manual p. 1-24; OKI
 * MSM80C85A Table 1. */
#include "cpu_i8085_test_harness.h"

#include <blumach/engine/engine.h>

#include <assert.h>
#include <string.h>

typedef struct recorder {
    unsigned int count;
    bm_i8085_timing_observation_t last;
    unsigned int ack_calls;
    unsigned int ack_cycles[4];
    uint8_t ack_bytes[3];
    bm_status_t ack_status;
    unsigned int sod_calls;
    int sod_level;
    uint32_t sod_delay;
} recorder_t;

static void
observe(void *context, const bm_i8085_timing_observation_t *observation)
{
    recorder_t *recorder = context;
    ++recorder->count;
    recorder->last = *observation;
}

static bm_status_t
acknowledge(void *context, unsigned int cycle, uint8_t *value)
{
    recorder_t *recorder = context;
    assert(cycle < 3U);
    if (recorder->ack_calls < 4U)
        recorder->ack_cycles[recorder->ack_calls] = cycle;
    ++recorder->ack_calls;
    *value = recorder->ack_bytes[cycle];
    return recorder->ack_status;
}

static void
sod_changed(void *context, int level, uint32_t half_clocks)
{
    recorder_t *recorder = context;
    ++recorder->sod_calls;
    recorder->sod_level = level;
    recorder->sod_delay = half_clocks;
}

/* All memory is NOP. PC starts at 1000h, SP at 8000h, IE set, masks clear. */
static void
start(i8085_test_machine_t *machine, recorder_t *recorder, int with_ack)
{
    i8085_test_config_t config = { 0 };
    bm_i8085_arch_state_t state;

    memset(recorder, 0, sizeof(*recorder));
    config.timing = observe;
    config.timing_context = recorder;
    config.sod = sod_changed;
    config.sod_context = recorder;
    if (with_ack) {
        config.interrupt_ack = acknowledge;
        config.interrupt_context = recorder;
    }
    i8085_test_create(machine, &config);
    state = i8085_test_state(machine);
    state.pc = 0x1000U;
    state.sp = 0x8000U;
    state.interrupt_enable = 1U;
    state.interrupt_mask = 0U;
    i8085_test_set_state(machine, &state);
}

static void
enable_interrupts(i8085_test_machine_t *machine)
{
    bm_i8085_arch_state_t state = i8085_test_state(machine);
    state.interrupt_enable = 1U;
    state.interrupt_enable_delay = 0U;
    i8085_test_set_state(machine, &state);
}

static void
expect_interrupt(i8085_test_machine_t *machine, recorder_t *recorder,
                 bm_i8085_interrupt_source_t source, uint16_t vector,
                 uint16_t return_address)
{
    bm_i8085_arch_state_t state;
    uint16_t sp;

    i8085_test_step(machine);
    state = i8085_test_state(machine);
    assert(recorder->last.kind == BM_I8085_BOUNDARY_INTERRUPT);
    assert(recorder->last.interrupt_source == source);
    assert(state.pc == vector);
    assert(state.interrupt_enable == 0U);
    sp = state.sp;
    assert(machine->memory[sp] == (uint8_t) return_address);
    assert(machine->memory[(uint16_t) (sp + 1U)] ==
           (uint8_t) (return_address >> 8U));
}

/* TRAP > RST 7.5 > RST 6.5 > RST 5.5 > INTR, vectors 24h/3Ch/34h/2Ch. */
static void
test_priority_and_vectors(void)
{
    i8085_test_machine_t machine;
    recorder_t recorder;

    start(&machine, &recorder, 1);
    recorder.ack_bytes[0] = 0xdfU; /* RST 3 */
    i8085_test_signal(&machine, BM_I8085_SIGNAL_INTR, 1);
    i8085_test_signal(&machine, BM_I8085_SIGNAL_RST55, 1);
    i8085_test_signal(&machine, BM_I8085_SIGNAL_RST65, 1);
    i8085_test_signal(&machine, BM_I8085_SIGNAL_RST75, 1);
    i8085_test_signal(&machine, BM_I8085_SIGNAL_TRAP, 1);

    expect_interrupt(&machine, &recorder, BM_I8085_INTERRUPT_TRAP, 0x24U, 0x1000U);
    assert(recorder.last.t_states == 12U && recorder.last.machine_cycles == 3U);
    assert(recorder.last.logical_bus_transactions == 2U);
    enable_interrupts(&machine);
    expect_interrupt(&machine, &recorder, BM_I8085_INTERRUPT_RST75, 0x3cU, 0x24U);
    assert(i8085_test_state(&machine).rst75_pending == 0U);
    enable_interrupts(&machine);
    expect_interrupt(&machine, &recorder, BM_I8085_INTERRUPT_RST65, 0x34U, 0x3cU);
    i8085_test_signal(&machine, BM_I8085_SIGNAL_RST65, 0);
    enable_interrupts(&machine);
    expect_interrupt(&machine, &recorder, BM_I8085_INTERRUPT_RST55, 0x2cU, 0x34U);
    i8085_test_signal(&machine, BM_I8085_SIGNAL_RST55, 0);
    enable_interrupts(&machine);
    expect_interrupt(&machine, &recorder, BM_I8085_INTERRUPT_INTR, 0x18U, 0x2cU);
    assert(recorder.ack_calls == 1U && recorder.last.opcode == 0xdfU);
    assert(recorder.last.t_states == 12U);
    i8085_test_destroy(&machine);
}

/* TRAP ignores IE and the masks, and is both edge and level sensitive. */
static void
test_trap_edge_and_level(void)
{
    i8085_test_machine_t machine;
    recorder_t recorder;
    bm_i8085_arch_state_t state;

    start(&machine, &recorder, 0);
    state = i8085_test_state(&machine);
    state.interrupt_enable = 0U;
    state.interrupt_mask = 7U;
    i8085_test_set_state(&machine, &state);

    /* An edge that disappears before sampling is not accepted. */
    i8085_test_signal(&machine, BM_I8085_SIGNAL_TRAP, 1);
    i8085_test_signal(&machine, BM_I8085_SIGNAL_TRAP, 0);
    i8085_test_step(&machine);
    assert(recorder.last.kind == BM_I8085_BOUNDARY_INSTRUCTION);

    i8085_test_signal(&machine, BM_I8085_SIGNAL_TRAP, 1);
    expect_interrupt(&machine, &recorder, BM_I8085_INTERRUPT_TRAP, 0x24U, 0x1001U);
    /* A level that stays high is not accepted again. */
    i8085_test_step(&machine);
    assert(recorder.last.kind == BM_I8085_BOUNDARY_INSTRUCTION);
    i8085_test_signal(&machine, BM_I8085_SIGNAL_TRAP, 0);
    i8085_test_signal(&machine, BM_I8085_SIGNAL_TRAP, 1);
    expect_interrupt(&machine, &recorder, BM_I8085_INTERRUPT_TRAP, 0x24U, 0x0025U);
    i8085_test_destroy(&machine);
}

/* The first RIM after TRAP reports the IE that existed before it; later
 * RIMs report the current IE ([MCS85] p. 5-17). */
static void
test_rim_after_trap(void)
{
    static const uint8_t rim_rim[] = { 0x20, 0x20 };
    i8085_test_machine_t machine;
    recorder_t recorder;
    bm_i8085_arch_state_t state;

    start(&machine, &recorder, 0);
    i8085_test_load(&machine, 0x0024U, rim_rim, sizeof(rim_rim));
    i8085_test_signal(&machine, BM_I8085_SIGNAL_TRAP, 1);
    i8085_test_step(&machine);
    state = i8085_test_state(&machine);
    assert(state.interrupt_enable == 0U && state.trap_saved_valid == 1U);
    i8085_test_step(&machine);
    assert((i8085_test_state(&machine).a & 0x08U) == 0x08U);
    i8085_test_step(&machine);
    assert((i8085_test_state(&machine).a & 0x08U) == 0x00U);
    assert(i8085_test_state(&machine).trap_saved_valid == 0U);
    i8085_test_destroy(&machine);
}

static void
test_masks_and_rst75_latch(void)
{
    static const uint8_t program[] = {
        0x3e, 0x0d,     /* MVI A,0Dh: MSE, mask 7.5 and 5.5 */
        0x30,           /* SIM */
        0x3e, 0x08,     /* MVI A,08h: MSE, all unmasked */
        0x30,           /* SIM */
    };
    static const uint8_t reset75[] = {
        0x3e, 0x18,     /* MVI A,18h: MSE, unmasked, reset RST 7.5 */
        0x30,           /* SIM */
    };
    i8085_test_machine_t machine;
    recorder_t recorder;
    bm_i8085_arch_state_t state;

    start(&machine, &recorder, 0);
    i8085_test_load(&machine, 0x1000U, program, sizeof(program));
    i8085_test_step(&machine);
    i8085_test_step(&machine);
    assert(i8085_test_state(&machine).interrupt_mask == 0x05U);
    /* RST 7.5 latches its edge while masked; RST 5.5 is masked. */
    i8085_test_signal(&machine, BM_I8085_SIGNAL_RST75, 1);
    i8085_test_signal(&machine, BM_I8085_SIGNAL_RST75, 0);
    i8085_test_signal(&machine, BM_I8085_SIGNAL_RST55, 1);
    i8085_test_step(&machine);
    assert(recorder.last.kind == BM_I8085_BOUNDARY_INSTRUCTION);
    assert(i8085_test_state(&machine).rst75_pending == 1U);
    i8085_test_step(&machine);
    expect_interrupt(&machine, &recorder, BM_I8085_INTERRUPT_RST75, 0x3cU, 0x1006U);
    i8085_test_signal(&machine, BM_I8085_SIGNAL_RST55, 0);
    i8085_test_destroy(&machine);

    /* SIM bit 4 clears a pending RST 7.5 request. */
    start(&machine, &recorder, 0);
    i8085_test_load(&machine, 0x1000U, reset75, sizeof(reset75));
    state = i8085_test_state(&machine);
    state.interrupt_enable = 0U;
    i8085_test_set_state(&machine, &state);
    i8085_test_signal(&machine, BM_I8085_SIGNAL_RST75, 1);
    i8085_test_step(&machine);
    i8085_test_step(&machine);
    assert(i8085_test_state(&machine).rst75_pending == 0U);
    enable_interrupts(&machine);
    i8085_test_step(&machine);
    assert(recorder.last.kind == BM_I8085_BOUNDARY_INSTRUCTION);
    i8085_test_destroy(&machine);
}

/* EI enables interrupts only after the following instruction; DI and IE=0
 * block every maskable source. */
static void
test_enable_delay_and_disable(void)
{
    static const uint8_t program[] = { 0xfb, 0x00, 0x00, 0xf3 };
    i8085_test_machine_t machine;
    recorder_t recorder;
    bm_i8085_arch_state_t state;

    start(&machine, &recorder, 0);
    i8085_test_load(&machine, 0x1000U, program, sizeof(program));
    state = i8085_test_state(&machine);
    state.interrupt_enable = 0U;
    i8085_test_set_state(&machine, &state);
    i8085_test_signal(&machine, BM_I8085_SIGNAL_RST65, 1);
    i8085_test_step(&machine);                  /* EI */
    assert(recorder.last.kind == BM_I8085_BOUNDARY_INSTRUCTION);
    assert(i8085_test_state(&machine).interrupt_enable_delay == 1U);
    i8085_test_step(&machine);                  /* NOP, still shadowed */
    assert(recorder.last.kind == BM_I8085_BOUNDARY_INSTRUCTION);
    assert(i8085_test_state(&machine).interrupt_enable_delay == 0U);
    expect_interrupt(&machine, &recorder, BM_I8085_INTERRUPT_RST65, 0x34U, 0x1002U);
    i8085_test_destroy(&machine);

    start(&machine, &recorder, 0);
    i8085_test_load(&machine, 0x1000U, program + 3, 1U);
    i8085_test_step(&machine);                  /* DI */
    i8085_test_signal(&machine, BM_I8085_SIGNAL_RST55, 1);
    i8085_test_signal(&machine, BM_I8085_SIGNAL_RST75, 1);
    i8085_test_step(&machine);
    assert(recorder.last.kind == BM_I8085_BOUNDARY_INSTRUCTION);
    assert(i8085_test_state(&machine).pc == 0x1002U);
    i8085_test_destroy(&machine);
}

/* INTR acknowledges a CALL through three INTA cycles (18 T-states), rejects
 * other opcodes and is ignored without an acknowledge callback. */
static void
test_intr_acknowledge(void)
{
    i8085_test_machine_t machine;
    recorder_t recorder;
    bm_tick_t consumed = 5U;

    start(&machine, &recorder, 1);
    recorder.ack_bytes[0] = 0xcdU;
    recorder.ack_bytes[1] = 0x34U;
    recorder.ack_bytes[2] = 0x12U;
    i8085_test_signal(&machine, BM_I8085_SIGNAL_INTR, 1);
    expect_interrupt(&machine, &recorder, BM_I8085_INTERRUPT_INTR, 0x1234U, 0x1000U);
    assert(recorder.ack_calls == 3U);
    assert(recorder.ack_cycles[0] == 0U && recorder.ack_cycles[1] == 1U &&
           recorder.ack_cycles[2] == 2U);
    assert(recorder.last.t_states == 18U && recorder.last.machine_cycles == 5U);
    assert(recorder.last.opcode == 0xcdU);
    i8085_test_destroy(&machine);

    start(&machine, &recorder, 1);
    recorder.ack_bytes[0] = 0xfbU; /* EI on the bus is not accepted. */
    i8085_test_signal(&machine, BM_I8085_SIGNAL_INTR, 1);
    assert(bm_i8085_step(&machine.cpu, &consumed) == BM_STATUS_UNSUPPORTED);
    assert(consumed == 0U && i8085_test_state(&machine).interrupt_enable == 1U);
    assert(i8085_test_state(&machine).pc == 0x1000U);
    i8085_test_destroy(&machine);

    start(&machine, &recorder, 0);
    i8085_test_signal(&machine, BM_I8085_SIGNAL_INTR, 1);
    i8085_test_step(&machine);
    assert(recorder.last.kind == BM_I8085_BOUNDARY_INSTRUCTION);
    i8085_test_destroy(&machine);
}

/* HALT is left by an acceptable interrupt or TRAP, with one extra THALT
 * clock ([MCS85] p. 2-16); IE=0 keeps the CPU halted. */
static void
test_halt_exit(void)
{
    static const uint8_t halt[] = { 0x76 };
    i8085_test_machine_t machine;
    recorder_t recorder;
    bm_i8085_arch_state_t state;
    bm_tick_t consumed = 3U;

    start(&machine, &recorder, 0);
    i8085_test_load(&machine, 0x1000U, halt, sizeof(halt));
    i8085_test_step(&machine);
    assert(recorder.last.t_states == 5U);
    assert(bm_i8085_step(&machine.cpu, &consumed) == BM_STATUS_IDLE);
    assert(consumed == 0U);
    i8085_test_signal(&machine, BM_I8085_SIGNAL_RST55, 1);
    expect_interrupt(&machine, &recorder, BM_I8085_INTERRUPT_RST55, 0x2cU, 0x1001U);
    assert(recorder.last.t_states == 13U);
    assert(i8085_test_state(&machine).halted == 0U);
    i8085_test_destroy(&machine);

    start(&machine, &recorder, 0);
    i8085_test_load(&machine, 0x1000U, halt, sizeof(halt));
    state = i8085_test_state(&machine);
    state.interrupt_enable = 0U;
    i8085_test_set_state(&machine, &state);
    i8085_test_step(&machine);
    i8085_test_signal(&machine, BM_I8085_SIGNAL_RST65, 1);
    assert(bm_i8085_step(&machine.cpu, &consumed) == BM_STATUS_IDLE);
    i8085_test_signal(&machine, BM_I8085_SIGNAL_TRAP, 1);
    expect_interrupt(&machine, &recorder, BM_I8085_INTERRUPT_TRAP, 0x24U, 0x1001U);
    i8085_test_destroy(&machine);
}

/* RIM layout: SID, I7.5, I6.5, I5.5, IE, M7.5, M6.5, M5.5. */
static void
test_rim_layout(void)
{
    static const uint8_t rim[] = { 0x20 };
    i8085_test_machine_t machine;
    recorder_t recorder;
    bm_i8085_arch_state_t state;

    start(&machine, &recorder, 0);
    i8085_test_load(&machine, 0x1000U, rim, sizeof(rim));
    state = i8085_test_state(&machine);
    state.interrupt_enable = 0U;
    state.interrupt_mask = 0x06U;
    i8085_test_set_state(&machine, &state);
    i8085_test_signal(&machine, BM_I8085_SIGNAL_SID, 1);
    i8085_test_signal(&machine, BM_I8085_SIGNAL_RST75, 1);
    i8085_test_signal(&machine, BM_I8085_SIGNAL_RST55, 1);
    i8085_test_step(&machine);
    assert(i8085_test_state(&machine).a == 0xd6U);
    i8085_test_destroy(&machine);
}

/* SIM loads SOD only with SOE set; RESET IN clears it. */
static void
test_sim_serial_output(void)
{
    static const uint8_t program[] = {
        0x3e, 0xc0,     /* MVI A,C0h: SOD=1, SOE */
        0x30,           /* SIM */
        0x3e, 0x00,     /* MVI A,00h: SOE clear */
        0x30,           /* SIM */
        0x3e, 0x40,     /* MVI A,40h: SOD=0, SOE */
        0x30,           /* SIM */
    };
    i8085_test_machine_t machine;
    recorder_t recorder;

    start(&machine, &recorder, 0);
    i8085_test_load(&machine, 0x1000U, program, sizeof(program));
    i8085_test_step(&machine);
    i8085_test_step(&machine);
    assert(recorder.sod_calls == 1U && recorder.sod_level == 1);
    assert(recorder.sod_delay == 3U && i8085_test_state(&machine).sod == 1U);
    assert(i8085_test_state(&machine).interrupt_mask == 0U);
    i8085_test_step(&machine);
    i8085_test_step(&machine);
    assert(recorder.sod_calls == 1U && i8085_test_state(&machine).sod == 1U);
    i8085_test_step(&machine);
    i8085_test_step(&machine);
    assert(recorder.sod_calls == 2U && recorder.sod_level == 0);
    assert(i8085_test_state(&machine).sod == 0U);

    machine.memory[0x1009U] = 0x3eU;
    machine.memory[0x100aU] = 0xc0U;
    machine.memory[0x100bU] = 0x30U;
    i8085_test_step(&machine);
    i8085_test_step(&machine);
    assert(i8085_test_state(&machine).sod == 1U);
    i8085_test_signal(&machine, BM_I8085_SIGNAL_RESET, 1);
    assert(i8085_test_state(&machine).sod == 0U);
    i8085_test_destroy(&machine);
}

/* RESET IN and READY low each consume one clock per boundary without any
 * bus cycle; execution resumes when they are released. */
static void
test_reset_and_wait_hold(void)
{
    i8085_test_machine_t machine;
    recorder_t recorder;
    uint64_t cycles = 0U;

    start(&machine, &recorder, 0);
    i8085_test_signal(&machine, BM_I8085_SIGNAL_RESET, 1);
    i8085_test_clear_log(&machine);
    assert(bm_i8085_step_clocked(machine.cpu.context, 0U, &cycles) == BM_STATUS_OK);
    assert(cycles == 1U && recorder.last.kind == BM_I8085_BOUNDARY_RESET);
    i8085_test_step(&machine);
    assert(machine.log_count == 0U && i8085_test_state(&machine).pc == 0U);
    i8085_test_signal(&machine, BM_I8085_SIGNAL_RESET, 0);
    i8085_test_step(&machine);
    assert(recorder.last.kind == BM_I8085_BOUNDARY_INSTRUCTION);
    assert(i8085_test_state(&machine).pc == 1U);

    i8085_test_signal(&machine, BM_I8085_SIGNAL_WAIT, 1);
    i8085_test_clear_log(&machine);
    assert(bm_i8085_step_clocked(machine.cpu.context, 0U, &cycles) == BM_STATUS_OK);
    assert(cycles == 1U && recorder.last.kind == BM_I8085_BOUNDARY_WAIT);
    assert(machine.log_count == 0U && i8085_test_state(&machine).pc == 1U);
    i8085_test_signal(&machine, BM_I8085_SIGNAL_WAIT, 0);
    i8085_test_step(&machine);
    assert(i8085_test_state(&machine).pc == 2U);
    i8085_test_destroy(&machine);
}

/* In a clocked engine HALT suspends the CPU until an interrupt line is
 * asserted through the engine. */
static void
test_clocked_halt_wakes_on_interrupt(void)
{
    static const uint8_t program[] = {
        0x3e, 0x08,     /* MVI A,08h */
        0x30,           /* SIM: unmask all */
        0xfb,           /* EI */
        0x76,           /* HLT */
    };
    bm_engine_config_t engine_config = { 1U, 1U, 0U };
    bm_clock_rate_t rate = { 2457600U, 1U };
    i8085_test_machine_t machine;
    recorder_t recorder;
    bm_engine_t *engine = NULL;
    bm_cpu_id_t id = 0U;

    start(&machine, &recorder, 0);
    i8085_test_load(&machine, 0x1000U, program, sizeof(program));
    assert(bm_engine_create_clocked(&machine.host, &engine_config, &engine) ==
           BM_STATUS_OK);
    assert(bm_engine_add_clocked_cpu(engine, &machine.cpu, bm_i8085_step_clocked,
                                     &rate, &id) == BM_STATUS_OK);
    assert(bm_engine_run_for(engine, 100000U) == BM_STATUS_OK);
    assert(i8085_test_state(&machine).halted == 1U);
    assert(i8085_test_state(&machine).pc == 0x1005U);
    assert(bm_engine_signal_cpu(engine, id, BM_I8085_SIGNAL_RST75, 1) ==
           BM_STATUS_OK);
    assert(bm_engine_run_for(engine, 1000U) == BM_STATUS_OK);
    assert(i8085_test_state(&machine).halted == 0U);
    /* One 13-state boundary (5.29 us) starts inside the 1 us window. */
    assert(recorder.last.interrupt_source == BM_I8085_INTERRUPT_RST75);
    assert(recorder.last.t_states == 13U);
    assert(i8085_test_state(&machine).pc == 0x003cU);
    bm_engine_destroy(engine);
    machine.cpu.ops.destroy = NULL;
    i8085_test_destroy(&machine);
}

int
main(void)
{
    test_priority_and_vectors();
    test_trap_edge_and_level();
    test_rim_after_trap();
    test_masks_and_rst75_latch();
    test_enable_delay_and_disable();
    test_intr_acknowledge();
    test_halt_exit();
    test_rim_layout();
    test_sim_serial_output();
    test_reset_and_wait_hold();
    test_clocked_halt_wakes_on_interrupt();
    return 0;
}
