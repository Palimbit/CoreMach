/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * RTC binding of the shared PIT-derived engine clock adapter. */
#include "at_clock_private.h"
#include "rtc_at_private.h"
static bm_status_t rtc_advance(void *rtc, uint64_t cycles)
{ return bm_at_rtc_advance(rtc, cycles); }
static bm_status_t rtc_next(const void *rtc, uint64_t *cycles)
{ return bm_at_rtc_next_deadline(rtc, cycles); }
static bm_status_t rtc_reset(void *rtc)
{ return bm_at_rtc_reset(rtc); }
static const bm_at_clock_device_ops_t rtc_ops = {
    rtc_advance, rtc_next, bm_at_rtc_io, rtc_reset
};
bm_status_t bm_at_rtc_attach_clock(const bm_host_services_t *host,
    bm_engine_t *engine, bm_at_rtc_t *rtc, bm_at_clock_link_t **out_link)
{
    static const bm_clock_rate_t rate = {32768, 1};
    if (out_link) *out_link = NULL;
    if (!rtc) return BM_STATUS_INVALID_ARGUMENT;
    if (rtc->state.cycles) return BM_STATUS_INVALID_STATE;
    return bm_at_clock_attach(host, engine, rtc, &rate, &rtc_ops,
                              &rtc->busy, &rtc->clock_link, out_link);
}
