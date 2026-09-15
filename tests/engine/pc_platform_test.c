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

int
main(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_bus_t *bus = NULL;
    bm_pic8259_t *pic = NULL;
    bm_pit8253_t *pit = NULL;
    bm_pic8259_config_t pic_config = { 0x20U };
    output_sink_t output = { 0 };
    bm_pit8253_config_t pit_config = { 0x40U, capture_output, &output };
    uint8_t vector = 0;

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
    assert(bm_pic8259_acknowledge(pic, &vector) == BM_STATUS_OK);
    assert(vector == 8U);
    assert(!bm_pic8259_pending(pic));
    assert(write_port(bus, 0x20U, 0x20U) == BM_STATUS_OK);

    /* Single-controller initialization omits ICW3. */
    assert(write_port(bus, 0x20U, 0x13U) == BM_STATUS_OK);
    assert(write_port(bus, 0x21U, 0x08U) == BM_STATUS_OK);
    assert(write_port(bus, 0x21U, 0x01U) == BM_STATUS_OK);
    assert(write_port(bus, 0x21U, 0xfeU) == BM_STATUS_OK);
    assert(read_port(bus, 0x21U) == 0xfeU);

    assert(write_port(bus, 0x43U, 0x30U) == BM_STATUS_OK); /* Channel 0, mode 0, lobyte/hibyte. */
    assert(write_port(bus, 0x40U, 0x04U) == BM_STATUS_OK);
    assert(write_port(bus, 0x40U, 0x00U) == BM_STATUS_OK);
    assert(bm_pit8253_advance(pit, 3) == BM_STATUS_OK);
    assert(output.changes == 1U && output.channel == 0U && output.value == 0);
    assert(bm_pit8253_advance(pit, 1) == BM_STATUS_OK);
    assert(output.changes == 2U && output.value == 1);

    bm_pit8253_destroy(pit);
    bm_pic8259_destroy(pic);
    bm_bus_destroy(bus);
    return 0;
}
