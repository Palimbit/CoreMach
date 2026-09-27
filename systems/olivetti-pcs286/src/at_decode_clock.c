/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors */
#include "at_decode_clock.h"
#include "clock_math.h"

bm_status_t bm_pcs286_at_convert_waits(bm_clock_rate_t service, bm_clock_rate_t requester,
                                 uint64_t clocks, uint32_t *out)
{
    bm_clock_position_t duration, rounded;
    uint64_t cycles;
    bm_status_t status = bm_clock_position_init(&duration, &service);
    if (status != BM_STATUS_OK) return status;
    status = bm_clock_position_init(&rounded, &requester);
    if (status != BM_STATUS_OK) return status;
    status = bm_clock_position_advance(&duration, clocks);
    if (status != BM_STATUS_OK) return status;
    status = bm_clock_cycles_at_or_before(&requester, &duration, &cycles);
    if (status != BM_STATUS_OK) return status;
    if (cycles > UINT32_MAX) return BM_STATUS_CAPACITY_EXCEEDED;
    status = bm_clock_position_advance(&rounded, cycles);
    if (status != BM_STATUS_OK) return status;
    if (bm_clock_position_compare(&rounded, &duration) < 0) ++cycles;
    if (cycles > UINT32_MAX) return BM_STATUS_CAPACITY_EXCEEDED;
    *out = (uint32_t)cycles;
    return BM_STATUS_OK;
}
