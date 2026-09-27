/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors */
#include "bus_priority_286.h"

bm_status_t bm_286_bus_select(unsigned ready, bool busy, bool lock_owned,
                            bool prefetch_inhibited, unsigned *out)
{
    unsigned bit;
    if (!out || (ready & ~63u)) return BM_STATUS_INVALID_ARGUMENT;
    if (busy) ready = 0;
    if (lock_owned) ready &= BM_286_BUS_LOCKED;
    if (prefetch_inhibited) ready &= ~BM_286_BUS_PREFETCH;
    for (bit = 1; bit <= BM_286_BUS_PREFETCH; bit <<= 1) {
        if (ready & bit) {
            *out = bit;
            return BM_STATUS_OK;
        }
    }
    *out = 0;
    return BM_STATUS_IDLE;
}
