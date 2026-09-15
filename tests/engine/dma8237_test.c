/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/components/bus.h>
#include <blumach/components/dma8237.h>
#include <blumach/platforms/null_host.h>

#include <assert.h>
#include <stdint.h>

static bm_status_t
write_port(bm_bus_t *bus, uint16_t port, uint8_t value)
{
    bm_bus_transaction_t transaction = {
        BM_ADDRESS_IO, BM_BUS_WRITE, port, value, 1, 1, 0, BM_ENDIAN_LITTLE, 0
    };
    return bm_bus_transact(bus, &transaction);
}

static bm_status_t
read_port(bm_bus_t *bus, uint16_t port, uint8_t *value)
{
    bm_bus_transaction_t transaction = {
        BM_ADDRESS_IO, BM_BUS_READ, port, 0, 1, 1, 0, BM_ENDIAN_LITTLE, 0
    };
    bm_status_t status = bm_bus_transact(bus, &transaction);
    if (status == BM_STATUS_OK)
        *value = (uint8_t) transaction.value;
    return status;
}

int
main(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_bus_t *bus = NULL;
    bm_dma8237_t *dma = NULL;
    bm_dma8237_config_t config = { 0x0000U };
    bm_dma8237_channel_state_t channel;
    uint8_t value;

    assert(bm_bus_create(&host, 1, &bus) == BM_STATUS_OK);
    assert(bm_dma8237_create(&host, bus, &config, &dma) == BM_STATUS_OK);
    assert(bm_dma8237_mask(dma) == 0x0fU);

    /* Channel 2 address and count use the shared low/high-byte pointer. */
    assert(write_port(bus, 0x0cU, 0) == BM_STATUS_OK);
    assert(write_port(bus, 0x04U, 0x34U) == BM_STATUS_OK);
    assert(write_port(bus, 0x04U, 0x12U) == BM_STATUS_OK);
    assert(write_port(bus, 0x05U, 0xffU) == BM_STATUS_OK);
    assert(write_port(bus, 0x05U, 0x01U) == BM_STATUS_OK);
    assert(write_port(bus, 0x0bU, 0x4aU) == BM_STATUS_OK);
    assert(write_port(bus, 0x0aU, 0x02U) == BM_STATUS_OK);
    assert(bm_dma8237_channel_state(dma, 2, &channel) == BM_STATUS_OK);
    assert(channel.base_address == 0x1234U);
    assert(channel.current_address == 0x1234U);
    assert(channel.base_count == 0x01ffU);
    assert(channel.current_count == 0x01ffU);
    assert(channel.mode == 0x4aU);
    assert(!channel.masked);

    /* Reads use the same pointer and return the programmed current values. */
    assert(write_port(bus, 0x0cU, 0) == BM_STATUS_OK);
    assert(read_port(bus, 0x04U, &value) == BM_STATUS_OK && value == 0x34U);
    assert(read_port(bus, 0x04U, &value) == BM_STATUS_OK && value == 0x12U);

    assert(write_port(bus, 0x08U, 0x10U) == BM_STATUS_OK);
    assert(bm_dma8237_command(dma) == 0x10U);
    assert(write_port(bus, 0x09U, 0x06U) == BM_STATUS_OK);
    assert(bm_dma8237_channel_state(dma, 2, &channel) == BM_STATUS_OK);
    assert(channel.requested);
    assert(read_port(bus, 0x08U, &value) == BM_STATUS_OK);
    assert(value == 0x40U);
    assert(write_port(bus, 0x09U, 0x02U) == BM_STATUS_OK);
    assert(bm_dma8237_set_dreq(dma, 2, 1) == BM_STATUS_OK);
    assert(read_port(bus, 0x08U, &value) == BM_STATUS_OK && value == 0x40U);

    assert(write_port(bus, 0x0fU, 0x05U) == BM_STATUS_OK);
    assert(bm_dma8237_mask(dma) == 0x05U);
    assert(write_port(bus, 0x0eU, 0) == BM_STATUS_OK);
    assert(bm_dma8237_mask(dma) == 0);

    /* Master clear resets controller registers but not an external DREQ pin. */
    assert(write_port(bus, 0x0dU, 0xa5U) == BM_STATUS_OK);
    assert(bm_dma8237_command(dma) == 0);
    assert(bm_dma8237_mask(dma) == 0x0fU);
    assert(bm_dma8237_channel_state(dma, 2, &channel) == BM_STATUS_OK);
    assert(channel.base_address == 0x1234U);
    assert(channel.base_count == 0x01ffU);
    assert(channel.mode == 0x4aU);
    assert(channel.masked);
    assert(channel.requested);

    assert(read_port(bus, 0x09U, &value) == BM_STATUS_UNSUPPORTED);
    assert(bm_dma8237_set_dreq(dma, 4, 1) == BM_STATUS_INVALID_ARGUMENT);

    bm_dma8237_destroy(dma);
    bm_bus_destroy(bus);
    return 0;
}
