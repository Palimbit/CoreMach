/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Keyboard binding of the shared PIT/RTC engine clock adapter. */
#include "at_clock_private.h"
#include "keyboard_at_private.h"
static bm_status_t keyboard_advance(void *device, uint64_t cycles)
{ return bm_at_keyboard_advance(device, cycles); }
static bm_status_t keyboard_next(const void *device, uint64_t *cycles)
{ return bm_at_keyboard_next_deadline(device, cycles); }
static bm_status_t keyboard_reset(void *device)
{ return bm_at_keyboard_reset(device); }
static bm_status_t no_io(void *device, bm_bus_transaction_t *transaction)
{
    (void)device; (void)transaction;
    return BM_STATUS_UNSUPPORTED; /* Keyboard has a byte link, not guest I/O ports. */
}
static const bm_at_clock_device_ops_t keyboard_ops = {
    keyboard_advance, keyboard_next, no_io, keyboard_reset
};
bm_status_t bm_at_keyboard_attach_clock(const bm_host_services_t *host,
    bm_engine_t *engine, bm_at_keyboard_t *keyboard, const bm_clock_rate_t *rate,
    bm_at_clock_link_t **out_link)
{
    if (out_link) *out_link = NULL;
    if (!keyboard || !bm_at_clock_rates_equal(rate, &keyboard->config.clock)) return BM_STATUS_INVALID_ARGUMENT;
    if (keyboard->s.cycles) return BM_STATUS_INVALID_STATE;
    return bm_at_clock_attach(host, engine, keyboard, rate, &keyboard_ops,
                              &keyboard->busy, &keyboard->clock_link, out_link);
}
static bm_status_t input(void *device, const void *event)
{ return bm_at_keyboard_input(device, event); }
static bm_status_t command(void *device, const void *value)
{ return bm_at_keyboard_command(device, *(const uint8_t *)value); }
static bm_status_t inhibit(void *device, const void *level)
{ return bm_at_keyboard_set_inhibit(device, *(const int *)level); }
bm_status_t bm_at_keyboard_clock_input(bm_at_clock_link_t *link, const bm_input_event_t *event)
{ return bm_at_clock_apply(link, &keyboard_ops, input, event); }
bm_status_t bm_at_keyboard_clock_command(bm_at_clock_link_t *link, uint8_t value)
{ return bm_at_clock_apply(link, &keyboard_ops, command, &value); }
bm_status_t bm_at_keyboard_clock_inhibit(bm_at_clock_link_t *link, int level)
{ return bm_at_clock_apply(link, &keyboard_ops, inhibit, &level); }
