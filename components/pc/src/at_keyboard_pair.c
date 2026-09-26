/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Composition of the attributed native KBC and keyboard, not a protocol rewrite. */
#include <blumach/components/at_keyboard_pair.h>
#include "at_clock_private.h"
#include "kbc8042_private.h"
#include "keyboard_at_private.h"
#include <string.h>
struct bm_at_keyboard_pair {
    bm_host_services_t host;
    bm_kbc8042_t      *controller;
    bm_at_keyboard_t  *keyboard;
    bm_clock_rate_t    rate;
    bm_status_t        failure;
    int                busy;
    void              *clock_link;
};
static bm_status_t
peer_byte(void *context, uint8_t byte)
{
    bm_at_keyboard_pair_t *p = context;
    return bm_kbc8042_receive_keyboard(p->controller, byte);
}
static bm_status_t
peer_command(void *context, uint8_t byte)
{
    bm_at_keyboard_pair_t *p = context;
    return bm_at_keyboard_command(p->keyboard, byte);
}
static bm_status_t
peer_inhibit(void *context, int level)
{
    bm_at_keyboard_pair_t *p = context;
    return bm_at_keyboard_set_inhibit(p->keyboard, level);
}
static bm_status_t
retain(bm_at_keyboard_pair_t *p, bm_status_t status)
{
    /* A native output may fail with the same status used for byte backpressure.
     * Consult sticky native state: accepted effects must never be retried. */
    if (p->controller->s.failure)
        p->failure = p->controller->s.failure;
    else if (p->keyboard->s.failure)
        p->failure = p->keyboard->s.failure;
    return p->failure ? p->failure : status;
}
bm_status_t
bm_at_keyboard_pair_create(const bm_host_services_t *host, const bm_at_keyboard_pair_config_t *config,
                           bm_at_keyboard_pair_t **out)
{
    bm_at_keyboard_pair_t  *p;
    bm_at_keyboard_config_t keyboard;
    bm_kbc8042_config_t     controller;
    bm_status_t             r;
    if (out)
        *out = NULL;
    if (!out || !config || bm_host_services_validate(host) != BM_STATUS_OK
        || !bm_at_clock_rates_equal(&config->controller.clock, &config->keyboard.clock)
        || config->keyboard.send || config->keyboard.send_context || config->controller.keyboard_command
        || config->controller.keyboard_inhibit || config->controller.keyboard_context)
        return BM_STATUS_INVALID_ARGUMENT;
    p = host->allocate(host->context, sizeof(*p));
    if (!p)
        return BM_STATUS_OUT_OF_MEMORY;
    memset(p, 0, sizeof(*p));
    p->host                     = *host;
    p->rate                     = config->controller.clock;
    keyboard                    = config->keyboard;
    keyboard.send               = peer_byte;
    keyboard.send_context       = p;
    controller                  = config->controller;
    controller.keyboard_command = peer_command;
    controller.keyboard_inhibit = peer_inhibit;
    controller.keyboard_context = p;
    r                           = bm_at_keyboard_create(host, &keyboard, &p->keyboard);
    if (r == BM_STATUS_OK)
        r = bm_kbc8042_create(host, &controller, &p->controller);
    if (r == BM_STATUS_OK)
        r = bm_at_keyboard_set_inhibit(p->keyboard, 1);
    if (r != BM_STATUS_OK) {
        bm_kbc8042_destroy(p->controller);
        bm_at_keyboard_destroy(p->keyboard);
        host->release(host->context, p);
        return r;
    }
    *out = p;
    return BM_STATUS_OK;
}
void
bm_at_keyboard_pair_destroy(bm_at_keyboard_pair_t *p)
{
    if (!p || p->busy || p->clock_link)
        return;
    bm_kbc8042_destroy(p->controller);
    bm_at_keyboard_destroy(p->keyboard);
    p->host.release(p->host.context, p);
}
bm_status_t
bm_at_keyboard_pair_reset(bm_at_keyboard_pair_t *p)
{
    bm_status_t r;
    if (!p)
        return BM_STATUS_INVALID_ARGUMENT;
    if (p->busy)
        return BM_STATUS_INVALID_STATE;
    p->busy    = 1;
    p->failure = BM_STATUS_OK;
    r          = bm_at_keyboard_reset(p->keyboard);
    if (r == BM_STATUS_OK)
        r = bm_kbc8042_reset(p->controller);
    if (r != BM_STATUS_OK)
        p->failure = r;
    p->busy = 0;
    return r;
}
bm_status_t
bm_at_keyboard_pair_io(void *context, bm_bus_transaction_t *t)
{
    bm_at_keyboard_pair_t *p = context;
    bm_status_t            r;
    if (!p || !t)
        return BM_STATUS_INVALID_ARGUMENT;
    if (t->attributes & BM_BUS_TRANSACTION_DEBUG)
        return bm_kbc8042_io(p->controller, t);
    if (p->busy)
        return BM_STATUS_INVALID_STATE;
    if (p->failure)
        return p->failure;
    p->busy = 1;
    r       = retain(p, bm_kbc8042_io(p->controller, t));
    p->busy = 0;
    return r;
}
bm_status_t
bm_at_keyboard_pair_input(bm_at_keyboard_pair_t *p, const bm_input_event_t *e)
{
    bm_status_t r;
    if (!p || !e)
        return BM_STATUS_INVALID_ARGUMENT;
    if (p->busy)
        return BM_STATUS_INVALID_STATE;
    if (p->failure)
        return p->failure;
    p->busy = 1;
    r       = retain(p, bm_at_keyboard_input(p->keyboard, e));
    p->busy = 0;
    return r;
}
bm_status_t
bm_at_keyboard_pair_next_deadline(const bm_at_keyboard_pair_t *p, uint64_t *cycles)
{
    uint64_t    a, b;
    bm_status_t r;
    if (!p || !cycles)
        return BM_STATUS_INVALID_ARGUMENT;
    if (p->failure)
        return p->failure;
    r = bm_kbc8042_next_deadline(p->controller, &a);
    if (r != BM_STATUS_OK && r != BM_STATUS_IDLE)
        return r;
    r = bm_at_keyboard_next_deadline(p->keyboard, &b);
    if (r != BM_STATUS_OK && r != BM_STATUS_IDLE)
        return r;
    *cycles = !a ? b : !b ? a : a < b ? a : b;
    return *cycles ? BM_STATUS_OK : BM_STATUS_IDLE;
}
bm_status_t
bm_at_keyboard_pair_advance(bm_at_keyboard_pair_t *p, uint64_t cycles)
{
    bm_status_t r = BM_STATUS_OK;
    if (!p)
        return BM_STATUS_INVALID_ARGUMENT;
    if (p->busy)
        return BM_STATUS_INVALID_STATE;
    if (p->failure)
        return p->failure;
    if (cycles > UINT64_MAX - p->controller->s.cycles || cycles > UINT64_MAX - p->keyboard->s.cycles)
        return BM_STATUS_CAPACITY_EXCEEDED;
    p->busy = 1;
    while (cycles && r == BM_STATUS_OK) {
        uint64_t           n, step;
        bm_kbc_edge_t      controller;
        bm_keyboard_edge_t keyboard;
        r = bm_at_keyboard_pair_next_deadline(p, &n);
        if (r != BM_STATUS_OK && r != BM_STATUS_IDLE)
            break;
        step = n && n < cycles ? n : cycles;
        bm_kbc8042_elapse(p->controller, step, &controller);
        bm_at_keyboard_elapse(p->keyboard, step, &keyboard);
        cycles -= step;
        p->controller->busy = 1;
        r                   = bm_kbc8042_settle(p->controller, &controller);
        p->controller->busy = 0;
        if (r == BM_STATUS_OK) {
            p->keyboard->busy = 1;
            r                 = bm_at_keyboard_settle(p->keyboard, &keyboard);
            p->keyboard->busy = 0;
        }
        r = retain(p, r);
    }
    if (r != BM_STATUS_OK)
        p->failure = r;
    p->busy = 0;
    return r;
}
bm_status_t
bm_at_keyboard_pair_state(const bm_at_keyboard_pair_t *p, bm_at_keyboard_pair_state_t *s)
{
    if (!p || !s)
        return BM_STATUS_INVALID_ARGUMENT;
    s->controller = p->controller->s;
    s->keyboard   = p->keyboard->s;
    s->failure    = p->failure;
    return BM_STATUS_OK;
}
static bm_status_t
advance(void *p, uint64_t n)
{
    return bm_at_keyboard_pair_advance(p, n);
}
static bm_status_t
next(const void *p, uint64_t *n)
{
    return bm_at_keyboard_pair_next_deadline(p, n);
}
static bm_status_t
reset(void *p)
{
    return bm_at_keyboard_pair_reset(p);
}
static const bm_at_clock_device_ops_t ops = { advance, next, bm_at_keyboard_pair_io, reset };
bm_status_t
bm_at_keyboard_pair_attach_clock(const bm_host_services_t *host, bm_engine_t *engine,
                                 bm_at_keyboard_pair_t *p, bm_at_clock_link_t **out)
{
    if (out)
        *out = NULL;
    if (!p)
        return BM_STATUS_INVALID_ARGUMENT;
    if (p->controller->s.cycles || p->keyboard->s.cycles)
        return BM_STATUS_INVALID_STATE;
    return bm_at_clock_attach(host, engine, p, &p->rate, &ops, &p->busy, &p->clock_link, out);
}
static bm_status_t
input(void *p, const void *e)
{
    return bm_at_keyboard_pair_input(p, e);
}
bm_status_t
bm_at_keyboard_pair_clock_input(bm_at_clock_link_t *link, const bm_input_event_t *event)
{
    return bm_at_clock_apply(link, &ops, input, event);
}
