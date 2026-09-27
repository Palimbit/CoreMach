/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Functional interpretation of IBM6280070 sheets3/10/17 and NMI-mask control.
 * Composes existing port61/CPU APIs, without duplicating either device. */
#include "checks.h"
#include <string.h>

static int level_valid(int value) { return value == 0 || value == 1; }
static bm_status_t writable(bm_pcs286_checks_t *c)
{
    if (!c || !c->output) return BM_STATUS_INVALID_ARGUMENT;
    if (c->notifying) return BM_STATUS_INVALID_STATE;
    return c->state.failure;
}
static bm_status_t publish(bm_pcs286_checks_t *c, int force)
{
    bm_status_t s;
    int nmi = !c->state.masked && (c->state.ram_latched ||
        (c->state.io_enabled && (c->state.io_latched || c->state.io_active)));
    if (!force && nmi == c->state.nmi) return BM_STATUS_OK;
    c->state.nmi = nmi;
    c->notifying = 1;
    s = c->output(c->context, nmi);
    c->notifying = 0;
    if (s == BM_STATUS_IDLE) s = BM_STATUS_INVALID_STATE;
    if (s != BM_STATUS_OK) c->state.failure = s;
    return s;
}

bm_status_t bm_pcs286_checks_initialize(bm_pcs286_checks_t *c,
                                       bm_pcs286_nmi_fn output, void *context)
{
    if (!c || !output) return BM_STATUS_INVALID_ARGUMENT;
    memset(c, 0, sizeof(*c));
    c->output = output; c->context = context;
    c->state.ram_enabled = c->state.io_enabled = c->state.masked = 1;
    c->state.failure = BM_STATUS_OK;
    return BM_STATUS_OK;
}

bm_status_t bm_pcs286_checks_reset(bm_pcs286_checks_t *c)
{
    int failed;
    if (!c || !c->output) return BM_STATUS_INVALID_ARGUMENT;
    if (c->notifying) return BM_STATUS_INVALID_STATE;
    failed = c->state.failure != BM_STATUS_OK;
    c->state.ram_enabled = c->state.io_enabled = c->state.masked = 1;
    c->state.ram_latched = 0;
    c->state.io_latched = c->state.io_active;
    c->state.failure = BM_STATUS_OK;
    return publish(c, failed);
}

bm_status_t bm_pcs286_checks_enable(void *context, int ram, int io)
{
    bm_pcs286_checks_t *c = context;
    bm_status_t s;
    if (!level_valid(ram) || !level_valid(io)) return BM_STATUS_INVALID_ARGUMENT;
    s = writable(c); if (s != BM_STATUS_OK) return s;
    c->state.ram_enabled = ram; c->state.io_enabled = io;
    if (!ram) c->state.ram_latched = 0;
    c->state.io_latched = io && (c->state.io_latched || c->state.io_active);
    return publish(c, 0);
}

bm_status_t bm_pcs286_checks_memory_sample(bm_pcs286_checks_t *c, int bad)
{
    bm_status_t s;
    if (!level_valid(bad)) return BM_STATUS_INVALID_ARGUMENT;
    s = writable(c); if (s != BM_STATUS_OK) return s;
    if (bad && c->state.ram_enabled) c->state.ram_latched = 1;
    return publish(c, 0);
}

bm_status_t bm_pcs286_checks_io_input(bm_pcs286_checks_t *c, int active)
{
    bm_status_t s;
    if (!level_valid(active)) return BM_STATUS_INVALID_ARGUMENT;
    s = writable(c); if (s != BM_STATUS_OK) return s;
    c->state.io_active = active;
    c->state.io_latched = c->state.io_enabled && (c->state.io_latched || active);
    return publish(c, 0);
}

bm_status_t bm_pcs286_checks_mask(bm_pcs286_checks_t *c, int masked)
{
    bm_status_t s;
    if (!level_valid(masked)) return BM_STATUS_INVALID_ARGUMENT;
    s = writable(c); if (s != BM_STATUS_OK) return s;
    c->state.masked = masked;
    return publish(c, 0);
}

bm_status_t bm_pcs286_checks_status(void *context, uint8_t *bits)
{
    const bm_pcs286_checks_t *c = context;
    if (!c || !c->output || !bits) return BM_STATUS_INVALID_ARGUMENT;
    *bits = (uint8_t)((c->state.ram_latched ? 0x80 : 0) |
                     ((c->state.io_latched || c->state.io_active) ? 0x40 : 0));
    return BM_STATUS_OK;
}

bm_status_t bm_pcs286_checks_state(const bm_pcs286_checks_t *c,
                                  bm_pcs286_checks_state_t *state)
{
    if (!c || !c->output || !state) return BM_STATUS_INVALID_ARGUMENT;
    *state = c->state;
    return BM_STATUS_OK;
}
