/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/components/bus.h>
#include <blumach/components/dma8237.h>
#include <blumach/components/linear_memory.h>
#include <blumach/components/xta.h>
#include <blumach/components/xta_clock.h>
#include <blumach/platforms/null_host.h>

#include <assert.h>
#include <stdint.h>
#include <string.h>

typedef struct test_media {
    uint8_t bytes[512];
    uint64_t reads;
} test_media_t;

static bm_status_t
media_read(void *context, uint64_t first, uint32_t count,
           uint8_t *destination)
{
    test_media_t *media = context;

    if ((first != 0U) || (count != 1U))
        return BM_STATUS_INVALID_ARGUMENT;
    memcpy(destination, media->bytes, sizeof(media->bytes));
    ++media->reads;
    return BM_STATUS_OK;
}

static bm_status_t
media_write(void *context, uint64_t first, uint32_t count,
            const uint8_t *source)
{
    (void) context;
    (void) first;
    (void) count;
    (void) source;
    return BM_STATUS_READ_ONLY;
}

static void
irq_change(void *context, int asserted)
{
    *(int *) context = asserted;
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
send_read_command(bm_bus_t *bus)
{
    static const uint8_t dcb[6] = { 0x08U, 0U, 0U, 0U, 1U, 0U };
    size_t index;

    io_write(bus, 0x0322U, 0U);
    for (index = 0U; index < sizeof(dcb); ++index)
        io_write(bus, 0x0320U, dcb[index]);
}

int
main(void)
{
    static const bm_clock_rate_t microsecond_rate = {
        UINT64_C(1000000), 1U
    };
    bm_host_services_t host = bm_null_host_services();
    bm_engine_config_t engine_config = { 1U, 1U, 1U };
    bm_engine_t *engine = NULL;
    bm_bus_t *bus = NULL;
    bm_dma8237_t *dma = NULL;
    bm_linear_memory_t *ram = NULL;
    bm_xta_t *xta = NULL;
    bm_dma8237_config_t dma_config = { 0x0000U };
    bm_linear_memory_config_t ram_config = {
        BM_ADDRESS_MEMORY, 0x1000U, 512U,
        BM_LINEAR_MEMORY_WRITABLE, NULL, 0U
    };
    test_media_t media = { { 0 }, 0U };
    bm_xta_config_t xta_config;
    bm_xta_state_t state;
    int irq = 0;
    size_t index;

    for (index = 0U; index < sizeof(media.bytes); ++index)
        media.bytes[index] = (uint8_t) index;
    assert(bm_engine_create_clocked(&host, &engine_config, &engine) ==
           BM_STATUS_OK);
    assert(bm_bus_create(&host, 8U, &bus) == BM_STATUS_OK);
    assert(bm_dma8237_create(&host, bus, &dma_config, &dma) == BM_STATUS_OK);
    assert(bm_linear_memory_create(&host, bus, &ram_config, &ram) ==
           BM_STATUS_OK);
    xta_config = (bm_xta_config_t) {
        .io_base = 0x0320U,
        .option_switches = 0xffU,
        .dma_channel = 3U,
        .dma = dma,
        .irq = irq_change,
        .irq_context = &irq,
        .drive_present = 1,
        .geometry = { 1U, 1U, 1U },
        .media = { &media, 1U, 512U, 1, media_read, media_write }
    };
    assert(bm_xta_create(&host, bus, &xta_config, &xta) == BM_STATUS_OK);
    assert(bm_xta_attach_service_clock(engine, xta, &microsecond_rate, 32U,
                                        NULL) == BM_STATUS_OK);
    assert(bm_xta_attach_service_clock(engine, xta, &microsecond_rate, 32U,
                                        NULL) == BM_STATUS_INVALID_STATE);

    bm_xta_set_enabled(xta, 1);
    io_write(bus, 0x000cU, 0U);
    io_write(bus, 0x0006U, 0x00U);
    io_write(bus, 0x0006U, 0x10U);
    io_write(bus, 0x0007U, 0xffU);
    io_write(bus, 0x0007U, 0x01U);
    io_write(bus, 0x000bU, 0x47U);
    io_write(bus, 0x0323U, 0x03U);
    send_read_command(bus);
    assert(bm_xta_service_pending(xta));

    assert(bm_engine_run_for(engine, UINT64_C(31000)) == BM_STATUS_OK);
    assert(media.reads == 0U);
    assert(irq == 0);
    assert(bm_engine_run_for(engine, UINT64_C(1000)) == BM_STATUS_OK);
    assert(bm_xta_service_pending(xta));
    assert(media.reads == 0U); /* The still-masked channel schedules one retry. */

    io_write(bus, 0x000aU, 0x03U);
    assert(bm_engine_run_for(engine, UINT64_C(31000)) == BM_STATUS_OK);
    assert(media.reads == 0U);
    assert(bm_engine_run_for(engine, UINT64_C(1000)) == BM_STATUS_OK);
    assert(!bm_xta_service_pending(xta));
    assert(media.reads == 1U);
    assert(irq == 1);
    assert(io_read(bus, 0x0320U) == 0U);
    for (index = 0U; index < sizeof(media.bytes); ++index) {
        uint8_t byte = 0U;

        assert(bm_linear_memory_peek(ram, 0x1000U + index, &byte) ==
               BM_STATUS_OK);
        assert(byte == (uint8_t) index);
    }
    assert(bm_xta_state(xta, &state) == BM_STATUS_OK);
    assert(state.read_operations == 1U);

    /* With no pending command the source remains idle for the whole run. */
    assert(bm_engine_run_for(engine, UINT64_C(1000000)) == BM_STATUS_OK);
    assert(media.reads == 1U);
    assert(bm_engine_now(engine) == UINT64_C(1064000));

    bm_engine_destroy(engine);
    bm_xta_destroy(xta);
    bm_linear_memory_destroy(ram);
    bm_dma8237_destroy(dma);
    bm_bus_destroy(bus);
    return 0;
}
