/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 */
#ifndef BLUMACH_SYSTEMS_PCS286_MEMORY_H
#define BLUMACH_SYSTEMS_PCS286_MEMORY_H
#include <blumach/components/bus.h>
#include <blumach/systems/olivetti_pcs286.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct bm_pcs286_memory bm_pcs286_memory_t;
typedef enum bm_pcs286_memory_region {
    BM_PCS286_MEMORY_RAM = 0,
    BM_PCS286_MEMORY_ROM
} bm_pcs286_memory_region_t;

/* Backing storage only, NOT a physical address map or chipset implementation.
 * Allocates private RAM and copies/interleaves firmware; publishes no callbacks.
 * ram_bytes is storage capacity (1 byte..4 MiB), not a claim about supported board
 * populations or straps. Initial RAM is zero by deterministic emulator policy,
 * not a claim about power-on DRAM. On any failure *out_memory is NULL.
 * This helper retains no firmware pointers; the machine configuration's wider
 * blob lifetime contract is unchanged. Host context must outlive the object. */
bm_status_t bm_pcs286_memory_create(const bm_host_services_t *host,
                                   size_t ram_bytes,
                                   const bm_pcs286_firmware_t *firmware,
                                   bm_pcs286_memory_t **out_memory);
void bm_pcs286_memory_destroy(bm_pcs286_memory_t *memory);

/* Access an already resolved backing offset. The physical transaction.address
 * is NOT decoded or masked here. Caller must resolve A20, aliases, target,
 * write enable, contiguity and waits BEFORE calling; split at decode boundaries.
 * Accepts MEMORY/PROGRAM/DATA, READ/WRITE/FETCH, 1..8 bytes, either endianness.
 * No alignment restriction on backing storage. RAM writes are supported, ROM
 * writes return READ_ONLY; board must choose any hardware ignored-write policy.
 * DEBUG writes are rejected. Invalid or out-of-bounds requests change neither
 * bytes nor transaction; successful reads change only value. wait_states is
 * preserved, NOT calculated: success does not establish known or zero timing.
 * No reset entry point: CPU-only reset must not clear backing storage. A future
 * whole-machine cold-reset policy remains separate from object construction. */
bm_status_t bm_pcs286_memory_access(bm_pcs286_memory_t *memory,
                                   bm_pcs286_memory_region_t region,
                                   uint32_t offset,
                                   bm_bus_transaction_t *transaction);
#ifdef __cplusplus
}
#endif
#endif
