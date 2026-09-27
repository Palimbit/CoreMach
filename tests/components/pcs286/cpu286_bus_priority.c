/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors */
#include "bus_priority_286.h"
#include "prefetch_286.h"
#include <assert.h>
#include <limits.h>

int main(void)
{
    unsigned ready, busy, lock, inhibit, winner;
    const unsigned priority[] = {BM_286_BUS_LOCKED, BM_286_BUS_SPLIT,
        BM_286_BUS_HOLD, BM_286_BUS_EXTENSION, BM_286_BUS_DATA, BM_286_BUS_PREFETCH};
    for (ready = 0; ready < 64; ++ready)
        for (busy = 0; busy < 2; ++busy)
            for (lock = 0; lock < 2; ++lock)
                for (inhibit = 0; inhibit < 2; ++inhibit) {
                    unsigned expected = 0, i;
                    if (!busy) {
                        for (i = 0; i < 6; ++i) {
                            unsigned candidate = priority[i];
                            if (!(ready & candidate)) continue;
                            if (lock && candidate != BM_286_BUS_LOCKED) continue;
                            if (inhibit && candidate == BM_286_BUS_PREFETCH) continue;
                            expected = candidate; break;
                        }
                    }
                    winner = 99;
                    assert(bm_286_bus_select(ready, busy != 0, lock != 0, inhibit != 0, &winner)
                           == (expected ? BM_STATUS_OK : BM_STATUS_IDLE));
                    assert(winner == expected);
                }
    winner = 99;
    assert(bm_286_bus_select(64, true, true, true, &winner) == BM_STATUS_INVALID_ARGUMENT);
    assert(winner == 99);
    assert(bm_286_bus_select(UINT_MAX, false, false, false, &winner) == BM_STATUS_INVALID_ARGUMENT);
    assert(winner == 99);
    assert(bm_286_bus_select(0, false, false, false, 0) == BM_STATUS_INVALID_ARGUMENT);
    /* Synthetic grant sequence: queue stays empty while data/HOLD own bus.
     * This is not a claim about the actual duration of those transfers. */
    {
        bm_286_prefetch_t q = {0};
        bm_286_prefetch_request_t request;
        uint8_t bytes[2] = {0x90, 0xf4}, value;
        assert(bm_286_prefetch_redirect(&q, 0x100, 0x1ff) == BM_STATUS_OK);
        assert(bm_286_bus_select(BM_286_BUS_DATA | BM_286_BUS_PREFETCH,
                                false, false, false, &winner) == BM_STATUS_OK);
        assert(winner == BM_286_BUS_DATA && q.count == 0 && !q.pending);
        assert(bm_286_bus_select(BM_286_BUS_PREFETCH, true, false, false, &winner) == BM_STATUS_IDLE);
        assert(bm_286_bus_select(BM_286_BUS_PREFETCH, false, false, false, &winner) == BM_STATUS_OK);
        assert(winner == BM_286_BUS_PREFETCH);
        assert(bm_286_prefetch_begin(&q, &request) == BM_STATUS_OK);
        assert(bm_286_bus_select(BM_286_BUS_HOLD, true, false, false, &winner) == BM_STATUS_IDLE);
        assert(bm_286_prefetch_complete(&q, request.ticket, bytes, 2) == BM_STATUS_OK);
        assert(bm_286_bus_select(BM_286_BUS_HOLD, false, false, false, &winner) == BM_STATUS_OK);
        assert(winner == BM_286_BUS_HOLD);
        assert(bm_286_prefetch_take(&q, &value) == BM_STATUS_OK && value == 0x90);
    }
    return 0;
}
