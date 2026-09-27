/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors */
#ifndef BLUMACH_FETCH_SUPPLY_286_H
#define BLUMACH_FETCH_SUPPLY_286_H
#include <blumach/components/bus.h>
#include "prefetch_286.h"
#include "bus_phase_286.h"

/* EXPERIMENTAL demand-driven integration adapter for a private test harness.
 * Connect access to bm_286_config_t, explicitly redirect on every control
 * transfer/reset/import (including branches to the sequential next address).
 * Only caller-validated contiguous executable memory; no MMIO code.
 * Refill can be called during known idle EU slots by a future scheduler.
 * No EU latency/IU decode model, write buffer or pin-level timing inference.
 * clock measures serialized BUS OCCUPANCY ONLY, never CPU elapsed duration.
 * The source callback's waits are fixed extra processor clocks. Side effects
 * happen synchronously, not at a modeled electrical edge. Failure stops adapter.
 * No automatic fallback or clocked-runtime activation is permitted.
 */
typedef struct bm_286_fetch_supply {
    bm_bus_access_fn source;
    void *context;
    bm_286_prefetch_t queue;
    bm_286_bus_phase_state_t bus;
    uint32_t first, last, consume;
    bool protected_window, stopped;
    uint64_t transfers;
} bm_286_fetch_supply_t;

bm_status_t bm_286_fetch_supply_redirect(bm_286_fetch_supply_t *s,
    uint32_t first, uint32_t last, bool protected_window);
bm_status_t bm_286_fetch_supply_refill(bm_286_fetch_supply_t *s);
bm_status_t bm_286_fetch_supply_access(void *context, bm_bus_transaction_t *t);
#endif
