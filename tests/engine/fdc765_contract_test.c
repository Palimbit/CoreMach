/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/components/bus.h>
#include <blumach/components/dma8237.h>
#include <blumach/components/fdc765.h>
#include <blumach/components/floppy_drive.h>
#include <blumach/components/linear_memory.h>
#include <blumach/platforms/null_host.h>

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define SECTOR_SIZE 512U
#define MEDIA_BLOCKS 8U

typedef struct scripted_media {
    uint8_t bytes[MEDIA_BLOCKS * SECTOR_SIZE];
    bm_status_t read_status;
    bm_status_t write_status;
    uint64_t last_read_block;
    uint64_t last_write_block;
    unsigned int reads;
    unsigned int writes;
} scripted_media_t;

typedef struct irq_sink {
    int asserted;
    unsigned int edges;
} irq_sink_t;

typedef struct fdc_fixture {
    bm_host_services_t host;
    bm_bus_t *bus;
    bm_linear_memory_t *ram;
    bm_dma8237_t *dma;
    bm_floppy_drive_t *drive[3];
    bm_fdc765_t *fdc;
    scripted_media_t media;
    irq_sink_t irq;
} fdc_fixture_t;

static bm_status_t
media_read(void *context, uint64_t first_block, uint32_t block_count,
           uint8_t *destination)
{
    scripted_media_t *media = context;
    assert(block_count == 1U);
    assert(first_block < MEDIA_BLOCKS);
    ++media->reads;
    media->last_read_block = first_block;
    if (media->read_status != BM_STATUS_OK)
        return media->read_status;
    memcpy(destination, media->bytes + first_block * SECTOR_SIZE, SECTOR_SIZE);
    return BM_STATUS_OK;
}

static bm_status_t
media_write(void *context, uint64_t first_block, uint32_t block_count,
            const uint8_t *source)
{
    scripted_media_t *media = context;
    assert(block_count == 1U);
    assert(first_block < MEDIA_BLOCKS);
    ++media->writes;
    media->last_write_block = first_block;
    if (media->write_status != BM_STATUS_OK)
        return media->write_status;
    memcpy(media->bytes + first_block * SECTOR_SIZE, source, SECTOR_SIZE);
    return BM_STATUS_OK;
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
memory_write(bm_bus_t *bus, uint64_t address, uint8_t value)
{
    bm_bus_transaction_t transaction = {
        BM_ADDRESS_MEMORY, BM_BUS_WRITE, address, value, 1U, 1U, 0U,
        BM_ENDIAN_LITTLE, 0
    };
    assert(bm_bus_transact(bus, &transaction) == BM_STATUS_OK);
}

static uint8_t
memory_read(bm_bus_t *bus, uint64_t address)
{
    bm_bus_transaction_t transaction = {
        BM_ADDRESS_MEMORY, BM_BUS_READ, address, 0U, 1U, 1U, 0U,
        BM_ENDIAN_LITTLE, 0
    };
    assert(bm_bus_transact(bus, &transaction) == BM_STATUS_OK);
    return (uint8_t) transaction.value;
}

static void
program_dma(bm_bus_t *bus, uint16_t address, uint16_t count, uint8_t mode)
{
    io_write(bus, 0x0cU, 0U);
    io_write(bus, 0x04U, (uint8_t) address);
    io_write(bus, 0x04U, (uint8_t) (address >> 8U));
    io_write(bus, 0x05U, (uint8_t) count);
    io_write(bus, 0x05U, (uint8_t) (count >> 8U));
    io_write(bus, 0x0bU, mode);
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

static void
read_results(bm_bus_t *bus, uint8_t results[7])
{
    size_t index;
    for (index = 0U; index < 7U; ++index)
        results[index] = io_read(bus, 0x03f5U);
}

static uint8_t
drive_dor(unsigned int drive)
{
    assert(drive < 4U);
    return (uint8_t) (0x0cU | drive | (0x10U << drive));
}

static void
fixture_create(fdc_fixture_t *fixture)
{
    bm_linear_memory_config_t ram_config = {
        BM_ADDRESS_MEMORY, 0U, 0x10000U, 0, NULL, 0U
    };
    bm_dma8237_config_t dma_config = { 0U };
    bm_floppy_drive_config_t drive_config = {
        1, 1, 0, { 2U, 2U, 2U, SECTOR_SIZE },
        { &fixture->media, MEDIA_BLOCKS, SECTOR_SIZE, 0,
          media_read, media_write }
    };
    bm_floppy_drive_config_t absent_config = {
        1, 0, 0, { 2U, 2U, 2U, SECTOR_SIZE }, { 0 }
    };
    bm_fdc765_config_t fdc_config;
    size_t block;
    size_t byte;

    memset(fixture, 0, sizeof(*fixture));
    fixture->host = bm_null_host_services();
    fixture->media.read_status = BM_STATUS_OK;
    fixture->media.write_status = BM_STATUS_OK;
    for (block = 0U; block < MEDIA_BLOCKS; ++block) {
        for (byte = 0U; byte < SECTOR_SIZE; ++byte) {
            fixture->media.bytes[block * SECTOR_SIZE + byte] =
                (uint8_t) ((block << 4U) ^ byte);
        }
    }

    assert(bm_bus_create(&fixture->host, 8U, &fixture->bus) == BM_STATUS_OK);
    assert(bm_linear_memory_create(&fixture->host, fixture->bus, &ram_config,
                                   &fixture->ram) == BM_STATUS_OK);
    assert(bm_dma8237_create(&fixture->host, fixture->bus, &dma_config,
                             &fixture->dma) == BM_STATUS_OK);
    assert(bm_floppy_drive_create(&fixture->host, &drive_config,
                                  &fixture->drive[0]) == BM_STATUS_OK);
    assert(bm_floppy_drive_create(&fixture->host, &absent_config,
                                  &fixture->drive[1]) == BM_STATUS_OK);
    assert(bm_floppy_drive_create(&fixture->host, &drive_config,
                                  &fixture->drive[2]) == BM_STATUS_OK);
    memset(&fdc_config, 0, sizeof(fdc_config));
    fdc_config.io_base = 0x03f0U;
    fdc_config.dma_channel = 2U;
    fdc_config.dma = fixture->dma;
    fdc_config.drives[0] = fixture->drive[0];
    fdc_config.drives[1] = fixture->drive[1];
    fdc_config.drives[2] = fixture->drive[2];
    fdc_config.irq = capture_irq;
    fdc_config.irq_context = &fixture->irq;
    assert(bm_fdc765_create(&fixture->host, fixture->bus, &fdc_config,
                            &fixture->fdc) == BM_STATUS_OK);
}

static void
fixture_destroy(fdc_fixture_t *fixture)
{
    bm_fdc765_destroy(fixture->fdc);
    bm_floppy_drive_destroy(fixture->drive[2]);
    bm_floppy_drive_destroy(fixture->drive[1]);
    bm_floppy_drive_destroy(fixture->drive[0]);
    bm_dma8237_destroy(fixture->dma);
    bm_linear_memory_destroy(fixture->ram);
    bm_bus_destroy(fixture->bus);
}

static void
release_reset(fdc_fixture_t *fixture, unsigned int drive)
{
    size_t index;
    io_write(fixture->bus, 0x03f2U, drive_dor(drive));
    for (index = 0U; index < 4U; ++index) {
        send_command(fixture->bus, 0x08U, NULL, 0U);
        (void) io_read(fixture->bus, 0x03f5U);
        (void) io_read(fixture->bus, 0x03f5U);
    }
}

static void
test_missing_media_and_backend_errors(fdc_fixture_t *fixture)
{
    uint8_t params[8] = { 1U, 0U, 0U, 1U, 2U, 2U, 0x2aU, 0xffU };
    uint8_t results[7];

    release_reset(fixture, 1U);
    program_dma(fixture->bus, 0x1000U, 0x01ffU, 0x46U);
    send_command(fixture->bus, 0x46U, params, 8U);
    read_results(fixture->bus, results);
    assert((results[0] & 0x48U) == 0x48U);
    assert((results[1] & 0x04U) != 0U);
    assert(fixture->media.reads == 0U);

    params[0] = 2U;
    io_write(fixture->bus, 0x03f2U, drive_dor(2U));
    fixture->media.read_status = BM_STATUS_DEVICE_ERROR;
    program_dma(fixture->bus, 0x1000U, 0x01ffU, 0x46U);
    send_command(fixture->bus, 0x46U, params, 8U);
    read_results(fixture->bus, results);
    assert((results[0] & 0x40U) != 0U);
    assert((results[1] & 0x10U) != 0U);
    assert(fixture->media.reads == 1U);

    fixture->media.write_status = BM_STATUS_DEVICE_ERROR;
    program_dma(fixture->bus, 0x1200U, 0x01ffU, 0x4aU);
    send_command(fixture->bus, 0x45U, params, 8U);
    read_results(fixture->bus, results);
    assert((results[0] & 0x40U) != 0U);
    assert((results[1] & 0x10U) != 0U);
    assert(fixture->media.writes == 1U);
    fixture->media.read_status = BM_STATUS_OK;
    fixture->media.write_status = BM_STATUS_OK;
}

static void
test_short_dma_is_rejected(fdc_fixture_t *fixture)
{
    uint8_t params[8] = { 0U, 0U, 0U, 1U, 2U, 2U, 0x2aU, 0xffU };
    uint8_t results[7];

    io_write(fixture->bus, 0x03f2U, drive_dor(0U));
    program_dma(fixture->bus, 0x1800U, 0x00ffU, 0x46U);
    send_command(fixture->bus, 0x46U, params, 8U);
    read_results(fixture->bus, results);
    assert((results[0] & 0x40U) != 0U);
    assert((results[1] & 0x10U) != 0U);
}

static void
test_unimplemented_commands_are_rejected(fdc_fixture_t *fixture)
{
    static const uint8_t commands[] = {
        0x09U, /* Write deleted data. */
        0x0cU, /* Read deleted data. */
        0x0dU  /* Format track. */
    };
    size_t index;

    for (index = 0U; index < sizeof(commands); ++index) {
        send_command(fixture->bus, commands[index], NULL, 0U);
        assert(io_read(fixture->bus, 0x03f5U) == 0x80U);
        assert(io_read(fixture->bus, 0x03f4U) == 0x80U);
    }
}

static void
test_multi_track_crosses_to_second_head(fdc_fixture_t *fixture)
{
    uint8_t params[8] = { 0U, 0U, 0U, 2U, 2U, 2U, 0x2aU, 0xffU };
    uint8_t results[7];
    size_t index;

    fixture->media.reads = 0U;
    program_dma(fixture->bus, 0x2000U, 0x03ffU, 0x46U);
    send_command(fixture->bus, 0xc6U, params, 8U);
    read_results(fixture->bus, results);
    assert(results[0] == 0U && results[1] == 0U && results[2] == 0U);
    assert(results[3] == 0U && results[4] == 1U && results[5] == 1U);
    assert(fixture->media.reads == 2U);
    assert(fixture->media.last_read_block == 2U);
    for (index = 0U; index < SECTOR_SIZE; ++index) {
        assert(memory_read(fixture->bus, 0x2000U + index) ==
               fixture->media.bytes[SECTOR_SIZE + index]);
        assert(memory_read(fixture->bus, 0x2200U + index) ==
               fixture->media.bytes[2U * SECTOR_SIZE + index]);
    }
}

static void
test_reset_discards_partial_and_pending_state(fdc_fixture_t *fixture)
{
    bm_fdc765_state_t state;
    bm_dma8237_channel_state_t dma_state;
    uint8_t params[8] = { 1U, 0U, 0U, 1U, 2U, 2U, 0x2aU, 0xffU };

    send_command(fixture->bus, 0x46U, params, 3U);
    assert(bm_fdc765_state(fixture->fdc, &state) == BM_STATUS_OK);
    assert(state.command == 0x46U && state.main_status == 0x90U);
    bm_fdc765_reset(fixture->fdc);
    assert(bm_fdc765_state(fixture->fdc, &state) == BM_STATUS_OK);
    assert(state.digital_output == 0U && state.main_status == 0x80U);
    assert(state.command == 0U && state.result_count == 0U);
    assert(!state.interrupt_pending && !state.interrupt_asserted);

    release_reset(fixture, 1U);
    program_dma(fixture->bus, 0x3000U, 0x01ffU, 0x46U);
    send_command(fixture->bus, 0x46U, params, 8U);
    assert(fixture->irq.asserted);
    assert(bm_fdc765_state(fixture->fdc, &state) == BM_STATUS_OK);
    assert(state.result_count == 7U && state.interrupt_pending);
    bm_fdc765_reset(fixture->fdc);
    assert(!fixture->irq.asserted);
    assert(bm_fdc765_state(fixture->fdc, &state) == BM_STATUS_OK);
    assert(state.result_count == 0U && !state.interrupt_pending);
    assert(bm_dma8237_channel_state(fixture->dma, 2U, &dma_state) ==
           BM_STATUS_OK);
    assert(!dma_state.requested);
}

int
main(void)
{
    fdc_fixture_t fixture;
    size_t index;

    fixture_create(&fixture);
    for (index = 0U; index < SECTOR_SIZE; ++index)
        memory_write(fixture.bus, 0x1200U + index, (uint8_t) index);
    test_missing_media_and_backend_errors(&fixture);
    test_short_dma_is_rejected(&fixture);
    test_unimplemented_commands_are_rejected(&fixture);
    test_multi_track_crosses_to_second_head(&fixture);
    test_reset_discards_partial_and_pending_state(&fixture);
    fixture_destroy(&fixture);
    return 0;
}
