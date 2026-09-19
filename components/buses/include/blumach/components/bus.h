/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_COMPONENTS_BUS_H
#define BLUMACH_COMPONENTS_BUS_H

#include <stddef.h>
#include <stdint.h>
#include <blumach/engine/host.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bm_bus bm_bus_t;

typedef enum bm_address_space {
    BM_ADDRESS_MEMORY = 0,
    BM_ADDRESS_IO,
    BM_ADDRESS_PROGRAM,
    BM_ADDRESS_DATA
} bm_address_space_t;

typedef enum bm_bus_operation {
    BM_BUS_READ = 0,
    BM_BUS_WRITE,
    BM_BUS_FETCH
} bm_bus_operation_t;

typedef enum bm_endianness {
    BM_ENDIAN_LITTLE = 0,
    BM_ENDIAN_BIG
} bm_endianness_t;

typedef enum bm_bus_transaction_attribute {
    /* The access is observational and must not have normal guest side effects. */
    BM_BUS_TRANSACTION_DEBUG = 1U << 0,
    /* The initiator holds its bus-lock window across this transaction. */
    BM_BUS_TRANSACTION_LOCKED = 1U << 1
} bm_bus_transaction_attribute_t;

typedef struct bm_bus_transaction {
    bm_address_space_t space;
    bm_bus_operation_t operation;
    uint64_t address;
    uint64_t value;
    uint32_t size;
    uint32_t alignment;
    uint32_t wait_states;
    bm_endianness_t endianness;
    uint32_t attributes;
} bm_bus_transaction_t;

/* A passive bus response has no device-owned state or side effects.  An OK
 * read/fetch repeats fill_value across the transaction; an OK write is
 * acknowledged and discarded.  A non-OK status is returned to the initiator.
 * This keeps electrical bus behaviour separate from strict diagnostic errors. */
typedef struct bm_bus_static_response {
    bm_status_t read_status;
    bm_status_t write_status;
    bm_status_t fetch_status;
    uint8_t fill_value;
} bm_bus_static_response_t;

typedef bm_status_t (*bm_bus_access_fn)(void *context, bm_bus_transaction_t *transaction);
typedef void (*bm_bus_observer_fn)(void *context, const bm_bus_transaction_t *transaction);

/* A mapped callback may return BM_STATUS_UNMAPPED to declare that its device
 * did not service the transaction. The bus discards any callback changes to
 * the transaction and then applies the address-space default, when configured;
 * otherwise BM_STATUS_UNMAPPED reaches the initiator. Changes made before any
 * other returned status remain part of the callback result. */

bm_status_t bm_bus_create(const bm_host_services_t *host, size_t max_mappings, bm_bus_t **out_bus);
void bm_bus_destroy(bm_bus_t *bus);
bm_status_t bm_bus_map(bm_bus_t *bus,
                       bm_address_space_t space,
                       uint64_t first,
                       uint64_t last,
                       bm_bus_access_fn access,
                       void *context);
bm_status_t bm_bus_map_static_response(
    bm_bus_t *bus,
    bm_address_space_t space,
    uint64_t first,
    uint64_t last,
    const bm_bus_static_response_t *response);
bm_status_t bm_bus_set_default_response(
    bm_bus_t *bus,
    bm_address_space_t space,
    const bm_bus_static_response_t *response);
bm_status_t bm_bus_transact(bm_bus_t *bus, bm_bus_transaction_t *transaction);
void bm_bus_set_observer(bm_bus_t *bus, bm_bus_observer_fn observer, void *context);

#ifdef __cplusplus
}
#endif

#endif
