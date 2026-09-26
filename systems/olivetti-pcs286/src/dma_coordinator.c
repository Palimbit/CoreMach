/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors */
#include "dma_coordinator.h"

#include <string.h>

static bm_status_t retain(bm_pcs286_dma_coordinator_t *c, bm_status_t status)
{
    if (status == BM_STATUS_IDLE) status = BM_STATUS_INVALID_STATE;
    if (c->state.failure == BM_STATUS_OK) c->state.failure = status;
    return c->state.failure;
}

static int transfer_channel(int channel)
{
    return (channel >= 0 && channel <= 3) || (channel >= 5 && channel <= 7);
}

static bm_status_t release_bus(bm_pcs286_dma_coordinator_t *c,
                               const bm_at_dma_state_t *dma)
{
    bm_status_t status;
    if (dma->bus_grant) {
        status = bm_at_dma_set_bus_grant(c->config.dma, 0);
        if (status != BM_STATUS_OK) return retain(c, status);
    }
    status = bm_at_bus_request(c->config.bus, c->state.master, 0);
    if (status != BM_STATUS_OK) return retain(c, status);
    c->state.requested = 0;
    c->state.master = BM_AT_MASTER_CPU;
    return BM_STATUS_OK;
}

bm_status_t bm_pcs286_dma_coordinator_initialize(
    bm_pcs286_dma_coordinator_t *c,
    const bm_pcs286_dma_coordinator_config_t *config)
{
    bm_at_dma_state_t dma;
    bm_at_bus_arbitration_t bus;
    if (!c || !config || !config->bus || !config->dma ||
        bm_at_dma_state(config->dma, &dma) != BM_STATUS_OK ||
        bm_at_bus_arbitration(config->bus, &bus) != BM_STATUS_OK ||
        dma.bus_grant || bus.requested || bus.hold || bus.hlda)
        return BM_STATUS_INVALID_ARGUMENT;
    memset(c, 0, sizeof(*c));
    c->config = *config;
    c->state.master = BM_AT_MASTER_CPU;
    return BM_STATUS_OK;
}

bm_status_t bm_pcs286_dma_coordinator_step(
    bm_pcs286_dma_coordinator_t *c, uint64_t *completed_clocks)
{
    bm_at_dma_state_t dma;
    bm_at_bus_arbitration_t bus;
    bm_status_t status;
    int changed = 0;
    if (!c || !completed_clocks) return BM_STATUS_INVALID_ARGUMENT;
    *completed_clocks = 0;
    if (c->busy) return BM_STATUS_INVALID_STATE;
    if (c->state.failure != BM_STATUS_OK) return c->state.failure;
    c->busy = 1;
    status = bm_at_dma_state(c->config.dma, &dma);
    if (status != BM_STATUS_OK) goto failed;
    status = bm_at_bus_arbitration(c->config.bus, &bus);
    if (status != BM_STATUS_OK) goto failed;

    if (!c->state.requested) {
        bm_at_master_t master;
        if (!dma.bus_request || bus.hlda) {
            c->busy = 0;
            return BM_STATUS_IDLE;
        }
        if (!transfer_channel(dma.pending_channel)) {
            status = BM_STATUS_UNSUPPORTED;
            goto failed;
        }
        master = dma.pending_channel < 4 ? BM_AT_MASTER_DMA8 : BM_AT_MASTER_DMA16;
        status = bm_at_bus_request(c->config.bus, master, 1);
        if (status != BM_STATUS_OK) goto failed;
        c->state.requested = 1;
        c->state.master = master;
        changed = 1;
        status = bm_at_bus_arbitration(c->config.bus, &bus);
        if (status != BM_STATUS_OK) goto failed;
    }

    if (!dma.bus_request) {
        status = release_bus(c, &dma);
        c->busy = 0;
        return status;
    }
    if (!bus.hlda) {
        c->busy = 0;
        return changed ? BM_STATUS_OK : BM_STATUS_IDLE;
    }
    if (!dma.bus_grant) {
        bm_at_master_t pending_master;
        if (!transfer_channel(dma.pending_channel)) {
            status = BM_STATUS_UNSUPPORTED;
            goto failed;
        }
        pending_master = dma.pending_channel < 4 ? BM_AT_MASTER_DMA8 : BM_AT_MASTER_DMA16;
        if (pending_master != c->state.master) {
            status = BM_STATUS_INVALID_STATE;
            goto failed;
        }
        status = bm_at_dma_set_bus_grant(c->config.dma, 1);
        if (status != BM_STATUS_OK) goto failed;
        changed = 1;
    }
    status = bm_at_dma_service(c->config.dma, completed_clocks);
    if (status != BM_STATUS_OK && status != BM_STATUS_IDLE) goto failed;
    if (status == BM_STATUS_OK) {
        ++c->state.completed_units;
        c->state.completed_clocks += *completed_clocks;
        changed = 1;
    }
    status = bm_at_dma_state(c->config.dma, &dma);
    if (status != BM_STATUS_OK) goto failed;
    if (!dma.bus_request) {
        status = release_bus(c, &dma);
        c->busy = 0;
        return status;
    }
    c->busy = 0;
    return changed ? BM_STATUS_OK : BM_STATUS_IDLE;

failed:
    c->busy = 0;
    return retain(c, status);
}

bm_status_t bm_pcs286_dma_coordinator_state(
    const bm_pcs286_dma_coordinator_t *c,
    bm_pcs286_dma_coordinator_state_t *state)
{
    if (!c || !state) return BM_STATUS_INVALID_ARGUMENT;
    *state = c->state;
    return BM_STATUS_OK;
}
