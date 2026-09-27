/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors */
#include "fetch_supply_286.h"

bm_status_t bm_286_fetch_supply_redirect(bm_286_fetch_supply_t *s,
    uint32_t first, uint32_t last, bool protected_window)
{
    bm_status_t status;
    if (!s || !s->source) return BM_STATUS_INVALID_ARGUMENT;
    status = protected_window ? bm_286_prefetch_redirect_protected(&s->queue, first, last) :
        bm_286_prefetch_redirect(&s->queue, first, last);
    if (status != BM_STATUS_OK) return status;
    s->first = s->consume = first; s->last = last;
    s->bus.phase = BM_286_PHASE_IDLE; s->bus.waits = 0;
    s->protected_window = protected_window; s->stopped = false;
    return BM_STATUS_OK;
}

static bm_status_t transact(bm_286_fetch_supply_t *s, bm_bus_transaction_t *t)
{
    bm_status_t status;
    /* Reserve worst fixed delay before invoking a side-effecting endpoint. */
    if (s->bus.clock > UINT64_MAX - (2u + (uint64_t)UINT32_MAX) || s->transfers == UINT64_MAX)
        return BM_STATUS_INVALID_STATE;
    status = bm_286_bus_phase_begin(&s->bus);
    if (status != BM_STATUS_OK) return status;
    t->wait_states = 0;
    status = s->source(s->context, t);
    if (status != BM_STATUS_OK) { s->stopped = true; return status; }
    ++s->transfers;
    status = bm_286_bus_phase_finish(&s->bus, t->wait_states);
    if (status != BM_STATUS_OK) s->stopped = true;
    return status;
}

bm_status_t bm_286_fetch_supply_refill(bm_286_fetch_supply_t *s)
{
    bm_286_prefetch_request_t request;
    bm_bus_transaction_t t = {0};
    bm_status_t status;
    uint8_t bytes[2];
    if (!s || !s->source) return BM_STATUS_INVALID_ARGUMENT;
    if (s->stopped) return BM_STATUS_INVALID_STATE;
    status = bm_286_prefetch_begin(&s->queue, &request);
    if (status != BM_STATUS_OK) return status;
    t.address = request.address; t.size = request.size; t.alignment = request.size;
    t.space = BM_ADDRESS_PROGRAM; t.operation = BM_BUS_FETCH; t.endianness = BM_ENDIAN_LITTLE;
    status = transact(s, &t);
    if (status != BM_STATUS_OK) { s->stopped = true; return status; }
    bytes[0] = (uint8_t)t.value; bytes[1] = (uint8_t)(t.value >> 8);
    return bm_286_prefetch_complete(&s->queue, request.ticket, bytes, request.size);
}

bm_status_t bm_286_fetch_supply_access(void *context, bm_bus_transaction_t *t)
{
    bm_286_fetch_supply_t *s = context;
    bm_status_t status;
    uint8_t value;
    if (!s || !s->source || !t) return BM_STATUS_INVALID_ARGUMENT;
    if (s->stopped) return BM_STATUS_INVALID_STATE;
    if (t->attributes & BM_BUS_TRANSACTION_DEBUG) return s->source(s->context, t);
    if (t->operation != BM_BUS_FETCH) return transact(s, t);
    if (t->size != 1 || t->address != s->consume || t->address > s->last ||
        t->space != BM_ADDRESS_PROGRAM || t->attributes)
        return BM_STATUS_UNSUPPORTED;
    if (!s->queue.count) {
        status = bm_286_fetch_supply_refill(s);
        if (status != BM_STATUS_OK) return status;
    }
    status = bm_286_prefetch_take(&s->queue, &value);
    if (status != BM_STATUS_OK) return status;
    ++s->consume;
    t->value = value;
    /* Bus occupancy is tracked separately; do not mislabel it CPU waits. */
    t->wait_states = 0;
    return BM_STATUS_OK;
}
