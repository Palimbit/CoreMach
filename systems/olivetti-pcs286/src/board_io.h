/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Private functional composition, not a qualified PCS286 hardware profile. */
#ifndef BM_PCS286_BOARD_IO_H
#define BM_PCS286_BOARD_IO_H
#include "legacy_gc103_memory.h"
#include "legacy_ioc02_registers.h"
#include <blumach/components/at_bus.h>
#include <blumach/components/at_pic.h>
#include <blumach/components/at_dma.h>

#define BM_PCS286_IO_RESOURCES 16U
typedef enum bm_pcs286_io_policy { BM_PCS286_IO_REJECT = 0, BM_PCS286_IO_FF } bm_pcs286_io_policy_t;
typedef enum bm_pcs286_io_timing { BM_PCS286_IO_STRICT = 0, BM_PCS286_IO_PROVISIONAL } bm_pcs286_io_timing_t;
typedef enum bm_pcs286_io_profile { BM_PCS286_IO_LEGACY_GC103_AT = 1 } bm_pcs286_io_profile_t;
typedef enum bm_pcs286_io_target {
    BM_PCS286_IO_HOLE = 0, BM_PCS286_IO_HEADLAND, BM_PCS286_IO_IOC02,
    BM_PCS286_IO_PIC, BM_PCS286_IO_DMA
} bm_pcs286_io_target_t;
typedef struct bm_pcs286_io_resource {
    uint16_t first, last; /* Inclusive, exact decode; no implicit mirrors. */
    unsigned width; /* 1 or 2; aligned words only, bounded by this resource. */
    bm_bus_access_fn access;
    void *context;
    uint32_t extra_clocks;
} bm_pcs286_io_resource_t;
typedef struct bm_pcs286_io_config {
    bm_pcs286_io_profile_t profile;
    bm_gc103_memory_t *headland;
    bm_ioc02_legacy_registers_t *ioc02;
    bm_at_pic_t *pic; /* Must already use bases 20/A0. No remap/probe/reset. */
    bm_at_dma_t *dma; /* Owner keeps mem2mem DISABLED for this profile. */
    bm_pcs286_io_policy_t holes;
    bm_pcs286_io_timing_t timing;
    bm_clock_rate_t service_clock;
    uint32_t extra_clocks[5]; /* Per built-in target, including holes. */
    unsigned resource_count;
    bm_pcs286_io_resource_t resources[BM_PCS286_IO_RESOURCES];
} bm_pcs286_io_config_t;
typedef struct bm_pcs286_io_progress {
    unsigned completed_bytes, attempted_bytes;
    bm_status_t status;
} bm_pcs286_io_progress_t;
typedef struct bm_pcs286_io {
    bm_pcs286_io_config_t config;
    bm_pcs286_io_progress_t last;
    int busy;
} bm_pcs286_io_t;
/* Initialize unused/quiescent caller storage; copy config, borrow initialized
 * children and callback contexts. No allocation/reset/publication. All children
 * required. Reject resource overlaps with each other or any built-in port.
 * Child/config mutation and reinitialization during access are forbidden. */
bm_status_t bm_pcs286_io_initialize(bm_pcs286_io_t *io, const bm_pcs286_io_config_t *config);
/* Invoke AFTER AT ownership checks. IO READ/WRITE 1..8 bytes, 16-bit port space,
 * no wrap. Plan entire request before effects. Headland aligned words go to its
 * inherited native handler (1EE word reads FFFF/ignores write); odd words split.
 * All other built-ins are byte-wide; gaps are real gaps. Native registers use
 * little-endian lanes; big-endian logical values are serialized accordingly.
 * STRICT rejects unknown timing. PROVISIONAL sums configured/endpoint EXTRA
 * clocks in one service domain then ceils once. DEBUG read-only/pure/untimed.
 * Unclaimed holes alone may FF/ignore. Installed endpoint errors propagate.
 * Preserve caller transaction on failure; retain prior/failed endpoint effects,
 * no retries/rollback/guest faults. last is host progress, not guest metadata.
 * Synchronous, non-reentrant; external DEBUG endpoints must be observational.
 * IOC02 latch effects are NOT connected to guessed physical output pins. */
bm_status_t bm_pcs286_io_access(void *context, bm_at_transfer_t *transfer);
#endif
