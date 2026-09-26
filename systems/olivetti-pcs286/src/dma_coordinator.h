/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Private functional DMA/bus boundary; no physical timing claim. */
#ifndef BM_PCS286_DMA_COORDINATOR_H
#define BM_PCS286_DMA_COORDINATOR_H
#include <blumach/components/at_dma.h>

typedef struct bm_pcs286_dma_coordinator_config {
    bm_at_bus_t *bus;
    bm_at_dma_t *dma;
} bm_pcs286_dma_coordinator_config_t;

typedef struct bm_pcs286_dma_coordinator_state {
    bm_at_master_t master;
    int requested;
    uint64_t completed_units, completed_clocks;
    bm_status_t failure;
} bm_pcs286_dma_coordinator_state_t;

typedef struct bm_pcs286_dma_coordinator {
    bm_pcs286_dma_coordinator_config_t config;
    bm_pcs286_dma_coordinator_state_t state;
    int busy;
} bm_pcs286_dma_coordinator_t;

/* Poll once at a board execution boundary, after CPU/peripheral line changes.
 * A new HRQ claims DMA8/16 from pending_channel, asserts bus HOLD and waits for
 * the CPU's real HLDA. Once acknowledged, one service call transfers at most
 * one unit. HRQ release drops the DMA grant before releasing the bus request;
 * a new request waits until stale HLDA falls. No CPU step or clock is invented.
 * Returns OK when state changed or a unit completed, IDLE while quiescent or
 * waiting for HLDA. completed_clocks is the DMA component's functional count.
 * First non-IDLE host error is retained and never converted to a guest event. */
bm_status_t bm_pcs286_dma_coordinator_initialize(
    bm_pcs286_dma_coordinator_t *coordinator,
    const bm_pcs286_dma_coordinator_config_t *config);
bm_status_t bm_pcs286_dma_coordinator_step(
    bm_pcs286_dma_coordinator_t *coordinator, uint64_t *completed_clocks);
bm_status_t bm_pcs286_dma_coordinator_state(
    const bm_pcs286_dma_coordinator_t *coordinator,
    bm_pcs286_dma_coordinator_state_t *state);
#endif
