/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * NEC INM/OUTM semantics are taken from the 16-Bit V Series Instruction
 * User's Manual. Firmware and external hardware-vector corpora are not inputs.
 */
#include "cpu_808x_test_harness.h"

#include <blumach/components/bus.h>

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

enum {
    FLAG_DF = 0x0400,
    MAX_IO_EVENTS = 16
};

typedef struct io_event {
    bm_bus_operation_t operation;
    uint16_t port;
    uint16_t value;
    uint8_t size;
} io_event_t;

typedef struct io_fixture {
    uint16_t read_values[MAX_IO_EVENTS];
    size_t read_count;
    size_t read_index;
    size_t fail_event;
    io_event_t events[MAX_IO_EVENTS];
    size_t event_count;
} io_fixture_t;

static bm_status_t
io_access(void *context, bm_bus_transaction_t *transaction)
{
    io_fixture_t *fixture = context;
    io_event_t *event;

    assert((transaction->size == 1U) || (transaction->size == 2U));
    assert(fixture->event_count < MAX_IO_EVENTS);
    event = &fixture->events[fixture->event_count++];
    event->operation = transaction->operation;
    event->port = (uint16_t) transaction->address;
    event->size = transaction->size;
    if ((fixture->fail_event != 0U) &&
        (fixture->event_count == fixture->fail_event))
        return BM_STATUS_DEVICE_ERROR;
    if (transaction->operation == BM_BUS_READ) {
        assert(fixture->read_index < fixture->read_count);
        transaction->value = fixture->read_values[fixture->read_index++];
        event->value = (uint16_t) transaction->value;
        return BM_STATUS_OK;
    }
    assert(transaction->operation == BM_BUS_WRITE);
    event->value = (uint16_t) transaction->value;
    return BM_STATUS_OK;
}

static void
create_machine(cpu_808x_test_machine_t *machine, io_fixture_t *fixture,
               const uint8_t *program, size_t program_size)
{
    cpu_808x_test_config_t config = { 0 };
    config.bus_capacity = 2U;
    cpu_808x_test_machine_create(machine, &config, program, program_size);
    assert(bm_bus_map(machine->bus, BM_ADDRESS_IO, 0U, 0xffffU,
                      io_access, fixture) == BM_STATUS_OK);
}

static bm_808x_arch_state_t
execution_state(cpu_808x_test_machine_t *machine)
{
    bm_808x_arch_state_t state = cpu_808x_test_get_state(machine);
    state.cs = 0xf000U;
    state.ds = 0x1000U;
    state.es = 0x2000U;
    state.ip = 0U;
    state.flags = 0xf002U;
    return state;
}

static void
test_insb_ignores_segment_override(void)
{
    static const uint8_t program[] = { 0x3eU, 0x6cU };
    cpu_808x_test_machine_t machine;
    io_fixture_t io = { { 0x5aU }, 1U, 0U, 0U, { { 0 } }, 0U };
    bm_808x_arch_state_t state;
    bm_tick_t consumed = 0U;

    create_machine(&machine, &io, program, sizeof(program));
    state = execution_state(&machine);
    state.dx = 0x1234U;
    state.di = 0x0010U;
    state.cx = 5U;
    cpu_808x_test_set_state(&machine, &state);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(consumed == 1U && state.ip == sizeof(program));
    assert(state.di == 0x0011U && state.cx == 5U);
    assert(state.flags == 0xf002U);
    assert(cpu_808x_test_peek(&machine, 0x20010U) == 0x5aU);
    assert(cpu_808x_test_peek(&machine, 0x10010U) == 0U);
    assert(io.event_count == 1U);
    assert(io.events[0].operation == BM_BUS_READ);
    assert(io.events[0].size == 1U);
    assert(io.events[0].port == 0x1234U && io.events[0].value == 0x5aU);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_rep_insw_uses_fixed_wrapping_port(void)
{
    static const uint8_t program[] = { 0xf3U, 0x6dU };
    cpu_808x_test_machine_t machine;
    io_fixture_t io = {
        { 0x11U, 0x22U, 0x33U, 0x44U }, 4U, 0U, 0U, { { 0 } }, 0U
    };
    bm_808x_arch_state_t state;
    bm_tick_t consumed = 0U;

    create_machine(&machine, &io, program, sizeof(program));
    state = execution_state(&machine);
    state.dx = 0xffffU;
    state.di = 0x0020U;
    state.cx = 2U;
    cpu_808x_test_set_state(&machine, &state);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.di == 0x0024U && state.cx == 0U);
    assert(state.flags == 0xf002U && io.event_count == 4U);
    assert(io.events[0].size == 1U && io.events[1].size == 1U);
    assert(io.events[2].size == 1U && io.events[3].size == 1U);
    assert(io.events[0].port == 0xffffU && io.events[1].port == 0U);
    assert(io.events[2].port == 0xffffU && io.events[3].port == 0U);
    assert(cpu_808x_test_peek(&machine, 0x20020U) == 0x11U);
    assert(cpu_808x_test_peek(&machine, 0x20021U) == 0x22U);
    assert(cpu_808x_test_peek(&machine, 0x20022U) == 0x33U);
    assert(cpu_808x_test_peek(&machine, 0x20023U) == 0x44U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_insw_even_port_uses_one_wide_transfer(void)
{
    static const uint8_t program[] = { 0x6dU };
    cpu_808x_test_machine_t machine;
    io_fixture_t io = { { 0x2211U }, 1U, 0U, 0U, { { 0 } }, 0U };
    bm_808x_arch_state_t state;
    bm_tick_t consumed = 0U;

    create_machine(&machine, &io, program, sizeof(program));
    state = execution_state(&machine);
    state.dx = 0x0200U;
    state.di = 0x0020U;
    cpu_808x_test_set_state(&machine, &state);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.di == 0x0022U);
    assert(io.event_count == 1U);
    assert(io.events[0].operation == BM_BUS_READ);
    assert(io.events[0].port == 0x0200U && io.events[0].size == 2U &&
           io.events[0].value == 0x2211U);
    assert(cpu_808x_test_peek(&machine, 0x20020U) == 0x11U);
    assert(cpu_808x_test_peek(&machine, 0x20021U) == 0x22U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_zero_count_rep_has_no_side_effect(void)
{
    static const uint8_t program[] = { 0xf3U, 0x6cU };
    cpu_808x_test_machine_t machine;
    io_fixture_t io = { { 0 }, 0U, 0U, 0U, { { 0 } }, 0U };
    bm_808x_arch_state_t state;
    bm_tick_t consumed = 0U;

    create_machine(&machine, &io, program, sizeof(program));
    state = execution_state(&machine);
    state.di = 0x0100U;
    state.cx = 0U;
    cpu_808x_test_set_state(&machine, &state);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.di == 0x0100U && state.cx == 0U);
    assert(state.ip == sizeof(program) && io.event_count == 0U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_rep_outsb_uses_source_override(void)
{
    static const uint8_t program[] = { 0x2eU, 0xf3U, 0x6eU };
    cpu_808x_test_machine_t machine;
    io_fixture_t io = { { 0 }, 0U, 0U, 0U, { { 0 } }, 0U };
    bm_808x_arch_state_t state;
    bm_tick_t consumed = 0U;

    create_machine(&machine, &io, program, sizeof(program));
    cpu_808x_test_poke(&machine, 0xf0100U, 0xa5U);
    cpu_808x_test_poke(&machine, 0xf0101U, 0x5aU);
    cpu_808x_test_poke(&machine, 0x10100U, 0xeeU);
    state = execution_state(&machine);
    state.dx = 0x03f8U;
    state.si = 0x0100U;
    state.cx = 2U;
    cpu_808x_test_set_state(&machine, &state);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.si == 0x0102U && state.cx == 0U);
    assert(io.event_count == 2U);
    assert(io.events[0].port == 0x03f8U && io.events[0].value == 0xa5U);
    assert(io.events[1].port == 0x03f8U && io.events[1].value == 0x5aU);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_rep_outsw_decrements_and_orders_bytes(void)
{
    static const uint8_t program[] = { 0xf3U, 0x6fU };
    cpu_808x_test_machine_t machine;
    io_fixture_t io = { { 0 }, 0U, 0U, 0U, { { 0 } }, 0U };
    bm_808x_arch_state_t state;
    bm_tick_t consumed = 0U;

    create_machine(&machine, &io, program, sizeof(program));
    cpu_808x_test_poke(&machine, 0x10102U, 0xcdU);
    cpu_808x_test_poke(&machine, 0x10103U, 0xabU);
    cpu_808x_test_poke(&machine, 0x10100U, 0x34U);
    cpu_808x_test_poke(&machine, 0x10101U, 0x12U);
    state = execution_state(&machine);
    state.dx = 0x0200U;
    state.si = 0x0102U;
    state.cx = 2U;
    state.flags = (uint16_t) (state.flags | FLAG_DF);
    cpu_808x_test_set_state(&machine, &state);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.si == 0x00feU && state.cx == 0U);
    assert(state.flags == (uint16_t) (0xf002U | FLAG_DF));
    assert(io.event_count == 2U);
    assert(io.events[0].port == 0x0200U && io.events[0].size == 2U &&
           io.events[0].value == 0xabcdU);
    assert(io.events[1].port == 0x0200U && io.events[1].size == 2U &&
           io.events[1].value == 0x1234U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_failed_repeat_keeps_completed_progress(void)
{
    static const uint8_t program[] = { 0xf3U, 0x6cU };
    cpu_808x_test_machine_t machine;
    io_fixture_t io = {
        { 0x7eU, 0x99U }, 2U, 0U, 2U, { { 0 } }, 0U
    };
    bm_808x_arch_state_t state;
    bm_tick_t consumed = 99U;

    create_machine(&machine, &io, program, sizeof(program));
    state = execution_state(&machine);
    state.dx = 0x0080U;
    state.di = 0x0040U;
    state.cx = 2U;
    cpu_808x_test_set_state(&machine, &state);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_DEVICE_ERROR);
    state = cpu_808x_test_get_state(&machine);
    assert(consumed == 0U && state.ip == sizeof(program));
    assert(state.di == 0x0041U && state.cx == 1U);
    assert(cpu_808x_test_peek(&machine, 0x20040U) == 0x7eU);
    assert(cpu_808x_test_peek(&machine, 0x20041U) == 0U);
    assert(io.event_count == 2U);
    cpu_808x_test_machine_destroy(&machine);
}

int
main(void)
{
    test_insb_ignores_segment_override();
    test_rep_insw_uses_fixed_wrapping_port();
    test_insw_even_port_uses_one_wide_transfer();
    test_zero_count_rep_has_no_side_effect();
    test_rep_outsb_uses_source_override();
    test_rep_outsw_decrements_and_orders_bytes();
    test_failed_repeat_keeps_completed_progress();
    return 0;
}
