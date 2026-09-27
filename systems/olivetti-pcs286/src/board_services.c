/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors */
#include "board_services.h"
#include "checks.h"
#include "port61.h"
#include "refresh.h"
#include <string.h>

struct bm_pcs286_services {
    bm_host_services_t          host;
    bm_pcs286_services_config_t config;
    bm_engine_t                *engine;
    bm_pit8254_t               *pit;
    bm_at_rtc_t                *rtc;
    bm_at_keyboard_pair_t      *keyboard;
    bm_at_clock_link_t         *links[3]; /* PIT, RTC, keyboard: explicit sync/reset order. */
    bm_pcs286_port61_t          port;
    bm_pcs286_checks_t          checks;
    bm_pcs286_refresh_t         refresh;
    bm_status_t                 failure;
    int                         busy, cpu_active, io_active, notifying, resetting, completed_refresh;
};
static bm_status_t
retain(bm_pcs286_services_t *s, bm_status_t error)
{
    if (error == BM_STATUS_IDLE)
        error = BM_STATUS_INVALID_STATE;
    if (s->failure == BM_STATUS_OK)
        s->failure = error;
    return s->failure;
}
static bm_status_t
available(bm_pcs286_services_t *s)
{
    if (!s)
        return BM_STATUS_INVALID_ARGUMENT;
    if (s->busy || s->notifying)
        return BM_STATUS_INVALID_STATE;
    return s->failure;
}
static bm_status_t
refresh(bm_pcs286_services_t *s)
{
    bm_status_t r = bm_pcs286_refresh_service(&s->refresh);
    if (r == BM_STATUS_OK)
        s->completed_refresh = 1;
    else if (r != BM_STATUS_IDLE)
        return retain(s, r);
    return s->failure;
}
static bm_status_t
sync(bm_pcs286_services_t *s)
{
    for (unsigned i = 0; i < 3; ++i) {
        bm_status_t r = bm_at_clock_link_sync(s->links[i]);
        if (r != BM_STATUS_OK)
            return retain(s, r);
        if (s->failure != BM_STATUS_OK)
            return s->failure;
    }
    return refresh(s);
}
static void
pit_output(void *p, unsigned channel, int level)
{
    bm_pcs286_services_t *s = p;
    bm_status_t           r;
    if (channel == 0)
        r = bm_at_pic_set_irq(s->config.control->config.pic, 0, level);
    else if (channel == 1) {
        r = bm_pcs286_refresh_pit_input(&s->refresh, level);
        /* Assert the request at this event, not at the end of a long run.
         * This never executes CPU or forces HLDA. During epoch reset just
         * record the transition; refresh state is reset after the PIT. */
        if (r == BM_STATUS_OK && !s->resetting)
            r = refresh(s);
    } else
        r = bm_pcs286_port61_pit_input(&s->port, channel, level);
    (void) retain(s, r);
}
static bm_status_t
irq8(void *p, int level)
{
    bm_pcs286_services_t *s = p;
    return retain(s, bm_at_pic_set_irq(s->config.control->config.pic, 8, level));
}
static bm_status_t
nmi_mask(void *p, int level)
{
    bm_pcs286_services_t *s = p;
    return retain(s, bm_pcs286_checks_mask(&s->checks, level));
}
static bm_status_t
irq1(void *p, int level)
{
    bm_pcs286_services_t *s = p;
    return retain(s, bm_pcs286_control_irq1(s->config.control, level));
}
static bm_status_t
a20(void *p, int level)
{
    bm_pcs286_services_t *s = p;
    return retain(s, bm_pcs286_control_a20(s->config.control, level));
}
static bm_status_t
reset_line(void *p, int level)
{
    bm_pcs286_services_t *s = p;
    return retain(s, bm_pcs286_control_reset_line(s->config.control, level));
}
static bm_status_t
check_status(void *p, uint8_t *bits)
{
    bm_pcs286_services_t *s = p;
    bm_status_t           r = bm_pcs286_checks_status(&s->checks, bits);
    if (r == BM_STATUS_OK)
        *bits |= s->refresh.state.refdet ? 0x10 : 0;
    return r;
}
static bm_status_t
check_enable(void *p, int ram, int io)
{
    bm_pcs286_services_t *s = p;
    return retain(s, bm_pcs286_checks_enable(&s->checks, ram, io));
}
static void
speaker(void *p, int level)
{
    bm_pcs286_services_t *s = p;
    s->notifying            = 1;
    if (s->config.speaker)
        s->config.speaker(s->config.speaker_context, level);
    s->notifying = 0;
}
void
bm_pcs286_services_destroy(bm_pcs286_services_t *s)
{
    if (!s || s->busy || s->notifying)
        return;
    bm_engine_destroy(s->engine);
    for (unsigned i = 0; i < 3; ++i)
        bm_at_clock_link_destroy(s->links[i]);
    bm_at_keyboard_pair_destroy(s->keyboard);
    bm_at_rtc_destroy(s->rtc);
    bm_pit8254_destroy(s->pit);
    s->host.release(s->host.context, s);
}
bm_status_t
bm_pcs286_services_create(const bm_host_services_t          *host,
                          const bm_pcs286_services_config_t *config, bm_pcs286_services_t **out)
{
    bm_pcs286_services_t        *s;
    bm_status_t                  r;
    bm_at_bus_arbitration_t      bus;
    bm_pit8254_config_t          pit;
    bm_pcs286_port61_config_t    port;
    bm_at_rtc_config_t           rtc;
    bm_at_keyboard_pair_config_t keyboard;
    bm_engine_config_t           engine = { 1, 1, 3 };
    if (!out)
        return BM_STATUS_INVALID_ARGUMENT;
    *out = NULL;
    if (bm_host_services_validate(host) != BM_STATUS_OK || !config || !config->control)
        return BM_STATUS_INVALID_ARGUMENT;
    rtc      = config->rtc;
    keyboard = config->keyboard;
    if (rtc.io_base != 0x70 || rtc.irq || rtc.nmi_mask || rtc.output_context || keyboard.controller.data_port != 0x60 || keyboard.controller.command_port != 0x64 || keyboard.controller.irq || keyboard.controller.a20 || keyboard.controller.cpu_reset || keyboard.controller.output_context || keyboard.controller.keyboard_command || keyboard.controller.keyboard_inhibit || keyboard.controller.keyboard_context || keyboard.keyboard.send || keyboard.keyboard.send_context)
        return BM_STATUS_INVALID_ARGUMENT;
    if (config->control->busy || config->control->state.failure || config->control->state.nmi || config->control->state.reset_pending || config->control->state.reset_level)
        return BM_STATUS_INVALID_STATE;
    r = bm_at_bus_arbitration(config->control->config.bus, &bus);
    if (r != BM_STATUS_OK)
        return r;
    if (bus.requested || bus.locked || bus.hlda || bus.hold)
        return BM_STATUS_INVALID_STATE;
    s = host->allocate(host->context, sizeof(*s));
    if (!s)
        return BM_STATUS_OUT_OF_MEMORY;
    memset(s, 0, sizeof(*s));
    s->host   = *host;
    s->config = *config;
    r         = bm_pcs286_refresh_initialize(&s->refresh, config->control->config.bus);
    if (r != BM_STATUS_OK)
        goto fail;
    r = bm_pcs286_checks_initialize(&s->checks, bm_pcs286_control_nmi, config->control);
    if (r != BM_STATUS_OK)
        goto fail;
    r = bm_engine_create_clocked(host, &engine, &s->engine);
    if (r != BM_STATUS_OK)
        goto fail;
    pit = (bm_pit8254_config_t) { 0x40, pit_output, s };
    r   = bm_pit8254_create(host, &pit, &s->pit);
    if (r != BM_STATUS_OK)
        goto fail;
    rtc.irq            = irq8;
    rtc.nmi_mask       = nmi_mask;
    rtc.output_context = s;
    r                  = bm_at_rtc_create(host, &rtc, &s->rtc);
    if (r != BM_STATUS_OK)
        goto fail;
    keyboard.controller.irq            = irq1;
    keyboard.controller.a20            = a20;
    keyboard.controller.cpu_reset      = reset_line;
    keyboard.controller.output_context = s;
    r                                  = bm_at_keyboard_pair_create(host, &keyboard, &s->keyboard);
    if (r != BM_STATUS_OK)
        goto fail;
    r = bm_pit8254_attach_clock(host, s->engine, s->pit, &config->pit_clock, &s->links[0]);
    if (r != BM_STATUS_OK)
        goto fail;
    r = bm_at_rtc_attach_clock(host, s->engine, s->rtc, &s->links[1]);
    if (r != BM_STATUS_OK)
        goto fail;
    r = bm_at_keyboard_pair_attach_clock(host, s->engine, s->keyboard, &s->links[2]);
    if (r != BM_STATUS_OK)
        goto fail;
    port = (bm_pcs286_port61_config_t) { BM_PCS286_PORT61_AT_SIGNALS, s->pit, s->links[0],
                                         check_status, check_enable, s, speaker, s };
    r    = bm_pcs286_port61_initialize(&s->port, &port);
    if (r != BM_STATUS_OK)
        goto fail;
    s->busy = 1;
    pit_output(s, 0, 0);
    r = s->failure;
    if (r == BM_STATUS_OK)
        r = bm_at_rtc_reset(s->rtc);
    if (r == BM_STATUS_OK)
        r = bm_at_clock_link_changed(s->links[1]);
    if (r == BM_STATUS_OK)
        r = bm_at_keyboard_pair_reset(s->keyboard);
    if (r == BM_STATUS_OK)
        r = bm_at_clock_link_changed(s->links[2]);
    s->busy = 0;
    if (r != BM_STATUS_OK)
        goto fail;
    *out = s;
    return BM_STATUS_OK;
fail:
    bm_pcs286_services_destroy(s);
    return r;
}
bm_status_t
bm_pcs286_services_advance(bm_pcs286_services_t *s, bm_tick_t ns)
{
    bm_status_t r = available(s);
    if (r != BM_STATUS_OK)
        return r;
    if (ns > UINT64_MAX - bm_engine_now(s->engine))
        return BM_STATUS_INVALID_ARGUMENT;
    s->busy = 1;
    r       = sync(s);
    if (r == BM_STATUS_OK)
        r = bm_engine_run_for(s->engine, ns);
    if (r == BM_STATUS_OK)
        r = sync(s);
    if (r != BM_STATUS_OK)
        (void) retain(s, r);
    s->busy = 0;
    return s->failure;
}
bm_status_t
bm_pcs286_services_step(bm_pcs286_services_t *s, bm_pcs286_services_step_t *result)
{
    bm_pcs286_services_step_t next = { 0 };
    bm_status_t               r;
    if (!result)
        return BM_STATUS_INVALID_ARGUMENT;
    r = available(s);
    if (r != BM_STATUS_OK)
        return r;
    s->busy              = 1;
    s->completed_refresh = 0;
    r                    = sync(s);
    if (r == BM_STATUS_OK) {
        s->cpu_active      = 1;
        r                  = bm_pcs286_control_step(s->config.control, &next.cpu);
        s->cpu_active      = 0;
        next.cpu_completed = r == BM_STATUS_OK;
        if (r == BM_STATUS_OK || r == BM_STATUS_IDLE) {
            bm_status_t serviced = refresh(s);
            if (serviced != BM_STATUS_OK)
                r = serviced;
        }
    }
    next.refresh_completed = s->completed_refresh;
    if (s->failure != BM_STATUS_OK)
        r = s->failure;
    if (r == BM_STATUS_OK || (r == BM_STATUS_IDLE && next.refresh_completed)) {
        *result = next;
        r       = BM_STATUS_OK;
    }
    /* CPU errors are retained by control, not confused with device errors. */
    s->busy = 0;
    return r;
}
bm_status_t
bm_pcs286_services_io(void *p, bm_bus_transaction_t *t)
{
    bm_pcs286_services_t *s = p;
    bm_bus_transaction_t  next;
    bm_status_t           r;
    int                   index, debug, outer;
    if (!s || !t || t->space != BM_ADDRESS_IO || t->size != 1 || t->alignment != 1)
        return BM_STATUS_INVALID_ARGUMENT;
    if (t->address >= 0x40 && t->address <= 0x43)
        index = 0;
    else if (t->address == 0x70 || t->address == 0x71)
        index = 1;
    else if (t->address == 0x60 || t->address == 0x64)
        index = 2;
    else if (t->address == 0x61)
        index = 3;
    else
        return BM_STATUS_UNMAPPED;
    debug = (t->attributes & BM_BUS_TRANSACTION_DEBUG) != 0;
    if (t->operation != BM_BUS_READ && t->operation != BM_BUS_WRITE)
        return BM_STATUS_UNSUPPORTED;
    if (debug && t->operation == BM_BUS_WRITE)
        return BM_STATUS_UNSUPPORTED;
    if (!debug && (s->notifying || s->io_active || (s->busy && !s->cpu_active)))
        return BM_STATUS_INVALID_STATE;
    if (!debug && s->failure != BM_STATUS_OK)
        return s->failure;
    outer = s->busy;
    if (!debug) {
        s->busy      = 1;
        s->io_active = 1;
    }
    next = *t;
    r    = debug ? BM_STATUS_OK : sync(s);
    if (r == BM_STATUS_OK)
        r = index == 3 ? bm_pcs286_port61_io(&s->port, &next) : bm_at_clock_link_io(s->links[index], &next);
    if (!debug) {
        bm_status_t observed = sync(s);
        if (observed != BM_STATUS_OK)
            r = observed;
    }
    if (!debug && s->failure != BM_STATUS_OK)
        r = s->failure;
    if (r == BM_STATUS_OK)
        *t = next;
    if (!debug) {
        s->io_active = 0;
        s->busy      = outer;
    }
    return r;
}
bm_status_t
bm_pcs286_services_input(bm_pcs286_services_t *s, const bm_input_event_t *event)
{
    bm_status_t r;
    if (!event)
        return BM_STATUS_INVALID_ARGUMENT;
    r = available(s);
    if (r != BM_STATUS_OK)
        return r;
    s->busy = 1;
    r       = sync(s);
    if (r == BM_STATUS_OK) {
        r                    = bm_at_keyboard_pair_clock_input(s->links[2], event);
        bm_status_t observed = sync(s);
        if (observed != BM_STATUS_OK)
            r = observed;
    }
    s->busy = 0;
    return s->failure != BM_STATUS_OK ? s->failure : r;
}
static bm_status_t
check_input(bm_pcs286_services_t *s, int level, int parity)
{
    bm_status_t r;
    if (level != 0 && level != 1)
        return BM_STATUS_INVALID_ARGUMENT;
    r = available(s);
    if (r != BM_STATUS_OK)
        return r;
    s->busy = 1;
    r       = sync(s);
    if (r == BM_STATUS_OK)
        r = parity ? bm_pcs286_checks_memory_sample(&s->checks, level) : bm_pcs286_checks_io_input(&s->checks, level);
    if (r != BM_STATUS_OK)
        (void) retain(s, r);
    s->busy = 0;
    return s->failure;
}
bm_status_t
bm_pcs286_services_io_check(bm_pcs286_services_t *s, int level)
{
    return check_input(s, level, 0);
}
bm_status_t
bm_pcs286_services_parity(bm_pcs286_services_t *s, int level)
{
    return check_input(s, level, 1);
}
bm_status_t
bm_pcs286_services_state(const bm_pcs286_services_t *s, bm_pcs286_services_state_t *state)
{
    bm_pcs286_services_state_t next = { 0 };
    bm_status_t                r;
    if (!s || !state)
        return BM_STATUS_INVALID_ARGUMENT;
    r = bm_engine_now_exact(s->engine, &next.time);
    if (r != BM_STATUS_OK)
        return r;
    next.failure         = s->failure;
    next.refresh_pending = s->refresh.state.pending;
    next.refdet          = s->refresh.state.refdet;
    next.nmi             = s->checks.state.nmi;
    next.io_check_active = s->checks.state.io_active;
    next.port61          = s->port.state.latch;
    *state               = next;
    return BM_STATUS_OK;
}
bm_status_t
bm_pcs286_services_inspect_rtc(const bm_pcs286_services_t *s, bm_at_rtc_state_t *state,
                               uint8_t *cmos, size_t size)
{
    bm_at_rtc_state_t next;
    uint8_t bytes[128];
    bm_status_t r;
    if (!s || !state || !cmos || size > sizeof(bytes)) return BM_STATUS_INVALID_ARGUMENT;
    r = bm_at_rtc_state(s->rtc, &next);
    if (r == BM_STATUS_OK) r = bm_at_rtc_export_cmos(s->rtc, bytes, size);
    if (r != BM_STATUS_OK) return r;
    *state = next; memcpy(cmos, bytes, size); return BM_STATUS_OK;
}
bm_status_t
bm_pcs286_services_reset_cpu(bm_pcs286_services_t *s)
{
    bm_status_t r = available(s);
    if (r != BM_STATUS_OK)
        return r;
    s->busy = 1;
    r       = sync(s);
    if (r == BM_STATUS_OK)
        r = bm_pcs286_control_reset_cpu(s->config.control);
    s->busy = 0;
    return r;
}
bm_status_t
bm_pcs286_services_reset_epoch(bm_pcs286_services_t *s, int recover)
{
    bm_at_bus_arbitration_t bus;
    bm_status_t             r;
    if (!s || (recover != 0 && recover != 1))
        return BM_STATUS_INVALID_ARGUMENT;
    if (s->busy || s->notifying)
        return BM_STATUS_INVALID_STATE;
    r = bm_at_bus_arbitration(s->config.control->config.bus, &bus);
    if (r != BM_STATUS_OK)
        return r;
    if (bus.requested && bus.requester != BM_AT_MASTER_REFRESH)
        return BM_STATUS_INVALID_STATE;
    if (!recover && s->failure != BM_STATUS_OK)
        return s->failure;
    s->busy = 1;
    if (!recover) {
        r = sync(s);
        if (r != BM_STATUS_OK)
            goto done;
    }
    s->failure   = BM_STATUS_OK;
    s->resetting = 1;
    if (bus.requested) {
        r = bm_at_bus_request(s->config.control->config.bus, BM_AT_MASTER_REFRESH, 0);
        if (r != BM_STATUS_OK)
            goto done;
    }
    r = bm_pcs286_control_reset_cpu(s->config.control);
    if (r != BM_STATUS_OK)
        goto done;
    bm_at_bus_reset(s->config.control->config.bus);
    bm_at_pic_reset(s->config.control->config.pic);
    r = bm_engine_reset(s->engine);
    if (r != BM_STATUS_OK)
        goto done;
    r = bm_pcs286_checks_reset(&s->checks);
    if (r != BM_STATUS_OK)
        goto done;
    for (unsigned i = 0; i < 3; ++i) {
        r = bm_at_clock_link_reset(s->links[i]);
        if (r != BM_STATUS_OK || s->failure != BM_STATUS_OK)
            goto done;
    }
    r = bm_pcs286_refresh_reset(&s->refresh);
    if (r != BM_STATUS_OK)
        goto done;
    r = bm_pcs286_port61_reset(&s->port);
    if (r != BM_STATUS_OK)
        goto done;
    r = bm_pcs286_control_reset_cpu(s->config.control);
done:
    if (r != BM_STATUS_OK)
        (void) retain(s, r);
    s->resetting = s->busy = 0;
    return s->failure;
}
