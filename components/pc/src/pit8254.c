/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2019-2020 Miran Grca
 * Copyright 2022-2026 Daniel Balsom
 * Copyright 2026 Clara
 * Copyright 2026 BluMach contributors
 * Adapted from the portable PIT wrapper, sharing its classic-derived edge core.
 * 8254 register/edge distinctions follow Intel 231164-005, not BIOS behavior. */
#include "pit8254_private.h"
#include <string.h>

static void outputs(const bm_pit8254_t *pit, bool previous[3])
{
    unsigned i;
    for (i = 0; i < 3U; ++i) previous[i] = bm_pit_exact_get_output(&pit->exact, i);
}
static void notify(bm_pit8254_t *pit, const bool previous[3])
{
    unsigned i;
    if (!pit->config.output) return;
    for (i = 0; i < 3U; ++i) {
        bool now = bm_pit_exact_get_output(&pit->exact, i);
        if (now != previous[i]) pit->config.output(pit->config.output_context, i, now ? 1 : 0);
    }
}
static void initialize(bm_pit8254_t *pit)
{
    bm_pit_exact_reset_8254(&pit->exact);
    bm_pit_exact_set_gate(&pit->exact, 0, true);
    bm_pit_exact_set_gate(&pit->exact, 1, true);
    bm_pit_exact_set_gate(&pit->exact, 2, false);
}
bm_status_t bm_pit8254_create(const bm_host_services_t *host,
                              const bm_pit8254_config_t *config, bm_pit8254_t **out_pit)
{
    bm_pit8254_t *pit;
    if (out_pit) *out_pit = NULL;
    if (bm_host_services_validate(host) != BM_STATUS_OK || !config || !out_pit ||
        config->io_base > UINT16_MAX - 3U) return BM_STATUS_INVALID_ARGUMENT;
    pit = host->allocate(host->context, sizeof(*pit));
    if (!pit) return BM_STATUS_OUT_OF_MEMORY;
    memset(pit, 0, sizeof(*pit)); pit->host = *host; pit->config = *config;
    initialize(pit); /* No pins or mappings published during construction. */
    *out_pit = pit;
    return BM_STATUS_OK;
}
void bm_pit8254_destroy(bm_pit8254_t *pit)
{
    if (pit && !pit->busy) pit->host.release(pit->host.context, pit);
}
void bm_pit8254_reset(bm_pit8254_t *pit)
{
    bool previous[3];
    if (!pit || pit->busy) return;
    outputs(pit, previous); pit->busy = 1; initialize(pit); notify(pit, previous); pit->busy = 0;
}
static int valid_count(const bm_pit_exact_channel_t *c, uint8_t value)
{
    unsigned count;
    if (!c->control) return 0; /* Owner must program mode before count. */
    if (c->rw_mode == 3U && !c->write_phase) return 1;
    count = c->rw_mode == 1U ? value : ((unsigned)value << 8U);
    if (c->rw_mode == 3U) count |= c->pending_lsb;
    if ((c->mode == 2U || c->mode == 3U) && count == 1U) return 0;
    return !c->bcd || ((count & 15U) <= 9U && ((count >> 4U) & 15U) <= 9U &&
        ((count >> 8U) & 15U) <= 9U && ((count >> 12U) & 15U) <= 9U);
}
bm_status_t bm_pit8254_io(void *context, bm_bus_transaction_t *t)
{
    bm_pit8254_t *pit = context;
    unsigned port;
    bool previous[3];
    if (!pit || !t) return BM_STATUS_INVALID_ARGUMENT;
    if (t->space != BM_ADDRESS_IO) return BM_STATUS_UNMAPPED;
    if (t->operation < BM_BUS_READ || t->operation > BM_BUS_FETCH ||
        t->address > UINT16_MAX || t->alignment > 1U ||
        (t->attributes & ~(uint32_t)(BM_BUS_TRANSACTION_DEBUG | BM_BUS_TRANSACTION_LOCKED)) ||
        (t->endianness != BM_ENDIAN_LITTLE && t->endianness != BM_ENDIAN_BIG))
        return BM_STATUS_INVALID_ARGUMENT;
    if (t->size != 1U || t->operation == BM_BUS_FETCH) return BM_STATUS_UNSUPPORTED;
    if (t->address < pit->config.io_base || t->address > (unsigned)pit->config.io_base + 3U)
        return BM_STATUS_UNMAPPED;
    port = (unsigned)t->address - pit->config.io_base;
    if (t->attributes & BM_BUS_TRANSACTION_DEBUG) {
        bm_pit_exact_device_t copy;
        if (t->operation == BM_BUS_WRITE) return BM_STATUS_UNSUPPORTED;
        if (port == 3U) return BM_STATUS_UNMAPPED;
        copy = pit->exact; t->value = bm_pit_exact_data_read(&copy, port); return BM_STATUS_OK;
    }
    if (pit->busy) return BM_STATUS_INVALID_STATE;
    if (t->operation == BM_BUS_READ) {
        if (port == 3U) return BM_STATUS_UNMAPPED;
        t->value = bm_pit_exact_data_read(&pit->exact, port);
        return BM_STATUS_OK; /* Reads cannot load CE, change OUT or advance clocks. */
    }
    if (port == 3U) {
        if (((uint8_t)t->value & 0xc1U) == 0xc1U) return BM_STATUS_UNSUPPORTED;
    } else if (!valid_count(&pit->exact.channel[port], (uint8_t)t->value))
        return BM_STATUS_UNSUPPORTED;
    outputs(pit, previous); pit->busy = 1;
    if (port == 3U) bm_pit_exact_control_write(&pit->exact, (uint8_t)t->value);
    else bm_pit_exact_data_write(&pit->exact, port, (uint8_t)t->value);
    notify(pit, previous); pit->busy = 0;
    return BM_STATUS_OK;
}
bm_status_t bm_pit8254_set_gate(bm_pit8254_t *pit, unsigned channel, int level)
{
    bool previous[3];
    if (!pit || channel >= 3U || (level != 0 && level != 1)) return BM_STATUS_INVALID_ARGUMENT;
    if (pit->busy) return BM_STATUS_INVALID_STATE;
    outputs(pit, previous); pit->busy = 1;
    bm_pit_exact_set_gate(&pit->exact, channel, level != 0);
    notify(pit, previous); pit->busy = 0;
    return BM_STATUS_OK;
}
bm_status_t bm_pit8254_output(const bm_pit8254_t *pit, unsigned channel, int *level)
{
    if (!pit || channel >= 3U || !level) return BM_STATUS_INVALID_ARGUMENT;
    *level = bm_pit_exact_get_output(&pit->exact, channel) ? 1 : 0;
    return BM_STATUS_OK;
}
bm_status_t bm_pit8254_advance(bm_pit8254_t *pit, uint64_t cycles)
{
    unsigned i;
    if (!pit) return BM_STATUS_INVALID_ARGUMENT;
    if (pit->busy) return BM_STATUS_INVALID_STATE;
    for (i = 0; i < 3U; ++i)
        if (cycles > UINT64_MAX - pit->exact.channel[i].clocks) return BM_STATUS_CAPACITY_EXCEEDED;
    pit->busy = 1;
    while (cycles) {
        bool previous[3]; uint64_t consumed;
        outputs(pit, previous);
        consumed = bm_pit_exact_advance_until_output_change64(&pit->exact, cycles);
        if (!consumed || consumed > cycles) { pit->busy = 0; return BM_STATUS_DEVICE_ERROR; }
        notify(pit, previous); cycles -= consumed;
    }
    pit->busy = 0;
    return BM_STATUS_OK;
}
bm_status_t bm_pit8254_next_deadline(const bm_pit8254_t *pit, uint64_t *cycles)
{
    if (!pit || !cycles) return BM_STATUS_INVALID_ARGUMENT;
    *cycles = bm_pit_exact_cycles_until_output_change(&pit->exact);
    return *cycles ? BM_STATUS_OK : BM_STATUS_IDLE;
}
