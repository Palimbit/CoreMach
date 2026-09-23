/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Architectural-boundary policy gate, not T-state electrical conformance.
 */
#include "bus_fixture.h"
#include <blumach/components/at_bus.h>
#include <blumach/platforms/null_host.h>
#include <assert.h>

typedef struct endpoint {
    bus_fixture_t memory;
    bm_at_master_t master;
    int hold;
    size_t edges;
} endpoint_t;
static void hold_changed(void *context, int level)
{
    endpoint_t *e = context;
    e->hold = level;
    ++e->edges;
}
static bm_status_t access_memory(void *context, bm_at_transfer_t *t)
{
    endpoint_t *e = context;
    e->master = t->master;
    return fixture_access(&e->memory, &t->bus);
}
int main(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_at_bus_config_t c = {0};
    bm_at_bus_t *bus = NULL;
    bm_at_transfer_t t = {0};
    endpoint_t e = {0};
    size_t edges, calls;
    fixture_init(&e.memory, 0U);
    e.memory.bytes[0] = 0x42U;
    e.memory.waits = 3U; /* already in requester clocks at this endpoint */
    c.cpu_clock = (bm_clock_rate_t){12000000U, 1U};
    c.isa_clock = (bm_clock_rate_t){8000000U, 1U}; /* synthetic test rate */
    c.memory = access_memory;
    c.io = access_memory;
    c.decode_context = &e;
    c.hold = hold_changed;
    c.hold_context = &e;
    assert(bm_at_bus_create(&host, &c, &bus) == BM_STATUS_OK);
    t.master = BM_AT_MASTER_CPU;
    t.requester_clock = c.cpu_clock;
    t.bus.size = 1U;
    t.bus.operation = BM_BUS_READ;
    assert(bm_at_bus_access(bus, &t) == BM_STATUS_OK);
    assert(t.bus.value == 0x42U && t.bus.wait_states == 3U);
    assert(bm_at_bus_set_lock(bus, 1) == BM_STATUS_OK);
    assert(bm_at_bus_request(bus, BM_AT_MASTER_DMA8, 1) == BM_STATUS_OK);
    assert(e.hold == 0);
    assert(bm_at_bus_hold_ack(bus, 1) == BM_STATUS_INVALID_STATE);
    assert(bm_at_bus_set_lock(bus, 0) == BM_STATUS_OK);
    assert(e.hold == 1);
    edges = e.edges;
    assert(bm_at_bus_request(bus, BM_AT_MASTER_DMA8, 1) == BM_STATUS_OK);
    assert(e.edges == edges);
    assert(bm_at_bus_request(bus, BM_AT_MASTER_ISA, 1) == BM_STATUS_UNSUPPORTED);
    t.master = BM_AT_MASTER_DMA8;
    t.requester_clock = c.isa_clock;
    t.bus.wait_states = 0U;
    calls = e.memory.trace_count;
    assert(bm_at_bus_access(bus, &t) == BM_STATUS_IDLE);
    assert(t.bus.wait_states == 0U && t.bus.value == 0x42U);
    assert(e.memory.trace_count == calls);
    assert(bm_at_bus_hold_ack(bus, 1) == BM_STATUS_OK);
    assert(bm_at_bus_access(bus, &t) == BM_STATUS_OK);
    assert(e.master == BM_AT_MASTER_DMA8);
    t.master = BM_AT_MASTER_CPU;
    t.requester_clock = c.cpu_clock;
    t.bus.wait_states = 0U;
    calls = e.memory.trace_count;
    assert(bm_at_bus_access(bus, &t) == BM_STATUS_IDLE);
    assert(e.memory.trace_count == calls);
    assert(bm_at_bus_set_lock(bus, 1) == BM_STATUS_INVALID_STATE);
    t.bus.attributes = BM_BUS_TRANSACTION_DEBUG;
    assert(bm_at_bus_access(bus, &t) == BM_STATUS_OK);
    assert(t.bus.wait_states == 0U && e.edges == edges);
    t.bus.attributes = 0U;
    assert(bm_at_bus_request(bus, BM_AT_MASTER_DMA8, 0) == BM_STATUS_OK);
    assert(e.hold == 0);
    assert(bm_at_bus_access(bus, &t) == BM_STATUS_IDLE);
    assert(bm_at_bus_hold_ack(bus, 0) == BM_STATUS_OK);
    assert(bm_at_bus_access(bus, &t) == BM_STATUS_OK);
    bm_at_bus_reset(bus);
    assert(e.hold == 0);
    assert(bm_at_bus_hold_ack(bus, 1) == BM_STATUS_INVALID_STATE);
    bm_at_bus_destroy(bus);
    bm_at_bus_destroy(NULL);
    return 0;
}
