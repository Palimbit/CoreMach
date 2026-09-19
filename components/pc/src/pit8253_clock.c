/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/components/pit8253_clock.h>

static bm_status_t
pit_clock_edge(bm_engine_t *engine, void *context,
               const bm_time_point_t *when, uint64_t *cycles_until_next)
{
    bm_pit8253_t *pit = context;
    bm_status_t status;

    (void) engine;
    (void) when;
    status = bm_pit8253_advance(pit, 1U);
    if (status != BM_STATUS_OK)
        return status;
    *cycles_until_next = 1U;
    return BM_STATUS_OK;
}

bm_status_t
bm_pit8253_attach_clock(bm_engine_t *engine, bm_pit8253_t *pit,
                        const bm_clock_rate_t *rate,
                        bm_timed_source_id_t *out_source_id)
{
    if ((engine == NULL) || (pit == NULL) || (rate == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    return bm_engine_add_timed_source(engine, pit_clock_edge, pit, rate, 1U,
                                      out_source_id);
}
