/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/components/bus.h>
#include <blumach/components/pvga1a.h>
#include <blumach/platforms/null_host.h>

#include <assert.h>
#include <stdint.h>

static bm_status_t
io_write(bm_bus_t *bus, uint16_t port, uint8_t value)
{
    bm_bus_transaction_t transaction = {
        BM_ADDRESS_IO, BM_BUS_WRITE, port, value, 1, 1, 0, BM_ENDIAN_LITTLE, 0
    };
    return bm_bus_transact(bus, &transaction);
}

static bm_status_t
io_read(bm_bus_t *bus, uint16_t port, uint8_t *value)
{
    bm_bus_transaction_t transaction = {
        BM_ADDRESS_IO, BM_BUS_READ, port, 0, 1, 1, 0, BM_ENDIAN_LITTLE, 0
    };
    bm_status_t status = bm_bus_transact(bus, &transaction);
    if (status == BM_STATUS_OK)
        *value = (uint8_t) transaction.value;
    return status;
}

static bm_status_t
memory_write(bm_bus_t *bus, uint32_t address, uint8_t value)
{
    bm_bus_transaction_t transaction = {
        BM_ADDRESS_MEMORY, BM_BUS_WRITE, address, value, 1, 1, 0,
        BM_ENDIAN_LITTLE, 0
    };
    return bm_bus_transact(bus, &transaction);
}

int
main(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_bus_t *bus = NULL;
    bm_pvga1a_t *video = NULL;
    bm_pvga1a_config_t config = { BM_PVGA1A_VRAM_SIZE };
    uint8_t value = 0;
    unsigned int plane;

    assert(bm_bus_create(&host, 2, &bus) == BM_STATUS_OK);
    assert(bm_pvga1a_create(&host, bus, &config, &video) == BM_STATUS_OK);

    /* Paradise extended graphics registers remain locked until 0Fh = 05h. */
    assert(io_write(bus, 0x03ceU, 0x09U) == BM_STATUS_OK);
    assert(io_write(bus, 0x03cfU, 0x5aU) == BM_STATUS_OK);
    assert(bm_pvga1a_inspect_register(video, BM_PVGA1A_GRAPHICS, 9,
                                      &value) == BM_STATUS_OK && value == 0);
    assert(io_write(bus, 0x03ceU, 0x0fU) == BM_STATUS_OK);
    assert(io_write(bus, 0x03cfU, 0x05U) == BM_STATUS_OK);
    assert(io_read(bus, 0x03cfU, &value) == BM_STATUS_OK && value == 0x85U);
    assert(io_write(bus, 0x03ceU, 0x09U) == BM_STATUS_OK);
    assert(io_write(bus, 0x03cfU, 0x5aU) == BM_STATUS_OK);
    assert(bm_pvga1a_inspect_register(video, BM_PVGA1A_GRAPHICS, 9,
                                      &value) == BM_STATUS_OK && value == 0x5aU);

    /* Planar write mode 0 writes every enabled plane through the bit mask. */
    assert(io_write(bus, 0x03c4U, 2U) == BM_STATUS_OK);
    assert(io_write(bus, 0x03c5U, 0x0fU) == BM_STATUS_OK);
    assert(io_write(bus, 0x03ceU, 8U) == BM_STATUS_OK);
    assert(io_write(bus, 0x03cfU, 0xffU) == BM_STATUS_OK);
    assert(memory_write(bus, 0x000a0123U, 0xa5U) == BM_STATUS_OK);
    for (plane = 0; plane < 4U; ++plane)
        assert(bm_pvga1a_inspect_vram(video, plane, 0x0123U,
                                      &value) == BM_STATUS_OK && value == 0xa5U);

    /* Sequencer map-mask selection keeps the remaining planes unchanged. */
    assert(io_write(bus, 0x03c5U, 0x04U) == BM_STATUS_OK);
    assert(memory_write(bus, 0x000a0123U, 0x3cU) == BM_STATUS_OK);
    assert(bm_pvga1a_inspect_vram(video, 2, 0x0123U,
                                  &value) == BM_STATUS_OK && value == 0x3cU);
    assert(bm_pvga1a_inspect_vram(video, 1, 0x0123U,
                                  &value) == BM_STATUS_OK && value == 0xa5U);

    /* Input status is deterministic, changes phase and resets attribute FF. */
    assert(io_read(bus, 0x03daU, &value) == BM_STATUS_OK && value == 0x09U);
    assert(io_read(bus, 0x03daU, &value) == BM_STATUS_OK && value == 0U);
    assert(io_write(bus, 0x03c0U, 0x12U) == BM_STATUS_OK);
    assert(io_write(bus, 0x03c0U, 0x34U) == BM_STATUS_OK);
    assert(bm_pvga1a_inspect_register(video, BM_PVGA1A_ATTRIBUTE, 0x12U,
                                      &value) == BM_STATUS_OK && value == 0x34U);

    bm_pvga1a_destroy(video);
    bm_bus_destroy(bus);
    return 0;
}
