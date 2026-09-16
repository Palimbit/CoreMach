/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/components/bus.h>
#include <blumach/platforms/null_host.h>

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct allocation_tracker {
    size_t calls;
    size_t fail_on_call;
    size_t outstanding;
} allocation_tracker_t;

typedef struct access_sink {
    bm_status_t status;
    uint64_t read_value;
    uint32_t added_wait_states;
    size_t calls;
    bm_bus_transaction_t last;
} access_sink_t;

typedef struct observer_sink {
    size_t calls;
    bm_bus_transaction_t last;
} observer_sink_t;

static void *
tracked_allocate(void *context, size_t size)
{
    allocation_tracker_t *tracker = context;
    void *allocation;

    if (tracker->calls++ == tracker->fail_on_call)
        return NULL;
    allocation = malloc(size);
    if (allocation != NULL)
        ++tracker->outstanding;
    return allocation;
}

static void
tracked_release(void *context, void *allocation)
{
    allocation_tracker_t *tracker = context;

    if (allocation != NULL) {
        assert(tracker->outstanding > 0U);
        --tracker->outstanding;
    }
    free(allocation);
}

static bm_tick_t
tracked_time(void *context)
{
    (void) context;
    return 0U;
}

static void
tracked_log(void *context, bm_log_level_t level, const char *message)
{
    (void) context;
    (void) level;
    (void) message;
}

static bm_status_t
capture_access(void *context, bm_bus_transaction_t *transaction)
{
    access_sink_t *sink = context;

    ++sink->calls;
    sink->last = *transaction;
    if (sink->status != BM_STATUS_OK)
        return sink->status;
    if ((transaction->operation == BM_BUS_READ) ||
        (transaction->operation == BM_BUS_FETCH))
        transaction->value = sink->read_value;
    transaction->wait_states += sink->added_wait_states;
    return BM_STATUS_OK;
}

static void
capture_observer(void *context, const bm_bus_transaction_t *transaction)
{
    observer_sink_t *sink = context;

    ++sink->calls;
    sink->last = *transaction;
}

static bm_bus_transaction_t
make_transaction(bm_address_space_t space,
                 bm_bus_operation_t operation,
                 uint64_t address,
                 uint32_t size)
{
    bm_bus_transaction_t transaction = {
        space,
        operation,
        address,
        UINT64_C(0x1122334455667788),
        size,
        1U,
        2U,
        BM_ENDIAN_LITTLE,
        0
    };
    return transaction;
}

static void
test_creation_and_partial_cleanup(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_host_services_t invalid_host = host;
    bm_bus_t *bus = NULL;
    size_t failure;

    assert(bm_bus_create(NULL, 1U, &bus) == BM_STATUS_INVALID_ARGUMENT);
    invalid_host.release = NULL;
    assert(bm_bus_create(&invalid_host, 1U, &bus) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_bus_create(&host, 0U, &bus) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_bus_create(&host, SIZE_MAX, &bus) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_bus_create(&host, 1U, NULL) == BM_STATUS_INVALID_ARGUMENT);

    for (failure = 0U; failure < 2U; ++failure) {
        allocation_tracker_t tracker = { 0U, failure, 0U };
        bm_host_services_t tracked_host = {
            &tracker,
            tracked_allocate,
            tracked_release,
            tracked_time,
            tracked_log
        };

        assert(bm_bus_create(&tracked_host, 1U, &bus) ==
               BM_STATUS_OUT_OF_MEMORY);
        assert(bus == NULL);
        assert(tracker.outstanding == 0U);
    }

    assert(bm_bus_create(&host, 1U, &bus) == BM_STATUS_OK);
    assert(bus != NULL);
    bm_bus_destroy(bus);
    bm_bus_destroy(NULL);
}

static void
test_mapping_contracts(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_bus_t *bus = NULL;
    access_sink_t memory = { 0 };
    access_sink_t io = { 0 };
    access_sink_t adjacent = { 0 };
    access_sink_t data = { 0 };

    assert(bm_bus_create(&host, 4U, &bus) == BM_STATUS_OK);
    assert(bm_bus_map(NULL, BM_ADDRESS_MEMORY, 0U, 0U, capture_access,
                      &memory) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_bus_map(bus, (bm_address_space_t) 99, 0U, 0U, capture_access,
                      &memory) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_bus_map(bus, BM_ADDRESS_MEMORY, 1U, 0U, capture_access,
                      &memory) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_bus_map(bus, BM_ADDRESS_MEMORY, 0U, 0U, NULL, &memory) ==
           BM_STATUS_INVALID_ARGUMENT);

    assert(bm_bus_map(bus, BM_ADDRESS_MEMORY, 0x100U, 0x10fU,
                      capture_access, &memory) == BM_STATUS_OK);
    assert(bm_bus_map(bus, BM_ADDRESS_MEMORY, 0x0f0U, 0x100U,
                      capture_access, &memory) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_bus_map(bus, BM_ADDRESS_MEMORY, 0x108U, 0x120U,
                      capture_access, &memory) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_bus_map(bus, BM_ADDRESS_MEMORY, 0x100U, 0x10fU,
                      capture_access, &memory) == BM_STATUS_INVALID_ARGUMENT);

    assert(bm_bus_map(bus, BM_ADDRESS_IO, 0x100U, 0x10fU,
                      capture_access, &io) == BM_STATUS_OK);
    assert(bm_bus_map(bus, BM_ADDRESS_MEMORY, 0x110U, 0x11fU,
                      capture_access, &adjacent) == BM_STATUS_OK);
    assert(bm_bus_map(bus, BM_ADDRESS_DATA, 0U, UINT64_MAX,
                      capture_access, &data) == BM_STATUS_OK);
    assert(bm_bus_map(bus, BM_ADDRESS_PROGRAM, 0U, 0U, capture_access,
                      &memory) == BM_STATUS_CAPACITY_EXCEEDED);
    bm_bus_destroy(bus);
}

static void
test_transaction_validation(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_bus_t *bus = NULL;
    access_sink_t sink = { 0 };
    bm_bus_transaction_t transaction;

    assert(bm_bus_create(&host, 1U, &bus) == BM_STATUS_OK);
    assert(bm_bus_map(bus, BM_ADDRESS_MEMORY, 0U, UINT64_MAX,
                      capture_access, &sink) == BM_STATUS_OK);
    transaction = make_transaction(BM_ADDRESS_MEMORY, BM_BUS_READ, 0U, 1U);

    assert(bm_bus_transact(NULL, &transaction) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_bus_transact(bus, NULL) == BM_STATUS_INVALID_ARGUMENT);
    transaction.space = (bm_address_space_t) -1;
    assert(bm_bus_transact(bus, &transaction) == BM_STATUS_INVALID_ARGUMENT);
    transaction = make_transaction(BM_ADDRESS_MEMORY, (bm_bus_operation_t) 99,
                                   0U, 1U);
    assert(bm_bus_transact(bus, &transaction) == BM_STATUS_INVALID_ARGUMENT);
    transaction = make_transaction(BM_ADDRESS_MEMORY, BM_BUS_READ, 0U, 1U);
    transaction.endianness = (bm_endianness_t) 99;
    assert(bm_bus_transact(bus, &transaction) == BM_STATUS_INVALID_ARGUMENT);
    transaction = make_transaction(BM_ADDRESS_MEMORY, BM_BUS_READ, 0U, 0U);
    assert(bm_bus_transact(bus, &transaction) == BM_STATUS_INVALID_ARGUMENT);
    transaction = make_transaction(BM_ADDRESS_MEMORY, BM_BUS_READ, 0U, 9U);
    assert(bm_bus_transact(bus, &transaction) == BM_STATUS_INVALID_ARGUMENT);
    transaction = make_transaction(BM_ADDRESS_MEMORY, BM_BUS_READ,
                                   UINT64_MAX - 6U, 8U);
    assert(bm_bus_transact(bus, &transaction) == BM_STATUS_INVALID_ARGUMENT);
    assert(sink.calls == 0U);

    transaction = make_transaction(BM_ADDRESS_MEMORY, BM_BUS_READ, 2U, 4U);
    transaction.alignment = 4U;
    assert(bm_bus_transact(bus, &transaction) == BM_STATUS_DEVICE_ERROR);
    assert(sink.calls == 0U);
    bm_bus_destroy(bus);
}

static void
test_routing_metadata_and_observer(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_bus_t *bus = NULL;
    access_sink_t memory = { 0 };
    access_sink_t io = { 0 };
    observer_sink_t observer = { 0 };
    bm_bus_transaction_t transaction;

    memory.read_value = UINT64_C(0xa1b2c3d4);
    memory.added_wait_states = 3U;
    io.read_value = 0x5aU;
    assert(bm_bus_create(&host, 2U, &bus) == BM_STATUS_OK);
    assert(bm_bus_map(bus, BM_ADDRESS_MEMORY, 0x100U, 0x10fU,
                      capture_access, &memory) == BM_STATUS_OK);
    assert(bm_bus_map(bus, BM_ADDRESS_IO, 0x100U, 0x10fU,
                      capture_access, &io) == BM_STATUS_OK);
    bm_bus_set_observer(bus, capture_observer, &observer);

    transaction = make_transaction(BM_ADDRESS_MEMORY, BM_BUS_READ, 0x104U, 4U);
    transaction.alignment = 4U;
    transaction.endianness = BM_ENDIAN_BIG;
    transaction.debug_access = 1;
    assert(bm_bus_transact(bus, &transaction) == BM_STATUS_OK);
    assert(memory.calls == 1U);
    assert(io.calls == 0U);
    assert(memory.last.space == BM_ADDRESS_MEMORY);
    assert(memory.last.operation == BM_BUS_READ);
    assert(memory.last.address == 0x104U);
    assert(memory.last.size == 4U);
    assert(memory.last.endianness == BM_ENDIAN_BIG);
    assert(memory.last.wait_states == 2U);
    assert(memory.last.debug_access == 1);
    assert(transaction.value == UINT64_C(0xa1b2c3d4));
    assert(transaction.wait_states == 5U);
    assert(observer.calls == 1U);
    assert(observer.last.value == transaction.value);
    assert(observer.last.wait_states == transaction.wait_states);

    transaction = make_transaction(BM_ADDRESS_IO, BM_BUS_FETCH, 0x100U, 1U);
    assert(bm_bus_transact(bus, &transaction) == BM_STATUS_OK);
    assert(io.calls == 1U);
    assert(transaction.value == 0x5aU);
    assert(observer.calls == 2U);

    transaction = make_transaction(BM_ADDRESS_MEMORY, BM_BUS_WRITE, 0x10fU, 2U);
    assert(bm_bus_transact(bus, &transaction) == BM_STATUS_UNMAPPED);
    assert(memory.calls == 1U);
    assert(observer.calls == 2U);

    memory.status = BM_STATUS_DEVICE_ERROR;
    transaction = make_transaction(BM_ADDRESS_MEMORY, BM_BUS_WRITE, 0x100U, 1U);
    assert(bm_bus_transact(bus, &transaction) == BM_STATUS_DEVICE_ERROR);
    assert(memory.calls == 2U);
    assert(observer.calls == 2U);

    memory.status = BM_STATUS_OK;
    bm_bus_set_observer(bus, NULL, NULL);
    transaction = make_transaction(BM_ADDRESS_MEMORY, BM_BUS_WRITE, 0x100U, 1U);
    assert(bm_bus_transact(bus, &transaction) == BM_STATUS_OK);
    assert(observer.calls == 2U);
    bm_bus_set_observer(NULL, capture_observer, &observer);
    bm_bus_destroy(bus);
}

int
main(void)
{
    test_creation_and_partial_cleanup();
    test_mapping_contracts();
    test_transaction_validation();
    test_routing_metadata_and_observer();
    return 0;
}
