/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/components/bus.h>

#include <string.h>

typedef struct bm_bus_mapping {
    bm_address_space_t space;
    uint64_t first;
    uint64_t last;
    bm_bus_access_fn access;
    void *context;
    bm_bus_static_response_t response;
    int is_static;
} bm_bus_mapping_t;

struct bm_bus {
    bm_host_services_t host;
    bm_bus_mapping_t *mappings;
    size_t count;
    size_t capacity;
    bm_bus_observer_fn observer;
    void *observer_context;
    bm_bus_static_response_t default_response[BM_ADDRESS_DATA + 1];
    int has_default_response[BM_ADDRESS_DATA + 1];
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

static int
valid_response_status(bm_status_t status)
{
    return (status == BM_STATUS_OK) || (status == BM_STATUS_UNMAPPED) ||
           (status == BM_STATUS_DEVICE_ERROR) ||
           (status == BM_STATUS_READ_ONLY) ||
           (status == BM_STATUS_UNSUPPORTED);
}

static int
valid_static_response(const bm_bus_static_response_t *response)
{
    return (response != NULL) && valid_response_status(response->read_status) &&
           valid_response_status(response->write_status) &&
           valid_response_status(response->fetch_status);
}

static bm_status_t
static_response_access(const bm_bus_static_response_t *response,
                       bm_bus_transaction_t *transaction)
{
    bm_status_t status;
    uint32_t index;

    if (transaction->operation == BM_BUS_READ)
        status = response->read_status;
    else if (transaction->operation == BM_BUS_WRITE)
        status = response->write_status;
    else
        status = response->fetch_status;
    if ((status != BM_STATUS_OK) ||
        (transaction->operation == BM_BUS_WRITE))
        return status;
    transaction->value = 0U;
    for (index = 0U; index < transaction->size; ++index)
        transaction->value |=
            (uint64_t) response->fill_value << (index * 8U);
    return BM_STATUS_OK;
}

static bm_status_t
add_mapping(bm_bus_t *bus,
            bm_address_space_t space,
            uint64_t first,
            uint64_t last,
            bm_bus_access_fn access,
            void *context,
            const bm_bus_static_response_t *response)
{
    size_t index;

    if ((bus == NULL) || !valid_address_space(space) || (first > last) ||
        ((access == NULL) == (response == NULL)) ||
        ((response != NULL) && !valid_static_response(response)))
        return BM_STATUS_INVALID_ARGUMENT;
    if (bus->count >= bus->capacity)
        return BM_STATUS_CAPACITY_EXCEEDED;
    for (index = 0; index < bus->count; ++index) {
        const bm_bus_mapping_t *mapping = &bus->mappings[index];
        if ((mapping->space == space) && (first <= mapping->last) &&
            (last >= mapping->first))
            return BM_STATUS_INVALID_ARGUMENT;
    }
    bus->mappings[bus->count] = (bm_bus_mapping_t) {
        .space = space,
        .first = first,
        .last = last,
        .access = access,
        .context = context,
        .is_static = response != NULL
    };
    if (response != NULL)
        bus->mappings[bus->count].response = *response;
    ++bus->count;
    return BM_STATUS_OK;
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
    return add_mapping(bus, space, first, last, access, context, NULL);
}

bm_status_t
bm_bus_map_static_response(bm_bus_t *bus,
                           bm_address_space_t space,
                           uint64_t first,
                           uint64_t last,
                           const bm_bus_static_response_t *response)
{
    return add_mapping(bus, space, first, last, NULL, NULL, response);
}

bm_status_t
bm_bus_set_default_response(bm_bus_t *bus,
                            bm_address_space_t space,
                            const bm_bus_static_response_t *response)
{
    if ((bus == NULL) || !valid_address_space(space))
        return BM_STATUS_INVALID_ARGUMENT;
    if (response == NULL) {
        bus->has_default_response[space] = 0;
        memset(&bus->default_response[space], 0,
               sizeof(bus->default_response[space]));
        return BM_STATUS_OK;
    }
    if (!valid_static_response(response))
        return BM_STATUS_INVALID_ARGUMENT;
    bus->default_response[space] = *response;
    bus->has_default_response[space] = 1;
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
        ((transaction->attributes &
          ~(BM_BUS_TRANSACTION_DEBUG | BM_BUS_TRANSACTION_LOCKED)) != 0U) ||
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
            bm_status_t status = mapping->is_static ?
                static_response_access(&mapping->response, transaction) :
                mapping->access(mapping->context, transaction);
            if (status != BM_STATUS_UNMAPPED) {
                if ((status == BM_STATUS_OK) && (bus->observer != NULL))
                    bus->observer(bus->observer_context, transaction);
                return status;
            }
            break;
        }
    }
    if (bus->has_default_response[transaction->space]) {
        bm_status_t status = static_response_access(
            &bus->default_response[transaction->space], transaction);
        if ((status == BM_STATUS_OK) && (bus->observer != NULL))
            bus->observer(bus->observer_context, transaction);
        return status;
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
