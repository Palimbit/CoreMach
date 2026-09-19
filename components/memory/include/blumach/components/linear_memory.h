/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_COMPONENTS_LINEAR_MEMORY_H
#define BLUMACH_COMPONENTS_LINEAR_MEMORY_H

#include <stddef.h>
#include <stdint.h>
#include <blumach/components/bus.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bm_linear_memory bm_linear_memory_t;

typedef enum bm_linear_memory_write_policy {
    BM_LINEAR_MEMORY_WRITABLE = 0,
    BM_LINEAR_MEMORY_WRITE_REJECT = 1,
    BM_LINEAR_MEMORY_WRITE_IGNORE = 2
} bm_linear_memory_write_policy_t;

typedef struct bm_linear_memory_config {
    bm_address_space_t space;
    uint64_t base;
    size_t size;
    bm_linear_memory_write_policy_t write_policy;
    const uint8_t *initial_data;
    size_t initial_data_size;
} bm_linear_memory_config_t;

bm_status_t bm_linear_memory_create(const bm_host_services_t *host,
                                    bm_bus_t *bus,
                                    const bm_linear_memory_config_t *config,
                                    bm_linear_memory_t **out_memory);
void bm_linear_memory_destroy(bm_linear_memory_t *memory);
bm_status_t bm_linear_memory_peek(const bm_linear_memory_t *memory,
                                  uint64_t address,
                                  uint8_t *value);

#ifdef __cplusplus
}
#endif

#endif
