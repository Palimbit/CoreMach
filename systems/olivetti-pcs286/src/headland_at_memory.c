/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Authored AT composition; invokes the separately attributed classic GC103
 * migration and existing backing/clock arithmetic without copying their code.
 */
#include "headland_at_memory.h"
#include "clock_math.h"
#include "at_decode_clock.h"
#include <string.h>

typedef struct fragment {
    bm_gc10x_route_t route;
    uint32_t address, size, first;
    int passive;
} fragment_t;


bm_status_t bm_headland_at_memory_initialize(bm_headland_at_memory_t *memory,
                                            const bm_headland_at_config_t *config)
{
    bm_headland_at_memory_t initial;
    bm_clock_position_t clock;
    bm_status_t status;
    if (memory == NULL || config == NULL ||
        (config->profile != BM_HEADLAND_AT_LEGACY_GC103 &&
         config->profile != BM_HEADLAND_AT_CONFIGURED_GC103) ||
        config->routes == NULL || config->backing == NULL ||
        (config->external_width != 1U && config->external_width != 2U) ||
        (config->holes != BM_HEADLAND_AT_HOLES_REJECT && config->holes != BM_HEADLAND_AT_HOLES_FF) ||
        (config->protected_writes != BM_HEADLAND_AT_PROTECTED_REJECT &&
         config->protected_writes != BM_HEADLAND_AT_PROTECTED_IGNORE) ||
        (config->timing != BM_HEADLAND_AT_STRICT &&
         config->timing != BM_HEADLAND_AT_PROVISIONAL_SERVICE_CLOCK) ||
        (config->cpu_a20 != 0 && config->cpu_a20 != 1))
        return BM_STATUS_INVALID_ARGUMENT;
    if (config->profile == BM_HEADLAND_AT_LEGACY_GC103 ?
        (config->routes->physical_bank_bytes != 0U ||
         config->routes->registers.control_profile != BM_GC103_CONTROL_LEGACY) :
        (config->routes->physical_bank_bytes == 0U ||
         config->routes->registers.control_profile != BM_GC103_CONTROL_STRAP_READBACK))
        return BM_STATUS_INVALID_ARGUMENT;
    status = bm_clock_position_init(&clock, &config->service_clock);
    if (status != BM_STATUS_OK) return status;
    memset(&initial, 0, sizeof(initial));
    initial.config = *config;
    *memory = initial;
    return BM_STATUS_OK;
}

bm_status_t bm_headland_at_memory_a20(bm_headland_at_memory_t *memory, int enabled)
{
    if (memory == NULL || (enabled != 0 && enabled != 1)) return BM_STATUS_INVALID_ARGUMENT;
    if (memory->busy) return BM_STATUS_INVALID_STATE;
    memory->config.cpu_a20 = enabled;
    return BM_STATUS_OK;
}

static bm_status_t plan(const bm_headland_at_memory_t *memory, const bm_at_transfer_t *t,
                        fragment_t *parts, unsigned *count, uint64_t *cost)
{
    uint32_t done = 0;
    const bm_headland_at_config_t *c = &memory->config;
    bm_gc10x_requester_t requester = t->master == BM_AT_MASTER_CPU ? BM_GC10X_CPU :
        (t->master == BM_AT_MASTER_ISA ? BM_GC10X_ISA_MASTER : BM_GC10X_DMA);
    *count = 0U; *cost = 0U;
    while (done < t->bus.size) {
        fragment_t *f = &parts[*count];
        uint32_t width;
        bm_status_t status = bm_gc103_memory_resolve(c->routes, requester, c->cpu_a20,
            (uint32_t)t->bus.address + done, t->bus.operation, &f->route);
        if (status != BM_STATUS_OK) return status;
        if (!(t->bus.attributes & BM_BUS_TRANSACTION_DEBUG) && c->timing == BM_HEADLAND_AT_STRICT)
            return BM_STATUS_UNSUPPORTED; /* All inherited routes have UNKNOWN timing. */
        f->address = (uint32_t)t->bus.address + done;
        if (requester == BM_GC10X_CPU && !c->cpu_a20) f->address &= ~0x100000U;
        f->first = done;
        width = f->route.target == BM_GC10X_EXTERNAL ? c->external_width : 2U;
        f->size = width == 2U && !(f->address & 1U) && t->bus.size - done >= 2U &&
                  f->route.contiguous_bytes >= 2U ? 2U : 1U;
        f->passive = 0;
        if (f->route.target == BM_GC10X_OPEN_BUS ||
            (f->route.target == BM_GC10X_EXTERNAL && c->external == NULL)) {
            if (c->holes != BM_HEADLAND_AT_HOLES_FF) return BM_STATUS_UNMAPPED;
            f->passive = 1;
        } else if (t->bus.operation == BM_BUS_WRITE && !f->route.writable) {
            if (c->protected_writes != BM_HEADLAND_AT_PROTECTED_IGNORE) return BM_STATUS_READ_ONLY;
            f->passive = 1;
        }
        *cost += c->extra_clocks[f->route.target]; /* <=8 * UINT32_MAX. */
        done += f->size;
        ++*count;
    }
    return BM_STATUS_OK;
}

bm_status_t bm_headland_at_memory_access(void *context, bm_at_transfer_t *transfer)
{
    bm_headland_at_memory_t *memory = context;
    const bm_bus_transaction_t *t;
    fragment_t parts[8];
    bm_headland_at_progress_t progress = {0};
    bm_status_t status;
    uint64_t cost, result = 0;
    uint32_t waits = 0;
    unsigned count, i;
    int debug;
    if (memory == NULL || transfer == NULL) return BM_STATUS_INVALID_ARGUMENT;
    if (memory->busy) return BM_STATUS_INVALID_STATE;
    t = &transfer->bus;
    if (transfer->master < BM_AT_MASTER_CPU || transfer->master > BM_AT_MASTER_ISA ||
        transfer->requester_clock.cycles_per_second_numerator == 0U ||
        transfer->requester_clock.cycles_per_second_denominator == 0U ||
        t->size == 0U || t->size > 8U || t->address > 0xffffffU ||
        t->size > 0x1000000U - t->address || t->wait_states != 0U ||
        t->operation < BM_BUS_READ || t->operation > BM_BUS_FETCH ||
        (t->endianness != BM_ENDIAN_LITTLE && t->endianness != BM_ENDIAN_BIG) ||
        (t->attributes & ~(uint32_t)(BM_BUS_TRANSACTION_DEBUG | BM_BUS_TRANSACTION_LOCKED)))
        return BM_STATUS_INVALID_ARGUMENT;
    if (t->space == BM_ADDRESS_IO) return BM_STATUS_UNMAPPED;
    if (t->space != BM_ADDRESS_MEMORY && t->space != BM_ADDRESS_PROGRAM && t->space != BM_ADDRESS_DATA)
        return BM_STATUS_INVALID_ARGUMENT;
    debug = (t->attributes & BM_BUS_TRANSACTION_DEBUG) != 0U;
    if (debug && t->operation == BM_BUS_WRITE) return BM_STATUS_UNSUPPORTED;
    status = plan(memory, transfer, parts, &count, &cost);
    if (status != BM_STATUS_OK) goto complete;
    if (!debug) {
        /* Known configured-cost overflow is rejected before any side effect. */
        status = bm_pcs286_at_convert_waits(memory->config.service_clock, transfer->requester_clock, cost, &waits);
        if (status != BM_STATUS_OK) goto complete;
        progress.timing = BM_GC10X_WAIT_PROVISIONAL;
    }
    memory->busy = 1;
    for (i = 0; i < count; ++i) {
        const fragment_t *f = &parts[i];
        bm_bus_transaction_t part = *t;
        unsigned shift = (t->endianness == BM_ENDIAN_LITTLE ? f->first :
                          t->size - f->first - f->size) * 8U;
        uint64_t mask = f->size == 2U ? 0xffffU : 0xffU;
        part.address = f->address; part.size = f->size; part.alignment = f->size;
        part.value = (t->value >> shift) & mask; part.wait_states = 0U;
        progress.attempted_bytes += f->size;
        if (f->passive) {
            part.value = mask;
            status = BM_STATUS_OK;
        } else if (f->route.target == BM_GC10X_EXTERNAL) {
            status = memory->config.external(memory->config.external_context, &part);
        } else {
            status = memory->config.backing(memory->config.backing_context,
                f->route.target == BM_GC10X_RAM ? BM_PCS286_MEMORY_RAM : BM_PCS286_MEMORY_ROM,
                f->route.offset, &part);
        }
        if (status != BM_STATUS_OK) goto complete;
        progress.completed_bytes += f->size;
        if (t->operation != BM_BUS_WRITE) result |= (part.value & mask) << shift;
        if (!debug) {
            cost += part.wait_states; /* At most 16 * UINT32_MAX in total. */
            status = bm_pcs286_at_convert_waits(memory->config.service_clock, transfer->requester_clock, cost, &waits);
            if (status != BM_STATUS_OK) goto complete;
        }
    }
    if (t->operation != BM_BUS_WRITE) transfer->bus.value = result;
    transfer->bus.wait_states = debug ? 0U : waits;
complete:
    memory->busy = 0;
    if (!debug) {
        progress.status = status;
        memory->last = progress;
    }
    return status;
}

bm_status_t bm_headland_at_memory_backing(void *context, bm_pcs286_memory_region_t region,
                                         uint32_t offset, bm_bus_transaction_t *transaction)
{
    return bm_pcs286_memory_access(context, region, offset, transaction);
}
