/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/components/rtc_mm58167_clock.h>

#include "rtc_mm58167_private.h"

typedef struct bm_mm58167_clock_binding {
    bm_engine_t *engine;
    bm_mm58167_t *rtc;
    bm_timed_source_id_t source_id;
    uint64_t synchronized_microseconds;
    bm_status_t failure;
    int firing;
} bm_mm58167_clock_binding_t;

static uint64_t
microseconds_until_transition(const bm_mm58167_t *rtc)
{
    uint64_t until_millisecond = 1000U - rtc->microsecond_remainder;

    if ((rtc->rollover_microseconds != 0U) &&
        (rtc->rollover_microseconds < until_millisecond))
        return rtc->rollover_microseconds;
    return until_millisecond;
}

static bm_status_t
advance_to_cursor(bm_mm58167_clock_binding_t *binding)
{
    uint64_t cursor;
    uint64_t elapsed;
    bm_status_t status;

    if (binding->failure != BM_STATUS_OK)
        return binding->failure;
    status = bm_engine_timed_source_cycle_count(binding->engine,
                                                 binding->source_id, &cursor);
    if (status != BM_STATUS_OK) {
        binding->failure = status;
        return status;
    }
    /* Engine reset begins a new virtual-time epoch. Component reset or the
     * source's registered one-cycle wake-up establishes its new cursor. */
    if (cursor < binding->synchronized_microseconds)
        binding->synchronized_microseconds = 0U;

    elapsed = cursor - binding->synchronized_microseconds;
    binding->synchronized_microseconds = cursor;
    if (elapsed == 0U)
        return BM_STATUS_OK;
    status = bm_mm58167_advance_microseconds(binding->rtc, elapsed);
    if (status != BM_STATUS_OK)
        binding->failure = status;
    return status;
}

static bm_status_t
schedule_next_transition(bm_mm58167_clock_binding_t *binding)
{
    bm_status_t status;

    if (binding->firing)
        return BM_STATUS_OK;
    status = bm_engine_arm_timed_source(
        binding->engine, binding->source_id,
        microseconds_until_transition(binding->rtc));
    if (status != BM_STATUS_OK)
        binding->failure = status;
    return status;
}

static bm_status_t
rtc_clock_synchronize(void *context)
{
    return advance_to_cursor(context);
}

static bm_status_t
rtc_clock_changed(void *context)
{
    bm_mm58167_clock_binding_t *binding = context;
    bm_status_t status = advance_to_cursor(binding);

    if (status != BM_STATUS_OK)
        return status;
    return schedule_next_transition(binding);
}

static bm_status_t
rtc_clock_transition(bm_engine_t *engine, void *context,
                     const bm_time_point_t *when,
                     uint64_t *cycles_until_next)
{
    bm_mm58167_clock_binding_t *binding = context;
    bm_status_t status;

    (void) engine;
    (void) when;
    binding->firing = 1;
    status = advance_to_cursor(binding);
    binding->firing = 0;
    if (status != BM_STATUS_OK) {
        *cycles_until_next = 0U;
        return status;
    }
    *cycles_until_next = microseconds_until_transition(binding->rtc);
    return BM_STATUS_OK;
}

bm_status_t
bm_mm58167_attach_clock(bm_engine_t *engine, bm_mm58167_t *rtc,
                        bm_timed_source_id_t *out_source_id)
{
    static const bm_clock_rate_t microsecond_rate = {
        UINT64_C(1000000), 1U
    };
    bm_mm58167_clock_binding_t *binding;
    bm_status_t status;

    if ((engine == NULL) || (rtc == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    if (rtc->clock_binding != NULL)
        return BM_STATUS_INVALID_STATE;
    binding = rtc->host.allocate(rtc->host.context, sizeof(*binding));
    if (binding == NULL)
        return BM_STATUS_OUT_OF_MEMORY;
    binding->engine = engine;
    binding->rtc = rtc;
    binding->source_id = 0U;
    binding->synchronized_microseconds = 0U;
    binding->failure = BM_STATUS_OK;
    binding->firing = 0;

    status = bm_engine_add_timed_source(
        engine, rtc_clock_transition, binding, &microsecond_rate, 1U,
        &binding->source_id);
    if (status != BM_STATUS_OK) {
        rtc->host.release(rtc->host.context, binding);
        return status;
    }
    rtc->clock_sync = rtc_clock_synchronize;
    rtc->clock_changed = rtc_clock_changed;
    rtc->clock_context = binding;
    rtc->clock_binding = binding;
    if (out_source_id != NULL)
        *out_source_id = binding->source_id;

    return schedule_next_transition(binding);
}
