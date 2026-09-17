/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/components/bus.h>
#include <blumach/components/dma8237.h>
#include <blumach/components/linear_memory.h>
#include <blumach/components/xta.h>
#include <blumach/platforms/null_host.h>

#include <assert.h>
#include <stdint.h>
#include <string.h>

#define TEST_BLOCKS (2U * 2U * 17U)

typedef struct test_media {
    uint8_t bytes[TEST_BLOCKS][512];
    int writes;
} test_media_t;

static bm_status_t
media_read(void *context, uint64_t first, uint32_t count, uint8_t *destination)
{
    test_media_t *media = context;
    if ((first >= TEST_BLOCKS) || (count > TEST_BLOCKS - first))
        return BM_STATUS_INVALID_ARGUMENT;
    memcpy(destination, media->bytes[first], (size_t) count * 512U);
    return BM_STATUS_OK;
}

static bm_status_t
media_write(void *context, uint64_t first, uint32_t count, const uint8_t *source)
{
    test_media_t *media = context;
    if ((first >= TEST_BLOCKS) || (count > TEST_BLOCKS - first))
        return BM_STATUS_INVALID_ARGUMENT;
    memcpy(media->bytes[first], source, (size_t) count * 512U);
    media->writes += (int) count;
    return BM_STATUS_OK;
}

static void
irq_change(void *context, int asserted)
{
    int *irq = context;
    *irq = asserted;
}

static uint8_t
io_read(bm_bus_t *bus, uint16_t port)
{
    bm_bus_transaction_t transaction = {
        BM_ADDRESS_IO, BM_BUS_READ, port, 0U, 1U, 1U, 0U,
        BM_ENDIAN_LITTLE, 0U
    };
    assert(bm_bus_transact(bus, &transaction) == BM_STATUS_OK);
    return (uint8_t) transaction.value;
}

static void
io_write(bm_bus_t *bus, uint16_t port, uint8_t value)
{
    bm_bus_transaction_t transaction = {
        BM_ADDRESS_IO, BM_BUS_WRITE, port, value, 1U, 1U, 0U,
        BM_ENDIAN_LITTLE, 0U
    };
    assert(bm_bus_transact(bus, &transaction) == BM_STATUS_OK);
}

static void
send_command(bm_bus_t *bus, const uint8_t dcb[6])
{
    size_t index;
    io_write(bus, 0x0322U, 0U);
    assert(io_read(bus, 0x0321U) == 0x0dU);
    for (index = 0U; index < 6U; ++index)
        io_write(bus, 0x0320U, dcb[index]);
}

int
main(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_bus_t *bus = NULL;
    bm_dma8237_t *dma = NULL;
    bm_linear_memory_t *ram = NULL;
    bm_xta_t *xta = NULL;
    test_media_t media = { { { 0 } }, 0 };
    bm_dma8237_config_t dma_config = { 0x0000U };
    bm_linear_memory_config_t ram_config = {
        BM_ADDRESS_MEMORY, 0x1000U, 512U, 0, NULL, 0U
    };
    bm_xta_config_t config;
    bm_xta_state_t state;
    int irq = 0;
    size_t index;
    static const uint8_t read_dcb[6] = { 0x08U, 0U, 0U, 0U, 1U, 0U };
    static const uint8_t write_dcb[6] = { 0x0aU, 0U, 1U, 0U, 1U, 0U };
    static const uint8_t bad_dcb[6] = { 0x02U, 0U, 0U, 0U, 1U, 0U };
    static const uint8_t sense_dcb[6] = { 0x03U, 0U, 0U, 0U, 1U, 0U };

    for (index = 0U; index < 512U; ++index)
        media.bytes[0][index] = (uint8_t) index;
    config = (bm_xta_config_t) {
        .io_base = 0x0320U,
        .option_switches = 0xa5U,
        .dma_channel = 3U,
        .irq = irq_change,
        .irq_context = &irq,
        .drive_present = 1,
        .geometry = { 2U, 2U, 17U },
        .media = { &media, TEST_BLOCKS, 512U, 0, media_read, media_write }
    };
    assert(bm_bus_create(&host, 8U, &bus) == BM_STATUS_OK);
    assert(bm_dma8237_create(&host, bus, &dma_config, &dma) == BM_STATUS_OK);
    assert(bm_linear_memory_create(&host, bus, &ram_config, &ram) == BM_STATUS_OK);
    config.dma = dma;
    assert(bm_xta_create(&host, bus, &config, &xta) == BM_STATUS_OK);

    assert(io_read(bus, 0x0321U) == 0xffU);
    bm_xta_set_enabled(xta, 1);
    assert(io_read(bus, 0x0322U) == 0xa5U);
    io_write(bus, 0x0323U, 0x02U);
    send_command(bus, read_dcb);
    assert(io_read(bus, 0x0321U) == 0x0bU);
    for (index = 0U; index < 512U; ++index)
        assert(io_read(bus, 0x0320U) == (uint8_t) index);
    assert(irq == 1);
    assert(io_read(bus, 0x0320U) == 0U);
    assert(irq == 0);

    io_write(bus, 0x000cU, 0U);       /* Clear DMA byte pointer. */
    io_write(bus, 0x0006U, 0x00U);    /* Channel 3 address 1000h. */
    io_write(bus, 0x0006U, 0x10U);
    io_write(bus, 0x0007U, 0xffU);    /* 512-byte count. */
    io_write(bus, 0x0007U, 0x01U);
    io_write(bus, 0x000bU, 0x47U);    /* Single, device-to-memory, channel 3. */
    io_write(bus, 0x000aU, 0x03U);    /* Unmask channel 3. */
    io_write(bus, 0x0323U, 0x03U);    /* Enable XTA DMA and IRQ. */
    send_command(bus, read_dcb);
    assert(irq == 1);
    assert(io_read(bus, 0x0320U) == 0U);
    for (index = 0U; index < 512U; ++index) {
        uint8_t byte = 0U;
        assert(bm_linear_memory_peek(ram, 0x1000U + index, &byte) == BM_STATUS_OK);
        assert(byte == (uint8_t) index);
    }

    io_write(bus, 0x0323U, 0x02U);    /* Return to PIO with IRQ enabled. */
    send_command(bus, write_dcb);
    for (index = 0U; index < 512U; ++index)
        io_write(bus, 0x0320U, (uint8_t) (255U - index));
    assert(irq == 1);
    assert(io_read(bus, 0x0320U) == 0U);
    assert(media.writes == 1);
    for (index = 0U; index < 512U; ++index)
        assert(media.bytes[1][index] == (uint8_t) (255U - index));

    send_command(bus, bad_dcb);
    assert(io_read(bus, 0x0320U) == 0x02U);
    send_command(bus, sense_dcb);
    assert(io_read(bus, 0x0320U) == 0x20U);
    assert(io_read(bus, 0x0320U) == 0U);
    (void) io_read(bus, 0x0320U);
    (void) io_read(bus, 0x0320U);
    assert(io_read(bus, 0x0320U) == 0U);

    assert(bm_xta_state(xta, &state) == BM_STATUS_OK);
    assert(state.sense == 0U);
    bm_xta_set_enabled(xta, 0);
    assert(io_read(bus, 0x0321U) == 0xffU);
    bm_xta_destroy(xta);
    bm_linear_memory_destroy(ram);
    bm_dma8237_destroy(dma);
    bm_bus_destroy(bus);
    return 0;
}
