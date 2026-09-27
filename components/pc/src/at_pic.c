/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2015-2020 Andrew Jenner
 * Copyright 2016-2020 Miran Grca
 * Copyright 2026 BluMach contributors
 * Derived rewrite of components/pc/src/pic8259.c at
 * ab5e8cbc527d615464bee0448a948846e8f4d1b0, extended from Intel 231468-003.
 * Two non-buffered 8259As in 8086 mode; architectural boundaries, not pins.
 */
#include <blumach/components/at_pic.h>
#include <string.h>

typedef struct pic_unit {
    uint8_t irr, isr, imr, lines, vector, icw3;
    uint8_t init, single, level, auto_eoi, rotate_auto, lowest, read_isr, special_mask;
    uint8_t ready;
} pic_unit_t;

struct bm_at_pic {
    bm_host_services_t host;
    bm_at_pic_config_t config;
    pic_unit_t unit[2];
    uint8_t external[2];
    uint8_t phase, vector;
    int selected[2];
    int intr;
};

static int priority(const pic_unit_t *unit, uint8_t mask)
{
    unsigned int i;
    for (i = 1; i <= 8; ++i) {
        unsigned int line = (unit->lowest + i) & 7U;
        if (mask & (1U << line)) return (int)line;
    }
    return -1;
}

static int pending(const pic_unit_t *unit)
{
    unsigned int i;
    uint8_t active = unit->isr;
    if (!unit->ready) return -1;
    if (unit->special_mask) active &= (uint8_t)~unit->imr;
    for (i = 1; i <= 8; ++i) {
        unsigned int line = (unit->lowest + i) & 7U;
        uint8_t bit = (uint8_t)(1U << line);
        if (active & bit) return -1;
        if ((unit->irr & (uint8_t)~unit->imr) & bit) return (int)line;
    }
    return -1;
}

static void inputs(pic_unit_t *unit, uint8_t lines)
{
    if (unit->level)
        unit->irr = lines;
    else {
        unit->irr |= lines & (uint8_t)~unit->lines;
        /* Requests withdrawn before INTA are not delivered as real IRQs.
         * Once selected, the pair's separate acknowledge latch is stable. */
        unit->irr &= lines;
    }
    unit->lines = lines;
}

static void update(bm_at_pic_t *pic)
{
    uint8_t master_lines = pic->external[0];
    int output;
    inputs(&pic->unit[1], pic->external[1]);
    if (pending(&pic->unit[1]) >= 0 && !(pic->phase && pic->selected[1] >= 0))
        master_lines |= (uint8_t)(1U << pic->config.cascade_line);
    inputs(&pic->unit[0], master_lines);
    output = !pic->phase && pending(&pic->unit[0]) >= 0;
    if (output != pic->intr) {
        pic->intr = output;
        if (pic->config.intr != NULL)
            pic->config.intr(pic->config.intr_context, output);
    }
}

static void accept(pic_unit_t *unit, int line)
{
    if (line < 0) return;
    unit->irr &= (uint8_t)~(1U << (unsigned int)line);
    unit->isr |= (uint8_t)(1U << (unsigned int)line);
}

bm_status_t bm_at_pic_create(const bm_host_services_t *host,
                             const bm_at_pic_config_t *config,
                             bm_at_pic_t **out_pic)
{
    bm_at_pic_t *pic;
    if (out_pic == NULL) return BM_STATUS_INVALID_ARGUMENT;
    *out_pic = NULL;
    if (bm_host_services_validate(host) != BM_STATUS_OK || config == NULL ||
        config->master_base == UINT16_MAX || config->slave_base == UINT16_MAX ||
        config->cascade_line >= 8U ||
        ((uint32_t)config->master_base <= (uint32_t)config->slave_base + 1U &&
         (uint32_t)config->slave_base <= (uint32_t)config->master_base + 1U))
        return BM_STATUS_INVALID_ARGUMENT;
    pic = host->allocate(host->context, sizeof(*pic));
    if (pic == NULL) return BM_STATUS_OUT_OF_MEMORY;
    memset(pic, 0, sizeof(*pic));
    pic->host = *host;
    pic->config = *config;
    bm_at_pic_reset(pic);
    *out_pic = pic;
    return BM_STATUS_OK;
}

void bm_at_pic_reset(bm_at_pic_t *pic)
{
    unsigned int i;
    int previous;
    if (pic == NULL) return;
    previous = pic->intr;
    memset(pic->unit, 0, sizeof(pic->unit));
    memset(pic->external, 0, sizeof(pic->external));
    for (i = 0; i < 2; ++i) {
        pic->unit[i].imr = 0xffU;
        pic->unit[i].lowest = 7U;
        pic->unit[i].icw3 = 7U;
        pic->selected[i] = -1;
    }
    pic->phase = pic->vector = 0;
    pic->intr = 0;
    if (previous && pic->config.intr != NULL)
        pic->config.intr(pic->config.intr_context, 0);
}

void bm_at_pic_destroy(bm_at_pic_t *pic)
{
    if (pic != NULL) pic->host.release(pic->host.context, pic);
}

bm_status_t bm_at_pic_set_irq(bm_at_pic_t *pic, unsigned int irq, int asserted)
{
    uint8_t bit;
    if (pic == NULL || irq >= 16U || (asserted != 0 && asserted != 1))
        return BM_STATUS_INVALID_ARGUMENT;
    bit = (uint8_t)(1U << (irq & 7U));
    if (asserted) pic->external[irq / 8U] |= bit;
    else pic->external[irq / 8U] &= (uint8_t)~bit;
    update(pic);
    return BM_STATUS_OK;
}

static bm_status_t command(pic_unit_t *unit, int data, uint8_t value)
{
    if (!data && (value & 0x10U)) {
        uint8_t lines = unit->lines;
        /* This component deliberately implements the two-pulse x86 protocol.
         * Omitting ICW4 selects MCS-80/85, which must not silently act as x86. */
        if (!(value & 1U)) return BM_STATUS_UNSUPPORTED;
        memset(unit, 0, sizeof(*unit));
        unit->lines = lines; /* initialization requires a fresh edge */
        unit->lowest = unit->icw3 = 7U;
        unit->single = (value >> 1U) & 1U;
        unit->level = (value >> 3U) & 1U;
        unit->init = 2U;
        return BM_STATUS_OK;
    }
    if (data) {
        switch (unit->init) {
        case 2:
            unit->vector = value & 0xf8U;
            unit->init = unit->single ? 4U : 3U;
            return BM_STATUS_OK;
        case 3:
            unit->icw3 = value;
            unit->init = 4U;
            return BM_STATUS_OK;
        case 4:
            /* Buffered mode and SFNM require separate reviewed semantics. */
            if (!(value & 1U) || (value & 0xf8U)) return BM_STATUS_UNSUPPORTED;
            unit->auto_eoi = (value >> 1U) & 1U;
            unit->init = 0;
            unit->ready = 1U;
            return BM_STATUS_OK;
        default:
            if (!unit->ready) return BM_STATUS_INVALID_STATE;
            unit->imr = value;
            return BM_STATUS_OK;
        }
    }
    if (!unit->ready) return BM_STATUS_INVALID_STATE;
    if (value & 8U) {
        /* Poll freezes priorities from WR to RD and has independent slave
         * acknowledgements. Reject it rather than pretending it is a read. */
        if (value & 4U) return BM_STATUS_UNSUPPORTED;
        if (value & 0x40U) unit->special_mask = (value >> 5U) & 1U;
        if (value & 2U) unit->read_isr = value & 1U;
    } else {
        unsigned int op = value >> 5U;
        int line = -1;
        if (op == 0U) unit->rotate_auto = 0;
        else if (op == 4U) unit->rotate_auto = 1;
        else if (op == 6U) unit->lowest = value & 7U;
        else if (op & 1U) {
            uint8_t active = unit->isr;
            if (unit->special_mask) active &= (uint8_t)~unit->imr;
            line = (op & 2U) ? (int)(value & 7U) : priority(unit, active);
            if (line >= 0) {
                unit->isr &= (uint8_t)~(1U << (unsigned int)line);
                if (op & 4U) unit->lowest = (uint8_t)line;
            }
        }
        /* OCW2 010 is the documented no-operation. */
    }
    return BM_STATUS_OK;
}

bm_status_t bm_at_pic_io(void *context, bm_bus_transaction_t *transaction)
{
    bm_at_pic_t *pic = context;
    unsigned int which;
    uint64_t offset;
    bm_status_t status;
    if (pic == NULL || transaction == NULL || transaction->space != BM_ADDRESS_IO ||
        transaction->operation < BM_BUS_READ || transaction->operation > BM_BUS_FETCH ||
        (transaction->endianness != BM_ENDIAN_LITTLE && transaction->endianness != BM_ENDIAN_BIG) ||
        (transaction->attributes & ~(uint32_t)(BM_BUS_TRANSACTION_DEBUG | BM_BUS_TRANSACTION_LOCKED)))
        return BM_STATUS_INVALID_ARGUMENT;
    if (transaction->size != 1U || transaction->operation == BM_BUS_FETCH)
        return BM_STATUS_UNSUPPORTED;
    if (transaction->address >= pic->config.master_base &&
        transaction->address <= (uint32_t)pic->config.master_base + 1U) {
        which = 0;
        offset = transaction->address - pic->config.master_base;
    } else if (transaction->address >= pic->config.slave_base &&
               transaction->address <= (uint32_t)pic->config.slave_base + 1U) {
        which = 1;
        offset = transaction->address - pic->config.slave_base;
    } else return BM_STATUS_UNMAPPED;
    if (transaction->operation == BM_BUS_READ) {
        const pic_unit_t *unit = &pic->unit[which];
        transaction->value = offset ? unit->imr : (unit->read_isr ? unit->isr : unit->irr);
        return BM_STATUS_OK;
    }
    if (transaction->attributes & BM_BUS_TRANSACTION_DEBUG) return BM_STATUS_UNSUPPORTED;
    if (pic->phase) return BM_STATUS_INVALID_STATE; /* no WR inside frozen INTA */
    status = command(&pic->unit[which], offset != 0, (uint8_t)transaction->value);
    if (status == BM_STATUS_OK) update(pic);
    return status;
}

bm_status_t bm_at_pic_acknowledge(bm_at_pic_t *pic, unsigned int phase, uint8_t *vector)
{
    int master, slave = -1, cascaded;
    unsigned int i;
    if (pic == NULL || vector == NULL || phase > 1U) return BM_STATUS_INVALID_ARGUMENT;
    if (phase != pic->phase || !pic->unit[0].ready) return BM_STATUS_INVALID_STATE;
    if (phase == 0) {
        master = pending(&pic->unit[0]);
        cascaded = !pic->unit[0].single &&
            (pic->unit[0].icw3 & (1U << (master < 0 ? 7U : (unsigned int)master)));
        if (cascaded) {
            unsigned int line = master < 0 ? 7U : (unsigned int)master;
            if (line != pic->config.cascade_line || !pic->unit[1].ready ||
                pic->unit[1].single || (pic->unit[1].icw3 & 7U) != line)
                return BM_STATUS_UNSUPPORTED; /* no responding slave: no made-up vector */
            slave = pending(&pic->unit[1]);
        }
        pic->selected[0] = master;
        pic->selected[1] = cascaded ? slave : -1;
        pic->vector = (uint8_t)(pic->unit[cascaded ? 1 : 0].vector +
            (cascaded ? (slave < 0 ? 7 : slave) : (master < 0 ? 7 : master)));
        accept(&pic->unit[0], master);
        if (cascaded) accept(&pic->unit[1], slave);
        pic->phase = 1;
        update(pic);
        return BM_STATUS_OK; /* phase 0 does not drive vector output */
    }
    *vector = pic->vector;
    for (i = 0; i < 2; ++i) {
        pic_unit_t *unit = &pic->unit[i];
        int line = pic->selected[i];
        if (line >= 0 && unit->auto_eoi) {
            unit->isr &= (uint8_t)~(1U << (unsigned int)line);
            if (unit->rotate_auto) unit->lowest = (uint8_t)line;
        }
    }
    pic->phase = 0;
    update(pic);
    return BM_STATUS_OK;
}

bm_status_t bm_at_pic_state(const bm_at_pic_t *pic, bm_at_pic_state_t *out_state)
{
    unsigned int i;
    if (pic == NULL || out_state == NULL) return BM_STATUS_INVALID_ARGUMENT;
    memset(out_state, 0, sizeof(*out_state));
    for (i = 0; i < 2; ++i) {
        out_state->irr[i] = pic->unit[i].irr;
        out_state->isr[i] = pic->unit[i].isr;
        out_state->imr[i] = pic->unit[i].imr;
        out_state->input_lines[i] = pic->unit[i].lines;
        out_state->vector_base[i] = pic->unit[i].vector;
    }
    out_state->acknowledge_phase = pic->phase;
    out_state->intr = pic->intr;
    return BM_STATUS_OK;
}
