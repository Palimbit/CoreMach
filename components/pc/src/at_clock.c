/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Adapted from pit8253_clock.c: exact engine cursor and output deadlines.
 * Shared PIT/RTC/KBC/keyboard cursor, deadlines and ownership. Device semantics stay separate. */
#include "at_clock_private.h"
#include <string.h>

struct bm_at_clock_link {
    bm_host_services_t host;
    bm_engine_t *engine;
    void *device;
    const bm_at_clock_device_ops_t *ops;
    int *device_busy;
    void **owner;
    bm_timed_source_id_t source;
    uint64_t serviced;
    bm_status_t failure;
    int busy;
};

static bm_status_t advance(bm_at_clock_link_t *link)
{
    uint64_t cursor;
    bm_status_t status;
    if (link->failure != BM_STATUS_OK) return link->failure;
    status = bm_engine_timed_source_cycle_count(link->engine, link->source, &cursor);
    if (status == BM_STATUS_OK && cursor < link->serviced)
        status = BM_STATUS_INVALID_STATE; /* Never infer a device reset. */
    if (status == BM_STATUS_OK) {
        status = link->ops->advance(link->device, cursor - link->serviced);
        /* A failed advance may have published a prefix. Latch the failure,
         * never retry that interval; reset/reconstruction is an owner action. */
        if (status == BM_STATUS_OK) link->serviced = cursor;
    }
    if (status != BM_STATUS_OK) link->failure = status;
    return status;
}

static bm_status_t rearm(bm_at_clock_link_t *link)
{
    uint64_t delay;
    bm_status_t status = link->ops->next(link->device, &delay);
    if (status == BM_STATUS_IDLE)
        status = bm_engine_disarm_timed_source(link->engine, link->source);
    else if (status == BM_STATUS_OK)
        status = bm_engine_arm_timed_source(link->engine, link->source, delay);
    if (status != BM_STATUS_OK) link->failure = status;
    return status;
}

static bm_status_t fire(bm_engine_t *engine, void *context,
                        const bm_time_point_t *when, uint64_t *next)
{
    bm_at_clock_link_t *link = context;
    bm_status_t status;
    (void)engine; (void)when;
    *next = 0;
    if (link->busy || *link->device_busy) return BM_STATUS_INVALID_STATE;
    link->busy = 1;
    status = advance(link);
    if (status == BM_STATUS_OK) status = link->ops->next(link->device, next);
    if (status != BM_STATUS_OK && status != BM_STATUS_IDLE) link->failure = status;
    link->busy = 0;
    return status; /* The engine rearms its firing source, not this callback. */
}

bm_status_t bm_at_clock_attach(const bm_host_services_t *host, bm_engine_t *engine,
    void *device, const bm_clock_rate_t *rate, const bm_at_clock_device_ops_t *ops,
    int *busy, void **owner, bm_at_clock_link_t **out_link)
{
    bm_at_clock_link_t *link;
    bm_status_t status;
    uint64_t delay;
    if (out_link) *out_link = NULL;
    if (bm_host_services_validate(host) != BM_STATUS_OK || !engine || !device ||
        !rate || !ops || !busy || !owner || !out_link) return BM_STATUS_INVALID_ARGUMENT;
    if (*busy || *owner) return BM_STATUS_INVALID_STATE;
    status = ops->next(device, &delay);
    if (status != BM_STATUS_OK && status != BM_STATUS_IDLE) return status;
    link = host->allocate(host->context, sizeof(*link));
    if (!link) return BM_STATUS_OUT_OF_MEMORY;
    memset(link, 0, sizeof(*link));
    link->host = *host; link->engine = engine; link->device = device;
    link->ops = ops; link->device_busy = busy; link->owner = owner;
    link->failure = BM_STATUS_OK;
    /* Last fallible step: failure publishes neither source nor ownership. */
    status = bm_engine_add_timed_source(engine, fire, link, rate, delay, &link->source);
    if (status != BM_STATUS_OK) { host->release(host->context, link); return status; }
    *owner = link; *out_link = link;
    return BM_STATUS_OK;
}

bm_status_t bm_at_clock_link_sync(bm_at_clock_link_t *link)
{
    bm_status_t status;
    if (!link) return BM_STATUS_INVALID_ARGUMENT;
    if (link->busy || *link->device_busy) return BM_STATUS_INVALID_STATE;
    link->busy = 1;
    status = advance(link);
    if (status == BM_STATUS_OK) status = rearm(link);
    link->busy = 0;
    return status;
}

bm_status_t bm_at_clock_link_changed(bm_at_clock_link_t *link)
{
    uint64_t cursor;
    bm_status_t status;
    if (!link) return BM_STATUS_INVALID_ARGUMENT;
    if (link->busy || *link->device_busy) return BM_STATUS_INVALID_STATE;
    if (link->failure != BM_STATUS_OK) return link->failure;
    status = bm_engine_timed_source_cycle_count(link->engine, link->source, &cursor);
    if (status != BM_STATUS_OK) return (link->failure = status);
    /* Input must follow sync at the SAME boundary. Advancing here would apply
     * the new gate/reset state retroactively to elapsed input pulses. */
    if (cursor != link->serviced) return BM_STATUS_INVALID_STATE;
    return rearm(link);
}

bm_status_t bm_at_clock_link_io(void *context, bm_bus_transaction_t *transaction)
{
    bm_at_clock_link_t *link = context;
    bm_bus_transaction_t staged;
    bm_status_t status, schedule_status;
    if (!link || !transaction) return BM_STATUS_INVALID_ARGUMENT;
    if (transaction->attributes & BM_BUS_TRANSACTION_DEBUG)
        return link->ops->io(link->device, transaction);
    if (link->busy || *link->device_busy) return BM_STATUS_INVALID_STATE;
    link->busy = 1;
    status = advance(link);
    if (status == BM_STATUS_OK) {
        staged = *transaction;
        status = link->ops->io(link->device, &staged);
        /* Even rejected register accesses follow elapsed-time synchronization.
         * A scheduling failure takes precedence, and is latched separately. */
        schedule_status = rearm(link);
        if (schedule_status != BM_STATUS_OK) status = schedule_status;
        if (status == BM_STATUS_OK) *transaction = staged;
    }
    link->busy = 0;
    return status;
}

bm_status_t bm_at_clock_link_reset(bm_at_clock_link_t *link)
{
    bm_time_point_t now;
    bm_status_t status;
    if (!link) return BM_STATUS_INVALID_ARGUMENT;
    if (link->busy || *link->device_busy) return BM_STATUS_INVALID_STATE;
    status = bm_engine_now_exact(link->engine, &now);
    if (status != BM_STATUS_OK) return status;
    if (now.nanoseconds || now.subnanosecond_numerator) return BM_STATUS_INVALID_STATE;
    link->busy = 1;
    link->serviced = 0; link->failure = BM_STATUS_OK;
    status = link->ops->reset(link->device);
    if (status == BM_STATUS_OK) status = rearm(link);
    if (status != BM_STATUS_OK) link->failure = status;
    link->busy = 0;
    return status;
}

bm_status_t bm_at_clock_apply(bm_at_clock_link_t *link,
    const bm_at_clock_device_ops_t *identity,
    bm_status_t (*change)(void *, const void *), const void *input)
{
    bm_status_t status, schedule_status;
    if (!link || !identity || !change || !input) return BM_STATUS_INVALID_ARGUMENT;
    if (link->ops != identity) return BM_STATUS_INVALID_ARGUMENT;
    if (link->busy || *link->device_busy) return BM_STATUS_INVALID_STATE;
    link->busy = 1;
    status = advance(link);
    if (status == BM_STATUS_OK) {
        status = change(link->device, input);
        schedule_status = rearm(link);
        if (schedule_status != BM_STATUS_OK) status = schedule_status;
    }
    link->busy = 0;
    return status;
}

static uint64_t clock_gcd(uint64_t a, uint64_t b)
{
    while (b) { uint64_t remainder = a % b; a = b; b = remainder; }
    return a;
}
int bm_at_clock_rates_equal(const bm_clock_rate_t *a, const bm_clock_rate_t *b)
{
    uint64_t x, y;
    if (!a || !b || !a->cycles_per_second_numerator || !a->cycles_per_second_denominator ||
        !b->cycles_per_second_numerator || !b->cycles_per_second_denominator) return 0;
    x = clock_gcd(a->cycles_per_second_numerator, a->cycles_per_second_denominator);
    y = clock_gcd(b->cycles_per_second_numerator, b->cycles_per_second_denominator);
    return a->cycles_per_second_numerator / x == b->cycles_per_second_numerator / y &&
           a->cycles_per_second_denominator / x == b->cycles_per_second_denominator / y;
}

void bm_at_clock_link_destroy(bm_at_clock_link_t *link)
{
    if (!link || link->busy || *link->device_busy) return;
    /* Owner must already have destroyed the engine and unmapped I/O. */
    *link->owner = NULL;
    link->host.release(link->host.context, link);
}
