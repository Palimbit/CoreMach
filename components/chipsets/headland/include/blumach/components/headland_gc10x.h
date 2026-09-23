/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Draft contract; GC101A/GC102 board identification remains evidence-qualified.
 */
#ifndef BLUMACH_COMPONENTS_HEADLAND_GC10X_H
#define BLUMACH_COMPONENTS_HEADLAND_GC10X_H
#include <blumach/components/bus.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct bm_gc10x bm_gc10x_t;
#define BM_GC10X_CONTRACT_VERSION 1U

typedef enum bm_gc10x_requester {
    BM_GC10X_CPU = 0,
    BM_GC10X_DMA,
    BM_GC10X_ISA_MASTER
} bm_gc10x_requester_t;
typedef enum bm_gc10x_memory_target {
    BM_GC10X_RAM = 0,
    BM_GC10X_FIRMWARE,
    BM_GC10X_EXTERNAL,
    BM_GC10X_OPEN_BUS
} bm_gc10x_memory_target_t;
typedef struct bm_gc10x_route {
    bm_gc10x_memory_target_t target;
    uint32_t offset;
    uint32_t contiguous_bytes;
    uint32_t extra_memory_clocks;
    int writable;
} bm_gc10x_route_t;
typedef struct bm_gc10x_config {
    uint32_t size;
    uint32_t version;
    uint32_t ram_bytes;
    /* Caller-selected, documented decode profile, initially PCS286 only.
     * No machine ID string checks inside the memory-controller core. */
    uint32_t profile;
} bm_gc10x_config_t;
#define BM_GC10X_PROFILE_PCS286 1U

/* Controller owns registers/mapping state, not the RAM or ROM backing bytes.
 * Construct without registering callbacks; board installs I/O decode only
 * after all components exist. Failure clears out_chipset. */
bm_status_t bm_gc10x_create(const bm_host_services_t *host,
                            const bm_gc10x_config_t *config,
                            bm_gc10x_t **out_chipset);
void bm_gc10x_destroy(bm_gc10x_t *chipset);
void bm_gc10x_reset(bm_gc10x_t *chipset);
bm_status_t bm_gc10x_io(void *context, bm_bus_transaction_t *transaction);
/* Pure query, usable by debugger: resolve according to live register state,
 * explicit requester and external CPU A20 input. Never hardwire the legacy
 * 60000h/80000h alias. contiguous_bytes stops before a decode boundary.
 * extra_memory_clocks are in the board memory-clock domain, not ns or CPU
 * clocks; board conversion must retain rational phase and check overflow.
 * Whether A20 affects each requester is a board policy, not a 20-bit mask. */
bm_status_t bm_gc10x_resolve(const bm_gc10x_t *chipset,
                             bm_gc10x_requester_t requester, int cpu_a20,
                             uint32_t address, bm_bus_operation_t operation,
                             bm_gc10x_route_t *out_route);
bm_status_t bm_gc10x_inspect(const bm_gc10x_t *chipset,
                             uint16_t register_id, uint16_t *value);
#ifdef __cplusplus
}
#endif
#endif
