/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors */
#include "board_control.h"
#include <string.h>

static bm_status_t
retain(bm_pcs286_control_t *c, bm_status_t s)
{
    if (s == BM_STATUS_IDLE)
        s = BM_STATUS_INVALID_STATE;
    if (c->state.failure == BM_STATUS_OK)
        c->state.failure = s;
    return c->state.failure;
}
static bm_status_t
line(bm_pcs286_control_t *c, int v)
{
    if (!c || (v != 0 && v != 1))
        return BM_STATUS_INVALID_ARGUMENT;
    return c->state.failure;
}
bm_status_t
bm_pcs286_control_initialize(bm_pcs286_control_t              *c,
                             const bm_pcs286_control_config_t *config)
{
    bm_286_arch_state_t arch;
    if (!c || !config || !config->cpu || !config->bus || !config->pic || !config->memory)
        return BM_STATUS_INVALID_ARGUMENT;
    if (bm_286_get_arch_state(config->cpu, &arch) != BM_STATUS_OK)
        return BM_STATUS_INVALID_ARGUMENT;
    memset(c, 0, sizeof(*c));
    c->config = *config;
    return BM_STATUS_OK;
}
bm_status_t
bm_pcs286_control_irq1(void *context, int v)
{
    bm_pcs286_control_t *c = context;
    bm_status_t          s = line(c, v);
    if (s != BM_STATUS_OK)
        return s;
    return retain(c, bm_at_pic_set_irq(c->config.pic, 1, v));
}
bm_status_t
bm_pcs286_control_a20(void *context, int v)
{
    bm_pcs286_control_t *c = context;
    bm_status_t          s = line(c, v);
    if (s != BM_STATUS_OK)
        return s;
    return retain(c, bm_headland_at_memory_a20(c->config.memory, v));
}
bm_status_t
bm_pcs286_control_reset_line(void *context, int v)
{
    bm_pcs286_control_t *c = context;
    bm_status_t          s = line(c, v);
    if (s != BM_STATUS_OK)
        return s;
    if (v && !c->state.reset_level)
        c->state.reset_pending = 1;
    c->state.reset_level = v;
    return BM_STATUS_OK;
}
bm_status_t
bm_pcs286_control_nmi(void *context, int v)
{
    bm_pcs286_control_t *c = context;
    bm_status_t          s = line(c, v);
    if (s != BM_STATUS_OK)
        return s;
    c->state.nmi = v;
    return retain(c, c->config.cpu->ops.signal(c->config.cpu->context, BM_286_SIGNAL_NMI, v));
}
static void
signal(bm_pcs286_control_t *c, uint32_t pin, int v)
{
    if (!c)
        return;
    bm_status_t s = line(c, v);
    if (s == BM_STATUS_OK)
        s = c->config.cpu->ops.signal(c->config.cpu->context, pin, v);
    (void) retain(c, s);
}
void
bm_pcs286_control_intr(void *context, int v)
{
    signal(context, BM_286_SIGNAL_INTR, v);
}
void
bm_pcs286_control_hold(void *context, int v)
{
    signal(context, BM_286_SIGNAL_HOLD, v);
}
void
bm_pcs286_control_hlda(void *context, int v)
{
    bm_pcs286_control_t *c = context;
    /* Always propagate CPU releases, even after an earlier host stop. */
    if (c)
        (void) retain(c, bm_at_bus_hold_ack(c->config.bus, v));
}
void
bm_pcs286_control_lock(void *context, int v)
{
    bm_pcs286_control_t *c = context;
    if (c)
        (void) retain(c, bm_at_bus_set_lock(c->config.bus, v));
}
static bm_status_t
reset_cpu(bm_pcs286_control_t *c)
{
    bm_at_pic_state_t       pic;
    bm_at_bus_arbitration_t bus;
    bm_cpu_t               *cpu = c->config.cpu;
    bm_status_t             s;
    c->state.failure = BM_STATUS_OK;
    s                = cpu->ops.reset(cpu->context);
    if (s != BM_STATUS_OK || c->state.failure != BM_STATUS_OK)
        return retain(c, s);
    c->state.reset_pending = 0;
    s                      = bm_at_pic_state(c->config.pic, &pic);
    if (s != BM_STATUS_OK)
        return retain(c, s);
    s = bm_at_bus_arbitration(c->config.bus, &bus);
    if (s != BM_STATUS_OK)
        return retain(c, s);
    bm_pcs286_control_intr(c, pic.intr);
    bm_pcs286_control_hold(c, bus.hold);
    if (c->state.failure != BM_STATUS_OK)
        return c->state.failure;
    return bm_pcs286_control_nmi(c, c->state.nmi);
}
bm_status_t
bm_pcs286_control_reset_cpu(bm_pcs286_control_t *c)
{
    bm_status_t s;
    if (!c)
        return BM_STATUS_INVALID_ARGUMENT;
    if (c->busy)
        return BM_STATUS_INVALID_STATE;
    c->busy = 1;
    s       = reset_cpu(c);
    c->busy = 0;
    return s;
}
bm_status_t
bm_pcs286_control_step(bm_pcs286_control_t *c, bm_pcs286_control_event_t *event)
{
    bm_pcs286_control_event_t result = { 0 };
    bm_status_t               s;
    if (!c || !event)
        return BM_STATUS_INVALID_ARGUMENT;
    if (c->busy)
        return BM_STATUS_INVALID_STATE;
    if (c->state.failure != BM_STATUS_OK)
        return c->state.failure;
    c->busy = 1;
    if (c->state.reset_pending) {
        s                    = reset_cpu(c);
        result.kind          = BM_PCS286_CONTROL_RESET;
        result.reset_applied = s == BM_STATUS_OK;
    } else if (c->state.reset_level) {
        s = BM_STATUS_IDLE;
    } else {
        s = bm_286_step(c->config.cpu, &result.cpu);
        if (c->state.failure != BM_STATUS_OK)
            s = c->state.failure;
        if (s == BM_STATUS_OK && c->state.reset_pending) {
            s                    = reset_cpu(c);
            result.reset_applied = s == BM_STATUS_OK;
        }
    }
    if (s == BM_STATUS_OK)
        *event = result;
    else if (s != BM_STATUS_IDLE)
        (void) retain(c, s);
    c->busy = 0;
    return s;
}
bm_status_t
bm_pcs286_control_state(const bm_pcs286_control_t *c, bm_pcs286_control_state_t *state)
{
    if (!c || !state)
        return BM_STATUS_INVALID_ARGUMENT;
    *state = c->state;
    return BM_STATUS_OK;
}
