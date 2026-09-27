/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors */
#include "bus_phase_286.h"
#include "bus_priority_286.h"
#include "prefetch_286.h"
#include <assert.h>
#include <string.h>

int main(void)
{
    uint32_t waits;
    bm_286_bus_phase_event_t event = {0}, old_event;
    for (waits = 0; waits <= 4096; ++waits) {
        bm_286_bus_phase_state_t stepped = {100, 0, BM_286_PHASE_IDLE}, batched = stepped;
        uint32_t i;
        assert(bm_286_bus_phase_begin(&stepped) == BM_STATUS_OK);
        assert(bm_286_bus_phase_begin(&stepped) == BM_STATUS_INVALID_STATE);
        assert(bm_286_bus_phase_begin(&batched) == BM_STATUS_OK);
        assert(bm_286_bus_phase_finish(&batched, waits) == BM_STATUS_OK);
        assert(bm_286_bus_phase_tick(&stepped, true, &event) == BM_STATUS_OK);
        assert(event.phase == BM_286_PHASE_TS && !event.completed && event.clock == 100);
        for (i = 0; i < waits; ++i) {
            assert(bm_286_bus_phase_tick(&stepped, false, &event) == BM_STATUS_OK);
            assert(event.phase == BM_286_PHASE_TC && !event.completed);
        }
        assert(bm_286_bus_phase_tick(&stepped, true, &event) == BM_STATUS_OK && event.completed);
        assert(stepped.phase == batched.phase && stepped.clock == batched.clock && stepped.waits == batched.waits);
        assert(stepped.clock == 102u + waits);
        assert(bm_286_bus_phase_begin(&stepped) == BM_STATUS_OK); /* No artificial idle bubble. */
        assert(bm_286_bus_phase_tick(&stepped, false, &event) == BM_STATUS_OK);
        assert(event.phase == BM_286_PHASE_TS && !event.completed && event.clock == 102u + waits);
    }
    {
        bm_286_bus_phase_state_t s = {UINT64_MAX - 2, 0, BM_286_PHASE_IDLE}, saved;
        assert(bm_286_bus_phase_begin(&s) == BM_STATUS_OK);
        saved = s;
        assert(bm_286_bus_phase_finish(&s, 1) == BM_STATUS_INVALID_STATE);
        assert(!memcmp(&s, &saved, sizeof(s)));
        assert(bm_286_bus_phase_finish(&s, 0) == BM_STATUS_OK && s.clock == UINT64_MAX);
        saved = s; old_event = event;
        assert(bm_286_bus_phase_tick(&s, true, &event) == BM_STATUS_INVALID_STATE);
        assert(!memcmp(&s, &saved, sizeof(s)) && !memcmp(&event, &old_event, sizeof(event)));
        assert(bm_286_bus_phase_begin(&s) == BM_STATUS_INVALID_STATE);
        assert(bm_286_bus_phase_tick(NULL, false, &event) == BM_STATUS_INVALID_ARGUMENT);
        assert(bm_286_bus_phase_tick(&s, false, NULL) == BM_STATUS_INVALID_ARGUMENT);
    }
    {
        bm_286_bus_phase_state_t s = {0};
        bm_286_prefetch_t q = {0};
        bm_286_prefetch_request_t request;
        unsigned winner;
        uint8_t bytes[2] = {0x90, 0xf4};
        assert(bm_286_bus_phase_tick(&s, false, &event) == BM_STATUS_OK);
        assert(event.phase == BM_286_PHASE_IDLE && !event.completed);
        assert(bm_286_prefetch_redirect(&q, 0, 100) == BM_STATUS_OK);
        assert(bm_286_bus_select(BM_286_BUS_PREFETCH, false, false, false, &winner) == BM_STATUS_OK);
        assert(bm_286_prefetch_begin(&q, &request) == BM_STATUS_OK);
        assert(bm_286_bus_phase_begin(&s) == BM_STATUS_OK);
        assert(bm_286_bus_phase_tick(&s, false, &event) == BM_STATUS_OK && q.count == 0);
        assert(bm_286_bus_phase_tick(&s, false, &event) == BM_STATUS_OK && q.count == 0);
        assert(bm_286_bus_select(BM_286_BUS_DATA, s.phase != BM_286_PHASE_IDLE, false, false, &winner) == BM_STATUS_IDLE);
        assert(bm_286_bus_phase_tick(&s, true, &event) == BM_STATUS_OK && event.completed);
        assert(bm_286_prefetch_complete(&q, request.ticket, bytes, 2) == BM_STATUS_OK && q.count == 2);
    }
    return 0;
}
