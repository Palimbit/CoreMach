/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/components/bus.h>
#include <blumach/components/dma8237.h>
#include <blumach/components/dma_page_registers.h>
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

static void
assert_page(bm_dma8237_t *dma, unsigned int channel, uint8_t page,
            uint32_t physical_address)
{
    bm_dma8237_channel_state_t state;
    assert(bm_dma8237_channel_state(dma, channel, &state) == BM_STATUS_OK);
    assert(state.page == page);
    assert(state.current_physical_address == physical_address);
}

int
main(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_bus_t *bus = NULL;
    bm_dma8237_t *dma = NULL;
    bm_dma_page_registers_t *pages = NULL;
    bm_dma8237_config_t dma_config = { 0x0000U };
    bm_dma_page_registers_config_t page_config = { 0x0080U, 0x0fU, NULL };
    uint8_t value = 0xffU;

    assert(bm_bus_create(&host, 2, &bus) == BM_STATUS_OK);
    assert(bm_dma8237_create(&host, bus, &dma_config, &dma) == BM_STATUS_OK);
    page_config.dma = dma;
    assert(bm_dma_page_registers_create(&host, bus, &page_config, &pages) ==
           BM_STATUS_OK);

    /* Establish distinct current offsets before supplying their upper nibbles. */
    assert(write_port(bus, 0x0cU, 0) == BM_STATUS_OK);
    assert(write_port(bus, 0x00U, 0x10U) == BM_STATUS_OK);
    assert(write_port(bus, 0x00U, 0x00U) == BM_STATUS_OK);
    assert(write_port(bus, 0x02U, 0x21U) == BM_STATUS_OK);
    assert(write_port(bus, 0x02U, 0x00U) == BM_STATUS_OK);
    assert(write_port(bus, 0x04U, 0x32U) == BM_STATUS_OK);
    assert(write_port(bus, 0x04U, 0x00U) == BM_STATUS_OK);
    assert(write_port(bus, 0x06U, 0x43U) == BM_STATUS_OK);
    assert(write_port(bus, 0x06U, 0x00U) == BM_STATUS_OK);

    /* PCS 86 firmware probes channels in the order 0, 1, 2, 3. */
    assert(write_port(bus, 0x87U, 0xfaU) == BM_STATUS_OK);
    assert(write_port(bus, 0x83U, 0x0bU) == BM_STATUS_OK);
    assert(write_port(bus, 0x81U, 0x0cU) == BM_STATUS_OK);
    assert(write_port(bus, 0x82U, 0x0dU) == BM_STATUS_OK);
    assert(read_port(bus, 0x87U, &value) == BM_STATUS_OK && value == 0x0aU);
    assert_page(dma, 0, 0x0aU, 0x000a0010U);
    assert_page(dma, 1, 0x0bU, 0x000b0021U);
    assert_page(dma, 2, 0x0cU, 0x000c0032U);
    assert_page(dma, 3, 0x0dU, 0x000d0043U);

    /* Reserved ports are still independent latches, not invented channels. */
    assert(write_port(bus, 0x80U, 0x07U) == BM_STATUS_OK);
    assert(read_port(bus, 0x80U, &value) == BM_STATUS_OK && value == 0x07U);
    assert_page(dma, 0, 0x0aU, 0x000a0010U);

    /* 8237 master clear cannot reset the physically external page latches. */
    assert(write_port(bus, 0x0dU, 0) == BM_STATUS_OK);
    assert_page(dma, 0, 0x0aU, 0x000a0010U);
    bm_dma_page_registers_reset(pages);
    assert(read_port(bus, 0x80U, &value) == BM_STATUS_OK && value == 0);
    assert_page(dma, 0, 0, 0x00000010U);
    assert_page(dma, 1, 0, 0x00000021U);
    assert_page(dma, 2, 0, 0x00000032U);
    assert_page(dma, 3, 0, 0x00000043U);

    bm_dma_page_registers_destroy(pages);
    bm_dma8237_destroy(dma);
    bm_bus_destroy(bus);
    return 0;
}
