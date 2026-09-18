/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/components/bus.h>
#include <blumach/components/pic8259.h>
#include <blumach/components/pit8253.h>
#include <blumach/platforms/null_host.h>

#include <assert.h>
#include <stdint.h>

typedef struct output_sink {
    unsigned int changes;
    unsigned int channel;
    int value;
} output_sink_t;

typedef struct irq_sink {
    unsigned int changes;
    int asserted;
} irq_sink_t;

static bm_status_t
write_port(bm_bus_t *bus, uint16_t port, uint8_t value)
{
    bm_bus_transaction_t transaction = {
        BM_ADDRESS_IO, BM_BUS_WRITE, port, value, 1, 1, 0, BM_ENDIAN_LITTLE, 0
    };
    return bm_bus_transact(bus, &transaction);
}

static uint8_t
read_port(bm_bus_t *bus, uint16_t port)
{
    bm_bus_transaction_t transaction = {
        BM_ADDRESS_IO, BM_BUS_READ, port, 0, 1, 1, 0, BM_ENDIAN_LITTLE, 0
    };
    assert(bm_bus_transact(bus, &transaction) == BM_STATUS_OK);
    return (uint8_t) transaction.value;
}

static void
capture_output(void *context, unsigned int channel, int output)
{
    output_sink_t *sink = context;
    ++sink->changes;
    sink->channel = channel;
    sink->value = output;
}

static void
capture_irq(void *context, int asserted)
{
    irq_sink_t *sink = context;
    ++sink->changes;
    sink->asserted = asserted;
}

int
main(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_bus_t *bus = NULL;
    bm_pic8259_t *pic = NULL;
    bm_pit8253_t *pit = NULL;
    irq_sink_t irq = { 0, 0 };
    bm_pic8259_config_t pic_config = { 0x20U, capture_irq, &irq };
    output_sink_t output = { 0 };
    bm_pit8253_config_t pit_config = { 0x40U, capture_output, &output };
    uint8_t vector = 0;
    uint16_t count = 0;
    int level = 0;

    assert(bm_bus_create(&host, 2, &bus) == BM_STATUS_OK);
    assert(bm_pic8259_create(&host, bus, &pic_config, &pic) == BM_STATUS_OK);
    assert(bm_pit8253_create(&host, bus, &pit_config, &pit) == BM_STATUS_OK);

    assert(write_port(bus, 0x20U, 0x11U) == BM_STATUS_OK);
    assert(write_port(bus, 0x21U, 0x08U) == BM_STATUS_OK);
    assert(write_port(bus, 0x21U, 0x00U) == BM_STATUS_OK);
    assert(write_port(bus, 0x21U, 0x01U) == BM_STATUS_OK);
    assert(write_port(bus, 0x21U, 0xfeU) == BM_STATUS_OK);
    assert(read_port(bus, 0x21U) == 0xfeU);
    assert(bm_pic8259_set_irq(pic, 0, 1) == BM_STATUS_OK);
    assert(bm_pic8259_pending(pic));
    assert(irq.changes == 1U && irq.asserted == 1);
    assert(bm_pic8259_acknowledge(pic, &vector) == BM_STATUS_OK);
    assert(vector == 8U);
    assert(!bm_pic8259_pending(pic));
    assert(irq.changes == 2U && irq.asserted == 0);
    assert(write_port(bus, 0x20U, 0x20U) == BM_STATUS_OK);
    assert(bm_pic8259_set_irq(pic, 0U, 0) == BM_STATUS_OK);

    /* A repeated edge at the active priority and lower-priority work wait
     * until EOI; a higher-priority line may still pre-empt it. */
    assert(write_port(bus, 0x21U, 0x00U) == BM_STATUS_OK);
    assert(bm_pic8259_set_irq(pic, 1U, 1) == BM_STATUS_OK);
    assert(bm_pic8259_acknowledge(pic, &vector) == BM_STATUS_OK);
    assert(vector == 9U);
    assert(bm_pic8259_set_irq(pic, 1U, 0) == BM_STATUS_OK);
    assert(bm_pic8259_set_irq(pic, 1U, 1) == BM_STATUS_OK);
    assert(bm_pic8259_set_irq(pic, 2U, 1) == BM_STATUS_OK);
    assert(!bm_pic8259_pending(pic));
    assert(bm_pic8259_acknowledge(pic, &vector) == BM_STATUS_IDLE);
    assert(bm_pic8259_set_irq(pic, 0U, 1) == BM_STATUS_OK);
    assert(bm_pic8259_pending(pic));
    assert(bm_pic8259_acknowledge(pic, &vector) == BM_STATUS_OK);
    assert(vector == 8U);
    assert(write_port(bus, 0x20U, 0x60U) == BM_STATUS_OK);
    assert(!bm_pic8259_pending(pic));
    assert(write_port(bus, 0x20U, 0x61U) == BM_STATUS_OK);
    assert(bm_pic8259_pending(pic));
    assert(bm_pic8259_acknowledge(pic, &vector) == BM_STATUS_OK);
    assert(vector == 9U);
    assert(write_port(bus, 0x20U, 0x61U) == BM_STATUS_OK);
    assert(bm_pic8259_pending(pic));
    assert(bm_pic8259_acknowledge(pic, &vector) == BM_STATUS_OK);
    assert(vector == 10U);
    assert(write_port(bus, 0x20U, 0x62U) == BM_STATUS_OK);
    assert(bm_pic8259_set_irq(pic, 0U, 0) == BM_STATUS_OK);
    assert(bm_pic8259_set_irq(pic, 1U, 0) == BM_STATUS_OK);
    assert(bm_pic8259_set_irq(pic, 2U, 0) == BM_STATUS_OK);
    bm_pic8259_reset(pic);
    irq.changes = 0U;
    irq.asserted = 0;

    /* Single-controller initialization omits ICW3. */
    assert(write_port(bus, 0x20U, 0x13U) == BM_STATUS_OK);
    assert(write_port(bus, 0x21U, 0x08U) == BM_STATUS_OK);
    assert(write_port(bus, 0x21U, 0x01U) == BM_STATUS_OK);
    assert(write_port(bus, 0x21U, 0xfeU) == BM_STATUS_OK);
    assert(read_port(bus, 0x21U) == 0xfeU);
    assert(bm_pic8259_set_irq(pic, 0, 0) == BM_STATUS_OK);
    assert(bm_pic8259_set_irq(pic, 0, 1) == BM_STATUS_OK);
    assert(irq.changes == 1U && irq.asserted == 1);
    /* An edge source withdrawn before INTA must not survive as that IRQ. */
    assert(bm_pic8259_set_irq(pic, 0, 0) == BM_STATUS_OK);
    assert(!bm_pic8259_pending(pic));
    assert(irq.changes == 2U && irq.asserted == 0);
    assert(bm_pic8259_set_irq(pic, 0, 1) == BM_STATUS_OK);
    bm_pic8259_reset(pic);
    assert(irq.changes == 4U && irq.asserted == 0);
    assert(!bm_pic8259_pending(pic));

    assert(write_port(bus, 0x43U, 0x30U) == BM_STATUS_OK); /* Channel 0, mode 0, lobyte/hibyte. */
    assert(write_port(bus, 0x40U, 0x04U) == BM_STATUS_OK);
    assert(write_port(bus, 0x40U, 0x00U) == BM_STATUS_OK);
    assert(bm_pit8253_advance(pit, 4) == BM_STATUS_OK);
    assert(output.changes == 0U);
    assert(bm_pit8253_advance(pit, 1) == BM_STATUS_OK);
    assert(output.changes == 1U && output.channel == 0U && output.value == 1);

    assert(write_port(bus, 0x43U, 0x30U) == BM_STATUS_OK);
    assert(write_port(bus, 0x40U, 0x34U) == BM_STATUS_OK);
    assert(write_port(bus, 0x40U, 0x12U) == BM_STATUS_OK);
    assert(bm_pit8253_advance(pit, 0x35U) == BM_STATUS_OK);
    assert(write_port(bus, 0x43U, 0x00U) == BM_STATUS_OK); /* Latch current count. */
    assert(bm_pit8253_advance(pit, 0x0100U) == BM_STATUS_OK);
    assert(read_port(bus, 0x40U) == 0x00U);
    assert(read_port(bus, 0x40U) == 0x12U);

    output.changes = 0U;
    assert(write_port(bus, 0x43U, 0x34U) == BM_STATUS_OK); /* Mode 2. */
    assert(write_port(bus, 0x40U, 0x04U) == BM_STATUS_OK);
    assert(write_port(bus, 0x40U, 0x00U) == BM_STATUS_OK);
    assert(bm_pit8253_advance(pit, 4U) == BM_STATUS_OK);
    assert(bm_pit8253_output(pit, 0U, &level) == BM_STATUS_OK && level == 0);
    assert(bm_pit8253_advance(pit, 1U) == BM_STATUS_OK);
    assert(bm_pit8253_output(pit, 0U, &level) == BM_STATUS_OK && level == 1);
    assert(output.changes == 3U);

    assert(write_port(bus, 0x43U, 0x36U) == BM_STATUS_OK); /* Mode 3. */
    assert(write_port(bus, 0x40U, 0x04U) == BM_STATUS_OK);
    assert(write_port(bus, 0x40U, 0x00U) == BM_STATUS_OK);
    assert(bm_pit8253_advance(pit, 3U) == BM_STATUS_OK);
    assert(bm_pit8253_output(pit, 0U, &level) == BM_STATUS_OK && level == 0);
    assert(bm_pit8253_advance(pit, 2U) == BM_STATUS_OK);
    assert(bm_pit8253_output(pit, 0U, &level) == BM_STATUS_OK && level == 1);

    assert(write_port(bus, 0x43U, 0x38U) == BM_STATUS_OK); /* Mode 4. */
    assert(write_port(bus, 0x40U, 0x02U) == BM_STATUS_OK);
    assert(write_port(bus, 0x40U, 0x00U) == BM_STATUS_OK);
    assert(bm_pit8253_advance(pit, 3U) == BM_STATUS_OK);
    assert(bm_pit8253_output(pit, 0U, &level) == BM_STATUS_OK && level == 0);
    assert(bm_pit8253_advance(pit, 1U) == BM_STATUS_OK);
    assert(bm_pit8253_output(pit, 0U, &level) == BM_STATUS_OK && level == 1);

    /* A physical I/O sequence spans a PIT edge, while the portable scheduler
     * can batch adjacent instructions. A read resolves a completed pending
     * load instead of exposing the previous counter as undefined data. */
    assert(bm_pit8253_set_gate(pit, 2U, 0) == BM_STATUS_OK);
    assert(write_port(bus, 0x43U, 0xb8U) == BM_STATUS_OK); /* Ch2 mode 4. */
    assert(write_port(bus, 0x42U, 0x00U) == BM_STATUS_OK);
    assert(write_port(bus, 0x42U, 0x01U) == BM_STATUS_OK);
    assert(read_port(bus, 0x42U) == 0x00U);
    assert(read_port(bus, 0x42U) == 0x01U);
    assert(write_port(bus, 0x42U, 0x00U) == BM_STATUS_OK);
    assert(write_port(bus, 0x42U, 0x02U) == BM_STATUS_OK);
    assert(read_port(bus, 0x42U) == 0x00U);
    assert(read_port(bus, 0x42U) == 0x02U);

    assert(write_port(bus, 0x43U, 0x31U) == BM_STATUS_OK); /* Mode 0, BCD 0010. */
    assert(write_port(bus, 0x40U, 0x10U) == BM_STATUS_OK);
    assert(write_port(bus, 0x40U, 0x00U) == BM_STATUS_OK);
    assert(bm_pit8253_advance(pit, 10U) == BM_STATUS_OK);
    assert(bm_pit8253_output(pit, 0U, &level) == BM_STATUS_OK && level == 0);
    assert(bm_pit8253_advance(pit, 1U) == BM_STATUS_OK);
    assert(bm_pit8253_output(pit, 0U, &level) == BM_STATUS_OK && level == 1);

    assert(write_port(bus, 0x43U, 0x72U) == BM_STATUS_OK); /* Channel 1, mode 1. */
    assert(write_port(bus, 0x41U, 0x02U) == BM_STATUS_OK);
    assert(write_port(bus, 0x41U, 0x00U) == BM_STATUS_OK);
    assert(bm_pit8253_set_gate(pit, 1U, 0) == BM_STATUS_OK);
    assert(bm_pit8253_set_gate(pit, 1U, 1) == BM_STATUS_OK);
    assert(bm_pit8253_advance(pit, 3U) == BM_STATUS_OK);
    assert(bm_pit8253_output(pit, 1U, &level) == BM_STATUS_OK && level == 1);

    assert(write_port(bus, 0x43U, 0x7aU) == BM_STATUS_OK); /* Channel 1, mode 5. */
    assert(write_port(bus, 0x41U, 0x02U) == BM_STATUS_OK);
    assert(write_port(bus, 0x41U, 0x00U) == BM_STATUS_OK);
    assert(bm_pit8253_set_gate(pit, 1U, 0) == BM_STATUS_OK);
    assert(bm_pit8253_set_gate(pit, 1U, 1) == BM_STATUS_OK);
    assert(bm_pit8253_advance(pit, 3U) == BM_STATUS_OK);
    assert(bm_pit8253_output(pit, 1U, &level) == BM_STATUS_OK && level == 0);
    assert(bm_pit8253_advance(pit, 1U) == BM_STATUS_OK);
    assert(bm_pit8253_output(pit, 1U, &level) == BM_STATUS_OK && level == 1);

    assert(bm_pit8253_count(pit, 1U, &count) == BM_STATUS_OK);
    assert(bm_pit8253_count(pit, 3U, &count) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_pit8253_output(NULL, 0U, &level) == BM_STATUS_INVALID_ARGUMENT);

    bm_pit8253_destroy(pit);
    bm_pic8259_destroy(pic);
    bm_bus_destroy(bus);
    return 0;
}
