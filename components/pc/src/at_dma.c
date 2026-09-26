/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2008-2020 Sarah Walker
 * Copyright 2016-2020 Miran Grca
 * Copyright 2017-2020 Fred N. van Kempen
 * Copyright 2026 BluMach contributors
 * Derived rewrite of the programming path in components/pc/src/dma8237.c
 * at 4769e40524bc194747b142f3e7ec908ae4df0897. Intel 231466-005 register
 * rules and IBM AT wiring. Functional transfers; not pin timing.
 */
#include <blumach/components/at_dma.h>
#include "at_dma_pair.h"
#include <string.h>

bm_status_t bm_at_dma_pair_prepare(bm_at_dma_pair_t *pair, bm_at_access_fn memory,
    void *context, bm_clock_rate_t clock, uint8_t page, uint16_t source,
    uint16_t destination, const uint8_t *eop)
{
    if (!pair) return BM_STATUS_INVALID_ARGUMENT;
    memset(pair, 0, sizeof(*pair));
    if (!memory || !clock.cycles_per_second_numerator ||
        !clock.cycles_per_second_denominator) return BM_STATUS_INVALID_ARGUMENT;
    pair->memory = memory; pair->context = context; pair->clock = clock;
    pair->source = ((uint32_t)page << 16) | source;
    pair->destination = ((uint32_t)page << 16) | destination;
    pair->eop = eop;
    pair->phase = BM_AT_DMA_PAIR_READ;
    return BM_STATUS_OK;
}

bm_status_t bm_at_dma_pair_step(bm_at_dma_pair_t *pair, uint64_t *clocks)
{
    if (clocks) *clocks = 0;
    if (!pair || !clocks) return BM_STATUS_INVALID_ARGUMENT;
    if (pair->phase != BM_AT_DMA_PAIR_READ && pair->phase != BM_AT_DMA_PAIR_WRITE)
        return BM_STATUS_INVALID_STATE;
    int reading = pair->phase == BM_AT_DMA_PAIR_READ;
    bm_at_transfer_t transfer;
    memset(&transfer, 0, sizeof(transfer));
    transfer.master = BM_AT_MASTER_DMA8;
    transfer.requester_clock = pair->clock;
    transfer.bus.space = BM_ADDRESS_MEMORY;
    transfer.bus.operation = reading ? BM_BUS_READ : BM_BUS_WRITE;
    transfer.bus.address = reading ? pair->source : pair->destination;
    transfer.bus.size = transfer.bus.alignment = 1;
    transfer.bus.endianness = BM_ENDIAN_LITTLE;
    transfer.bus.value = reading ? 0 : pair->temporary;
    pair->phase = BM_AT_DMA_PAIR_BUSY; /* consume this phase before callbacks */
    bm_status_t status = pair->memory(pair->context, &transfer);
    if (status != BM_STATUS_OK) {
        pair->phase = BM_AT_DMA_PAIR_STOPPED;
        return status == BM_STATUS_IDLE ? BM_STATUS_INVALID_STATE : status;
    }
    *clocks = 4U + (uint64_t)transfer.bus.wait_states;
    pair->completed_clocks += *clocks;
    uint8_t eop = pair->eop && *pair->eop ? 1U : 0U;
    if (reading) {
        pair->temporary = (uint8_t)transfer.bus.value;
        pair->read_complete = 1; pair->source_eop = eop;
        pair->phase = BM_AT_DMA_PAIR_WRITE;
    } else {
        pair->write_complete = 1; pair->destination_eop = eop;
        pair->phase = BM_AT_DMA_PAIR_COMPLETE;
    }
    return BM_STATUS_OK;
}

typedef struct dma_unit {
    uint16_t address[4], count[4], base_address[4], base_count[4];
    uint8_t mode[4], command, software, mask, tc, temporary, high, priority;
} dma_unit_t;

struct bm_at_dma {
    bm_host_services_t host;
    bm_at_dma_config_t config;
    dma_unit_t unit[2];
    uint8_t page[8], dreq, eop[2];
    int request, grant, stopped, selected, started, release_wait, busy;
    int mem2mem_active;
    bm_at_dma_pair_t pair;
    uint8_t address_high;
};

/* Intel explicitly calls software requests non-maskable (and requires Block
 * mode to service them). They are retained independently of external DREQ.
 * Software requests outside Block mode are rejected before service. */
static uint8_t eligible(const dma_unit_t *unit, uint8_t lines)
{
    if (unit->command & 4U) return 0;
    return (uint8_t)(unit->software | (lines & (uint8_t)~unit->mask));
}

static int cascade_order_valid(const dma_unit_t *unit)
{
    /* Intel p19 design consideration1: cascade starting at channel0. Reject
     * the documented unsafe configuration instead of emulating undefined
     * channel0 behavior or silently fixing guest programming. */
    if ((unit->mode[0] & 0xc0U) == 0xc0U) return 1;
    for (unsigned ch = 1; ch < 4; ++ch)
        if ((unit->mode[ch] & 0xc0U) == 0xc0U) return 0;
    return 1;
}

static uint8_t requests(const bm_at_dma_t *dma, unsigned which)
{
    uint8_t lines = which ? (uint8_t)(dma->dreq >> 4) : (uint8_t)(dma->dreq & 15U);
    if (which && ((dma->selected >= 0 && dma->selected < 4 && (dma->started || dma->mem2mem_active)) ||
                  eligible(&dma->unit[0], dma->dreq & 15U))) lines |= 1U;
    return (uint8_t)(lines | dma->unit[which].software);
}

static int pick(const dma_unit_t *unit, uint8_t bits)
{
    unsigned first = unit->command & 0x10U ? unit->priority : 0U;
    for (unsigned i = 0; i < 4; ++i) {
        unsigned ch = (first + i) & 3U;
        if (bits & (1U << ch)) return (int)ch;
    }
    return -1;
}

static int candidate(const bm_at_dma_t *dma)
{
    uint8_t low = eligible(&dma->unit[0], dma->dreq & 15U);
    uint8_t upper = (uint8_t)(dma->dreq >> 4);
    if (low) upper |= 1U;
    int ch = pick(&dma->unit[1], eligible(&dma->unit[1], upper));
    if (ch == 0 && low) return pick(&dma->unit[0], low);
    return ch < 0 ? -1 : ch + 4;
}

static void update(bm_at_dma_t *dma)
{
    int request = !dma->stopped && !dma->release_wait &&
        (dma->selected >= 0 || candidate(dma) >= 0);
    if (request != dma->request) {
        dma->request = request;
        if (dma->config.bus_request) dma->config.bus_request(dma->config.bus_context, request);
    }
}

static void finish_service(bm_at_dma_t *dma)
{
    int ch = dma->selected;
    if (ch >= 0 && (dma->started || dma->mem2mem_active)) {
        dma_unit_t *unit = &dma->unit[(unsigned)ch >> 2];
        if (unit->command & 0x10U) unit->priority = dma->mem2mem_active ? 2U : (uint8_t)((ch + 1) & 3);
        if (ch < 4 && (dma->unit[1].command & 0x10U)) dma->unit[1].priority = 1;
        bm_at_dma_endpoint_t *ep = &dma->config.endpoints[ch];
        int had_dack = dma->started;
        dma->started = dma->mem2mem_active = 0;
        if (had_dack && ep->dack) ep->dack(ep->context, 0);
    }
    dma->selected = -1;
    dma->release_wait = dma->grant;
    update(dma);
}

static bm_status_t stop(bm_at_dma_t *dma, bm_status_t status)
{
    dma->stopped = 1;
    finish_service(dma);
    dma->busy = 0;
    /* An endpoint IDLE after a promised grant is a host contract failure,
     * never an invitation to replay a possibly consumed device value. */
    return status == BM_STATUS_IDLE ? BM_STATUS_INVALID_STATE : status;
}

static void master_clear(dma_unit_t *unit)
{
    unit->command = unit->software = unit->tc = unit->temporary = unit->high = 0;
    unit->mask = 15U;
    unit->priority = 0;
    /* Address/count/mode are not in Intel's reset list. Page latches and
     * external input levels are not 8237 registers either. */
}

bm_status_t bm_at_dma_create(const bm_host_services_t *host,
                            const bm_at_dma_config_t *config, bm_at_dma_t **out_dma)
{
    bm_at_dma_t *dma;
    if (!out_dma) return BM_STATUS_INVALID_ARGUMENT;
    *out_dma = NULL;
    if (!host || !host->allocate || !host->release || !config || !config->memory ||
        !config->clock.cycles_per_second_numerator || !config->clock.cycles_per_second_denominator ||
        (config->mem2mem_profile != BM_AT_DMA_MEM2MEM_DISABLED &&
         config->mem2mem_profile != BM_AT_DMA_MEM2MEM_IBM_MATCHED_COUNTS))
        return BM_STATUS_INVALID_ARGUMENT;
    dma = host->allocate(host->context, sizeof(*dma));
    if (!dma) return BM_STATUS_OUT_OF_MEMORY;
    memset(dma, 0, sizeof(*dma));
    dma->host = *host; dma->config = *config;
    dma->selected = -1;
    master_clear(&dma->unit[0]); master_clear(&dma->unit[1]);
    *out_dma = dma;
    return BM_STATUS_OK;
}

void bm_at_dma_reset(bm_at_dma_t *dma)
{
    if (!dma) return;
    dma->stopped = 1;
    finish_service(dma);
    master_clear(&dma->unit[0]); master_clear(&dma->unit[1]);
    memset(&dma->pair, 0, sizeof(dma->pair));
    dma->stopped = 0;
    update(dma);
}

void bm_at_dma_destroy(bm_at_dma_t *dma)
{
    if (!dma) return;
    dma->stopped = 1;
    finish_service(dma);
    dma->host.release(dma->host.context, dma);
}

uint16_t bm_at_dma_ibm_page_port(uint8_t dack_asserted, int refresh_asserted)
{
    /* IBM Type1 sheet15: MA3=-DACK4; MA2=NAND(-DACK0,-REFRESH);
     * MA1=AND(-DACK2,-DACK6); MA0=AND(-DACK3,-DACK7).
     * Convert semantic assertions to the physical decoder result. */
    return (uint16_t)(0x80U | (!(dack_asserted & 0x10U) ? 8U : 0U) |
        ((dack_asserted & 1U) || refresh_asserted ? 4U : 0U) |
        (!(dack_asserted & 0x44U) ? 2U : 0U) |
        (!(dack_asserted & 0x88U) ? 1U : 0U));
}

static int page_channel(uint64_t port)
{
    switch (port) {
    case 0x87: return 0;
    case 0x83: return 1;
    case 0x81: return 2;
    case 0x82: return 3;
    case 0x8b: return 5;
    case 0x89: return 6;
    case 0x8a: return 7;
    default: return -1; /* 8F refresh and spare latches belong to board glue. */
    }
}

static uint16_t with_byte(uint16_t old, uint8_t value, uint8_t high)
{
    return high ? (uint16_t)((old & 0xffU) | ((uint16_t)value << 8)) :
        (uint16_t)((old & 0xff00U) | value);
}

bm_status_t bm_at_dma_io(void *context, bm_bus_transaction_t *t)
{
    bm_at_dma_t *dma = context;
    unsigned which, offset, channel;
    int page, debug;
    uint8_t value, bit;
    dma_unit_t *unit;
    if (!dma || !t) return BM_STATUS_INVALID_ARGUMENT;
    if (t->space != BM_ADDRESS_IO) return BM_STATUS_UNMAPPED;
    if (t->address > UINT16_MAX || t->alignment > 1U ||
        (t->attributes & ~(BM_BUS_TRANSACTION_DEBUG | BM_BUS_TRANSACTION_LOCKED)) ||
        (t->endianness != BM_ENDIAN_LITTLE && t->endianness != BM_ENDIAN_BIG))
        return BM_STATUS_INVALID_ARGUMENT;
    if (t->size != 1U || (t->operation != BM_BUS_READ && t->operation != BM_BUS_WRITE))
        return BM_STATUS_UNSUPPORTED;
    page = page_channel(t->address);
    if (t->address < 16U) { which = 0; offset = (unsigned)t->address; }
    else if (t->address >= 0xc0U && t->address <= 0xdeU && !(t->address & 1U)) {
        which = 1; offset = (unsigned)(t->address - 0xc0U) >> 1;
    } else if (page >= 0) { which = offset = 0; }
    else return BM_STATUS_UNMAPPED;
    debug = (t->attributes & BM_BUS_TRANSACTION_DEBUG) != 0;
    if (debug && t->operation == BM_BUS_WRITE) return BM_STATUS_UNSUPPORTED;
    if (!debug && (dma->grant || dma->busy)) return BM_STATUS_INVALID_STATE;
    if (page >= 0) {
        if (t->operation == BM_BUS_READ) t->value = dma->page[page];
        else dma->page[page] = (uint8_t)t->value;
        t->wait_states = 0;
        return BM_STATUS_OK;
    }
    unit = &dma->unit[which];
    if (t->operation == BM_BUS_READ) {
        if (offset < 8U) {
            uint16_t word = offset & 1U ? unit->count[offset >> 1] : unit->address[offset >> 1];
            value = (uint8_t)(word >> (unit->high ? 8 : 0));
            if (!debug) unit->high ^= 1U;
        } else if (offset == 8U) {
            value = (uint8_t)((requests(dma, which) << 4) | unit->tc);
            if (!debug) unit->tc = 0;
        } else if (offset == 13U) value = unit->temporary;
        else return BM_STATUS_UNMAPPED; /* Intel write-only register codes. */
        t->value = value; t->wait_states = 0;
        return BM_STATUS_OK;
    }
    value = (uint8_t)t->value;
    channel = value & 3U; bit = (uint8_t)(1U << channel);
    if (offset < 8U) {
        channel = offset >> 1;
        if (offset & 1U) {
            unit->count[channel] = with_byte(unit->count[channel], value, unit->high);
            unit->base_count[channel] = with_byte(unit->base_count[channel], value, unit->high);
        } else {
            unit->address[channel] = with_byte(unit->address[channel], value, unit->high);
            unit->base_address[channel] = with_byte(unit->base_address[channel], value, unit->high);
        }
        unit->high ^= 1U;
    } else switch (offset) {
    case 8: unit->command = value; break;
    case 9:
        if (value & 4U) unit->software |= bit;
        else unit->software &= (uint8_t)~bit;
        break;
    case 10:
        if (value & 4U) unit->mask |= bit;
        else unit->mask &= (uint8_t)~bit;
        break;
    case 11: unit->mode[channel] = value & 0xfcU; break;
    case 12: unit->high = 0; break;
    case 13: master_clear(unit); break;
    case 14: unit->mask = 0; break;
    case 15: unit->mask = value & 15U; break;
    }
    t->wait_states = 0;
    update(dma);
    return BM_STATUS_OK;
}

bm_status_t bm_at_dma_set_dreq(bm_at_dma_t *dma, unsigned channel, int level)
{
    if (!dma || channel >= 8U || channel == 4U || (level != 0 && level != 1))
        return BM_STATUS_INVALID_ARGUMENT;
    uint8_t bit = (uint8_t)(1U << channel);
    if (level) dma->dreq |= bit;
    else dma->dreq &= (uint8_t)~bit;
    if (dma->busy) return BM_STATUS_OK;
    uint8_t kind = dma->unit[channel >> 2].mode[channel & 3U] & 0xc0U;
    if (dma->started && dma->selected == (int)channel && !level &&
        (kind == 0 || kind == 0xc0U)) {
        finish_service(dma);
        return BM_STATUS_OK;
    }
    update(dma);
    return BM_STATUS_OK;
}

bm_status_t bm_at_dma_set_bus_grant(bm_at_dma_t *dma, int level)
{
    if (!dma || (level != 0 && level != 1)) return BM_STATUS_INVALID_ARGUMENT;
    if (dma->busy) return BM_STATUS_INVALID_STATE;
    if (dma->grant == level) return BM_STATUS_OK;
    if (!level && (dma->started || dma->mem2mem_active)) return BM_STATUS_INVALID_STATE;
    dma->grant = level;
    if (level) {
        if (dma->request) dma->selected = candidate(dma);
    } else {
        dma->selected = -1;
        dma->release_wait = 0;
        update(dma);
    }
    return BM_STATUS_OK;
}

bm_status_t bm_at_dma_set_eop(bm_at_dma_t *dma, unsigned controller, int asserted)
{
    if (!dma || controller >= 2U || (asserted != 0 && asserted != 1))
        return BM_STATUS_INVALID_ARGUMENT;
    /* External input, safe from endpoint callbacks. Successful unit completion
     * is our functional sampling boundary; never terminate a partial failure. */
    dma->eop[controller] = (uint8_t)asserted;
    return BM_STATUS_OK;
}

bm_status_t bm_at_dma_channel_timing(const bm_at_dma_t *dma, unsigned channel,
                                    bm_at_dma_timing_t *out)
{
    if (out) memset(out, 0, sizeof(*out));
    if (!dma || !out || channel >= 8U) return BM_STATUS_INVALID_ARGUMENT;
    const dma_unit_t *unit = &dma->unit[channel >> 2];
    uint8_t mode = unit->mode[channel & 3U], type = mode & 12U;
    if (channel == 4U || (mode >> 6) == 3U || type == 12U || (unit->command & 1U))
        return BM_STATUS_UNSUPPORTED;
    /* Intel231466-005 p8, p14 note3, figures11/14. Nominal intervals only:
     * actual minimum pulse widths subtract the specified AC delay margins.
     * Extended write advances assertion by one clock, not total completion. */
    int compressed = (unit->command & 8U) != 0;
    out->transfer_clocks = compressed ? 2U : 3U;
    if (type) {
        out->read_pulse_clocks = compressed ? 1U : 2U;
        out->write_pulse_clocks = !compressed && (unit->command & 32U) ? 2U : 1U;
    }
    return BM_STATUS_OK;
}

static bm_status_t mem2mem_service(bm_at_dma_t *dma, uint64_t *cycles)
{
    dma_unit_t *unit = &dma->unit[0];
    if (dma->config.mem2mem_profile != BM_AT_DMA_MEM2MEM_IBM_MATCHED_COUNTS ||
        dma->selected != 0 || !(unit->software & 1U) || (unit->software & 2U) ||
        (unit->command & 32U) || (unit->mode[0] & 0xccU) != 0x88U ||
        (unit->mode[1] & 0xccU) != 0x84U ||
        ((unit->mode[0] ^ unit->mode[1]) & 0x10U) || unit->count[0] != unit->count[1] ||
        ((unit->mode[0] & 16U) && unit->base_count[0] != unit->base_count[1]))
        return stop(dma, BM_STATUS_UNSUPPORTED);
    /* Gate handled by caller: valid lower-to-upper cascade and safe ordering.
     * Internal DACK4 alone selects port83h for BOTH byte phases. */
    int page = page_channel(bm_at_dma_ibm_page_port(0x10U, 0));
    bm_status_t status = bm_at_dma_pair_prepare(&dma->pair, dma->config.memory,
        dma->config.memory_context, dma->config.clock, dma->page[page],
        unit->address[0], unit->address[1], &dma->eop[0]);
    if (status != BM_STATUS_OK) return stop(dma, status);
    dma->busy = dma->mem2mem_active = 1;
    uint64_t phase_clocks;
    status = bm_at_dma_pair_step(&dma->pair, &phase_clocks);
    if (status != BM_STATUS_OK) return stop(dma, status);
    unit->temporary = dma->pair.temporary; /* retain successful READ on WRITE failure */
    status = bm_at_dma_pair_step(&dma->pair, &phase_clocks);
    if (status != BM_STATUS_OK) return stop(dma, status);
    if (dma->pair.source_eop != dma->pair.destination_eop)
        return stop(dma, BM_STATUS_UNSUPPORTED); /* evidence stop, not guest EOP semantics */
    int terminal = unit->count[1] == 0;
    int end_process = terminal || dma->pair.destination_eop;
    for (unsigned ch = 0; ch < 2; ++ch) {
        if (ch || !(unit->command & 2U))
            unit->address[ch] = (uint16_t)(unit->address[ch] + (unit->mode[ch] & 32U ? -1 : 1));
        --unit->count[ch];
    }
    *cycles = dma->pair.completed_clocks;
    if (end_process) {
        /* Intel pp7/9 per-channel count/status rules, restricted to matched
         * underflow or EOP observed on BOTH phases (p6). This explicit profile
         * is a functional interpretation, not proof of source-TC silicon. */
        unit->tc |= 3U; unit->software &= 0xfcU;
        if (unit->mode[0] & 16U) {
            for (unsigned ch = 0; ch < 2; ++ch) {
                unit->address[ch] = unit->base_address[ch];
                unit->count[ch] = unit->base_count[ch];
            }
        } else unit->mask |= 3U;
        /* Intel p3/p6: only destination TC drives the controller EOP output. */
        bm_at_dma_endpoint_t *ep = &dma->config.endpoints[1];
        if (terminal && ep->terminal_count) {
            ep->terminal_count(ep->context, 1); ep->terminal_count(ep->context, 0);
        }
        finish_service(dma);
    }
    dma->busy = 0;
    return BM_STATUS_OK;
}

bm_status_t bm_at_dma_service(bm_at_dma_t *dma, uint64_t *cycles)
{
    if (!dma || !cycles) return BM_STATUS_INVALID_ARGUMENT;
    *cycles = 0;
    if (dma->stopped || dma->busy) return BM_STATUS_INVALID_STATE;
    if (!dma->request || !dma->grant || dma->selected < 0 || dma->release_wait)
        return BM_STATUS_IDLE;
    unsigned ch = (unsigned)dma->selected, local = ch & 3U;
    dma_unit_t *unit = &dma->unit[ch >> 2];
    uint8_t mode = unit->mode[local], kind = mode >> 6, type = mode & 12U;
    uint8_t bit = (uint8_t)(1U << local);
    bm_at_dma_endpoint_t *ep = &dma->config.endpoints[ch];
    bm_at_dma_timing_t timing;
    /* Reject unsupported controls before DACK or any irreversible endpoint.
     * Polarity bits are semantic; address-hold has no effect outside mem2mem. */
    /* Intel p8: extended-write selection is don't-care under compressed
     * timing. A cascade-only controller emits no transfer strobes (pp5-6),
     * so its timing bits cannot change the active lower controller's clocks. */
    if (ch == 4 || !cascade_order_valid(unit) ||
        ((unit->software & bit) && kind != 2) ||
        (ch < 4 && ((dma->unit[1].mode[0] & 0xc0U) != 0xc0U ||
                    (dma->unit[1].software & 1U) ||
                    (dma->unit[1].command & 1U))))
        return stop(dma, BM_STATUS_UNSUPPORTED);
    if (unit->command & 1U) return mem2mem_service(dma, cycles);
    if (kind == 3) {
        /* Cascade delegates ownership only. DACK is the external master's
         * grant notification; never call its data endpoints or consume this
         * channel's address/count/TC/EOP. Type bits are don't-care (Intel p8).
         * IDLE/zero means no local transfer completed, even when DACK rises.
         * The coordinator advances the external master, never this callback. */
        if (!ep->dack) return stop(dma, BM_STATUS_INVALID_STATE);
        if (!(dma->dreq & (1U << ch))) {
            finish_service(dma);
            return BM_STATUS_IDLE;
        }
        dma->busy = 1;
        if (!dma->started) {
            dma->started = 1;
            ep->dack(ep->context, 1);
        }
        /* A callback may withdraw DREQ. Input edges are sampled after the
         * notification; do not recursively execute or leave a phantom grant. */
        if (!(dma->dreq & (1U << ch))) finish_service(dma);
        dma->busy = 0;
        return BM_STATUS_IDLE;
    }
    if (bm_at_dma_channel_timing(dma, ch, &timing) != BM_STATUS_OK)
        return stop(dma, BM_STATUS_UNSUPPORTED);
    if ((type == 4 && !ep->read) || (type == 8 && !ep->write))
        return stop(dma, BM_STATUS_INVALID_STATE);
    if (!dma->started && !(eligible(unit, (uint8_t)(dma->dreq >> ((ch >> 2) * 4U))) & bit)) {
        finish_service(dma);
        return BM_STATUS_IDLE;
    }
    uint16_t address = unit->address[local], value = 0;
    /* Intel p7/figure14: compressed transfers omit S3, retaining S1 on
     * first/address-high change. Adapter waits remain EXTRA DMA clocks. */
    uint64_t clocks = timing.transfer_clocks +
        (!dma->started || dma->address_high != (address >> 8));
    bm_at_transfer_t transfer;
    memset(&transfer, 0, sizeof(transfer));
    transfer.master = ch < 4 ? BM_AT_MASTER_DMA8 : BM_AT_MASTER_DMA16;
    transfer.requester_clock = dma->config.clock;
    transfer.bus.space = BM_ADDRESS_MEMORY;
    transfer.bus.operation = type == 4 ? BM_BUS_WRITE : BM_BUS_READ;
    /* A lower local transfer also asserts the upper controller's internal
     * cascade DACK4. The seven supported normal routes always select one of
     * the implemented page latches. Refresh and mem2mem are not serviced here. */
    uint8_t dacks = (uint8_t)((1U << ch) | (ch < 4 ? 0x10U : 0U));
    int page_source = page_channel(bm_at_dma_ibm_page_port(dacks, 0));
    transfer.bus.address = ch < 4 ? ((uint32_t)dma->page[page_source] << 16) | address :
        ((uint32_t)(dma->page[page_source] & 0xfeU) << 16) | ((uint32_t)address << 1);
    transfer.bus.size = transfer.bus.alignment = ch < 4 ? 1U : 2U;
    transfer.bus.endianness = BM_ENDIAN_LITTLE;
    dma->busy = 1;
    if (!dma->started) {
        dma->started = 1;
        if (ep->dack) ep->dack(ep->context, 1);
    }
    bm_status_t status;
    if (type == 4) {
        status = ep->read(ep->context, &value);
        if (status != BM_STATUS_OK) return stop(dma, status);
        transfer.bus.value = ch < 4 ? (uint8_t)value : value;
    }
    if (type) {
        status = dma->config.memory(dma->config.memory_context, &transfer);
        if (status != BM_STATUS_OK) return stop(dma, status);
        /* wait_states is uint32_t; widen before adding the at-most-four states. */
        clocks += transfer.bus.wait_states;
        if (type == 8) {
            value = ch < 4 ? (uint8_t)transfer.bus.value : (uint16_t)transfer.bus.value;
            status = ep->write(ep->context, value);
            if (status != BM_STATUS_OK) return stop(dma, status);
        }
    }
    /* Architectural transfer commits only after every required endpoint OK.
     * External partial effects are retained on a host stop, never rolled back. */
    dma->address_high = (uint8_t)(address >> 8);
    unit->address[local] = (uint16_t)(address + (mode & 0x20U ? -1 : 1));
    int terminal = unit->count[local] == 0;
    int end_process = terminal || dma->eop[ch >> 2];
    --unit->count[local];
    *cycles = clocks;
    if (end_process) {
        unit->tc |= bit;
        unit->software &= (uint8_t)~bit;
        if (mode & 0x10U) {
            unit->address[local] = unit->base_address[local];
            unit->count[local] = unit->base_count[local];
        } else unit->mask |= bit;
        /* EOP input and internal terminal output share a pin in hardware,
         * but an external termination must not invent a generated TC pulse. */
        if (terminal && ep->terminal_count) {
            ep->terminal_count(ep->context, 1);
            ep->terminal_count(ep->context, 0);
        }
    }
    if (end_process || kind == 1 || (kind == 0 && !(dma->dreq & (1U << ch))))
        finish_service(dma);
    dma->busy = 0;
    return BM_STATUS_OK;
}

bm_status_t bm_at_dma_channel_state(const bm_at_dma_t *dma, unsigned channel,
                                    bm_at_dma_channel_state_t *out)
{
    if (!dma || !out || channel >= 8U) return BM_STATUS_INVALID_ARGUMENT;
    const dma_unit_t *unit = &dma->unit[channel >> 2];
    unsigned local = channel & 3U;
    memset(out, 0, sizeof(*out));
    out->base_address = unit->base_address[local]; out->current_address = unit->address[local];
    out->base_count = unit->base_count[local]; out->current_count = unit->count[local];
    out->page = dma->page[channel]; out->mode = unit->mode[local];
    out->masked = (unit->mask >> local) & 1U;
    out->requested = (requests(dma, channel >> 2) >> local) & 1U;
    out->terminal_count = (unit->tc >> local) & 1U;
    return BM_STATUS_OK;
}

bm_status_t bm_at_dma_state(const bm_at_dma_t *dma, bm_at_dma_state_t *out)
{
    if (!dma || !out) return BM_STATUS_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    for (unsigned i = 0; i < 2; ++i) {
        out->command[i] = dma->unit[i].command; out->mask[i] = dma->unit[i].mask;
        out->software_request[i] = dma->unit[i].software;
        out->byte_high[i] = dma->unit[i].high;
        out->priority_first[i] = dma->unit[i].priority;
        out->eop[i] = dma->eop[i];
    }
    out->dreq = dma->dreq; out->bus_request = dma->request;
    out->bus_grant = dma->grant; out->stopped = dma->stopped;
    out->selected_channel = dma->selected;
    out->pending_channel = dma->request ? (dma->selected >= 0 ? dma->selected : candidate(dma)) : -1;
    out->dack = dma->started;
    out->release_wait = dma->release_wait;
    out->cascade_active = dma->started && dma->selected >= 0 &&
        (dma->unit[(unsigned)dma->selected >> 2].mode[(unsigned)dma->selected & 3U] & 0xc0U) == 0xc0U;
    out->mem2mem_active = dma->mem2mem_active;
    out->last_pair_read_complete = dma->pair.read_complete;
    out->last_pair_write_complete = dma->pair.write_complete;
    out->last_pair_completed_clocks = dma->pair.completed_clocks;
    return BM_STATUS_OK;
}
