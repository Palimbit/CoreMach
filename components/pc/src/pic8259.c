/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2015-2020 Andrew Jenner
 * Copyright 2016-2020 Miran Grca
 * Copyright 2026 BluMach contributors
 *
 * Derived rewrite of the inherited 8259 implementation. This deliberately
 * models the single-PIC XT contract needed by the PCS 86 bring-up.
 */
#include <blumach/components/pic8259.h>

#include <string.h>

enum {
    PIC_READY = 0,
    PIC_EXPECT_ICW2,
    PIC_EXPECT_ICW3,
    PIC_EXPECT_ICW4
};

struct bm_pic8259 {
    bm_host_services_t host;
    uint16_t io_base;
    uint8_t vector_base;
    uint8_t imr;
    uint8_t irr;
    uint8_t isr;
    uint8_t lines;
    uint8_t init_state;
    uint8_t need_icw4;
    uint8_t single;
    uint8_t read_isr;
    uint8_t auto_eoi;
    int output_asserted;
    bm_pic8259_output_fn output;
    void *output_context;
};

static int
highest_priority(uint8_t value)
{
    unsigned int line;
    for (line = 0; line < 8; ++line) {
        if ((value & (uint8_t) (1U << line)) != 0)
            return (int) line;
    }
    return -1;
}

static uint8_t
eligible_requests(const bm_pic8259_t *pic)
{
    uint8_t pending = pic->irr & (uint8_t) ~pic->imr;
    const int active = highest_priority(pic->isr);

    /* Fixed-priority fully nested mode only permits a request that outranks
     * the highest-priority interrupt already in service. */
    if (active == 0)
        return 0U;
    if (active > 0)
        pending &= (uint8_t) ((1U << active) - 1U);
    return pending;
}

static void
update_output(bm_pic8259_t *pic)
{
    int asserted = eligible_requests(pic) != 0U;
    if (asserted != pic->output_asserted) {
        pic->output_asserted = asserted;
        if (pic->output != NULL)
            pic->output(pic->output_context, asserted);
    }
}

static bm_status_t
pic_access(void *context, bm_bus_transaction_t *transaction)
{
    bm_pic8259_t *pic = context;
    unsigned int data_port;

    if ((transaction->size != 1) || (transaction->operation == BM_BUS_FETCH))
        return BM_STATUS_UNSUPPORTED;
    data_port = (unsigned int) (transaction->address - pic->io_base);
    if (transaction->operation == BM_BUS_READ) {
        transaction->value = data_port ? pic->imr : (pic->read_isr ? pic->isr : pic->irr);
        update_output(pic);
        return BM_STATUS_OK;
    }

    if (!data_port) {
        uint8_t value = (uint8_t) transaction->value;
        if ((value & 0x10U) != 0) {
            pic->imr = 0;
            pic->irr = 0;
            pic->isr = 0;
            pic->need_icw4 = value & 1U;
            pic->single = (value >> 1U) & 1U;
            pic->init_state = PIC_EXPECT_ICW2;
        } else if ((value & 0x18U) == 0x08U) {
            pic->read_isr = ((value & 3U) == 3U);
        } else if ((value & 0xe0U) == 0x20U) {
            int line = highest_priority(pic->isr);
            if (line >= 0)
                pic->isr &= (uint8_t) ~(1U << line);
        } else if ((value & 0xe0U) == 0x60U) {
            pic->isr &= (uint8_t) ~(1U << (value & 0x07U));
        }
        update_output(pic);
        return BM_STATUS_OK;
    }

    switch (pic->init_state) {
        case PIC_EXPECT_ICW2:
            pic->vector_base = (uint8_t) transaction->value & 0xf8U;
            pic->init_state = pic->single ?
                (pic->need_icw4 ? PIC_EXPECT_ICW4 : PIC_READY) : PIC_EXPECT_ICW3;
            break;
        case PIC_EXPECT_ICW3:
            pic->init_state = pic->need_icw4 ? PIC_EXPECT_ICW4 : PIC_READY;
            break;
        case PIC_EXPECT_ICW4:
            pic->auto_eoi = ((uint8_t) transaction->value >> 1U) & 1U;
            pic->init_state = PIC_READY;
            break;
        default:
            pic->imr = (uint8_t) transaction->value;
            break;
    }
    update_output(pic);
    return BM_STATUS_OK;
}

bm_status_t
bm_pic8259_create(const bm_host_services_t *host,
                  bm_bus_t *bus,
                  const bm_pic8259_config_t *config,
                  bm_pic8259_t **out_pic)
{
    bm_pic8259_t *pic;
    bm_status_t status;
    if ((bm_host_services_validate(host) != BM_STATUS_OK) || (bus == NULL) ||
        (config == NULL) || (out_pic == NULL) || (config->io_base == UINT16_MAX))
        return BM_STATUS_INVALID_ARGUMENT;
    *out_pic = NULL;
    pic = host->allocate(host->context, sizeof(*pic));
    if (pic == NULL)
        return BM_STATUS_OUT_OF_MEMORY;
    memset(pic, 0, sizeof(*pic));
    pic->host = *host;
    pic->io_base = config->io_base;
    pic->output = config->output;
    pic->output_context = config->output_context;
    bm_pic8259_reset(pic);
    status = bm_bus_map(bus, BM_ADDRESS_IO, config->io_base, config->io_base + 1U,
                        pic_access, pic);
    if (status != BM_STATUS_OK) {
        host->release(host->context, pic);
        return status;
    }
    *out_pic = pic;
    return BM_STATUS_OK;
}

void
bm_pic8259_destroy(bm_pic8259_t *pic)
{
    if (pic != NULL)
        pic->host.release(pic->host.context, pic);
}

void
bm_pic8259_reset(bm_pic8259_t *pic)
{
    int was_asserted;
    if (pic == NULL)
        return;
    was_asserted = pic->output_asserted;
    pic->vector_base = 8;
    pic->imr = 0xffU;
    pic->irr = 0;
    pic->isr = 0;
    pic->lines = 0;
    pic->init_state = PIC_READY;
    pic->need_icw4 = 0;
    pic->single = 0;
    pic->read_isr = 0;
    pic->auto_eoi = 0;
    pic->output_asserted = 0;
    if (was_asserted && (pic->output != NULL))
        pic->output(pic->output_context, 0);
}

bm_status_t
bm_pic8259_set_irq(bm_pic8259_t *pic, unsigned int line, int asserted)
{
    uint8_t mask;
    if ((pic == NULL) || (line >= 8))
        return BM_STATUS_INVALID_ARGUMENT;
    mask = (uint8_t) (1U << line);
    if (asserted) {
        if ((pic->lines & mask) == 0)
            pic->irr |= mask;
        pic->lines |= mask;
    } else {
        pic->lines &= (uint8_t) ~mask;
        /* In edge-triggered mode the source must hold IR high until the first
         * interrupt-acknowledge pulse.  If it drops first, the request input
         * latch is cleared instead of delivering the original IRQ.  This is
         * also the behaviour required when a PIT control word withdraws an
         * old OUT pulse before the BIOS unmasks IRQ0. */
        if ((pic->isr & mask) == 0U)
            pic->irr &= (uint8_t) ~mask;
    }
    update_output(pic);
    return BM_STATUS_OK;
}

int
bm_pic8259_pending(const bm_pic8259_t *pic)
{
    return (pic != NULL) && (eligible_requests(pic) != 0U);
}

bm_status_t
bm_pic8259_acknowledge(bm_pic8259_t *pic, uint8_t *vector)
{
    int line;
    uint8_t pending;
    if ((pic == NULL) || (vector == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    pending = eligible_requests(pic);
    line = highest_priority(pending);
    if (line < 0)
        return BM_STATUS_IDLE;
    pic->irr &= (uint8_t) ~(1U << line);
    if (!pic->auto_eoi)
        pic->isr |= (uint8_t) (1U << line);
    *vector = (uint8_t) (pic->vector_base + line);
    update_output(pic);
    return BM_STATUS_OK;
}

bm_status_t
bm_pic8259_state(const bm_pic8259_t *pic, bm_pic8259_state_t *state)
{
    if ((pic == NULL) || (state == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    state->interrupt_mask = pic->imr;
    state->interrupt_requests = pic->irr;
    state->in_service = pic->isr;
    state->input_lines = pic->lines;
    return BM_STATUS_OK;
}
