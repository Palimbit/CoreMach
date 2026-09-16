/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/components/bus.h>
#include <blumach/components/dma8237.h>
#include <blumach/components/fdc765.h>
#include <blumach/components/floppy_drive.h>
#include <blumach/components/linear_memory.h>
#include <blumach/platforms/null_host.h>

#include <assert.h>
#include <string.h>

typedef struct memory_media {
    uint8_t bytes[80U * 2U * 9U * 512U];
} memory_media_t;

typedef struct guarded_media {
    unsigned int reads;
    unsigned int writes;
} guarded_media_t;

typedef struct irq_sink {
    int asserted;
    unsigned int edges;
} irq_sink_t;

static bm_status_t
media_read(void *context, uint64_t first_block, uint32_t block_count,
           uint8_t *destination)
{
    memory_media_t *media = context;
    memcpy(destination, media->bytes + first_block * 512U,
           (size_t) block_count * 512U);
    return BM_STATUS_OK;
}

static bm_status_t
media_write(void *context, uint64_t first_block, uint32_t block_count,
            const uint8_t *source)
{
    memory_media_t *media = context;
    memcpy(media->bytes + first_block * 512U, source,
           (size_t) block_count * 512U);
    return BM_STATUS_OK;
}

static bm_status_t
guarded_media_read(void *context, uint64_t first_block, uint32_t block_count,
                   uint8_t *destination)
{
    guarded_media_t *media = context;
    (void) first_block;
    (void) block_count;
    (void) destination;
    ++media->reads;
    return BM_STATUS_DEVICE_ERROR;
}

static bm_status_t
guarded_media_write(void *context, uint64_t first_block, uint32_t block_count,
                    const uint8_t *source)
{
    guarded_media_t *media = context;
    (void) first_block;
    (void) block_count;
    (void) source;
    ++media->writes;
    return BM_STATUS_DEVICE_ERROR;
}

static void
capture_irq(void *context, int asserted)
{
    irq_sink_t *sink = context;
    if (asserted && !sink->asserted)
        ++sink->edges;
    sink->asserted = asserted;
}

static void
io_write(bm_bus_t *bus, uint16_t port, uint8_t value)
{
    bm_bus_transaction_t transaction = {
        BM_ADDRESS_IO, BM_BUS_WRITE, port, value, 1U, 1U, 0U,
        BM_ENDIAN_LITTLE, 0
    };
    assert(bm_bus_transact(bus, &transaction) == BM_STATUS_OK);
}

static uint8_t
io_read(bm_bus_t *bus, uint16_t port)
{
    bm_bus_transaction_t transaction = {
        BM_ADDRESS_IO, BM_BUS_READ, port, 0U, 1U, 1U, 0U,
        BM_ENDIAN_LITTLE, 0
    };
    assert(bm_bus_transact(bus, &transaction) == BM_STATUS_OK);
    return (uint8_t) transaction.value;
}

static void
program_dma_read(bm_bus_t *bus, uint16_t address, uint16_t count)
{
    io_write(bus, 0x0cU, 0U);
    io_write(bus, 0x04U, (uint8_t) address);
    io_write(bus, 0x04U, (uint8_t) (address >> 8U));
    io_write(bus, 0x05U, (uint8_t) count);
    io_write(bus, 0x05U, (uint8_t) (count >> 8U));
    io_write(bus, 0x0bU, 0x46U);
    io_write(bus, 0x0aU, 0x02U);
}

static void
program_dma_write(bm_bus_t *bus, uint16_t address, uint16_t count)
{
    io_write(bus, 0x0cU, 0U);
    io_write(bus, 0x04U, (uint8_t) address);
    io_write(bus, 0x04U, (uint8_t) (address >> 8U));
    io_write(bus, 0x05U, (uint8_t) count);
    io_write(bus, 0x05U, (uint8_t) (count >> 8U));
    io_write(bus, 0x0bU, 0x4aU);
    io_write(bus, 0x0aU, 0x02U);
}

static void
send_command(bm_bus_t *bus, uint8_t command,
             const uint8_t *params, size_t count)
{
    size_t index;
    io_write(bus, 0x03f5U, command);
    for (index = 0U; index < count; ++index)
        io_write(bus, 0x03f5U, params[index]);
}

int
main(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_bus_t *bus = NULL;
    bm_linear_memory_t *ram = NULL;
    bm_dma8237_t *dma = NULL;
    bm_floppy_drive_t *drive = NULL;
    bm_floppy_drive_t *oversized_drive = NULL;
    bm_floppy_drive_t *writable_drive = NULL;
    bm_fdc765_t *fdc = NULL;
    static memory_media_t media = { { 0 } };
    guarded_media_t guarded_media = { 0U, 0U };
    irq_sink_t irq = { 0, 0U };
    bm_linear_memory_config_t ram_config = {
        BM_ADDRESS_MEMORY, 0U, 0x10000U, 0, NULL, 0U
    };
    bm_dma8237_config_t dma_config = { 0U };
    bm_floppy_drive_config_t drive_config = {
        1, 1, 1, { 80U, 2U, 9U, 512U },
        { &media, 1440U, 512U, 1, media_read, NULL }
    };
    bm_floppy_drive_config_t oversized_drive_config = {
        1, 1, 0, { 1U, 1U, 1U, 8192U },
        { &guarded_media, 1U, 8192U, 0,
          guarded_media_read, guarded_media_write }
    };
    bm_floppy_drive_config_t writable_drive_config = {
        1, 1, 0, { 80U, 2U, 9U, 512U },
        { &media, 1440U, 512U, 0, media_read, media_write }
    };
    bm_fdc765_config_t fdc_config;
    bm_bus_transaction_t transaction;
    uint8_t reset_sense[2];
    uint8_t read_params[8] = { 0U, 0U, 0U, 1U, 2U, 9U, 0x2aU, 0xffU };
    uint8_t oversized_params[8] = {
        1U, 0U, 0U, 1U, 6U, 1U, 0x2aU, 0xffU
    };
    uint8_t write_params[8] = {
        2U, 0U, 0U, 2U, 2U, 2U, 0x2aU, 0xffU
    };
    uint8_t results[7];
    size_t index;

    for (index = 0U; index < 512U; ++index)
        media.bytes[index] = (uint8_t) (index ^ 0x5aU);

    assert(bm_bus_create(&host, 8U, &bus) == BM_STATUS_OK);
    assert(bm_linear_memory_create(&host, bus, &ram_config, &ram) == BM_STATUS_OK);
    assert(bm_dma8237_create(&host, bus, &dma_config, &dma) == BM_STATUS_OK);
    assert(bm_floppy_drive_create(&host, &drive_config, &drive) == BM_STATUS_OK);
    assert(bm_floppy_drive_create(&host, &oversized_drive_config,
                                  &oversized_drive) == BM_STATUS_OK);
    assert(bm_floppy_drive_create(&host, &writable_drive_config,
                                  &writable_drive) == BM_STATUS_OK);
    memset(&fdc_config, 0, sizeof(fdc_config));
    fdc_config.io_base = 0x03f0U;
    fdc_config.dma_channel = 2U;
    fdc_config.disk_change_active_low = 1;
    fdc_config.dma = dma;
    fdc_config.drives[0] = drive;
    fdc_config.drives[1] = oversized_drive;
    fdc_config.drives[2] = writable_drive;
    fdc_config.irq = capture_irq;
    fdc_config.irq_context = &irq;
    assert(bm_fdc765_create(&host, bus, &fdc_config, &fdc) == BM_STATUS_OK);

    assert(io_read(bus, 0x03f4U) == 0x80U);
    io_write(bus, 0x03f2U, 0x1cU); /* Release reset, DMA/IRQ, motor 0. */
    assert(irq.asserted && irq.edges == 1U);
    for (index = 0U; index < 4U; ++index) {
        send_command(bus, 0x08U, NULL, 0U);
        reset_sense[0] = io_read(bus, 0x03f5U);
        reset_sense[1] = io_read(bus, 0x03f5U);
        assert(reset_sense[0] == (uint8_t) (0xc0U | index));
        assert(reset_sense[1] == 0U);
    }

    /* Deleted-data operations are deliberately unsupported, not approximated. */
    send_command(bus, 0x09U, NULL, 0U);
    assert(io_read(bus, 0x03f5U) == 0x80U);

    program_dma_read(bus, 0x2000U, 0x01ffU);
    send_command(bus, 0x46U, read_params, 8U);
    assert(irq.asserted && irq.edges == 2U);
    for (index = 0U; index < 7U; ++index)
        results[index] = io_read(bus, 0x03f5U);
    assert(!irq.asserted);
    assert(results[0] == 0U && results[1] == 0U && results[2] == 0U);
    assert(results[3] == 0U && results[4] == 0U && results[5] == 1U &&
           results[6] == 2U);
    transaction = (bm_bus_transaction_t) {
        BM_ADDRESS_MEMORY, BM_BUS_READ, 0x2000U, 0U, 1U, 1U, 0U,
        BM_ENDIAN_LITTLE, 0
    };
    for (index = 0U; index < 512U; ++index) {
        transaction.address = 0x2000U + index;
        assert(bm_bus_transact(bus, &transaction) == BM_STATUS_OK);
        assert((uint8_t) transaction.value == media.bytes[index]);
    }

    /* A protected medium must report write-protect without consuming DMA. */
    send_command(bus, 0x45U, read_params, 8U);
    for (index = 0U; index < 7U; ++index)
        results[index] = io_read(bus, 0x03f5U);
    assert((results[0] & 0x40U) != 0U);
    assert((results[1] & 0x02U) != 0U);

    /* A normal 512-byte sector also succeeds through the FDC write path. */
    transaction = (bm_bus_transaction_t) {
        BM_ADDRESS_MEMORY, BM_BUS_WRITE, 0x2200U, 0U, 1U, 1U, 0U,
        BM_ENDIAN_LITTLE, 0
    };
    for (index = 0U; index < 512U; ++index) {
        transaction.address = 0x2200U + index;
        transaction.value = (uint8_t) (index ^ 0xa5U);
        assert(bm_bus_transact(bus, &transaction) == BM_STATUS_OK);
    }
    program_dma_write(bus, 0x2200U, 0x01ffU);
    io_write(bus, 0x03f2U, 0x4eU); /* Drive 2, DMA/IRQ, motor 2. */
    send_command(bus, 0x45U, write_params, 8U);
    for (index = 0U; index < 7U; ++index)
        results[index] = io_read(bus, 0x03f5U);
    assert(results[0] == 2U && results[1] == 0U && results[2] == 0U);
    for (index = 0U; index < 512U; ++index)
        assert(media.bytes[512U + index] == (uint8_t) (index ^ 0xa5U));

    /* Oversized sectors are rejected before DMA or media callbacks. */
    assert(BM_FDC765_MAX_SECTOR_SIZE == 4096U);
    io_write(bus, 0x03f2U, 0x2dU); /* Drive 1, DMA/IRQ, motor 1. */
    send_command(bus, 0x46U, oversized_params, 8U);
    for (index = 0U; index < 7U; ++index)
        results[index] = io_read(bus, 0x03f5U);
    assert((results[0] & 0x40U) != 0U);
    assert((results[1] & 0x04U) != 0U);
    assert(guarded_media.reads == 0U && guarded_media.writes == 0U);

    send_command(bus, 0x45U, oversized_params, 8U);
    for (index = 0U; index < 7U; ++index)
        results[index] = io_read(bus, 0x03f5U);
    assert((results[0] & 0x40U) != 0U);
    assert((results[1] & 0x04U) != 0U);
    assert(guarded_media.reads == 0U && guarded_media.writes == 0U);

    bm_fdc765_destroy(fdc);
    bm_floppy_drive_destroy(writable_drive);
    bm_floppy_drive_destroy(oversized_drive);
    bm_floppy_drive_destroy(drive);
    bm_dma8237_destroy(dma);
    bm_linear_memory_destroy(ram);
    bm_bus_destroy(bus);

    /* A writable block backend is valid independently of the FDC path. */
    {
        bm_block_media_t writable = {
            &media, 1440U, 512U, 0, media_read, media_write
        };
        uint8_t sector[512] = { 0xa5U };
        assert(bm_block_media_write(&writable, 1U, 1U, sector) == BM_STATUS_OK);
        assert(media.bytes[512U] == 0xa5U);
    }
    return 0;
}
