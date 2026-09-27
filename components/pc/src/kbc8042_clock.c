/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * KBC binding of the shared PIT/RTC engine clock adapter. */
#include "at_clock_private.h"
#include "kbc8042_private.h"
static bm_status_t kbc_advance(void *device, uint64_t cycles)
{ return bm_kbc8042_advance(device, cycles); }
static bm_status_t kbc_next(const void *device, uint64_t *cycles)
{ return bm_kbc8042_next_deadline(device, cycles); }
static bm_status_t kbc_reset(void *device)
{ return bm_kbc8042_reset(device); }
static const bm_at_clock_device_ops_t kbc_ops = {
    kbc_advance, kbc_next, bm_kbc8042_io, kbc_reset
};
bm_status_t bm_kbc8042_attach_clock(const bm_host_services_t *host,
    bm_engine_t *engine, bm_kbc8042_t *kbc, const bm_clock_rate_t *rate,
    bm_at_clock_link_t **out_link)
{
    if (out_link) *out_link = NULL;
    if (!kbc || !bm_at_clock_rates_equal(rate, &kbc->config.clock)) return BM_STATUS_INVALID_ARGUMENT;
    if (kbc->s.cycles) return BM_STATUS_INVALID_STATE;
    return bm_at_clock_attach(host, engine, kbc, rate, &kbc_ops,
                              &kbc->busy, &kbc->clock_link, out_link);
}
static bm_status_t receive(void *device, const void *value)
{ return bm_kbc8042_receive_keyboard(device, *(const uint8_t *)value); }
bm_status_t bm_kbc8042_clock_receive(bm_at_clock_link_t *link, uint8_t value)
{ return bm_at_clock_apply(link, &kbc_ops, receive, &value); }
