/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors */
#ifndef BLUMACH_BUS_PHASE_286_H
#define BLUMACH_BUS_PHASE_286_H
#include <blumach/engine/types.h>
#include <stdbool.h>

/* Intel210760-002 p3-32: a transfer has Ts then Tc; inactive READY
 * repeats Tc. Units are processor clocks, NOT twice-frequency CLK pins.
 * Digital phase model only: no electrical setup/hold or 82284 synchronizer.
 * Ready below is normalized true=active (physical pin is active low).
 * Zero initialization starts an idle bus at clock0. One owner, no globals.
 */
typedef enum bm_286_bus_phase { BM_286_PHASE_IDLE, BM_286_PHASE_TS, BM_286_PHASE_TC } bm_286_bus_phase_t;
typedef struct bm_286_bus_phase_state {
    uint64_t clock, waits;
    bm_286_bus_phase_t phase;
} bm_286_bus_phase_state_t;
typedef struct bm_286_bus_phase_event {
    uint64_t clock;
    bm_286_bus_phase_t phase;
    bool completed;
} bm_286_bus_phase_event_t;

/* Grants immediately at the current clock; fails if already occupied.
 * Caller owns request/address/data lifetime until completion. */
bm_status_t bm_286_bus_phase_begin(bm_286_bus_phase_state_t *s);
/* Advance exactly one processor clock; output describes the elapsed phase.
 * READY sampled only in Tc; idle advances time but completes nothing.
 * Invalid arguments/state or overflow leave state/output unchanged. */
bm_status_t bm_286_bus_phase_tick(bm_286_bus_phase_state_t *s, bool ready,
                                bm_286_bus_phase_event_t *out);
/* O(1) equivalent of Ts, additional_waits inactive Tc samples, then active Tc.
 * Only at Ts; no callback/pin activity is synthesized. Intended for adapters
 * with a fixed known delay and no intervening external events. */
bm_status_t bm_286_bus_phase_finish(bm_286_bus_phase_state_t *s, uint32_t additional_waits);
#endif
