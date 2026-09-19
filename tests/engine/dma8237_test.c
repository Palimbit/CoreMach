/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/components/bus.h>
#include <blumach/components/dma8237.h>
#include <blumach/components/linear_memory.h>
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

    assert(bm_bus_create(&host, 2, &bus) == BM_STATUS_OK);
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

    assert(read_port(bus, 0x09U, &value) == BM_STATUS_UNMAPPED);
    assert(bm_dma8237_set_dreq(dma, 4, 1) == BM_STATUS_INVALID_ARGUMENT);

    {
        bm_linear_memory_t *memory = NULL;
        bm_linear_memory_config_t memory_config = {
            BM_ADDRESS_MEMORY, 0U, 0x10000U,
            BM_LINEAR_MEMORY_WRITABLE, NULL, 0U
        };
        bm_bus_transaction_t transaction;
        int terminal = 0;
        uint8_t transferred_value = 0U;

        /* The first DMA instance already owns the I/O mapping, so memory can
         * share the same bus and exercise real device-facing transfers. */
        assert(bm_linear_memory_create(&host, bus, &memory_config, &memory) ==
               BM_STATUS_OK);
        bm_dma8237_reset(dma);
        assert(write_port(bus, 0x0cU, 0U) == BM_STATUS_OK);
        assert(write_port(bus, 0x04U, 0x00U) == BM_STATUS_OK);
        assert(write_port(bus, 0x04U, 0x20U) == BM_STATUS_OK);
        assert(write_port(bus, 0x05U, 0x03U) == BM_STATUS_OK);
        assert(write_port(bus, 0x05U, 0x00U) == BM_STATUS_OK);
        assert(write_port(bus, 0x0bU, 0x46U) == BM_STATUS_OK);
        assert(write_port(bus, 0x0aU, 0x02U) == BM_STATUS_OK);
        assert(bm_dma8237_set_dreq(dma, 2U, 1) == BM_STATUS_OK);
        assert(bm_dma8237_device_write(dma, 2U, 0x11U, &terminal) == BM_STATUS_OK);
        assert(!terminal);
        assert(bm_dma8237_device_write(dma, 2U, 0x22U, &terminal) == BM_STATUS_OK);
        assert(!terminal);
        assert(bm_dma8237_device_write(dma, 2U, 0x33U, &terminal) == BM_STATUS_OK);
        assert(!terminal);
        assert(bm_dma8237_device_write(dma, 2U, 0x44U, &terminal) == BM_STATUS_OK);
        assert(terminal);
        transaction = (bm_bus_transaction_t) {
            BM_ADDRESS_MEMORY, BM_BUS_READ, 0x2000U, 0U, 4U, 1U, 0U,
            BM_ENDIAN_LITTLE, 0
        };
        assert(bm_bus_transact(bus, &transaction) == BM_STATUS_OK);
        assert(transaction.value == UINT64_C(0x44332211));
        assert(bm_dma8237_channel_state(dma, 2U, &channel) == BM_STATUS_OK);
        assert(channel.current_address == 0x2004U);
        assert(channel.current_count == 0xffffU);
        assert(channel.terminal_count);

        assert(bm_dma8237_set_dreq(dma, 2U, 0) == BM_STATUS_OK);
        assert(write_port(bus, 0x0cU, 0U) == BM_STATUS_OK);
        assert(write_port(bus, 0x04U, 0x00U) == BM_STATUS_OK);
        assert(write_port(bus, 0x04U, 0x20U) == BM_STATUS_OK);
        assert(write_port(bus, 0x05U, 0x00U) == BM_STATUS_OK);
        assert(write_port(bus, 0x05U, 0x00U) == BM_STATUS_OK);
        assert(write_port(bus, 0x0bU, 0x4aU) == BM_STATUS_OK);
        assert(write_port(bus, 0x0aU, 0x02U) == BM_STATUS_OK);
        assert(bm_dma8237_set_dreq(dma, 2U, 1) == BM_STATUS_OK);
        assert(bm_dma8237_device_read(dma, 2U, &transferred_value, &terminal) ==
               BM_STATUS_OK);
        assert(transferred_value == 0x11U && terminal);
        bm_linear_memory_destroy(memory);
    }

    bm_dma8237_destroy(dma);
    bm_bus_destroy(bus);
    return 0;
}
