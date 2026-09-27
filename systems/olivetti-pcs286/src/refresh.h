/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Functional AT refresh, not a DRAM or physical chipset timing model. */
#ifndef BM_PCS286_REFRESH_H
#define BM_PCS286_REFRESH_H
#include <blumach/components/at_bus.h>

typedef struct bm_pcs286_refresh_state {
    int out1, pending, refdet;
} bm_pcs286_refresh_state_t;
typedef struct bm_pcs286_refresh {
    bm_at_bus_t *bus;
    bm_pcs286_refresh_state_t state;
    int busy;
} bm_pcs286_refresh_t;

/* Caller-owned unused storage, borrowed reset bus and reset PIT (OUT1 low).
 * Exactly one refresh coordinator per bus, single-threaded owner. No allocation,
 * mapping or callbacks. Initial REF DET=0 is a deterministic emulator policy,
 * not a measured PCS286 power-on value. Reset engine/PIT/bus before this helper;
 * CPU-only reset preserves its state. Destroy mappings/owners before storage. */
bm_status_t bm_pcs286_refresh_initialize(bm_pcs286_refresh_t *refresh, bm_at_bus_t *bus);
bm_status_t bm_pcs286_refresh_reset(bm_pcs286_refresh_t *refresh);
/* Forward EVERY OUT1 transition, including programming-induced transitions.
 * Rising edges latch one pending request; additional edges coalesce until
 * serviced. This pin input never executes CPUs, reads RAM or changes REF DET. */
bm_status_t bm_pcs286_refresh_pit_input(bm_pcs286_refresh_t *refresh, int level);
/* Call at board service boundaries after timer synchronization and after CPU
 * HOLD acknowledgement. IDLE means not completed: a pending request may have
 * raised HOLD. Existing DMA/ISA ownership, LOCK and stale HLDA defer service.
 * OK means exactly one logical event completed: pending clears, REF DET toggles
 * and HOLD releases. No memory transaction, row counter or physical duration.
 * Do not batch across possible service boundaries: deferred timer pulses really
 * coalesce. This helper does not implement the machine's scheduling loop.
 * No preemption/priority claim; existing owners finish first. Failed bus calls
 * propagate unchanged, never becoming guest errors. No fallible DRAM endpoint
 * exists because backing RAM has no charge-decay model. Callback mutator reentry
 * rejects; pure state inspection is allowed. */
bm_status_t bm_pcs286_refresh_service(bm_pcs286_refresh_t *refresh);
bm_status_t bm_pcs286_refresh_state(const bm_pcs286_refresh_t *refresh,
                                   bm_pcs286_refresh_state_t *state);
#endif
