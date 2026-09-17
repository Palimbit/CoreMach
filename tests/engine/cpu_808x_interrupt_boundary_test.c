/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Interrupt-disable timing and block restart semantics follow the NEC
 * uPD70108/uPD70116 User's Manual. Firmware and disk images are not inputs.
 */
#include "cpu_808x_test_harness.h"

#include <blumach/components/bus.h>

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

enum {
    FLAG_IF = 0x0200,
    VECTOR = 0x20
};

typedef struct interrupt_fixture {
    cpu_808x_test_machine_t *machine;
    unsigned int acknowledge_calls;
    unsigned int trace_calls;
    int signal_from_trace;
    uint8_t output[8];
    size_t output_count;
    size_t signal_after_output;
} interrupt_fixture_t;

static bm_status_t
acknowledge_interrupt(void *context, uint8_t *vector)
{
    interrupt_fixture_t *fixture = context;
    ++fixture->acknowledge_calls;
    *vector = VECTOR;
    return BM_STATUS_OK;
}

static void
signal_from_trace(void *context, const bm_808x_trace_t *trace)
{
    interrupt_fixture_t *fixture = context;
    (void) trace;
    ++fixture->trace_calls;
    if (fixture->signal_from_trace) {
        fixture->signal_from_trace = 0;
        assert(bm_engine_signal_cpu(fixture->machine->engine, 0U, 0U, 1) ==
               BM_STATUS_OK);
    }
}

static bm_status_t
capture_output(void *context, bm_bus_transaction_t *transaction)
{
    interrupt_fixture_t *fixture = context;

    assert(transaction->operation == BM_BUS_WRITE);
    assert(transaction->size == 1U);
    assert(fixture->output_count < sizeof(fixture->output));
    fixture->output[fixture->output_count++] = (uint8_t) transaction->value;
    if ((fixture->signal_after_output != 0U) &&
        (fixture->output_count == fixture->signal_after_output))
        assert(bm_engine_signal_cpu(fixture->machine->engine, 0U, 0U, 1) ==
               BM_STATUS_OK);
    return BM_STATUS_OK;
}

static void
create_interrupt_machine(cpu_808x_test_machine_t *machine,
                         interrupt_fixture_t *fixture,
                         const uint8_t *program, size_t program_size,
                         int with_io)
{
    cpu_808x_test_config_t config = { 0 };
    static const uint8_t vector[] = {
        0x00U, 0x02U, 0x00U, 0x00U /* 20h -> 0000:0200. */
    };

    config.bus_capacity = with_io ? 2U : 1U;
    config.trace = signal_from_trace;
    config.trace_context = fixture;
    config.interrupt_ack = acknowledge_interrupt;
    config.interrupt_context = fixture;
    cpu_808x_test_machine_create(machine, &config, program, program_size);
    fixture->machine = machine;
    cpu_808x_test_write(machine, VECTOR * 4U, vector, sizeof(vector));
    cpu_808x_test_poke(machine, 0x0200U, 0xcfU); /* IRET. */
    if (with_io)
        assert(bm_bus_map(machine->bus, BM_ADDRESS_IO, 0U, 0xffffU,
                          capture_output, fixture) == BM_STATUS_OK);
}

static bm_808x_arch_state_t
execution_state(cpu_808x_test_machine_t *machine, uint16_t flags)
{
    bm_808x_arch_state_t state = cpu_808x_test_get_state(machine);
    state.cs = 0xf000U;
    state.ip = 0U;
    state.ss = 0U;
    state.sp = 0x0400U;
    state.flags = (uint16_t) (0xa002U | flags);
    state.halted = 0U;
    state.interrupt_inhibit = 0U;
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
test_ei_delays_pending_interrupt(void)
{
    static const uint8_t program[] = { 0xfbU, 0x90U, 0xf4U };
    cpu_808x_test_machine_t machine;
    interrupt_fixture_t fixture = { 0 };
    bm_808x_arch_state_t state;
    bm_tick_t consumed = 0U;

    create_interrupt_machine(&machine, &fixture, program, sizeof(program), 0);
    state = execution_state(&machine, 0U);
    cpu_808x_test_set_state(&machine, &state);
    assert(bm_engine_signal_cpu(machine.engine, 0U, 0U, 1) == BM_STATUS_OK);

    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(consumed == 1U && state.ip == 1U);
    assert((state.flags & FLAG_IF) != 0U && state.interrupt_inhibit == 1U);
    assert(fixture.acknowledge_calls == 0U);

    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.ip == 2U && state.interrupt_inhibit == 0U);
    assert(fixture.acknowledge_calls == 0U);

    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.cs == 0U && state.ip == 0x0200U);
    assert(state.sp == 0x03faU && peek_word(&machine, 0x03faU) == 2U);
    assert(fixture.acknowledge_calls == 1U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_ei_halt_does_not_strand_pending_interrupt(void)
{
    static const uint8_t program[] = { 0xfbU, 0xf4U };
    cpu_808x_test_machine_t machine;
    interrupt_fixture_t fixture = { 0 };
    bm_808x_arch_state_t state;

    create_interrupt_machine(&machine, &fixture, program, sizeof(program), 0);
    state = execution_state(&machine, 0U);
    cpu_808x_test_set_state(&machine, &state);
    assert(bm_engine_signal_cpu(machine.engine, 0U, 0U, 1) == BM_STATUS_OK);

    assert(cpu_808x_test_run(&machine, 3U) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(fixture.acknowledge_calls == 1U);
    assert(state.cs == 0U && state.ip == 0x0200U && state.halted == 0U);
    assert(state.sp == 0x03faU && peek_word(&machine, 0x03faU) == 2U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
verify_segment_transfer_shadow(const uint8_t *program, size_t program_size,
                               uint16_t stack_value)
{
    cpu_808x_test_machine_t machine;
    interrupt_fixture_t fixture = { 0 };
    bm_808x_arch_state_t state;
    bm_tick_t consumed = 0U;

    create_interrupt_machine(&machine, &fixture, program, program_size, 0);
    state = execution_state(&machine, FLAG_IF);
    state.ax = 0x1234U;
    state.ds = 0U;
    cpu_808x_test_poke(&machine, state.sp, (uint8_t) stack_value);
    cpu_808x_test_poke(&machine, state.sp + 1U, (uint8_t) (stack_value >> 8U));
    cpu_808x_test_poke(&machine, 0x0100U, 0x78U);
    cpu_808x_test_poke(&machine, 0x0101U, 0x56U);
    cpu_808x_test_poke(&machine, 0x0102U, 0xbcU);
    cpu_808x_test_poke(&machine, 0x0103U, 0x9aU);
    cpu_808x_test_set_state(&machine, &state);

    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.interrupt_inhibit == 1U);
    assert(bm_engine_signal_cpu(machine.engine, 0U, 0U, 1) == BM_STATUS_OK);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.ip == program_size && state.interrupt_inhibit == 0U);
    assert(fixture.acknowledge_calls == 0U);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    assert(fixture.acknowledge_calls == 1U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_segment_transfers_delay_interrupts(void)
{
    static const uint8_t mov_to_segment[] = { 0x8eU, 0xd8U, 0x90U };
    static const uint8_t mov_from_segment[] = { 0x8cU, 0xd8U, 0x90U };
    static const uint8_t pop_es[] = { 0x07U, 0x90U };
    static const uint8_t pop_ss[] = { 0x17U, 0x90U };
    static const uint8_t pop_ds[] = { 0x1fU, 0x90U };
    static const uint8_t les[] = { 0xc4U, 0x06U, 0x00U, 0x01U, 0x90U };
    static const uint8_t lds[] = { 0xc5U, 0x06U, 0x00U, 0x01U, 0x90U };

    verify_segment_transfer_shadow(mov_to_segment, sizeof(mov_to_segment), 0U);
    verify_segment_transfer_shadow(mov_from_segment, sizeof(mov_from_segment), 0U);
    verify_segment_transfer_shadow(pop_es, sizeof(pop_es), 0x1111U);
    verify_segment_transfer_shadow(pop_ss, sizeof(pop_ss), 0x2222U);
    verify_segment_transfer_shadow(pop_ds, sizeof(pop_ds), 0x3333U);
    verify_segment_transfer_shadow(les, sizeof(les), 0U);
    verify_segment_transfer_shadow(lds, sizeof(lds), 0U);
}

static void
test_rep_memory_interrupt_restarts_at_prefix(void)
{
    static const uint8_t program[] = { 0xf3U, 0xa4U, 0xf4U };
    cpu_808x_test_machine_t machine;
    interrupt_fixture_t fixture = { 0 };
    bm_808x_arch_state_t state;
    bm_tick_t consumed = 0U;

    fixture.signal_from_trace = 1;
    create_interrupt_machine(&machine, &fixture, program, sizeof(program), 0);
    cpu_808x_test_poke(&machine, 0x10100U, 0x11U);
    cpu_808x_test_poke(&machine, 0x10101U, 0x22U);
    cpu_808x_test_poke(&machine, 0x10102U, 0x33U);
    state = execution_state(&machine, FLAG_IF);
    state.ds = 0x1000U;
    state.es = 0x2000U;
    state.si = 0x0100U;
    state.di = 0x0200U;
    state.cx = 3U;
    cpu_808x_test_set_state(&machine, &state);

    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.cs == 0U && state.ip == 0x0200U && state.cx == 2U);
    assert(state.si == 0x0101U && state.di == 0x0201U);
    assert(cpu_808x_test_peek(&machine, 0x20200U) == 0x11U);
    assert(cpu_808x_test_peek(&machine, 0x20201U) == 0U);
    assert(peek_word(&machine, 0x03faU) == 0U);

    assert(bm_engine_signal_cpu(machine.engine, 0U, 0U, 0) == BM_STATUS_OK);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK); /* IRET. */
    state = cpu_808x_test_get_state(&machine);
    assert(state.cs == 0xf000U && state.ip == 0U && state.cx == 2U);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.ip == 2U && state.cx == 0U);
    assert(cpu_808x_test_peek(&machine, 0x20201U) == 0x22U);
    assert(cpu_808x_test_peek(&machine, 0x20202U) == 0x33U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_rep_io_retains_last_three_prefixes(void)
{
    static const uint8_t program[] = {
        0x3eU, 0x36U, 0x2eU, 0xf3U, 0x6eU, 0xf4U
    };
    cpu_808x_test_machine_t machine;
    interrupt_fixture_t fixture = { 0 };
    bm_808x_arch_state_t state;
    bm_tick_t consumed = 0U;

    fixture.signal_after_output = 1U;
    create_interrupt_machine(&machine, &fixture, program, sizeof(program), 1);
    cpu_808x_test_poke(&machine, 0xf0100U, 0xa1U);
    cpu_808x_test_poke(&machine, 0xf0101U, 0xb2U);
    cpu_808x_test_poke(&machine, 0xf0102U, 0xc3U);
    state = execution_state(&machine, FLAG_IF);
    state.si = 0x0100U;
    state.cx = 3U;
    state.dx = 0x0080U;
    cpu_808x_test_set_state(&machine, &state);

    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.cs == 0U && state.ip == 0x0200U && state.cx == 2U);
    assert(state.si == 0x0101U && fixture.output_count == 1U);
    assert(fixture.output[0] == 0xa1U);
    assert(peek_word(&machine, 0x03faU) == 1U);

    assert(bm_engine_signal_cpu(machine.engine, 0U, 0U, 0) == BM_STATUS_OK);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK); /* IRET. */
    state = cpu_808x_test_get_state(&machine);
    assert(state.cs == 0xf000U && state.ip == 1U && state.cx == 2U);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.ip == 5U && state.cx == 0U && state.si == 0x0103U);
    assert(fixture.output_count == 3U);
    assert(fixture.output[1] == 0xb2U && fixture.output[2] == 0xc3U);
    cpu_808x_test_machine_destroy(&machine);
}

int
main(void)
{
    test_ei_delays_pending_interrupt();
    test_ei_halt_does_not_strand_pending_interrupt();
    test_segment_transfers_delay_interrupts();
    test_rep_memory_interrupt_restarts_at_prefix();
    test_rep_io_retains_last_three_prefixes();
    return 0;
}
