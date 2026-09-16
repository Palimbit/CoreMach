/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "fdc765_test_harness.h"

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

int
main(void)
{
    fdc765_test_machine_t machine;
    static memory_media_t media = { { 0 } };
    guarded_media_t guarded_media = { 0U, 0U };
    irq_sink_t irq = { 0, 0U };
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
    fdc765_test_config_t config = { 0 };
    uint8_t reset_sense[2];
    uint8_t read_params[8] = { 0U, 0U, 0U, 1U, 2U, 9U, 0x2aU, 0xffU };
    uint8_t extended_eot_params[8] = {
        0U, 0U, 0U, 1U, 2U, 18U, 0x1bU, 0xffU
    };
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

    config.drive_configs[0] = &drive_config;
    config.drive_configs[1] = &oversized_drive_config;
    config.drive_configs[2] = &writable_drive_config;
    config.disk_change_active_low = 1;
    config.irq = capture_irq;
    config.irq_context = &irq;
    fdc765_test_machine_create(&machine, &config);

    assert(fdc765_test_io_read(&machine, 0x03f4U) == 0x80U);
    fdc765_test_io_write(&machine, 0x03f2U, 0x1cU);
    assert(irq.asserted && irq.edges == 1U);
    for (index = 0U; index < 4U; ++index) {
        fdc765_test_send_command(&machine, 0x08U, NULL, 0U);
        reset_sense[0] = fdc765_test_io_read(&machine, 0x03f5U);
        reset_sense[1] = fdc765_test_io_read(&machine, 0x03f5U);
        assert(reset_sense[0] == (uint8_t) (0xc0U | index));
        assert(reset_sense[1] == 0U);
    }

    /* Deleted-data operations are deliberately unsupported, not approximated. */
    fdc765_test_send_command(&machine, 0x09U, NULL, 0U);
    assert(fdc765_test_io_read(&machine, 0x03f5U) == 0x80U);

    fdc765_test_program_dma(&machine, 0x2000U, 0x01ffU, 0x46U);
    fdc765_test_send_command(&machine, 0x46U, read_params, 8U);
    assert(irq.asserted && irq.edges == 2U);
    fdc765_test_read_results(&machine, results, 7U);
    assert(!irq.asserted);
    assert(results[0] == 0U && results[1] == 0U && results[2] == 0U);
    assert(results[3] == 0U && results[4] == 0U && results[5] == 1U &&
           results[6] == 2U);
    for (index = 0U; index < 512U; ++index) {
        assert(fdc765_test_memory_read(&machine, 0x2000U + index) ==
               media.bytes[index]);
    }

    /* EOT may exceed this track when DMA TC ends after a complete sector. */
    fdc765_test_program_dma(&machine, 0x2400U, 0x01ffU, 0x46U);
    fdc765_test_send_command(&machine, 0xe6U, extended_eot_params, 8U);
    fdc765_test_read_results(&machine, results, 7U);
    assert(results[0] == 0U && results[1] == 0U && results[2] == 0U);
    assert(results[3] == 0U && results[4] == 0U && results[5] == 1U &&
           results[6] == 2U);
    for (index = 0U; index < 512U; ++index) {
        assert(fdc765_test_memory_read(&machine, 0x2400U + index) ==
               media.bytes[index]);
    }

    /* A protected medium must report write-protect without consuming DMA. */
    fdc765_test_send_command(&machine, 0x45U, read_params, 8U);
    fdc765_test_read_results(&machine, results, 7U);
    assert((results[0] & 0x40U) != 0U);
    assert((results[1] & 0x02U) != 0U);

    /* A normal 512-byte sector also succeeds through the FDC write path. */
    for (index = 0U; index < 512U; ++index) {
        fdc765_test_memory_write(&machine, 0x2200U + index,
                                 (uint8_t) (index ^ 0xa5U));
    }
    fdc765_test_program_dma(&machine, 0x2200U, 0x01ffU, 0x4aU);
    fdc765_test_io_write(&machine, 0x03f2U, 0x4eU);
    fdc765_test_send_command(&machine, 0x45U, write_params, 8U);
    fdc765_test_read_results(&machine, results, 7U);
    assert(results[0] == 2U && results[1] == 0U && results[2] == 0U);
    for (index = 0U; index < 512U; ++index)
        assert(media.bytes[512U + index] == (uint8_t) (index ^ 0xa5U));

    /* Oversized sectors are rejected before DMA or media callbacks. */
    assert(BM_FDC765_MAX_SECTOR_SIZE == 4096U);
    fdc765_test_io_write(&machine, 0x03f2U, 0x2dU);
    fdc765_test_send_command(&machine, 0x46U, oversized_params, 8U);
    fdc765_test_read_results(&machine, results, 7U);
    assert((results[0] & 0x40U) != 0U);
    assert((results[1] & 0x04U) != 0U);
    assert(guarded_media.reads == 0U && guarded_media.writes == 0U);

    fdc765_test_send_command(&machine, 0x45U, oversized_params, 8U);
    fdc765_test_read_results(&machine, results, 7U);
    assert((results[0] & 0x40U) != 0U);
    assert((results[1] & 0x04U) != 0U);
    assert(guarded_media.reads == 0U && guarded_media.writes == 0U);

    fdc765_test_machine_destroy(&machine);

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
