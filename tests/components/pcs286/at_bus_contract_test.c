/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Authored architectural-boundary policy tests, not electrical cycle tests.
 */
#include <blumach/components/at_bus.h>
#include <blumach/platforms/null_host.h>

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct test_host {
    unsigned int allocations;
    unsigned int outstanding;
    int fail_next;
} test_host_t;

typedef struct endpoint {
    unsigned int memory_calls;
    unsigned int io_calls;
    unsigned int normal_reads;
    unsigned int normal_writes;
    unsigned int debug_reads;
    unsigned int hold_edges;
    int hold;
    bm_at_master_t last_master;
    bm_clock_rate_t last_clock;
    uint32_t last_attributes;
    uint8_t cell;
    uint32_t waits;
    bm_status_t result;
} endpoint_t;

static void *
test_allocate(void *context, size_t size)
{
    test_host_t *tracker = context;
    void *allocation;
    ++tracker->allocations;
    if (tracker->fail_next) {
        tracker->fail_next = 0;
        return NULL;
    }
    allocation = malloc(size);
    if (allocation != NULL)
        ++tracker->outstanding;
    return allocation;
}

static void
test_release(void *context, void *allocation)
{
    test_host_t *tracker = context;
    assert(allocation != NULL && tracker->outstanding != 0U);
    --tracker->outstanding;
    free(allocation);
}

static bm_host_services_t
tracked_host(test_host_t *tracker)
{
    bm_host_services_t host = bm_null_host_services();
    host.context = tracker;
    host.allocate = test_allocate;
    host.release = test_release;
    return host;
}

static void
hold_changed(void *context, int asserted)
{
    endpoint_t *endpoint = context;
    assert(endpoint->hold != asserted);
    endpoint->hold = asserted;
    ++endpoint->hold_edges;
}

static bm_status_t
route_access(endpoint_t *endpoint, bm_at_transfer_t *transfer, int io)
{
    const int debug = (transfer->bus.attributes & BM_BUS_TRANSACTION_DEBUG) != 0U;
    if (io)
        ++endpoint->io_calls;
    else
        ++endpoint->memory_calls;
    endpoint->last_master = transfer->master;
    endpoint->last_clock = transfer->requester_clock;
    endpoint->last_attributes = transfer->bus.attributes;
    if (endpoint->result != BM_STATUS_OK) {
        transfer->bus.value = 0xffU;
        transfer->bus.wait_states = 99U;
        return endpoint->result;
    }
    if (debug) {
        assert(transfer->bus.operation != BM_BUS_WRITE);
        ++endpoint->debug_reads;
    } else if (transfer->bus.operation == BM_BUS_WRITE) {
        ++endpoint->normal_writes;
        endpoint->cell = (uint8_t) transfer->bus.value;
    } else {
        ++endpoint->normal_reads;
    }
    if (transfer->bus.operation != BM_BUS_WRITE)
        transfer->bus.value = endpoint->cell;
    /* The decode adapter has already converted this count to the requester's
     * clock domain. Deliberately report waits on DEBUG to test suppression. */
    transfer->bus.wait_states = endpoint->waits;
    return BM_STATUS_OK;
}

static bm_status_t
memory_access(void *context, bm_at_transfer_t *transfer)
{
    return route_access(context, transfer, 0);
}

static bm_status_t
io_access(void *context, bm_at_transfer_t *transfer)
{
    return route_access(context, transfer, 1);
}

static bm_at_bus_config_t
config_for(endpoint_t *endpoint)
{
    bm_at_bus_config_t config = {0};
    config.cpu_clock = (bm_clock_rate_t) {12000000U, 1U};
    config.isa_clock = (bm_clock_rate_t) {8000000U, 1U};
    config.memory = memory_access;
    config.io = io_access;
    config.decode_context = endpoint;
    config.hold = hold_changed;
    config.hold_context = endpoint;
    return config;
}

static bm_at_transfer_t
transfer_for(bm_at_master_t master, bm_clock_rate_t clock)
{
    bm_at_transfer_t transfer = {0};
    transfer.master = master;
    transfer.requester_clock = clock;
    transfer.bus.space = BM_ADDRESS_MEMORY;
    transfer.bus.operation = BM_BUS_READ;
    transfer.bus.size = 1U;
    transfer.bus.endianness = BM_ENDIAN_LITTLE;
    return transfer;
}

static void
test_creation_and_failure_cleanup(void)
{
    test_host_t tracker = {0};
    endpoint_t endpoint = {0};
    bm_host_services_t host = tracked_host(&tracker);
    bm_host_services_t bad_host = host;
    bm_at_bus_config_t config = config_for(&endpoint);
    bm_at_bus_t *bus = (bm_at_bus_t *) (uintptr_t) 1U;

    assert(bm_at_bus_create(&host, &config, NULL) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_at_bus_create(NULL, &config, &bus) == BM_STATUS_INVALID_ARGUMENT);
    assert(bus == NULL);
    bus = (bm_at_bus_t *) (uintptr_t) 1U;
    assert(bm_at_bus_create(&host, NULL, &bus) == BM_STATUS_INVALID_ARGUMENT);
    assert(bus == NULL);
    bad_host.release = NULL;
    assert(bm_at_bus_create(&bad_host, &config, &bus) == BM_STATUS_INVALID_ARGUMENT);
    config.memory = NULL;
    assert(bm_at_bus_create(&host, &config, &bus) == BM_STATUS_INVALID_ARGUMENT);
    config = config_for(&endpoint);
    config.io = NULL;
    assert(bm_at_bus_create(&host, &config, &bus) == BM_STATUS_INVALID_ARGUMENT);
    config = config_for(&endpoint);
    config.cpu_clock.cycles_per_second_numerator = 0U;
    assert(bm_at_bus_create(&host, &config, &bus) == BM_STATUS_INVALID_ARGUMENT);
    config = config_for(&endpoint);
    config.isa_clock.cycles_per_second_denominator = 0U;
    assert(bm_at_bus_create(&host, &config, &bus) == BM_STATUS_INVALID_ARGUMENT);
    assert(bus == NULL && tracker.allocations == 0U);

    config = config_for(&endpoint);
    tracker.fail_next = 1;
    assert(bm_at_bus_create(&host, &config, &bus) == BM_STATUS_OUT_OF_MEMORY);
    assert(bus == NULL && tracker.outstanding == 0U);
    assert(bm_at_bus_create(&host, &config, &bus) == BM_STATUS_OK);
    assert(bus != NULL && tracker.outstanding == 1U);
    bm_at_bus_destroy(bus);
    bm_at_bus_destroy(NULL);
    bm_at_bus_reset(NULL);
    assert(tracker.outstanding == 0U);
}

static void
test_transfers_and_errors(void)
{
    bm_host_services_t host = bm_null_host_services();
    endpoint_t endpoint = {0};
    bm_at_bus_config_t config = config_for(&endpoint);
    bm_at_bus_t *bus = NULL;
    bm_at_transfer_t transfer;
    bm_bus_transaction_t cpu;

    endpoint.cell = 0x42U;
    endpoint.waits = 7U;
    assert(bm_at_bus_create(&host, &config, &bus) == BM_STATUS_OK);
    /* Config is copied; changing the caller's copy cannot reroute the bus. */
    config.cpu_clock.cycles_per_second_numerator = 1U;
    config.memory = NULL;
    transfer = transfer_for(BM_AT_MASTER_CPU,
                            (bm_clock_rate_t) {12000000U, 1U});
    assert(bm_at_bus_access(bus, &transfer) == BM_STATUS_OK);
    assert(transfer.bus.value == 0x42U && transfer.bus.wait_states == 7U);
    assert(endpoint.last_master == BM_AT_MASTER_CPU);
    assert(endpoint.last_clock.cycles_per_second_numerator == 12000000U);
    assert(endpoint.memory_calls == 1U && endpoint.io_calls == 0U);

    cpu = transfer.bus;
    cpu.space = BM_ADDRESS_IO;
    cpu.wait_states = 0U;
    assert(bm_at_bus_cpu_access(bus, &cpu) == BM_STATUS_OK);
    assert(cpu.wait_states == 7U && endpoint.io_calls == 1U);
    assert(endpoint.last_clock.cycles_per_second_numerator == 12000000U);
    assert(bm_at_bus_cpu_access(NULL, &cpu) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_at_bus_cpu_access(bus, NULL) == BM_STATUS_INVALID_ARGUMENT);

    transfer = transfer_for(BM_AT_MASTER_CPU,
                            (bm_clock_rate_t) {12000000U, 1U});
    transfer.bus.wait_states = 1U;
    assert(bm_at_bus_access(bus, &transfer) == BM_STATUS_INVALID_ARGUMENT);
    transfer.bus.wait_states = 0U;
    transfer.requester_clock.cycles_per_second_denominator = 0U;
    assert(bm_at_bus_access(bus, &transfer) == BM_STATUS_INVALID_ARGUMENT);
    transfer.requester_clock.cycles_per_second_denominator = 1U;
    transfer.bus.size = 0U;
    assert(bm_at_bus_access(bus, &transfer) == BM_STATUS_INVALID_ARGUMENT);
    transfer.bus.size = 1U;
    transfer.bus.attributes = 0x80000000U;
    assert(bm_at_bus_access(bus, &transfer) == BM_STATUS_INVALID_ARGUMENT);
    transfer.bus.attributes = 0U;
    transfer.master = (bm_at_master_t) 99;
    assert(bm_at_bus_access(bus, &transfer) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_at_bus_access(bus, NULL) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_at_bus_access(NULL, &transfer) == BM_STATUS_INVALID_ARGUMENT);
    assert(endpoint.memory_calls == 1U && endpoint.io_calls == 1U);

    transfer = transfer_for(BM_AT_MASTER_CPU,
                            (bm_clock_rate_t) {12000000U, 1U});
    endpoint.result = BM_STATUS_DEVICE_ERROR;
    assert(bm_at_bus_access(bus, &transfer) == BM_STATUS_DEVICE_ERROR);
    assert(transfer.bus.value == 0U && transfer.bus.wait_states == 0U);
    bm_at_bus_destroy(bus);
}

static void
test_ownership_and_debug(void)
{
    bm_host_services_t host = bm_null_host_services();
    endpoint_t a = {0}, b = {0};
    bm_at_bus_config_t ca = config_for(&a), cb = config_for(&b);
    bm_at_bus_t *first = NULL, *second = NULL;
    bm_at_transfer_t cpu = transfer_for(BM_AT_MASTER_CPU, ca.cpu_clock);
    bm_at_transfer_t dma = transfer_for(BM_AT_MASTER_DMA8,
                                         (bm_clock_rate_t) {4000000U, 1U});
    bm_at_transfer_t isa = transfer_for(BM_AT_MASTER_ISA, cb.isa_clock);
    unsigned int calls, edges;

    a.cell = 0x56U;
    b.cell = 0xa5U;
    a.waits = 11U;
    assert(bm_at_bus_create(&host, &ca, &first) == BM_STATUS_OK);
    assert(bm_at_bus_create(&host, &cb, &second) == BM_STATUS_OK);
    assert(bm_at_bus_access(second, &cpu) == BM_STATUS_OK);
    assert(cpu.bus.value == 0xa5U && b.normal_reads == 1U);
    cpu.bus.value = 0U;
    cpu.bus.wait_states = 0U;

    assert(bm_at_bus_request(first, BM_AT_MASTER_CPU, 1) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_at_bus_request(first, (bm_at_master_t) 99, 1) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_at_bus_request(NULL, BM_AT_MASTER_DMA8, 1) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_at_bus_set_lock(NULL, 1) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_at_bus_hold_ack(NULL, 1) == BM_STATUS_INVALID_ARGUMENT);
    calls = a.memory_calls;
    assert(bm_at_bus_access(first, &dma) == BM_STATUS_IDLE);
    assert(a.memory_calls == calls && dma.bus.value == 0U && dma.bus.wait_states == 0U);
    assert(bm_at_bus_hold_ack(first, 1) == BM_STATUS_INVALID_STATE);

    assert(bm_at_bus_set_lock(first, 1) == BM_STATUS_OK);
    assert(bm_at_bus_set_lock(first, 1) == BM_STATUS_OK);
    assert(bm_at_bus_request(first, BM_AT_MASTER_DMA8, 1) == BM_STATUS_OK);
    assert(a.hold == 0 && a.hold_edges == 0U);
    assert(bm_at_bus_hold_ack(first, 1) == BM_STATUS_INVALID_STATE);
    assert(bm_at_bus_access(first, &cpu) == BM_STATUS_OK);
    assert(cpu.bus.value == 0x56U && cpu.bus.wait_states == 11U);
    cpu.bus.value = 0U;
    cpu.bus.wait_states = 0U;
    calls = a.memory_calls;
    assert(bm_at_bus_set_lock(first, 0) == BM_STATUS_OK);
    assert(a.hold == 1 && a.hold_edges == 1U);
    edges = a.hold_edges;
    assert(bm_at_bus_request(first, BM_AT_MASTER_DMA8, 1) == BM_STATUS_OK);
    assert(bm_at_bus_request(first, BM_AT_MASTER_ISA, 1) == BM_STATUS_UNSUPPORTED);
    assert(bm_at_bus_request(first, BM_AT_MASTER_ISA, 0) == BM_STATUS_OK);
    assert(a.hold_edges == edges);
    assert(bm_at_bus_access(first, &dma) == BM_STATUS_IDLE);
    assert(a.memory_calls == calls);
    assert(bm_at_bus_access(first, &cpu) == BM_STATUS_OK);
    assert(cpu.bus.value == 0x56U && cpu.bus.wait_states == 11U);
    cpu.bus.value = 0U;
    cpu.bus.wait_states = 0U;
    calls = a.memory_calls;

    assert(bm_at_bus_set_lock(first, 1) == BM_STATUS_OK);
    assert(a.hold == 0 && a.hold_edges == 2U);
    assert(bm_at_bus_set_lock(first, 0) == BM_STATUS_OK);
    assert(a.hold == 1 && a.hold_edges == 3U);
    assert(bm_at_bus_hold_ack(first, 1) == BM_STATUS_OK);
    assert(bm_at_bus_hold_ack(first, 1) == BM_STATUS_OK);
    assert(bm_at_bus_set_lock(first, 1) == BM_STATUS_INVALID_STATE);
    assert(a.hold_edges == 3U);
    assert(bm_at_bus_access(first, &dma) == BM_STATUS_OK);
    assert(dma.bus.value == 0x56U && dma.bus.wait_states == 11U);
    assert(a.last_master == BM_AT_MASTER_DMA8);
    assert(a.last_clock.cycles_per_second_numerator == 4000000U);
    assert(a.last_clock.cycles_per_second_numerator != ca.isa_clock.cycles_per_second_numerator);
    calls = a.memory_calls;
    assert(bm_at_bus_access(first, &cpu) == BM_STATUS_IDLE);
    assert(a.memory_calls == calls && cpu.bus.value == 0U && cpu.bus.wait_states == 0U);

    cpu.bus.attributes = BM_BUS_TRANSACTION_DEBUG;
    assert(bm_at_bus_access(first, &cpu) == BM_STATUS_OK);
    assert(cpu.bus.value == 0x56U && cpu.bus.wait_states == 0U);
    assert(a.debug_reads == 1U && a.normal_reads == 3U);
    assert(a.last_attributes == BM_BUS_TRANSACTION_DEBUG);
    assert(a.hold_edges == 3U);
    cpu.bus.operation = BM_BUS_WRITE;
    cpu.bus.value = 0x99U;
    calls = a.memory_calls;
    assert(bm_at_bus_access(first, &cpu) == BM_STATUS_UNSUPPORTED);
    assert(a.memory_calls == calls && a.cell == 0x56U && cpu.bus.value == 0x99U);
    cpu.bus.operation = BM_BUS_READ;
    cpu.bus.attributes = 0U;
    cpu.bus.value = 0U;
    calls = a.memory_calls;

    assert(bm_at_bus_request(first, BM_AT_MASTER_DMA8, 0) == BM_STATUS_OK);
    assert(a.hold == 0 && a.hold_edges == 4U);
    assert(bm_at_bus_request(first, BM_AT_MASTER_DMA8, 0) == BM_STATUS_OK);
    assert(a.hold_edges == 4U);
    dma.bus.value = 0U;
    dma.bus.wait_states = 0U;
    assert(bm_at_bus_access(first, &cpu) == BM_STATUS_IDLE);
    assert(bm_at_bus_access(first, &dma) == BM_STATUS_IDLE);
    assert(a.memory_calls == calls && cpu.bus.value == 0U && dma.bus.value == 0U);
    assert(bm_at_bus_request(first, BM_AT_MASTER_ISA, 1) == BM_STATUS_INVALID_STATE);
    assert(bm_at_bus_hold_ack(first, 1) == BM_STATUS_OK);
    assert(bm_at_bus_hold_ack(first, 0) == BM_STATUS_OK);
    assert(bm_at_bus_hold_ack(first, 0) == BM_STATUS_OK);
    assert(bm_at_bus_access(first, &cpu) == BM_STATUS_OK);
    assert(cpu.bus.value == 0x56U);

    /* The second instance never inherited the first instance's grant. */
    cpu.bus.value = 0U;
    cpu.bus.wait_states = 0U;
    assert(bm_at_bus_access(second, &cpu) == BM_STATUS_OK);
    assert(cpu.bus.value == 0xa5U);
    assert(bm_at_bus_access(second, &dma) == BM_STATUS_IDLE);
    assert(b.hold_edges == 0U);

    assert(bm_at_bus_request(first, BM_AT_MASTER_ISA, 1) == BM_STATUS_OK);
    assert(a.hold == 1 && a.hold_edges == 5U);
    assert(bm_at_bus_hold_ack(first, 1) == BM_STATUS_OK);
    assert(bm_at_bus_access(first, &isa) == BM_STATUS_OK);
    assert(isa.bus.wait_states == 11U);
    bm_at_bus_reset(first);
    assert(a.hold == 0 && a.hold_edges == 6U);
    bm_at_bus_reset(first);
    assert(a.hold_edges == 6U);
    isa.bus.value = 0U;
    isa.bus.wait_states = 0U;
    assert(bm_at_bus_access(first, &isa) == BM_STATUS_IDLE);
    assert(bm_at_bus_hold_ack(first, 1) == BM_STATUS_INVALID_STATE);
    cpu.bus.value = 0U;
    cpu.bus.wait_states = 0U;
    assert(bm_at_bus_access(first, &cpu) == BM_STATUS_OK);
    assert(cpu.bus.value == 0x56U && cpu.bus.wait_states == 11U);
    bm_at_bus_destroy(first);
    bm_at_bus_destroy(second);
    assert(a.hold_edges == 6U && b.hold_edges == 0U);
}

int
main(void)
{
    test_creation_and_failure_cleanup();
    test_transfers_and_errors();
    test_ownership_and_debug();
    return 0;
}
