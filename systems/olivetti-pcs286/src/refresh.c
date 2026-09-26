/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * IBM6280070 timer/request/REF DET behavior at architectural boundaries.
 * Reuses the portable AT arbiter; no discrete-gate or DRAM simulation. */
#include "refresh.h"
#include <string.h>

static bm_status_t quiescent(bm_at_bus_t *bus)
{
    bm_at_bus_arbitration_t a;
    bm_status_t s = bm_at_bus_arbitration(bus, &a);
    if (s != BM_STATUS_OK) return s;
    return a.requested || a.locked || a.hold || a.hlda ?
        BM_STATUS_INVALID_STATE : BM_STATUS_OK;
}

bm_status_t bm_pcs286_refresh_initialize(bm_pcs286_refresh_t *r, bm_at_bus_t *bus)
{
    bm_status_t s;
    if (r == NULL || bus == NULL) return BM_STATUS_INVALID_ARGUMENT;
    s = quiescent(bus);
    if (s != BM_STATUS_OK) return s;
    memset(r, 0, sizeof(*r));
    r->bus = bus;
    return BM_STATUS_OK;
}

bm_status_t bm_pcs286_refresh_reset(bm_pcs286_refresh_t *r)
{
    bm_status_t s;
    if (r == NULL || r->bus == NULL) return BM_STATUS_INVALID_ARGUMENT;
    if (r->busy) return BM_STATUS_INVALID_STATE;
    s = quiescent(r->bus);
    if (s != BM_STATUS_OK) return s;
    memset(&r->state, 0, sizeof(r->state));
    return BM_STATUS_OK;
}

bm_status_t bm_pcs286_refresh_pit_input(bm_pcs286_refresh_t *r, int level)
{
    if (r == NULL || r->bus == NULL || (level != 0 && level != 1))
        return BM_STATUS_INVALID_ARGUMENT;
    if (r->busy) return BM_STATUS_INVALID_STATE;
    if (level && !r->state.out1) r->state.pending = 1;
    r->state.out1 = level;
    return BM_STATUS_OK;
}

bm_status_t bm_pcs286_refresh_service(bm_pcs286_refresh_t *r)
{
    bm_at_bus_arbitration_t a;
    bm_status_t s;
    if (r == NULL || r->bus == NULL) return BM_STATUS_INVALID_ARGUMENT;
    if (r->busy) return BM_STATUS_INVALID_STATE;
    if (!r->state.pending) return BM_STATUS_IDLE;
    r->busy = 1;
    s = bm_at_bus_arbitration(r->bus, &a);
    if (s != BM_STATUS_OK) goto done;
    if (!a.requested) {
        if (a.hlda) { s = BM_STATUS_IDLE; goto done; }
        s = bm_at_bus_request(r->bus, BM_AT_MASTER_REFRESH, 1);
        if (s != BM_STATUS_OK) goto done;
        s = bm_at_bus_arbitration(r->bus, &a);
        if (s != BM_STATUS_OK) goto done;
    }
    if (a.requester != BM_AT_MASTER_REFRESH || !a.hold || !a.hlda || a.locked) {
        s = BM_STATUS_IDLE;
        goto done;
    }
    /* The event is atomic at this functional boundary. Publish its state before
     * lowering HOLD so read-only pin observers see the completed event. */
    r->state.pending = 0;
    r->state.refdet ^= 1;
    s = bm_at_bus_request(r->bus, BM_AT_MASTER_REFRESH, 0);
done:
    r->busy = 0;
    return s;
}

bm_status_t bm_pcs286_refresh_state(const bm_pcs286_refresh_t *r,
                                   bm_pcs286_refresh_state_t *state)
{
    if (r == NULL || r->bus == NULL || state == NULL) return BM_STATUS_INVALID_ARGUMENT;
    *state = r->state;
    return BM_STATUS_OK;
}
