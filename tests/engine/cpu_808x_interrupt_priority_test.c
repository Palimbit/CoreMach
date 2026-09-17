/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * NMI edge latching, interrupt priority and BRK/single-step boundaries follow
 * the NEC uPD70108/uPD70116 User's Manual. Firmware and disk images are not
 * inputs to these synthetic tests.
 */
#include "cpu_808x_test_harness.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

enum {
    FLAG_TF = 0x0100,
    FLAG_IF = 0x0200,
    NMI_VECTOR = 2,
    TRAP_VECTOR = 1,
    INT_VECTOR = 0x20,
    NMI_HANDLER = 0x0200,
    TRAP_HANDLER = 0x0300,
    INT_HANDLER = 0x0400,
    SOFTWARE_HANDLER = 0x0500
};

typedef struct interrupt_fixture {
    cpu_808x_test_machine_t *machine;
    unsigned int acknowledge_calls;
    uint32_t trace_signal;
    int signal_from_trace;
} interrupt_fixture_t;

static bm_status_t
acknowledge_interrupt(void *context, uint8_t *vector)
{
    interrupt_fixture_t *fixture = context;

    ++fixture->acknowledge_calls;
    *vector = INT_VECTOR;
    return BM_STATUS_OK;
}

static void
signal_from_trace(void *context, const bm_808x_trace_t *trace)
{
    interrupt_fixture_t *fixture = context;

    (void) trace;
    if (fixture->signal_from_trace) {
        fixture->signal_from_trace = 0;
        assert(bm_engine_signal_cpu(fixture->machine->engine, 0U,
                                    fixture->trace_signal, 1) == BM_STATUS_OK);
    }
}

static void
install_vector(cpu_808x_test_machine_t *machine, uint8_t vector,
               uint16_t handler)
{
    uint8_t entry[4] = {
        (uint8_t) handler, (uint8_t) (handler >> 8U), 0U, 0U
    };

    cpu_808x_test_write(machine, (uint32_t) vector * 4U, entry, sizeof(entry));
    cpu_808x_test_poke(machine, handler, 0xcfU); /* IRET. */
}

static void
create_interrupt_machine(cpu_808x_test_machine_t *machine,
                         interrupt_fixture_t *fixture,
                         const uint8_t *program, size_t program_size)
{
    cpu_808x_test_config_t config = { 0 };

    config.trace = signal_from_trace;
    config.trace_context = fixture;
    config.interrupt_ack = acknowledge_interrupt;
    config.interrupt_context = fixture;
    cpu_808x_test_machine_create(machine, &config, program, program_size);
    fixture->machine = machine;
    install_vector(machine, NMI_VECTOR, NMI_HANDLER);
    install_vector(machine, TRAP_VECTOR, TRAP_HANDLER);
    install_vector(machine, INT_VECTOR, INT_HANDLER);
    install_vector(machine, 3U, SOFTWARE_HANDLER);
}

static bm_808x_arch_state_t
execution_state(cpu_808x_test_machine_t *machine, uint16_t flags)
{
    bm_808x_arch_state_t state = cpu_808x_test_get_state(machine);

    state.cs = 0xf000U;
    state.ip = 0U;
    state.ss = 0U;
    state.sp = 0x0800U;
    state.flags = (uint16_t) (0xa002U | flags);
    state.halted = 0U;
    state.interrupt_inhibit = 0U;
    state.boundary_inhibit = 0U;
    state.nmi_pending = 0U;
    state.trap_pending = 0U;
    return state;
}

static uint16_t
peek_word(const cpu_808x_test_machine_t *machine, uint32_t address)
{
    return (uint16_t) (cpu_808x_test_peek(machine, address) |
                       ((uint16_t) cpu_808x_test_peek(machine, address + 1U)
                        << 8U));
}

static void
test_nmi_is_edge_latched_and_ignores_if(void)
{
    static const uint8_t program[] = { 0x90U, 0x90U };
    cpu_808x_test_machine_t machine;
    interrupt_fixture_t fixture = { 0 };
    bm_808x_arch_state_t state;
    bm_tick_t consumed = 0U;

    create_interrupt_machine(&machine, &fixture, program, sizeof(program));
    state = execution_state(&machine, 0U);
    cpu_808x_test_set_state(&machine, &state);

    assert(bm_engine_signal_cpu(machine.engine, 0U, BM_808X_SIGNAL_NMI, 1) ==
           BM_STATUS_OK);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(consumed == 1U && state.cs == 0U && state.ip == NMI_HANDLER);
    assert(state.sp == 0x07faU && peek_word(&machine, 0x07faU) == 0U);
    assert((peek_word(&machine, 0x07feU) & FLAG_IF) == 0U);
    assert(state.nmi_pending == 0U && fixture.acknowledge_calls == 0U);

    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK); /* IRET */
    assert(bm_engine_signal_cpu(machine.engine, 0U, BM_808X_SIGNAL_NMI, 1) ==
           BM_STATUS_OK);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.cs == 0xf000U && state.ip == 1U); /* Held high: no new edge. */

    assert(bm_engine_signal_cpu(machine.engine, 0U, BM_808X_SIGNAL_NMI, 0) ==
           BM_STATUS_OK);
    assert(bm_engine_signal_cpu(machine.engine, 0U, BM_808X_SIGNAL_NMI, 1) ==
           BM_STATUS_OK);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.cs == 0U && state.ip == NMI_HANDLER);
    assert(peek_word(&machine, 0x07faU) == 1U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_priority_is_nmi_then_int_then_single_step(void)
{
    static const uint8_t program[] = { 0x90U };
    cpu_808x_test_machine_t machine;
    interrupt_fixture_t fixture = { 0 };
    bm_808x_arch_state_t state;
    bm_tick_t consumed = 0U;

    create_interrupt_machine(&machine, &fixture, program, sizeof(program));
    state = execution_state(&machine, FLAG_IF);
    state.trap_pending = 1U;
    cpu_808x_test_set_state(&machine, &state);
    assert(bm_engine_signal_cpu(machine.engine, 0U, BM_808X_SIGNAL_INT, 1) ==
           BM_STATUS_OK);
    assert(bm_engine_signal_cpu(machine.engine, 0U, BM_808X_SIGNAL_NMI, 1) ==
           BM_STATUS_OK);

    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.cs == 0U && state.ip == NMI_HANDLER);
    assert(fixture.acknowledge_calls == 0U && state.trap_pending == 0U);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK); /* IRET */
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.cs == 0U && state.ip == INT_HANDLER);
    assert(fixture.acknowledge_calls == 1U);
    cpu_808x_test_machine_destroy(&machine);

    fixture = (interrupt_fixture_t) { 0 };
    create_interrupt_machine(&machine, &fixture, program, sizeof(program));
    state = execution_state(&machine, FLAG_IF);
    state.trap_pending = 1U;
    cpu_808x_test_set_state(&machine, &state);
    assert(bm_engine_signal_cpu(machine.engine, 0U, BM_808X_SIGNAL_INT, 1) ==
           BM_STATUS_OK);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.cs == 0U && state.ip == INT_HANDLER);
    assert(fixture.acknowledge_calls == 1U && state.trap_pending == 0U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_single_step_uses_completed_instruction_boundary(void)
{
    static const uint8_t program[] = { 0x90U, 0x90U };
    cpu_808x_test_machine_t machine;
    interrupt_fixture_t fixture = { 0 };
    bm_808x_arch_state_t state;
    bm_tick_t consumed = 0U;

    create_interrupt_machine(&machine, &fixture, program, sizeof(program));
    state = execution_state(&machine, FLAG_TF);
    cpu_808x_test_set_state(&machine, &state);

    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.cs == 0xf000U && state.ip == 1U && state.trap_pending == 1U);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.cs == 0U && state.ip == TRAP_HANDLER);
    assert(state.sp == 0x07faU && peek_word(&machine, 0x07faU) == 1U);
    assert((peek_word(&machine, 0x07feU) & FLAG_TF) != 0U);
    assert((state.flags & FLAG_TF) == 0U && state.trap_pending == 0U);

    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK); /* IRET */
    state = cpu_808x_test_get_state(&machine);
    assert(state.cs == 0xf000U && state.ip == 1U);
    assert((state.flags & FLAG_TF) != 0U && state.trap_pending == 0U);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.ip == 2U && state.trap_pending == 1U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_synchronous_interrupt_does_not_add_single_step(void)
{
    static const uint8_t program[] = { 0xccU }; /* BRK3/INT3. */
    cpu_808x_test_machine_t machine;
    interrupt_fixture_t fixture = { 0 };
    bm_808x_arch_state_t state;
    bm_tick_t consumed = 0U;

    create_interrupt_machine(&machine, &fixture, program, sizeof(program));
    state = execution_state(&machine, FLAG_TF);
    cpu_808x_test_set_state(&machine, &state);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.cs == 0U && state.ip == SOFTWARE_HANDLER);
    assert(peek_word(&machine, 0x07faU) == 1U);
    assert((peek_word(&machine, 0x07feU) & FLAG_TF) != 0U);
    assert((state.flags & FLAG_TF) == 0U && state.trap_pending == 0U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_nmi_wakes_halt(void)
{
    static const uint8_t program[] = { 0xf4U };
    cpu_808x_test_machine_t machine;
    interrupt_fixture_t fixture = { 0 };
    bm_808x_arch_state_t state;
    bm_tick_t consumed = 0U;

    create_interrupt_machine(&machine, &fixture, program, sizeof(program));
    state = execution_state(&machine, 0U);
    cpu_808x_test_set_state(&machine, &state);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_IDLE);
    state = cpu_808x_test_get_state(&machine);
    assert(consumed == 1U && state.halted == 1U && state.ip == 1U);
    assert(bm_engine_signal_cpu(machine.engine, 0U, BM_808X_SIGNAL_NMI, 1) ==
           BM_STATUS_OK);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.halted == 0U && state.cs == 0U && state.ip == NMI_HANDLER);
    assert(peek_word(&machine, 0x07faU) == 1U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_ei_does_not_defer_nmi(void)
{
    static const uint8_t program[] = { 0xfbU, 0x90U };
    cpu_808x_test_machine_t machine;
    interrupt_fixture_t fixture = {
        .trace_signal = BM_808X_SIGNAL_NMI,
        .signal_from_trace = 1
    };
    bm_808x_arch_state_t state;
    bm_tick_t consumed = 0U;

    create_interrupt_machine(&machine, &fixture, program, sizeof(program));
    state = execution_state(&machine, 0U);
    cpu_808x_test_set_state(&machine, &state);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.ip == 1U && state.interrupt_inhibit == 1U);
    assert(state.boundary_inhibit == 0U && state.nmi_pending == 1U);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.cs == 0U && state.ip == NMI_HANDLER);
    assert(peek_word(&machine, 0x07faU) == 1U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_segment_transfer_defers_nmi_and_single_step(void)
{
    static const uint8_t program[] = { 0x8eU, 0xd8U, 0x90U };
    cpu_808x_test_machine_t machine;
    interrupt_fixture_t fixture = {
        .trace_signal = BM_808X_SIGNAL_NMI,
        .signal_from_trace = 1
    };
    bm_808x_arch_state_t state;
    bm_tick_t consumed = 0U;

    create_interrupt_machine(&machine, &fixture, program, sizeof(program));
    state = execution_state(&machine, 0U);
    state.ax = 0x1234U;
    cpu_808x_test_set_state(&machine, &state);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.ds == 0x1234U && state.ip == 2U);
    assert(state.boundary_inhibit == 1U && state.nmi_pending == 1U);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.ip == 3U && state.boundary_inhibit == 0U);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.cs == 0U && state.ip == NMI_HANDLER);
    assert(peek_word(&machine, 0x07faU) == 3U);
    cpu_808x_test_machine_destroy(&machine);

    fixture = (interrupt_fixture_t) { 0 };
    create_interrupt_machine(&machine, &fixture, program, sizeof(program));
    state = execution_state(&machine, FLAG_TF);
    state.ax = 0x5678U;
    cpu_808x_test_set_state(&machine, &state);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.ip == 2U && state.boundary_inhibit == 1U);
    assert(state.trap_pending == 0U);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.ip == 3U && state.boundary_inhibit == 0U);
    assert(state.trap_pending == 1U);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.cs == 0U && state.ip == TRAP_HANDLER);
    assert(peek_word(&machine, 0x07faU) == 3U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_nmi_can_restart_rep_but_not_split_buslock(void)
{
    static const uint8_t rep_program[] = { 0xf3U, 0xaaU };
    static const uint8_t locked_program[] = { 0xf0U, 0xf3U, 0xaaU };
    cpu_808x_test_machine_t machine;
    interrupt_fixture_t fixture = {
        .trace_signal = BM_808X_SIGNAL_NMI,
        .signal_from_trace = 1
    };
    bm_808x_arch_state_t state;
    bm_tick_t consumed = 0U;

    create_interrupt_machine(&machine, &fixture, rep_program,
                             sizeof(rep_program));
    state = execution_state(&machine, 0U);
    state.ax = 0x005aU;
    state.cx = 3U;
    state.es = 0U;
    state.di = 0x0100U;
    cpu_808x_test_set_state(&machine, &state);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.cs == 0U && state.ip == NMI_HANDLER);
    assert(state.cx == 2U && state.di == 0x0101U);
    assert(cpu_808x_test_peek(&machine, 0x0100U) == 0x5aU);
    assert(peek_word(&machine, 0x07faU) == 0U);
    cpu_808x_test_machine_destroy(&machine);

    fixture = (interrupt_fixture_t) {
        .trace_signal = BM_808X_SIGNAL_NMI,
        .signal_from_trace = 1
    };
    create_interrupt_machine(&machine, &fixture, locked_program,
                             sizeof(locked_program));
    state = execution_state(&machine, 0U);
    state.ax = 0x00a5U;
    state.cx = 3U;
    state.es = 0U;
    state.di = 0x0120U;
    cpu_808x_test_set_state(&machine, &state);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.cs == 0xf000U && state.ip == 3U);
    assert(state.cx == 0U && state.di == 0x0123U && state.nmi_pending == 1U);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.cs == 0U && state.ip == NMI_HANDLER);
    assert(peek_word(&machine, 0x07faU) == 3U);
    cpu_808x_test_machine_destroy(&machine);
}

int
main(void)
{
    test_nmi_is_edge_latched_and_ignores_if();
    test_priority_is_nmi_then_int_then_single_step();
    test_single_step_uses_completed_instruction_boundary();
    test_synchronous_interrupt_does_not_add_single_step();
    test_nmi_wakes_halt();
    test_ei_does_not_defer_nmi();
    test_segment_transfer_defers_nmi_and_single_step();
    test_nmi_can_restart_rep_but_not_split_buslock();
    return 0;
}
