/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/components/bus.h>

#include <string.h>

typedef struct bm_bus_mapping {
    bm_address_space_t space;
    uint64_t first;
    uint64_t last;
    bm_bus_access_fn access;
    void *context;
} bm_bus_mapping_t;

struct bm_bus {
    bm_host_services_t host;
    bm_bus_mapping_t *mappings;
    size_t count;
    size_t capacity;
    bm_bus_observer_fn observer;
    void *observer_context;
};

static int
valid_address_space(bm_address_space_t space)
{
    return (space >= BM_ADDRESS_MEMORY) && (space <= BM_ADDRESS_DATA);
}

static int
valid_operation(bm_bus_operation_t operation)
{
    return (operation >= BM_BUS_READ) && (operation <= BM_BUS_FETCH);
}

static int
valid_endianness(bm_endianness_t endianness)
{
    return (endianness >= BM_ENDIAN_LITTLE) && (endianness <= BM_ENDIAN_BIG);
}

bm_status_t
bm_bus_create(const bm_host_services_t *host, size_t max_mappings, bm_bus_t **out_bus)
{
    bm_bus_t *bus;

    if ((bm_host_services_validate(host) != BM_STATUS_OK) || (max_mappings == 0) ||
        (max_mappings > (SIZE_MAX / sizeof(bm_bus_mapping_t))) || (out_bus == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    *out_bus = NULL;
    bus = host->allocate(host->context, sizeof(*bus));
    if (bus == NULL)
        return BM_STATUS_OUT_OF_MEMORY;
    memset(bus, 0, sizeof(*bus));
    bus->host = *host;
    bus->mappings = host->allocate(host->context, max_mappings * sizeof(*bus->mappings));
    if (bus->mappings == NULL) {
        host->release(host->context, bus);
        return BM_STATUS_OUT_OF_MEMORY;
    }
    memset(bus->mappings, 0, max_mappings * sizeof(*bus->mappings));
    bus->capacity = max_mappings;
    *out_bus = bus;
    return BM_STATUS_OK;
}

void
bm_bus_destroy(bm_bus_t *bus)
{
    if (bus == NULL)
        return;
    bus->host.release(bus->host.context, bus->mappings);
    bus->host.release(bus->host.context, bus);
}

bm_status_t
bm_bus_map(bm_bus_t *bus,
           bm_address_space_t space,
           uint64_t first,
           uint64_t last,
           bm_bus_access_fn access,
           void *context)
{
    size_t index;

    if ((bus == NULL) || !valid_address_space(space) || (access == NULL) ||
        (first > last))
        return BM_STATUS_INVALID_ARGUMENT;
    if (bus->count >= bus->capacity)
        return BM_STATUS_CAPACITY_EXCEEDED;
    for (index = 0; index < bus->count; ++index) {
        const bm_bus_mapping_t *mapping = &bus->mappings[index];
        if ((mapping->space == space) && (first <= mapping->last) && (last >= mapping->first))
            return BM_STATUS_INVALID_ARGUMENT;
    }
    bus->mappings[bus->count++] = (bm_bus_mapping_t) { space, first, last, access, context };
    return BM_STATUS_OK;
}

bm_status_t
bm_bus_transact(bm_bus_t *bus, bm_bus_transaction_t *transaction)
{
    size_t index;
    uint64_t last;

    if ((bus == NULL) || (transaction == NULL) ||
        !valid_address_space(transaction->space) ||
        !valid_operation(transaction->operation) ||
        !valid_endianness(transaction->endianness) ||
        (transaction->size == 0) || (transaction->size > 8) ||
        (transaction->address > (UINT64_MAX - transaction->size + 1)))
        return BM_STATUS_INVALID_ARGUMENT;
    last = transaction->address + transaction->size - 1;
    if ((transaction->alignment != 0) && ((transaction->address % transaction->alignment) != 0))
        return BM_STATUS_DEVICE_ERROR;
    for (index = 0; index < bus->count; ++index) {
        bm_bus_mapping_t *mapping = &bus->mappings[index];
        if ((mapping->space == transaction->space) && (transaction->address >= mapping->first) &&
            (last <= mapping->last)) {
            bm_status_t status = mapping->access(mapping->context, transaction);
            if ((status == BM_STATUS_OK) && (bus->observer != NULL))
                bus->observer(bus->observer_context, transaction);
            return status;
        }
    }
    return BM_STATUS_UNMAPPED;
}

void
bm_bus_set_observer(bm_bus_t *bus, bm_bus_observer_fn observer, void *context)
{
    if (bus == NULL)
        return;
    bus->observer = observer;
    bus->observer_context = context;
}
