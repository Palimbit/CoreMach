/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors */
#include "write_buffer_286.h"
#include "bus_priority_286.h"
#include <assert.h>
#include <string.h>
int main(void)
{
    bm_286_write_buffer_t b = {0}, other = {0}, saved;
    bm_bus_transaction_t t = {0}, out = {0}, sentinel;
    unsigned winner;
    t.operation = BM_BUS_WRITE; t.space = BM_ADDRESS_DATA; t.address = 0x100;
    t.size = t.alignment = 2; t.value = 0x1234; t.wait_states = 99;
    assert(bm_286_write_buffer_put(&b, &t) == BM_STATUS_OK);
    saved = b; sentinel = out;
    t.value = 0x5678;
    assert(bm_286_write_buffer_put(&b, &t) == BM_STATUS_IDLE);
    assert(!memcmp(&b, &saved, sizeof(b)));
    assert(bm_286_write_buffer_issue(&b, false, &out) == BM_STATUS_IDLE);
    assert(!memcmp(&b, &saved, sizeof(b)) && !memcmp(&out, &sentinel, sizeof(out)));
    assert(bm_286_bus_select(BM_286_BUS_DATA | BM_286_BUS_PREFETCH, false, false, false, &winner) == BM_STATUS_OK);
    assert(winner == BM_286_BUS_DATA);
    assert(bm_286_write_buffer_issue(&b, true, &out) == BM_STATUS_OK);
    assert(out.value == 0x1234 && out.wait_states == 0 && !b.pending);
    assert(bm_286_write_buffer_put(&b, &t) == BM_STATUS_OK);
    assert(out.value == 0x1234); /* Active transfer snapshot independent of next pending write. */
    assert(bm_286_write_buffer_put(&other, &t) == BM_STATUS_OK);
    saved = b; t.operation = BM_BUS_READ;
    assert(bm_286_write_buffer_put(&b, &t) == BM_STATUS_INVALID_ARGUMENT);
    assert(!memcmp(&b, &saved, sizeof(b)));
    t.operation = BM_BUS_WRITE; t.address = 0xffffff;
    assert(bm_286_write_buffer_put(&b, &t) == BM_STATUS_INVALID_ARGUMENT);
    t.size = 1; t.attributes = BM_BUS_TRANSACTION_DEBUG;
    assert(bm_286_write_buffer_put(&b, &t) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_286_write_buffer_issue(NULL, true, &out) == BM_STATUS_INVALID_ARGUMENT);
    return 0;
}
