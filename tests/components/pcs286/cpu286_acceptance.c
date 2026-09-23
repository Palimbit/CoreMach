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
    assert((sa.msw & 1U) == 0U);
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
    test_partial_cleanup();
    return 0;
}
