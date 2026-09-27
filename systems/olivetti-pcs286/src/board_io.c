/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Authored composition of separately attributed, existing device engines. */
#include "board_io.h"
#include "at_decode_clock.h"
#include <string.h>

typedef struct io_part {
    unsigned port, size, first, target;
    const bm_pcs286_io_resource_t *resource;
} io_part_t;

static unsigned builtin(unsigned port)
{
    if (port >= 0x1ecU && port <= 0x1efU) return BM_PCS286_IO_HEADLAND;
    if (port == 0x68U || port == 0x6aU || port == 0x6cU) return BM_PCS286_IO_IOC02;
    if (port == 0x20U || port == 0x21U || port == 0xa0U || port == 0xa1U)
        return BM_PCS286_IO_PIC;
    if (port < 0x10U || (port >= 0xc0U && port <= 0xdeU && !(port & 1U)) ||
        port == 0x87U || port == 0x83U || port == 0x81U || port == 0x82U ||
        port == 0x8bU || port == 0x89U || port == 0x8aU) return BM_PCS286_IO_DMA;
    return BM_PCS286_IO_HOLE;
}

bm_status_t bm_pcs286_io_initialize(bm_pcs286_io_t *io, const bm_pcs286_io_config_t *c)
{
    bm_pcs286_io_t initial;
    uint32_t ignored;
    unsigned i, j, port;
    bm_status_t status;
    if (!io || !c || c->profile != BM_PCS286_IO_LEGACY_GC103_AT || !c->headland ||
        !c->ioc02 || !c->pic || !c->dma || c->resource_count > BM_PCS286_IO_RESOURCES ||
        (c->holes != BM_PCS286_IO_REJECT && c->holes != BM_PCS286_IO_FF) ||
        (c->timing != BM_PCS286_IO_STRICT && c->timing != BM_PCS286_IO_PROVISIONAL))
        return BM_STATUS_INVALID_ARGUMENT;
    status = bm_pcs286_at_convert_waits(c->service_clock, c->service_clock, 0, &ignored);
    if (status != BM_STATUS_OK) return status;
    for (i = 0; i < c->resource_count; ++i) {
        const bm_pcs286_io_resource_t *r = &c->resources[i];
        if (!r->access || r->first > r->last || (r->width != 1U && r->width != 2U))
            return BM_STATUS_INVALID_ARGUMENT;
        for (port = r->first; port <= r->last; ++port)
            if (builtin(port) != BM_PCS286_IO_HOLE) return BM_STATUS_INVALID_ARGUMENT;
        for (j = 0; j < i; ++j)
            if (r->first <= c->resources[j].last && c->resources[j].first <= r->last)
                return BM_STATUS_INVALID_ARGUMENT;
    }
    memset(&initial, 0, sizeof(initial));
    initial.config = *c;
    *io = initial;
    return BM_STATUS_OK;
}

static bm_status_t plan(const bm_pcs286_io_config_t *c, const bm_bus_transaction_t *t,
                         io_part_t *parts, unsigned *count, uint64_t *cost)
{
    unsigned done = 0, i;
    *count = 0; *cost = 0;
    while (done < t->size) {
        io_part_t *p = &parts[(*count)++];
        unsigned width, last;
        p->port = (unsigned)t->address + done; p->first = done;
        p->target = builtin(p->port); p->resource = NULL;
        width = p->target == BM_PCS286_IO_HEADLAND ? 2U : 1U;
        last = p->target == BM_PCS286_IO_HEADLAND ? 0x1efU : p->port;
        if (p->target == BM_PCS286_IO_HOLE) {
            for (i = 0; i < c->resource_count; ++i) {
                const bm_pcs286_io_resource_t *r = &c->resources[i];
                if (p->port >= r->first && p->port <= r->last) {
                    p->resource = r; width = r->width; last = r->last; break;
                }
            }
            if (!p->resource && c->holes == BM_PCS286_IO_REJECT) return BM_STATUS_UNMAPPED;
        }
        p->size = width == 2U && !(p->port & 1U) && t->size - done >= 2U &&
                  p->port < last ? 2U : 1U;
        *cost += p->resource ? p->resource->extra_clocks : c->extra_clocks[p->target];
        done += p->size;
    }
    return BM_STATUS_OK;
}

static bm_status_t dispatch(bm_pcs286_io_t *io, const io_part_t *p, bm_bus_transaction_t *t)
{
    uint16_t value = (uint16_t)t->value;
    bm_ioc02_legacy_effect_t effect;
    bm_status_t status;
    int debug = (t->attributes & BM_BUS_TRANSACTION_DEBUG) != 0;
    if (p->resource) return p->resource->access(p->resource->context, t);
    switch (p->target) {
    case BM_PCS286_IO_HEADLAND:
        if (t->size == 2U && t->endianness == BM_ENDIAN_BIG)
            value = (uint16_t)((value << 8) | (value >> 8));
        status = bm_gc103_memory_io(io->config.headland, (uint16_t)p->port,
            t->size, t->operation, debug, &value);
        if (status == BM_STATUS_OK && t->operation == BM_BUS_READ) {
            if (t->size == 2U && t->endianness == BM_ENDIAN_BIG)
                value = (uint16_t)((value << 8) | (value >> 8));
            t->value = value;
        }
        return status;
    case BM_PCS286_IO_IOC02:
        status = bm_ioc02_legacy_access(io->config.ioc02, (uint16_t)p->port,
            t->size, t->operation, debug, &value, &effect);
        if (status == BM_STATUS_OK && t->operation == BM_BUS_READ) t->value = value;
        return status; /* Raw effects deliberately do not drive undocumented pins. */
    case BM_PCS286_IO_PIC: return bm_at_pic_io(io->config.pic, t);
    case BM_PCS286_IO_DMA: return bm_at_dma_io(io->config.dma, t);
    default: t->value = 0xffU; return BM_STATUS_OK;
    }
}

bm_status_t bm_pcs286_io_access(void *context, bm_at_transfer_t *transfer)
{
    bm_pcs286_io_t *io = context;
    const bm_bus_transaction_t *t;
    io_part_t parts[8];
    bm_pcs286_io_progress_t progress = {0};
    uint64_t cost, result = 0;
    uint32_t waits = 0;
    unsigned count, i;
    bm_status_t status;
    int debug;
    if (!io || !transfer) return BM_STATUS_INVALID_ARGUMENT;
    if (io->busy) return BM_STATUS_INVALID_STATE;
    t = &transfer->bus;
    if (transfer->master < BM_AT_MASTER_CPU || transfer->master > BM_AT_MASTER_ISA ||
        !transfer->requester_clock.cycles_per_second_numerator ||
        !transfer->requester_clock.cycles_per_second_denominator || !t->size || t->size > 8U ||
        t->address > UINT16_MAX || t->size > 0x10000U - t->address || t->wait_states ||
        t->operation < BM_BUS_READ || t->operation > BM_BUS_FETCH ||
        (t->endianness != BM_ENDIAN_LITTLE && t->endianness != BM_ENDIAN_BIG) ||
        (t->attributes & ~(uint32_t)(BM_BUS_TRANSACTION_DEBUG | BM_BUS_TRANSACTION_LOCKED)))
        return BM_STATUS_INVALID_ARGUMENT;
    if (t->space != BM_ADDRESS_IO) return BM_STATUS_UNMAPPED;
    debug = (t->attributes & BM_BUS_TRANSACTION_DEBUG) != 0;
    if (t->operation == BM_BUS_FETCH || (debug && t->operation == BM_BUS_WRITE))
        return BM_STATUS_UNSUPPORTED;
    status = plan(&io->config, t, parts, &count, &cost);
    if (status != BM_STATUS_OK) goto complete;
    if (!debug) {
        if (io->config.timing == BM_PCS286_IO_STRICT) { status = BM_STATUS_UNSUPPORTED; goto complete; }
        status = bm_pcs286_at_convert_waits(io->config.service_clock, transfer->requester_clock, cost, &waits);
        if (status != BM_STATUS_OK) goto complete;
    }
    io->busy = 1;
    for (i = 0; i < count; ++i) {
        const io_part_t *p = &parts[i];
        bm_bus_transaction_t part = *t;
        unsigned shift = (t->endianness == BM_ENDIAN_LITTLE ? p->first : t->size - p->first - p->size) * 8U;
        uint64_t mask = p->size == 2U ? 0xffffU : 0xffU;
        part.address = p->port; part.size = p->size; part.alignment = p->size;
        part.value = (t->value >> shift) & mask; part.wait_states = 0;
        progress.attempted_bytes += p->size;
        status = dispatch(io, p, &part);
        if (status != BM_STATUS_OK) goto complete;
        progress.completed_bytes += p->size;
        if (t->operation == BM_BUS_READ) result |= (part.value & mask) << shift;
        if (!debug) {
            cost += part.wait_states;
            status = bm_pcs286_at_convert_waits(io->config.service_clock, transfer->requester_clock, cost, &waits);
            if (status != BM_STATUS_OK) goto complete;
        }
    }
    if (t->operation == BM_BUS_READ) transfer->bus.value = result;
    transfer->bus.wait_states = debug ? 0U : waits;
complete:
    io->busy = 0;
    if (!debug) { progress.status = status; io->last = progress; }
    return status;
}
