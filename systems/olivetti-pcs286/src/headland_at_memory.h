/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Private composition of the inherited GC103 routes, not a PCS286 board ABI.
 */
#ifndef BM_HEADLAND_AT_MEMORY_H
#define BM_HEADLAND_AT_MEMORY_H
#include "legacy_gc103_memory.h"
#include <blumach/components/at_bus.h>
#include <blumach/systems/pcs286_memory.h>

#define BM_HEADLAND_AT_LEGACY_GC103 1U
#define BM_HEADLAND_AT_CONFIGURED_GC103 2U
typedef enum bm_headland_at_timing {
    BM_HEADLAND_AT_STRICT = 0,
    BM_HEADLAND_AT_PROVISIONAL_SERVICE_CLOCK
} bm_headland_at_timing_t;
typedef enum bm_headland_at_holes {
    BM_HEADLAND_AT_HOLES_REJECT = 0,
    BM_HEADLAND_AT_HOLES_FF
} bm_headland_at_holes_t;
typedef enum bm_headland_at_protected {
    BM_HEADLAND_AT_PROTECTED_REJECT = 0,
    BM_HEADLAND_AT_PROTECTED_IGNORE
} bm_headland_at_protected_t;

/* Endpoints receive native aligned 1/2-byte fragments. wait_states starts at
 * zero and returns EXTRA service_clock cycles (not requester clocks).
 * Backing offsets are already resolved; external addresses are A20-conditioned
 * physical addresses. Return errors unchanged; never retry a failed fragment.
 * DEBUG endpoints must be observational. No reentry or destruction/reset/
 * mutation of the borrowed route model/configuration during a callback.
 */
typedef bm_status_t (*bm_headland_at_backing_fn)(void *, bm_pcs286_memory_region_t,
                                               uint32_t, bm_bus_transaction_t *);
typedef struct bm_headland_at_config {
    uint32_t profile; /* Explicit LEGACY_GC103 or CONFIGURED_GC103, matching routes. */
    const bm_gc103_memory_t *routes; /* Borrowed initialized model. */
    bm_headland_at_backing_fn backing;
    void *backing_context;
    bm_bus_access_fn external; /* NULL: explicitly unpopulated external space. */
    void *external_context;
    uint32_t external_width; /* 1 or 2 bytes. Internal RAM/ROM width is 2. */
    bm_headland_at_holes_t holes;
    bm_headland_at_protected_t protected_writes;
    bm_headland_at_timing_t timing;
    bm_clock_rate_t service_clock;
    uint32_t extra_clocks[4]; /* Per fragment, indexed by bm_gc10x_memory_target. */
    int cpu_a20; /* Explicit board input, no guessed reset/source. */
} bm_headland_at_config_t;
typedef struct bm_headland_at_progress {
    uint32_t completed_bytes;
    uint32_t attempted_bytes; /* Failed endpoint may already have had effects. */
    bm_status_t status;
    bm_gc10x_wait_quality_t timing;
} bm_headland_at_progress_t;
typedef struct bm_headland_at_memory {
    bm_headland_at_config_t config;
    bm_headland_at_progress_t last;
    int busy;
} bm_headland_at_memory_t;

/* No allocation, bus publication or child reset. Config copied, dependencies
 * borrowed. Initialize only an unused/quiescent object; failure preserves it.
 * The owner's AT bus must call access AFTER checking ownership/HOLD/HLDA.
 */
bm_status_t bm_headland_at_memory_initialize(bm_headland_at_memory_t *,
                                            const bm_headland_at_config_t *);
bm_status_t bm_headland_at_memory_a20(bm_headland_at_memory_t *, int enabled);
/* MEMORY/PROGRAM/DATA, READ/WRITE/FETCH, 1..8 bytes, either byte order. Reject
 * requests extending beyond 24 bits BEFORE any endpoint effect; no wrap here.
 * Plan the full transfer from one route/A20 snapshot; split on 16-bit lanes,
 * target width and route bounds. Preflight rejects strict UNKNOWN timing,
 * protected writes (unless IGNORE) and holes (unless FF) before all endpoints.
 * FF is permitted ONLY for route OPEN_BUS or absent external endpoint. An
 * installed endpoint's UNMAPPED/READ_ONLY/host error is never changed to OK.
 * Success commits read value and total waits; failure preserves transaction,
 * but completed writes and even failed-endpoint effects are NOT rolled back.
 * last records completed/attempted bytes for host diagnostics, not guest fault
 * metadata. No automatic retry. Reentrant access/set_a20 return INVALID_STATE.
 * DEBUG skips timing, changes neither last nor routes and rejects writes.
 * Provisional waits: sum configured costs plus endpoint extra service clocks,
 * then convert EXACTLY and ceil ONCE using existing engine clock arithmetic.
 * No rounding credit between requests. Overflow is CAPACITY_EXCEEDED; dynamic
 * endpoint-wait overflow may follow completed effects. Strict always rejects
 * these inherited UNKNOWN routes. No timing quality is promoted to documented.
 */
bm_status_t bm_headland_at_memory_access(void *, bm_at_transfer_t *);
/* Ready-made backing endpoint for the existing owned RAM/ROM storage. */
bm_status_t bm_headland_at_memory_backing(void *, bm_pcs286_memory_region_t,
                                         uint32_t, bm_bus_transaction_t *);
#endif
