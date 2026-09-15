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

typedef struct bm_bus_transaction {
    bm_address_space_t space;
    bm_bus_operation_t operation;
    uint64_t address;
    uint64_t value;
    uint32_t size;
    uint32_t alignment;
    uint32_t wait_states;
    bm_endianness_t endianness;
    int debug_access;
} bm_bus_transaction_t;

typedef bm_status_t (*bm_bus_access_fn)(void *context, bm_bus_transaction_t *transaction);
typedef void (*bm_bus_observer_fn)(void *context, const bm_bus_transaction_t *transaction);

bm_status_t bm_bus_create(const bm_host_services_t *host, size_t max_mappings, bm_bus_t **out_bus);
void bm_bus_destroy(bm_bus_t *bus);
bm_status_t bm_bus_map(bm_bus_t *bus,
                       bm_address_space_t space,
                       uint64_t first,
                       uint64_t last,
                       bm_bus_access_fn access,
                       void *context);
bm_status_t bm_bus_transact(bm_bus_t *bus, bm_bus_transaction_t *transaction);
void bm_bus_set_observer(bm_bus_t *bus, bm_bus_observer_fn observer, void *context);

#ifdef __cplusplus
}
#endif

#endif
