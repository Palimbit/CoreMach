/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Cascaded routing/EOI smoke gate; full 8259 mode coverage is agent-owned.
 */
#include <blumach/components/at_pic.h>
#include <blumach/platforms/null_host.h>
#include <assert.h>

static void write_port(bm_at_pic_t *pic, uint16_t port, uint8_t value)
{
    bm_bus_transaction_t t = {0};
    t.space = BM_ADDRESS_IO;
    t.operation = BM_BUS_WRITE;
    t.address = port;
    t.size = 1U;
    t.value = value;
    assert(bm_at_pic_io(pic, &t) == BM_STATUS_OK);
}
int main(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_at_pic_config_t c = {0};
    bm_at_pic_t *pic = NULL;
    bm_at_pic_state_t s = {0};
    uint8_t vector = 0U;
    c.master_base = 0x20U;
    c.slave_base = 0xa0U;
    c.cascade_line = 2U;
    assert(bm_at_pic_create(&host, &c, &pic) == BM_STATUS_OK);
    write_port(pic, 0x20U, 0x11U);
    write_port(pic, 0xa0U, 0x11U);
    write_port(pic, 0x21U, 0x30U); /* deliberately not BIOS defaults */
    write_port(pic, 0xa1U, 0x68U);
    write_port(pic, 0x21U, 0x04U);
    write_port(pic, 0xa1U, 0x02U);
    write_port(pic, 0x21U, 0x01U);
    write_port(pic, 0xa1U, 0x01U);
    write_port(pic, 0x21U, 0xfbU); /* only cascade unmasked */
    write_port(pic, 0xa1U, 0xfeU); /* only IRQ8 unmasked */
    assert(bm_at_pic_set_irq(pic, 8U, 1) == BM_STATUS_OK);
    assert(bm_at_pic_state(pic, &s) == BM_STATUS_OK && s.intr);
    assert(bm_at_pic_acknowledge(pic, 0U, &vector) == BM_STATUS_OK);
    assert(bm_at_pic_acknowledge(pic, 1U, &vector) == BM_STATUS_OK);
    assert(vector == 0x68U);
    assert(bm_at_pic_state(pic, &s) == BM_STATUS_OK);
    assert((s.isr[0] & 4U) != 0U && (s.isr[1] & 1U) != 0U);
    write_port(pic, 0xa0U, 0x20U);
    assert(bm_at_pic_state(pic, &s) == BM_STATUS_OK);
    assert(s.isr[1] == 0U && (s.isr[0] & 4U) != 0U);
    write_port(pic, 0x20U, 0x20U);
    assert(bm_at_pic_state(pic, &s) == BM_STATUS_OK && s.isr[0] == 0U);
    bm_at_pic_destroy(pic);
    bm_at_pic_destroy(NULL);
    return 0;
}
