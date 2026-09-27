/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * New experimental queue logic from Intel 210760-002 p3-24, not emulator code. */
#include "prefetch_286.h"

bm_status_t bm_286_prefetch_redirect(bm_286_prefetch_t *q, uint32_t first, uint32_t last)
{
    if (!q || first > last || last > 0xffffffu) return BM_STATUS_INVALID_ARGUMENT;
    q->next = first; q->last = last;
    q->head = q->count = q->pending = q->stopped = 0;
    q->active = 1;
    q->protected_tail = 0;
    return BM_STATUS_OK;
}

bm_status_t bm_286_prefetch_redirect_protected(bm_286_prefetch_t *q, uint32_t first, uint32_t last)
{
    bm_status_t status = bm_286_prefetch_redirect(q, first, last);
    if (status == BM_STATUS_OK) q->protected_tail = 1;
    return status;
}

bm_status_t bm_286_prefetch_stop(bm_286_prefetch_t *q)
{
    if (!q) return BM_STATUS_INVALID_ARGUMENT;
    if (!q->active) return BM_STATUS_INVALID_STATE;
    q->stopped = 1;
    return BM_STATUS_OK;
}

bm_status_t bm_286_prefetch_begin(bm_286_prefetch_t *q, bm_286_prefetch_request_t *out)
{
    unsigned size;
    if (!q || !out) return BM_STATUS_INVALID_ARGUMENT;
    if (!q->active) return BM_STATUS_INVALID_STATE;
    if (q->pending || q->stopped || q->count > 4 || q->next > q->last)
        return BM_STATUS_IDLE;
    size = (q->next & 1u) ? 1u : 2u;
    /* Boundary-byte policy is not established here. Do not invent an access
     * past the supplied window or claim this is a CPU segment exception. */
    if (size > q->last - q->next + 1u && !q->protected_tail) return BM_STATUS_UNSUPPORTED;
    if (q->serial == UINT64_MAX) return BM_STATUS_INVALID_STATE;
    q->request.ticket = ++q->serial;
    q->request.address = q->next; q->request.size = (uint8_t)size;
    q->pending = 1;
    *out = q->request;
    return BM_STATUS_OK;
}

bm_status_t bm_286_prefetch_complete(bm_286_prefetch_t *q, uint64_t ticket,
                                    const uint8_t *bytes, size_t length)
{
    unsigned i, valid;
    if (!q || !bytes) return BM_STATUS_INVALID_ARGUMENT;
    if (!q->active || !q->pending || ticket != q->request.ticket)
        return BM_STATUS_INVALID_STATE;
    if (length != q->request.size) return BM_STATUS_INVALID_ARGUMENT;
    valid = (unsigned)length;
    if (valid > q->last - q->next + 1u) valid = q->last - q->next + 1u;
    for (i = 0; i < valid; ++i) q->bytes[(q->head + q->count + i) % 6u] = bytes[i];
    q->count = (uint8_t)(q->count + valid);
    q->next += (uint32_t)length;
    q->pending = 0;
    return BM_STATUS_OK;
}

bm_status_t bm_286_prefetch_take(bm_286_prefetch_t *q, uint8_t *out)
{
    if (!q || !out) return BM_STATUS_INVALID_ARGUMENT;
    if (!q->active) return BM_STATUS_INVALID_STATE;
    if (!q->count) return BM_STATUS_IDLE;
    *out = q->bytes[q->head];
    q->head = (uint8_t)((q->head + 1u) % 6u); --q->count;
    return BM_STATUS_OK;
}
