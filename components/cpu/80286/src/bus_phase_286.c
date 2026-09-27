/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors */
#include "bus_phase_286.h"

bm_status_t bm_286_bus_phase_begin(bm_286_bus_phase_state_t *s)
{
    if (!s) return BM_STATUS_INVALID_ARGUMENT;
    if (s->phase != BM_286_PHASE_IDLE || s->clock > UINT64_MAX - 2)
        return BM_STATUS_INVALID_STATE;
    s->phase = BM_286_PHASE_TS; s->waits = 0;
    return BM_STATUS_OK;
}

bm_status_t bm_286_bus_phase_tick(bm_286_bus_phase_state_t *s, bool ready,
                                bm_286_bus_phase_event_t *out)
{
    bm_286_bus_phase_event_t e;
    if (!s || !out) return BM_STATUS_INVALID_ARGUMENT;
    if (s->phase < BM_286_PHASE_IDLE || s->phase > BM_286_PHASE_TC || s->clock == UINT64_MAX ||
        (s->phase == BM_286_PHASE_TC && !ready && s->waits == UINT64_MAX))
        return BM_STATUS_INVALID_STATE;
    e.clock = s->clock; e.phase = s->phase;
    e.completed = s->phase == BM_286_PHASE_TC && ready;
    ++s->clock;
    if (s->phase == BM_286_PHASE_TS) s->phase = BM_286_PHASE_TC;
    else if (e.completed) s->phase = BM_286_PHASE_IDLE;
    else if (s->phase == BM_286_PHASE_TC) ++s->waits;
    *out = e;
    return BM_STATUS_OK;
}

bm_status_t bm_286_bus_phase_finish(bm_286_bus_phase_state_t *s, uint32_t additional_waits)
{
    uint64_t duration = 2u + (uint64_t)additional_waits;
    if (!s) return BM_STATUS_INVALID_ARGUMENT;
    if (s->phase != BM_286_PHASE_TS || s->clock > UINT64_MAX - duration)
        return BM_STATUS_INVALID_STATE;
    s->clock += duration; s->waits = additional_waits; s->phase = BM_286_PHASE_IDLE;
    return BM_STATUS_OK;
}
