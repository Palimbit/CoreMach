/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "fdc765_test_harness.h"

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
    fdc765_test_machine_t machine;
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
fixture_create(fdc_fixture_t *fixture)
{
    bm_floppy_drive_config_t drive_config = {
        1, 1, 0, { 2U, 2U, 2U, SECTOR_SIZE },
        { &fixture->media, MEDIA_BLOCKS, SECTOR_SIZE, 0,
          media_read, media_write }
    };
    bm_floppy_drive_config_t absent_config = {
        1, 0, 0, { 2U, 2U, 2U, SECTOR_SIZE }, { 0 }
    };
    fdc765_test_config_t config = { 0 };
    size_t block;
    size_t byte;

    memset(fixture, 0, sizeof(*fixture));
    fixture->media.read_status = BM_STATUS_OK;
    fixture->media.write_status = BM_STATUS_OK;
    for (block = 0U; block < MEDIA_BLOCKS; ++block) {
        for (byte = 0U; byte < SECTOR_SIZE; ++byte) {
            fixture->media.bytes[block * SECTOR_SIZE + byte] =
                (uint8_t) ((block << 4U) ^ byte);
        }
    }

    config.drive_configs[0] = &drive_config;
    config.drive_configs[1] = &absent_config;
    config.drive_configs[2] = &drive_config;
    config.irq = capture_irq;
    config.irq_context = &fixture->irq;
    fdc765_test_machine_create(&fixture->machine, &config);
}

static void
fixture_destroy(fdc_fixture_t *fixture)
{
    fdc765_test_machine_destroy(&fixture->machine);
}

static void
release_reset(fdc_fixture_t *fixture, unsigned int drive)
{
    size_t index;
    fdc765_test_io_write(&fixture->machine, 0x03f2U,
                         fdc765_test_drive_dor(drive));
    for (index = 0U; index < 4U; ++index) {
        fdc765_test_send_command(&fixture->machine, 0x08U, NULL, 0U);
        (void) fdc765_test_io_read(&fixture->machine, 0x03f5U);
        (void) fdc765_test_io_read(&fixture->machine, 0x03f5U);
    }
}

static void
test_missing_media_and_backend_errors(fdc_fixture_t *fixture)
{
    uint8_t params[8] = { 1U, 0U, 0U, 1U, 2U, 2U, 0x2aU, 0xffU };
    uint8_t results[7];

    release_reset(fixture, 1U);
    fdc765_test_program_dma(&fixture->machine, 0x1000U, 0x01ffU, 0x46U);
    fdc765_test_send_command(&fixture->machine, 0x46U, params, 8U);
    fdc765_test_read_results(&fixture->machine, results, 7U);
    assert((results[0] & 0x48U) == 0x48U);
    assert((results[1] & 0x04U) != 0U);
    assert(fixture->media.reads == 0U);

    params[0] = 2U;
    fdc765_test_io_write(&fixture->machine, 0x03f2U,
                         fdc765_test_drive_dor(2U));
    fixture->media.read_status = BM_STATUS_DEVICE_ERROR;
    fdc765_test_program_dma(&fixture->machine, 0x1000U, 0x01ffU, 0x46U);
    fdc765_test_send_command(&fixture->machine, 0x46U, params, 8U);
    fdc765_test_read_results(&fixture->machine, results, 7U);
    assert((results[0] & 0x40U) != 0U);
    assert((results[1] & 0x10U) != 0U);
    assert(fixture->media.reads == 1U);

    fixture->media.write_status = BM_STATUS_DEVICE_ERROR;
    fdc765_test_program_dma(&fixture->machine, 0x1200U, 0x01ffU, 0x4aU);
    fdc765_test_send_command(&fixture->machine, 0x45U, params, 8U);
    fdc765_test_read_results(&fixture->machine, results, 7U);
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

    fdc765_test_io_write(&fixture->machine, 0x03f2U,
                         fdc765_test_drive_dor(0U));
    fdc765_test_program_dma(&fixture->machine, 0x1800U, 0x00ffU, 0x46U);
    fdc765_test_send_command(&fixture->machine, 0x46U, params, 8U);
    fdc765_test_read_results(&fixture->machine, results, 7U);
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
        fdc765_test_send_command(&fixture->machine, commands[index], NULL, 0U);
        assert(fdc765_test_io_read(&fixture->machine, 0x03f5U) == 0x80U);
        assert(fdc765_test_io_read(&fixture->machine, 0x03f4U) == 0x80U);
    }
}

static void
test_multi_track_crosses_to_second_head(fdc_fixture_t *fixture)
{
    uint8_t params[8] = { 0U, 0U, 0U, 2U, 2U, 2U, 0x2aU, 0xffU };
    uint8_t results[7];
    size_t index;

    fixture->media.reads = 0U;
    fdc765_test_program_dma(&fixture->machine, 0x2000U, 0x03ffU, 0x46U);
    fdc765_test_send_command(&fixture->machine, 0xc6U, params, 8U);
    fdc765_test_read_results(&fixture->machine, results, 7U);
    assert(results[0] == 0U && results[1] == 0U && results[2] == 0U);
    assert(results[3] == 0U && results[4] == 1U && results[5] == 1U);
    assert(fixture->media.reads == 2U);
    assert(fixture->media.last_read_block == 2U);
    for (index = 0U; index < SECTOR_SIZE; ++index) {
        assert(fdc765_test_memory_read(&fixture->machine, 0x2000U + index) ==
               fixture->media.bytes[SECTOR_SIZE + index]);
        assert(fdc765_test_memory_read(&fixture->machine, 0x2200U + index) ==
               fixture->media.bytes[2U * SECTOR_SIZE + index]);
    }
}

static void
test_reset_discards_partial_and_pending_state(fdc_fixture_t *fixture)
{
    bm_fdc765_state_t state;
    bm_dma8237_channel_state_t dma_state;
    uint8_t params[8] = { 1U, 0U, 0U, 1U, 2U, 2U, 0x2aU, 0xffU };

    fdc765_test_send_command(&fixture->machine, 0x46U, params, 3U);
    assert(bm_fdc765_state(fixture->machine.fdc, &state) == BM_STATUS_OK);
    assert(state.command == 0x46U && state.main_status == 0x90U);
    bm_fdc765_reset(fixture->machine.fdc);
    assert(bm_fdc765_state(fixture->machine.fdc, &state) == BM_STATUS_OK);
    assert(state.digital_output == 0U && state.main_status == 0x80U);
    assert(state.command == 0U && state.result_count == 0U);
    assert(!state.interrupt_pending && !state.interrupt_asserted);

    release_reset(fixture, 1U);
    fdc765_test_program_dma(&fixture->machine, 0x3000U, 0x01ffU, 0x46U);
    fdc765_test_send_command(&fixture->machine, 0x46U, params, 8U);
    assert(fixture->irq.asserted);
    assert(bm_fdc765_state(fixture->machine.fdc, &state) == BM_STATUS_OK);
    assert(state.result_count == 7U && state.interrupt_pending);
    bm_fdc765_reset(fixture->machine.fdc);
    assert(!fixture->irq.asserted);
    assert(bm_fdc765_state(fixture->machine.fdc, &state) == BM_STATUS_OK);
    assert(state.result_count == 0U && !state.interrupt_pending);
    assert(bm_dma8237_channel_state(fixture->machine.dma, 2U, &dma_state) ==
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
        fdc765_test_memory_write(&fixture.machine, 0x1200U + index,
                                 (uint8_t) index);
    test_missing_media_and_backend_errors(&fixture);
    test_short_dma_is_rejected(&fixture);
    test_unimplemented_commands_are_rejected(&fixture);
    test_multi_track_crosses_to_second_head(&fixture);
    test_reset_discards_partial_and_pending_state(&fixture);
    fixture_destroy(&fixture);
    return 0;
}
