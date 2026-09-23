/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Authored Intel 8259A architectural fixtures, not physical chip captures.
 */
#include <blumach/components/at_pic.h>
#include <blumach/platforms/null_host.h>
#include "failure_injection_host.h"
#include <assert.h>
#include <string.h>

static bm_status_t io(bm_at_pic_t *pic, uint16_t port, uint8_t value)
{
    bm_bus_transaction_t t = {0};
    t.space = BM_ADDRESS_IO; t.operation = BM_BUS_WRITE;
    t.address = port; t.size = 1U; t.value = value;
    return bm_at_pic_io(pic, &t);
}
static void wr(bm_at_pic_t *pic, uint16_t port, uint8_t value)
{
    assert(io(pic, port, value) == BM_STATUS_OK);
}
static bm_at_pic_state_t state(bm_at_pic_t *pic)
{
    bm_at_pic_state_t s;
    assert(bm_at_pic_state(pic, &s) == BM_STATUS_OK);
    return s;
}
static uint8_t irr(bm_at_pic_t *pic, unsigned int chip)
{
    bm_at_pic_state_t snapshot = state(pic);
    return snapshot.irr[chip];
}
static uint8_t isr(bm_at_pic_t *pic, unsigned int chip)
{
    bm_at_pic_state_t snapshot = state(pic);
    return snapshot.isr[chip];
}
static uint8_t imr(bm_at_pic_t *pic, unsigned int chip)
{
    bm_at_pic_state_t snapshot = state(pic);
    return snapshot.imr[chip];
}
static uint8_t rd(bm_at_pic_t *pic, uint16_t port)
{
    bm_bus_transaction_t t = {0};
    t.space = BM_ADDRESS_IO; t.operation = BM_BUS_READ;
    t.address = port; t.size = 1U; t.wait_states = 13U;
    t.attributes = BM_BUS_TRANSACTION_DEBUG;
    assert(bm_at_pic_io(pic, &t) == BM_STATUS_OK && t.wait_states == 13U);
    return (uint8_t)t.value;
}
static void line(bm_at_pic_t *pic, unsigned int irq, int high)
{
    assert(bm_at_pic_set_irq(pic, irq, high) == BM_STATUS_OK);
}
static void init(bm_at_pic_t *pic, uint8_t level, uint8_t auto_eoi)
{
    bm_at_pic_reset(pic);
    wr(pic, 0x20, (uint8_t)(0x11U | level));
    wr(pic, 0xa0, (uint8_t)(0x11U | level));
    wr(pic, 0x21, 0x33); wr(pic, 0xa1, 0x6f); /* low three bits ignored */
    wr(pic, 0x21, 4); wr(pic, 0xa1, 2);
    wr(pic, 0x21, (uint8_t)(1U | auto_eoi)); wr(pic, 0xa1, (uint8_t)(1U | auto_eoi));
    wr(pic, 0x21, 0); wr(pic, 0xa1, 0);
}
static uint8_t ack(bm_at_pic_t *pic)
{
    uint8_t v = 0xa5;
    assert(bm_at_pic_acknowledge(pic, 0, &v) == BM_STATUS_OK && v == 0xa5);
    assert(!state(pic).intr && state(pic).acknowledge_phase == 1);
    assert(bm_at_pic_acknowledge(pic, 1, &v) == BM_STATUS_OK);
    return v;
}
static void output(void *context, int high)
{
    unsigned int *edges = context;
    assert(high == 0 || high == 1);
    ++*edges;
}

int main(void)
{
    bm_host_services_t host = bm_null_host_services();
    unsigned int edges = 0, irq, low, mask;
    bm_at_pic_config_t config = {0x20, 0xa0, 2, output, &edges};
    bm_at_pic_t *pic = NULL, *other = NULL;
    bm_at_pic_state_t before, after;
    uint8_t vector;
    bm_bus_transaction_t t = {0};
    failure_injection_host_t failure;
    bm_host_services_t failing;
    failure_injection_host_initialize(&failure);
    failing = failure_injection_host_services(&failure);
    failure_injection_host_fail_on(&failure, 0);
    assert(bm_at_pic_create(&failing, &config, &pic) == BM_STATUS_OUT_OF_MEMORY);
    assert(pic == NULL && failure.outstanding_allocations == 0);
    assert(bm_at_pic_create(&host, &config, &pic) == BM_STATUS_OK && edges == 0);
    assert(bm_at_pic_create(&host, &config, &other) == BM_STATUS_OK && edges == 0);
    assert(imr(pic, 0) == 255 && !state(pic).intr);
    assert(bm_at_pic_acknowledge(pic, 0, &vector) == BM_STATUS_INVALID_STATE);

    /* Every external IRQ, masks and separate cascaded EOIs. */
    for (irq = 0; irq < 16; ++irq) {
        if (irq == 2) continue;
        init(pic, 0, 0);
        line(pic, irq, 1);
        assert(state(pic).intr);
        assert(ack(pic) == (uint8_t)((irq < 8 ? 0x30 : 0x68) + (irq & 7U)));
        assert(!state(pic).intr);
        if (irq >= 8) {
            assert(isr(pic, 0) == 4 && isr(pic, 1) == (1U << (irq & 7U)));
            wr(pic, 0xa0, 0x20);
            assert(isr(pic, 0) == 4 && isr(pic, 1) == 0);
        }
        wr(pic, 0x20, 0x20);
        assert(isr(pic, 0) == 0 && !state(pic).intr); /* held edge cannot recur */
        line(pic, irq, 0); line(pic, irq, 1);
        assert(state(pic).intr);
        assert(irr(other, 0) == 0 && irr(other, 1) == 0);
    }
    /* Exhaustive rotating priority and mask resolution for direct master pins. */
    for (low = 0; low < 8; ++low) {
        for (mask = 0; mask < 256; ++mask) {
            int expected = -1;
            init(pic, 0, 0);
            wr(pic, 0x20, (uint8_t)(0xc0U | low));
            wr(pic, 0x21, (uint8_t)(mask | 4U));
            for (irq = 0; irq < 8; ++irq) if (irq != 2) line(pic, irq, 1);
            for (irq = 1; irq <= 8; ++irq) {
                unsigned int candidate = (low + irq) & 7U;
                if (!(mask & (1U << candidate)) && candidate != 2) { expected = (int)candidate; break; }
            }
            assert(state(pic).intr == (expected >= 0));
            assert(ack(pic) == (uint8_t)(0x30 + (expected < 0 ? 7 : expected)));
            assert(isr(pic, 0) == (expected < 0 ? 0 : (1U << (unsigned int)expected)));
        }
    }
    /* Fully nested priorities: higher can interrupt, equal/lower cannot. */
    init(pic, 0, 0);
    line(pic, 5, 1); assert(ack(pic) == 0x35);
    line(pic, 6, 1); assert(!state(pic).intr);
    line(pic, 3, 1); assert(ack(pic) == 0x33);
    assert(isr(pic, 0) == 0x28);
    wr(pic, 0x20, 0x20); assert(isr(pic, 0) == 0x20 && !state(pic).intr);
    wr(pic, 0x20, 0x65); assert(state(pic).intr);
    assert(ack(pic) == 0x36);
    /* Normal cascade blocks a higher slave priority until master EOI. */
    init(pic, 0, 0);
    line(pic, 13, 1); assert(ack(pic) == 0x6d);
    line(pic, 8, 1); assert(!state(pic).intr);
    wr(pic, 0x20, 0x62); assert(state(pic).intr);
    assert(ack(pic) == 0x68);
    assert(isr(pic, 1) == 0x21);

    /* Level repeats after EOI, edge withdrawal gives spurious master IRQ7. */
    init(pic, 8, 0);
    line(pic, 4, 1); assert(ack(pic) == 0x34);
    wr(pic, 0x20, 0x20); assert(state(pic).intr);
    assert(ack(pic) == 0x34);
    line(pic, 4, 0); wr(pic, 0x20, 0x20); assert(!state(pic).intr);
    init(pic, 0, 0);
    line(pic, 4, 1); line(pic, 4, 0);
    assert(ack(pic) == 0x37 && isr(pic, 0) == 0);
    /* Direct raw cascade pin with no slave request: IRQ15, master ISR only. */
    line(pic, 2, 1);
    assert(ack(pic) == 0x6f && isr(pic, 0) == 4 && isr(pic, 1) == 0);

    /* Freeze selection across INTA, no vector on phase 0, no early AEOI. */
    init(pic, 0, 2);
    line(pic, 12, 1);
    vector = 0x99;
    assert(bm_at_pic_acknowledge(pic, 1, &vector) == BM_STATUS_INVALID_STATE && vector == 0x99);
    assert(bm_at_pic_acknowledge(pic, 0, &vector) == BM_STATUS_OK && vector == 0x99);
    assert(isr(pic, 0) == 4 && isr(pic, 1) == 16);
    assert(bm_at_pic_acknowledge(pic, 0, &vector) == BM_STATUS_INVALID_STATE);
    line(pic, 12, 0); line(pic, 0, 1);
    assert(io(pic, 0x20, 0x20) == BM_STATUS_INVALID_STATE);
    assert(bm_at_pic_acknowledge(pic, 1, &vector) == BM_STATUS_OK && vector == 0x6c);
    assert(isr(pic, 0) == 0 && isr(pic, 1) == 0 && state(pic).intr);
    assert(ack(pic) == 0x30);

    /* Each chip owns its AEOI mode; master AEOI must not erase slave ISR. */
    init(pic, 0, 0);
    wr(pic, 0x20, 0x11); wr(pic, 0x21, 0x30); wr(pic, 0x21, 4); wr(pic, 0x21, 3);
    line(pic, 9, 1); assert(ack(pic) == 0x69);
    assert(isr(pic, 0) == 0 && isr(pic, 1) == 2);

    /* Specific/non-specific rotation and auto-rotation. */
    init(pic, 0, 0); line(pic, 0, 1); line(pic, 1, 1);
    assert(ack(pic) == 0x30); wr(pic, 0x20, 0xa0); assert(ack(pic) == 0x31);
    wr(pic, 0x20, 0xe1); assert(isr(pic, 0) == 0);
    init(pic, 8, 2); wr(pic, 0x20, 0x80); line(pic, 0, 1); line(pic, 1, 1);
    assert(ack(pic) == 0x30); assert(ack(pic) == 0x31);
    wr(pic, 0x20, 0); assert(ack(pic) == 0x30); assert(ack(pic) == 0x30);
    /* Special mask makes a masked ISR stop blocking; nonspecific EOI skips it. */
    init(pic, 0, 0); line(pic, 1, 1); assert(ack(pic) == 0x31);
    wr(pic, 0x21, 2); line(pic, 5, 1); assert(!state(pic).intr);
    wr(pic, 0x20, 0x68); assert(state(pic).intr && ack(pic) == 0x35);
    wr(pic, 0x20, 0x20); assert(isr(pic, 0) == 2);
    wr(pic, 0x20, 0x61); assert(isr(pic, 0) == 0);

    /* OCW3 RR=0 preserves the read selector; DEBUG reads have no effects. */
    init(pic, 0, 0); line(pic, 3, 1); assert(ack(pic) == 0x33); line(pic, 5, 1);
    wr(pic, 0x20, 0x0b); assert(rd(pic, 0x20) == 8);
    wr(pic, 0x20, 0x08); assert(rd(pic, 0x20) == 8);
    wr(pic, 0x20, 0x0a); assert(rd(pic, 0x20) == 32);
    before = state(pic); assert(rd(pic, 0x20) == 32); after = state(pic);
    assert(memcmp(&before, &after, sizeof(before)) == 0);
    assert(io(pic, 0x20, 0x0c) == BM_STATUS_UNSUPPORTED); /* poll */
    after = state(pic); assert(memcmp(&before, &after, sizeof(before)) == 0);

    /* ICW3 mismatch must fail without consuming either request. */
    init(pic, 0, 0);
    wr(pic, 0xa0, 0x11); wr(pic, 0xa1, 0x68); wr(pic, 0xa1, 3); wr(pic, 0xa1, 1);
    line(pic, 8, 1); before = state(pic);
    vector = 0x99;
    assert(bm_at_pic_acknowledge(pic, 0, &vector) == BM_STATUS_UNSUPPORTED && vector == 0x99);
    after = state(pic); assert(memcmp(&before, &after, sizeof(before)) == 0);
    /* Fresh initialization while input is held must not manufacture an edge. */
    init(pic, 0, 0); line(pic, 1, 1);
    wr(pic, 0x20, 0x11); wr(pic, 0x21, 0x30); wr(pic, 0x21, 4); wr(pic, 0x21, 1);
    assert(!state(pic).intr); line(pic, 1, 0); line(pic, 1, 1); assert(state(pic).intr);
    wr(pic, 0x20, 0x11); wr(pic, 0x21, 0x30); wr(pic, 0x21, 4);
    assert(io(pic, 0x21, 0x11) == BM_STATUS_UNSUPPORTED); /* SFNM */
    assert(io(pic, 0x21, 0x09) == BM_STATUS_UNSUPPORTED); /* buffered */
    assert(io(pic, 0x21, 0x00) == BM_STATUS_UNSUPPORTED); /* MCS */
    wr(pic, 0x21, 1);
    assert(io(pic, 0x20, 0x10) == BM_STATUS_UNSUPPORTED); /* no ICW4 */

    assert(bm_at_pic_set_irq(pic, 16, 1) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_at_pic_set_irq(pic, 1, 2) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_at_pic_state(NULL, &before) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_at_pic_state(pic, NULL) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_at_pic_acknowledge(pic, 2, &vector) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_at_pic_acknowledge(pic, 0, NULL) == BM_STATUS_INVALID_ARGUMENT);
    t.space = BM_ADDRESS_IO; t.size = 1; t.address = 0x20; t.operation = BM_BUS_WRITE;
    t.attributes = BM_BUS_TRANSACTION_DEBUG;
    assert(bm_at_pic_io(pic, &t) == BM_STATUS_UNSUPPORTED);
    t.attributes = 0; t.size = 2; assert(bm_at_pic_io(pic, &t) == BM_STATUS_UNSUPPORTED);
    t.size = 1; t.address = 0x10020; assert(bm_at_pic_io(pic, &t) == BM_STATUS_UNMAPPED);
    t.address = 0x20; t.space = BM_ADDRESS_MEMORY; assert(bm_at_pic_io(pic, &t) == BM_STATUS_INVALID_ARGUMENT);
    config.slave_base = 0x21;
    bm_at_pic_destroy(other); other = NULL;
    assert(bm_at_pic_create(&host, &config, &other) == BM_STATUS_INVALID_ARGUMENT && other == NULL);
    config.slave_base = 0xa0; config.cascade_line = 8;
    assert(bm_at_pic_create(&host, &config, &other) == BM_STATUS_INVALID_ARGUMENT);
    /* Nonstandard ports, physical cascade pin and matching ICW3 values. */
    config.master_base = 0x320; config.slave_base = 0x3a0; config.cascade_line = 5;
    assert(bm_at_pic_create(&host, &config, &other) == BM_STATUS_OK);
    wr(other, 0x320, 0x11); wr(other, 0x321, 0x40);
    wr(other, 0x321, 0x20); wr(other, 0x321, 1);
    wr(other, 0x3a0, 0x11); wr(other, 0x3a1, 0x70);
    wr(other, 0x3a1, 5); wr(other, 0x3a1, 1);
    line(other, 11, 1); assert(ack(other) == 0x73);
    assert(isr(other, 0) == 0x20 && isr(other, 1) == 8);
    bm_at_pic_destroy(other);
    bm_at_pic_reset(pic); assert(!state(pic).intr && isr(pic, 0) == 0);
    bm_at_pic_destroy(pic); bm_at_pic_destroy(NULL);
    return 0;
}
