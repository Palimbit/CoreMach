/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * PIT8254 binding; shared cursor/deadlines now live in at_clock.c. */
#include "at_clock_private.h"
#include "pit8254_private.h"
static bm_status_t pit_advance(void *pit, uint64_t cycles)
{ return bm_pit8254_advance(pit, cycles); }
static bm_status_t pit_next(const void *pit, uint64_t *cycles)
{ return bm_pit8254_next_deadline(pit, cycles); }
static bm_status_t pit_reset(void *pit)
{ bm_pit8254_reset(pit); return BM_STATUS_OK; }
static const bm_at_clock_device_ops_t pit_ops = {
    pit_advance, pit_next, bm_pit8254_io, pit_reset
};
bm_status_t bm_pit8254_attach_clock(const bm_host_services_t *host,
    bm_engine_t *engine, bm_pit8254_t *pit, const bm_clock_rate_t *rate,
    bm_at_clock_link_t **out_link)
{
    unsigned i;
    if (out_link) *out_link = NULL;
    if (!pit) return BM_STATUS_INVALID_ARGUMENT;
    for (i = 0; i < 3; ++i)
        if (pit->exact.channel[i].clocks) return BM_STATUS_INVALID_STATE;
    return bm_at_clock_attach(host, engine, pit, rate, &pit_ops,
                              &pit->busy, &pit->clock_link, out_link);
}
