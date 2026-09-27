/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Synthetic queue-policy tests, not hardware timing certification. */
#include "prefetch_286.h"
#include <assert.h>
#include <string.h>

static void fill(bm_286_prefetch_t *q)
{
    bm_286_prefetch_request_t r;
    uint8_t b[2];
    assert(bm_286_prefetch_begin(q, &r) == BM_STATUS_OK);
    b[0] = (uint8_t)r.address; b[1] = (uint8_t)(r.address + 1);
    assert(bm_286_prefetch_complete(q, r.ticket, b, r.size) == BM_STATUS_OK);
}

int main(void)
{
    bm_286_prefetch_t q = {0}, saved, other = {0};
    bm_286_prefetch_request_t r = {0}, stale, untouched;
    uint8_t value = 99, bytes[2] = {42, 43};
    unsigned start, i;
    assert(bm_286_prefetch_take(&q, &value) == BM_STATUS_INVALID_STATE && value == 99);
    for (start = 0; start < 32; ++start) {
        assert(bm_286_prefetch_redirect(&q, start, 4095) == BM_STATUS_OK);
        assert(bm_286_prefetch_begin(&q, &r) == BM_STATUS_OK);
        assert(r.address == start && r.size == ((start & 1) ? 1 : 2));
        bytes[0] = (uint8_t)start; bytes[1] = (uint8_t)(start + 1);
        assert(bm_286_prefetch_complete(&q, r.ticket, bytes, r.size) == BM_STATUS_OK);
        /* Fill/drain repeatedly across ring wraps and pending producer reads. */
        for (i = start; i < start + 1024; ++i) {
            while (q.count <= 4) fill(&q);
            saved = q; untouched = r;
            assert(bm_286_prefetch_begin(&q, &r) == BM_STATUS_IDLE);
            assert(!memcmp(&q, &saved, sizeof(q)) && !memcmp(&r, &untouched, sizeof(r)));
            assert(bm_286_prefetch_take(&q, &value) == BM_STATUS_OK);
            assert(value == (uint8_t)i && q.count <= 5);
        }
    }
    assert(bm_286_prefetch_redirect(&q, 0, 100) == BM_STATUS_OK);
    fill(&q); fill(&q);
    assert(bm_286_prefetch_begin(&q, &r) == BM_STATUS_OK && r.address == 4);
    saved = q;
    assert(bm_286_prefetch_begin(&q, &stale) == BM_STATUS_IDLE);
    assert(!memcmp(&q, &saved, sizeof(q)));
    for (i = 0; i < 4; ++i) {
        assert(bm_286_prefetch_take(&q, &value) == BM_STATUS_OK && value == i);
    }
    bytes[0] = 4; bytes[1] = 5;
    assert(bm_286_prefetch_complete(&q, r.ticket, bytes, 2) == BM_STATUS_OK);
    assert(bm_286_prefetch_take(&q, &value) == BM_STATUS_OK && value == 4);
    assert(bm_286_prefetch_take(&q, &value) == BM_STATUS_OK && value == 5);
    assert(bm_286_prefetch_complete(&q, r.ticket, bytes, 2) == BM_STATUS_INVALID_STATE);
    assert(bm_286_prefetch_redirect(&q, 0x101, 0xffff) == BM_STATUS_OK);
    assert(bm_286_prefetch_begin(&q, &stale) == BM_STATUS_OK);
    assert(bm_286_prefetch_redirect(&q, 0x101, 0xffff) == BM_STATUS_OK);
    assert(bm_286_prefetch_begin(&q, &r) == BM_STATUS_OK && r.ticket != stale.ticket);
    saved = q;
    assert(bm_286_prefetch_complete(&q, stale.ticket, bytes, 1) == BM_STATUS_INVALID_STATE);
    assert(bm_286_prefetch_complete(&q, r.ticket, bytes, 2) == BM_STATUS_INVALID_ARGUMENT);
    assert(!memcmp(&q, &saved, sizeof(q)));
    assert(bm_286_prefetch_stop(&q) == BM_STATUS_OK);
    assert(bm_286_prefetch_complete(&q, r.ticket, bytes, 1) == BM_STATUS_OK);
    assert(bm_286_prefetch_begin(&q, &r) == BM_STATUS_IDLE);
    assert(bm_286_prefetch_take(&q, &value) == BM_STATUS_OK);
    value = 99;
    assert(bm_286_prefetch_take(&q, &value) == BM_STATUS_IDLE && value == 99);
    assert(bm_286_prefetch_redirect(&q, 0xffffff, 0xffffff) == BM_STATUS_OK);
    fill(&q);
    assert(q.next == 0x1000000 && q.count == 1);
    assert(bm_286_prefetch_begin(&q, &r) == BM_STATUS_IDLE);
    assert(bm_286_prefetch_redirect(&q, 0x100, 0x100) == BM_STATUS_OK);
    saved = q;
    assert(bm_286_prefetch_begin(&q, &r) == BM_STATUS_UNSUPPORTED);
    assert(bm_286_prefetch_redirect(&q, 2, 1) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_286_prefetch_redirect(&q, 0, 0x1000000) == BM_STATUS_INVALID_ARGUMENT);
    assert(!memcmp(&q, &saved, sizeof(q)));
    assert(bm_286_prefetch_redirect(&q, 0, 100) == BM_STATUS_OK);
    assert(bm_286_prefetch_redirect_protected(&q, 0xfffffe, 0xfffffe) == BM_STATUS_OK);
    assert(bm_286_prefetch_begin(&q, &r) == BM_STATUS_OK && r.size == 2);
    bytes[0] = 0x90; bytes[1] = 0xff;
    assert(bm_286_prefetch_complete(&q, r.ticket, bytes, 2) == BM_STATUS_OK);
    assert(q.count == 1 && q.next == 0x1000000);
    assert(bm_286_prefetch_take(&q, &value) == BM_STATUS_OK && value == 0x90);
    assert(bm_286_prefetch_take(&q, &value) == BM_STATUS_IDLE);
    assert(bm_286_prefetch_begin(&q, &r) == BM_STATUS_IDLE);
    assert(bm_286_prefetch_redirect(&q, 0, 100) == BM_STATUS_OK);
    q.serial = UINT64_MAX; saved = q;
    assert(bm_286_prefetch_begin(&q, &r) == BM_STATUS_INVALID_STATE);
    assert(!memcmp(&q, &saved, sizeof(q)));
    assert(bm_286_prefetch_redirect(&other, 10, 100) == BM_STATUS_OK);
    fill(&other);
    assert(!memcmp(&q, &saved, sizeof(q)));
    assert(bm_286_prefetch_redirect(NULL, 0, 1) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_286_prefetch_begin(&other, NULL) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_286_prefetch_complete(&other, 1, NULL, 1) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_286_prefetch_take(&other, NULL) == BM_STATUS_INVALID_ARGUMENT);
    return 0;
}
