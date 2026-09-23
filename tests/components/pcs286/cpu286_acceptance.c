/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Initial integration gate, NOT full instruction/protected-mode conformance.
 */
#include "bus_fixture.h"
#include "failure_injection_host.h"
#include <blumach/components/cpu_80286.h>
#include <blumach/platforms/null_host.h>
#include <assert.h>

static bm_286_config_t config_for(bus_fixture_t *rom)
{
    bm_286_config_t c = {0};
    c.size = sizeof(c);
    c.version = BM_286_CONTRACT_VERSION;
    c.access = fixture_access;
    c.access_context = rom;
    return c;
}
static bm_286_arch_state_t state_of(const bm_cpu_t *cpu)
{
    bm_286_arch_state_t s = {0};
    s.size = sizeof(s);
    s.version = BM_286_STATE_VERSION;
    assert(bm_286_get_arch_state(cpu, &s) == BM_STATUS_OK);
    return s;
}

typedef struct hold_observer {
    unsigned int asserted;
    unsigned int deasserted;
} hold_observer_t;

static void observe_hold(void *context, int asserted)
{
    hold_observer_t *observer = context;
    if (asserted)
        ++observer->asserted;
    else
        ++observer->deasserted;
}
static void test_reset_and_instances(void)
{
    bm_host_services_t host = bm_null_host_services();
    bus_fixture_t rom;
    bm_286_config_t c;
    bm_cpu_t a = {0}, b = {0};
    bm_286_arch_state_t sa, sb;
    bm_286_boundary_t boundary = {0};
    fixture_init(&rom, 0xffff00U);
    rom.bytes[0xf0] = 0x90U; /* authored NOP, not firmware */
    c = config_for(&rom);
    assert(bm_286_create(&host, &c, &a) == BM_STATUS_OK);
    assert(bm_286_create(&host, &c, &b) == BM_STATUS_OK);
    assert(rom.trace_count == 0U); /* constructors must not execute */
    assert(a.ops.reset(a.context) == BM_STATUS_OK);
    sa = state_of(&a);
    assert(sa.cs.base == 0xff0000U && sa.ip == 0xfff0U);
    assert(sa.cs.selector == 0xf000U);
    assert(sa.msw == 0xfff0U && sa.flags == 0x0002U);
    assert(sa.idtr.base == 0U && sa.idtr.limit == 0x03ffU);
    assert(bm_286_step(&a, &boundary) == BM_STATUS_OK);
    assert(rom.trace_count > 0U);
    assert(rom.trace[0].request.address == 0xfffff0U);
    assert(rom.trace[0].request.operation == BM_BUS_FETCH);
    sa = state_of(&a);
    sb = state_of(&b);
    assert(sa.ip == 0xfff1U && sb.ip == 0xfff0U);
    assert(boundary.kind == BM_286_BOUNDARY_INSTRUCTION);
    assert(a.ops.reset(a.context) == BM_STATUS_OK);
    sa = state_of(&a);
    assert(sa.ip == 0xfff0U);
    a.ops.destroy(a.context);
    b.ops.destroy(b.context);
}

static void test_state_import_and_sampled_trap(void)
{
    bm_host_services_t host = bm_null_host_services();
    bus_fixture_t rom;
    bm_286_config_t c;
    bm_cpu_t cpu = {0};
    bm_286_arch_state_t state, invalid, after;
    bm_286_boundary_t boundary = {0};
    fixture_init(&rom, 0xffff00U);
    rom.bytes[0xf0] = 0x90U;
    rom.bytes[0xf1] = 0x90U;
    c = config_for(&rom);
    assert(bm_286_create(&host, &c, &cpu) == BM_STATUS_OK);
    state = state_of(&cpu);
    invalid = state;
    invalid.trap_pending = 2U;
    assert(bm_286_set_arch_state(&cpu, &invalid) == BM_STATUS_INVALID_ARGUMENT);
    after = state_of(&cpu);
    assert(after.ip == state.ip && after.trap_pending == 0U);
    invalid = state;
    invalid.version = BM_286_STATE_VERSION - 1U;
    assert(bm_286_set_arch_state(&cpu, &invalid) == BM_STATUS_INVALID_ARGUMENT);
    after = state_of(&cpu);
    assert(after.version == BM_286_STATE_VERSION && after.ip == state.ip);
    state.flags |= 0x0100U;
    assert(bm_286_set_arch_state(&cpu, &state) == BM_STATUS_OK);
    after = state_of(&cpu);
    assert(after.trap_pending == 0U); /* TF alone is not a sampled trap. */
    assert(bm_286_step(&cpu, &boundary) == BM_STATUS_OK);
    after = state_of(&cpu);
    assert(after.ip == 0xfff1U && after.trap_pending == 1U);
    assert(bm_286_step(&cpu, &boundary) == BM_STATUS_UNSUPPORTED);
    assert(bm_286_step(&cpu, &boundary) == BM_STATUS_INVALID_STATE);
    assert(cpu.ops.reset(cpu.context) == BM_STATUS_OK);
    after = state_of(&cpu);
    assert(after.trap_pending == 0U && after.flags == 0x0002U);
    cpu.ops.destroy(cpu.context);
}

static void test_strict_clocked_stops_without_fetch(void)
{
    bm_host_services_t host = bm_null_host_services();
    bus_fixture_t rom;
    bm_286_config_t c;
    bm_cpu_t cpu = {0};
    bm_286_boundary_t boundary = {0};
    uint64_t cycles = 99U;
    fixture_init(&rom, 0xffff00U);
    rom.bytes[0xf0] = 0x90U;
    c = config_for(&rom);
    assert(bm_286_create(&host, &c, &cpu) == BM_STATUS_OK);
    assert(bm_286_step_clocked(cpu.context, 0U, &cycles) ==
           BM_STATUS_UNSUPPORTED);
    assert(cycles == 0U && rom.fetches == 0U);
    assert(bm_286_step_clocked(cpu.context, 1U, &cycles) ==
           BM_STATUS_INVALID_STATE);
    assert(bm_286_step(&cpu, &boundary) == BM_STATUS_INVALID_STATE);
    assert(cpu.ops.reset(cpu.context) == BM_STATUS_OK);
    assert(bm_286_step(&cpu, &boundary) == BM_STATUS_OK);
    assert(rom.fetches == 1U);
    cpu.ops.destroy(cpu.context);
}

static void test_hold_and_invalid_import(void)
{
    bm_host_services_t host = bm_null_host_services();
    bus_fixture_t rom;
    bm_286_config_t c;
    bm_cpu_t cpu = {0};
    bm_286_arch_state_t state, invalid;
    bm_286_boundary_t boundary = {0};
    hold_observer_t observer = {0};
    fixture_init(&rom, 0xffff00U);
    rom.bytes[0xf0] = 0x90U;
    c = config_for(&rom);
    c.hold_ack = observe_hold;
    c.pin_context = &observer;
    assert(bm_286_create(&host, &c, &cpu) == BM_STATUS_OK);
    state = state_of(&cpu);
    invalid = state;
    invalid.cs.base = 0x01000000U;
    assert(bm_286_set_arch_state(&cpu, &invalid) == BM_STATUS_INVALID_ARGUMENT);
    invalid = state;
    invalid.cpl = 4U;
    assert(bm_286_set_arch_state(&cpu, &invalid) == BM_STATUS_INVALID_ARGUMENT);
    assert(state_of(&cpu).cs.base == state.cs.base);
    assert(cpu.ops.signal(cpu.context, BM_286_SIGNAL_HOLD, 1) == BM_STATUS_OK);
    assert(bm_286_step(&cpu, &boundary) == BM_STATUS_IDLE);
    assert(boundary.kind == BM_286_BOUNDARY_HOLD && rom.fetches == 0U);
    assert(observer.asserted == 1U && observer.deasserted == 0U);
    assert(bm_286_step(&cpu, &boundary) == BM_STATUS_IDLE);
    assert(observer.asserted == 1U); /* HLDA is a level, not a new pulse. */
    assert(cpu.ops.reset(cpu.context) == BM_STATUS_OK);
    assert(observer.deasserted == 1U);
    assert(bm_286_step(&cpu, &boundary) == BM_STATUS_OK);
    assert(rom.fetches == 1U);
    cpu.ops.destroy(cpu.context);
}

static void test_invalid_creation_clears_output(void)
{
    bm_host_services_t host = bm_null_host_services();
    bus_fixture_t rom;
    bm_286_config_t c;
    bm_cpu_t cpu = {0};
    fixture_init(&rom, 0xffff00U);
    c = config_for(&rom);
    cpu.context = &rom;
    c.version = BM_286_CONTRACT_VERSION - 1U;
    assert(bm_286_create(&host, &c, &cpu) == BM_STATUS_INVALID_ARGUMENT);
    assert(cpu.context == NULL && cpu.ops.destroy == NULL);
    c = config_for(&rom);
    cpu.context = &rom;
    c.access = NULL;
    assert(bm_286_create(&host, &c, &cpu) == BM_STATUS_INVALID_ARGUMENT);
    assert(cpu.context == NULL && cpu.ops.destroy == NULL);
    c = config_for(&rom);
    cpu.context = &rom;
    host.allocate = NULL;
    assert(bm_286_create(&host, &c, &cpu) == BM_STATUS_INVALID_ARGUMENT);
    assert(cpu.context == NULL && cpu.ops.destroy == NULL);
}

static void test_bus_failure_latches_without_retry(void)
{
    bm_host_services_t host = bm_null_host_services();
    bus_fixture_t rom;
    bm_286_config_t c;
    bm_cpu_t cpu = {0};
    bm_286_boundary_t boundary = {0};
    fixture_init(&rom, 0xffff00U);
    rom.result = BM_STATUS_DEVICE_ERROR;
    c = config_for(&rom);
    assert(bm_286_create(&host, &c, &cpu) == BM_STATUS_OK);
    assert(bm_286_step(&cpu, &boundary) == BM_STATUS_DEVICE_ERROR);
    assert(rom.trace_count == 1U && state_of(&cpu).ip == 0xfff0U);
    rom.result = BM_STATUS_OK;
    rom.bytes[0xf0] = 0x90U;
    assert(bm_286_step(&cpu, &boundary) == BM_STATUS_INVALID_STATE);
    assert(rom.trace_count == 1U);
    assert(cpu.ops.reset(cpu.context) == BM_STATUS_OK);
    assert(bm_286_step(&cpu, &boundary) == BM_STATUS_OK);
    assert(rom.trace_count == 2U);
    cpu.ops.destroy(cpu.context);
}

static void test_halt_shutdown_and_pending_signals(void)
{
    bm_host_services_t host = bm_null_host_services();
    bus_fixture_t rom;
    bm_286_config_t c;
    bm_cpu_t cpu = {0};
    bm_286_arch_state_t state;
    bm_286_boundary_t boundary = {0};
    size_t fetches_before_shutdown;
    fixture_init(&rom, 0xffff00U);
    rom.bytes[0xf0] = 0x90U;
    c = config_for(&rom);
    assert(bm_286_create(&host, &c, &cpu) == BM_STATUS_OK);

    state = state_of(&cpu);
    state.halted = 1U;
    state.flags |= 0x0200U;
    assert(bm_286_set_arch_state(&cpu, &state) == BM_STATUS_OK);
    assert(bm_286_step(&cpu, &boundary) == BM_STATUS_IDLE);
    assert(boundary.kind == BM_286_BOUNDARY_HALT && rom.fetches == 0U);
    assert(cpu.ops.signal(cpu.context, BM_286_SIGNAL_INTR, 1) == BM_STATUS_OK);
    assert(bm_286_step(&cpu, &boundary) == BM_STATUS_UNSUPPORTED);
    assert(rom.fetches == 0U && state_of(&cpu).halted == 1U);
    assert(cpu.ops.reset(cpu.context) == BM_STATUS_OK);

    state = state_of(&cpu);
    state.halted = 1U;
    state.nmi_pending = 1U;
    state.nmi_blocked = 1U;
    assert(bm_286_set_arch_state(&cpu, &state) == BM_STATUS_OK);
    assert(bm_286_step(&cpu, &boundary) == BM_STATUS_IDLE);
    assert(boundary.kind == BM_286_BOUNDARY_HALT);
    state.nmi_blocked = 0U;
    assert(bm_286_set_arch_state(&cpu, &state) == BM_STATUS_OK);
    assert(bm_286_step(&cpu, &boundary) == BM_STATUS_UNSUPPORTED);
    assert(cpu.ops.reset(cpu.context) == BM_STATUS_OK);

    state = state_of(&cpu);
    state.halted = 1U;
    state.nmi_pending = 1U;
    state.interrupt_shadow = 1U;
    assert(bm_286_set_arch_state(&cpu, &state) == BM_STATUS_OK);
    assert(bm_286_step(&cpu, &boundary) == BM_STATUS_IDLE);
    assert(boundary.kind == BM_286_BOUNDARY_HALT);
    assert(cpu.ops.reset(cpu.context) == BM_STATUS_OK);

    state = state_of(&cpu);
    state.flags |= 0x0200U;
    state.interrupt_shadow = 1U;
    assert(bm_286_set_arch_state(&cpu, &state) == BM_STATUS_OK);
    assert(cpu.ops.signal(cpu.context, BM_286_SIGNAL_INTR, 1) == BM_STATUS_OK);
    assert(bm_286_step(&cpu, &boundary) == BM_STATUS_OK);
    assert(state_of(&cpu).interrupt_shadow == 0U);
    assert(bm_286_step(&cpu, &boundary) == BM_STATUS_UNSUPPORTED);
    assert(cpu.ops.reset(cpu.context) == BM_STATUS_OK);

    fetches_before_shutdown = rom.fetches;
    state = state_of(&cpu);
    state.shutdown = 1U;
    state.msw |= 1U;
    state.flags |= 0x0200U;
    assert(bm_286_set_arch_state(&cpu, &state) == BM_STATUS_OK);
    assert(cpu.ops.signal(cpu.context, BM_286_SIGNAL_INTR, 1) == BM_STATUS_OK);
    assert(bm_286_step(&cpu, &boundary) == BM_STATUS_IDLE);
    assert(boundary.kind == BM_286_BOUNDARY_SHUTDOWN);
    assert(cpu.ops.signal(cpu.context, BM_286_SIGNAL_NMI, 1) == BM_STATUS_OK);
    assert(bm_286_step(&cpu, &boundary) == BM_STATUS_UNSUPPORTED);
    state = state_of(&cpu);
    assert(state.shutdown == 1U && (state.msw & 1U) == 1U);
    assert(rom.fetches == fetches_before_shutdown);
    assert(cpu.ops.reset(cpu.context) == BM_STATUS_OK);
    state = state_of(&cpu);
    assert(state.shutdown == 0U && (state.msw & 1U) == 0U);
    cpu.ops.destroy(cpu.context);
}

static void test_unknown_timing_is_lower_bound(void)
{
    bm_host_services_t host = bm_null_host_services();
    bus_fixture_t rom;
    bm_286_config_t c;
    bm_cpu_t cpu = {0};
    bm_286_boundary_t boundary = {0};
    fixture_init(&rom, 0xffff00U);
    rom.bytes[0xf0] = 0x90U;
    rom.waits = 3U;
    c = config_for(&rom);
    assert(bm_286_create(&host, &c, &cpu) == BM_STATUS_OK);
    assert(bm_286_step(&cpu, &boundary) == BM_STATUS_OK);
    assert(boundary.timing == BM_286_TIMING_UNKNOWN);
    assert(boundary.cpu_cycles == 3U && boundary.bus_wait_cycles == 3U);
    cpu.ops.destroy(cpu.context);
}

static void test_run_is_instruction_count_only(void)
{
    bm_host_services_t host = bm_null_host_services();
    bus_fixture_t rom;
    bm_286_config_t c;
    bm_cpu_t cpu = {0};
    bm_tick_t consumed = 99U;
    fixture_init(&rom, 0xffff00U);
    rom.bytes[0xf0] = 0x90U;
    rom.bytes[0xf1] = 0x90U;
    c = config_for(&rom);
    assert(bm_286_create(&host, &c, &cpu) == BM_STATUS_OK);
    assert(cpu.ops.run(cpu.context, 2U, &consumed) == BM_STATUS_OK);
    assert(consumed == 2U && state_of(&cpu).ip == 0xfff2U);
    assert(rom.fetches == 2U);
    cpu.ops.destroy(cpu.context);
}
static void test_partial_cleanup(void)
{
    bus_fixture_t rom;
    bm_286_config_t c;
    size_t fail;
    int reached_success = 0;
    fixture_init(&rom, 0xffff00U);
    c = config_for(&rom);
    for (fail = 0U; fail < 64U; ++fail) {
        failure_injection_host_t tracker;
        bm_host_services_t host;
        bm_cpu_t cpu = {0};
        bm_status_t status;
        failure_injection_host_initialize(&tracker);
        failure_injection_host_fail_on(&tracker, fail);
        host = failure_injection_host_services(&tracker);
        status = bm_286_create(&host, &c, &cpu);
        if (status == BM_STATUS_OK) {
            cpu.ops.destroy(cpu.context);
            reached_success = 1;
        } else {
            assert(status == BM_STATUS_OUT_OF_MEMORY);
            assert(cpu.context == NULL && cpu.ops.destroy == NULL);
        }
        assert(tracker.outstanding_allocations == 0U);
        if (reached_success) break;
    }
    assert(reached_success);
}
int main(void)
{
    test_reset_and_instances();
    test_state_import_and_sampled_trap();
    test_strict_clocked_stops_without_fetch();
    test_hold_and_invalid_import();
    test_invalid_creation_clears_output();
    test_bus_failure_latches_without_retry();
    test_halt_shutdown_and_pending_signals();
    test_unknown_timing_is_lower_bound();
    test_run_is_instruction_count_only();
    test_partial_cleanup();
    return 0;
}
