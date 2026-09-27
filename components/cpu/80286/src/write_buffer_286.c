/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors */
#include "write_buffer_286.h"

bm_status_t bm_286_write_buffer_put(bm_286_write_buffer_t *b, const bm_bus_transaction_t *t)
{
    if (!b || !t || t->operation != BM_BUS_WRITE || (t->size != 1 && t->size != 2) ||
        t->endianness != BM_ENDIAN_LITTLE || (t->size == 2 && (t->address & 1u)) ||
        (t->attributes & ~BM_BUS_TRANSACTION_LOCKED) ||
        (t->space != BM_ADDRESS_MEMORY && t->space != BM_ADDRESS_DATA && t->space != BM_ADDRESS_IO) ||
        t->address > (t->space == BM_ADDRESS_IO ? 0xffffu : 0xffffffu) - (t->size - 1u))
        return BM_STATUS_INVALID_ARGUMENT;
    if (b->pending) return BM_STATUS_IDLE;
    b->request = *t; b->request.wait_states = 0; b->pending = true;
    return BM_STATUS_OK;
}

bm_status_t bm_286_write_buffer_issue(bm_286_write_buffer_t *b, bool granted, bm_bus_transaction_t *out)
{
    if (!b || !out) return BM_STATUS_INVALID_ARGUMENT;
    if (!granted || !b->pending) return BM_STATUS_IDLE;
    *out = b->request; b->pending = false;
    return BM_STATUS_OK;
}
