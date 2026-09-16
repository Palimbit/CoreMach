/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/components/floppy_drive.h>
#include <blumach/engine/storage.h>
#include <blumach/platforms/null_host.h>

#include <assert.h>
#include <stdint.h>
#include <string.h>

enum {
    TEST_BLOCK_SIZE = 4,
    TEST_BLOCK_COUNT = 12
};

typedef struct test_media {
    uint8_t bytes[TEST_BLOCK_SIZE * TEST_BLOCK_COUNT];
    bm_status_t read_status;
    bm_status_t write_status;
    uint64_t last_block;
    uint32_t last_count;
    unsigned int read_calls;
    unsigned int write_calls;
} test_media_t;

static bm_status_t
test_read(void *context,
          uint64_t first_block,
          uint32_t block_count,
          uint8_t *destination)
{
    test_media_t *media = context;

    media->last_block = first_block;
    media->last_count = block_count;
    ++media->read_calls;
    if (media->read_status != BM_STATUS_OK)
        return media->read_status;
    memcpy(destination, &media->bytes[first_block * TEST_BLOCK_SIZE],
           (size_t) block_count * TEST_BLOCK_SIZE);
    return BM_STATUS_OK;
}

static bm_status_t
test_write(void *context,
           uint64_t first_block,
           uint32_t block_count,
           const uint8_t *source)
{
    test_media_t *media = context;

    media->last_block = first_block;
    media->last_count = block_count;
    ++media->write_calls;
    if (media->write_status != BM_STATUS_OK)
        return media->write_status;
    memcpy(&media->bytes[first_block * TEST_BLOCK_SIZE], source,
           (size_t) block_count * TEST_BLOCK_SIZE);
    return BM_STATUS_OK;
}

static bm_block_media_t
make_media(test_media_t *context, int read_only)
{
    bm_block_media_t media = {
        context,
        TEST_BLOCK_COUNT,
        TEST_BLOCK_SIZE,
        read_only,
        test_read,
        read_only ? NULL : test_write
    };
    return media;
}

static bm_floppy_drive_config_t
make_drive_config(test_media_t *context, int read_only)
{
    bm_floppy_drive_config_t config = {
        1,
        1,
        0,
        { 2U, 2U, 3U, TEST_BLOCK_SIZE },
        { NULL, 0U, 0U, 0, NULL, NULL }
    };
    config.media = make_media(context, read_only);
    return config;
}

static void
test_block_media_validation(void)
{
    test_media_t context;
    bm_block_media_t media;

    memset(&context, 0, sizeof(context));
    media = make_media(&context, 0);
    assert(bm_block_media_validate(NULL) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_block_media_validate(&media) == BM_STATUS_OK);

    media.block_count = 0U;
    assert(bm_block_media_validate(&media) == BM_STATUS_INVALID_ARGUMENT);
    media = make_media(&context, 0);
    media.block_size = 0U;
    assert(bm_block_media_validate(&media) == BM_STATUS_INVALID_ARGUMENT);
    media = make_media(&context, 0);
    media.read = NULL;
    assert(bm_block_media_validate(&media) == BM_STATUS_INVALID_ARGUMENT);
    media = make_media(&context, 0);
    media.write = NULL;
    assert(bm_block_media_validate(&media) == BM_STATUS_INVALID_ARGUMENT);

    media = make_media(&context, 1);
    assert(media.write == NULL);
    assert(bm_block_media_validate(&media) == BM_STATUS_OK);
}

static void
test_block_media_access(void)
{
    static const uint8_t replacement[TEST_BLOCK_SIZE] = {
        0xa1U, 0xb2U, 0xc3U, 0xd4U
    };
    test_media_t context;
    bm_block_media_t media;
    uint8_t bytes[TEST_BLOCK_SIZE];

    memset(&context, 0, sizeof(context));
    context.bytes[TEST_BLOCK_SIZE] = 0x5aU;
    media = make_media(&context, 0);

    assert(bm_block_media_read(&media, 1U, 1U, bytes) == BM_STATUS_OK);
    assert(context.read_calls == 1U);
    assert(context.last_block == 1U);
    assert(context.last_count == 1U);
    assert(bytes[0] == 0x5aU);

    assert(bm_block_media_read(NULL, 0U, 1U, bytes) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_block_media_read(&media, 0U, 0U, bytes) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_block_media_read(&media, TEST_BLOCK_COUNT, 1U, bytes) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_block_media_read(&media, TEST_BLOCK_COUNT - 1U, 2U, bytes) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_block_media_read(&media, 0U, 1U, NULL) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(context.read_calls == 1U);

    context.read_status = BM_STATUS_DEVICE_ERROR;
    assert(bm_block_media_read(&media, 0U, 1U, bytes) ==
           BM_STATUS_DEVICE_ERROR);
    assert(context.read_calls == 2U);
    context.read_status = BM_STATUS_OK;

    assert(bm_block_media_write(&media, 2U, 1U, replacement) == BM_STATUS_OK);
    assert(context.write_calls == 1U);
    assert(context.last_block == 2U);
    assert(memcmp(&context.bytes[2U * TEST_BLOCK_SIZE], replacement,
                  sizeof(replacement)) == 0);

    assert(bm_block_media_write(&media, 0U, 0U, replacement) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_block_media_write(&media, 0U, 1U, NULL) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(context.write_calls == 1U);

    context.write_status = BM_STATUS_DEVICE_ERROR;
    assert(bm_block_media_write(&media, 0U, 1U, replacement) ==
           BM_STATUS_DEVICE_ERROR);
    assert(context.write_calls == 2U);

    media = make_media(&context, 1);
    assert(bm_block_media_write(&media, 0U, 1U, replacement) ==
           BM_STATUS_READ_ONLY);
    assert(context.write_calls == 2U);
}

static void
test_drive_configuration(void)
{
    test_media_t context;
    bm_floppy_drive_config_t config;

    memset(&context, 0, sizeof(context));
    memset(&config, 0, sizeof(config));
    assert(bm_floppy_drive_config_validate(NULL) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_floppy_drive_config_validate(&config) == BM_STATUS_OK);

    config.media_present = 1;
    assert(bm_floppy_drive_config_validate(&config) ==
           BM_STATUS_INVALID_ARGUMENT);

    config = make_drive_config(&context, 0);
    assert(bm_floppy_drive_config_validate(&config) == BM_STATUS_OK);
    config.geometry.cylinders = 0U;
    assert(bm_floppy_drive_config_validate(&config) ==
           BM_STATUS_INVALID_ARGUMENT);
    config = make_drive_config(&context, 0);
    config.geometry.heads = 0U;
    assert(bm_floppy_drive_config_validate(&config) ==
           BM_STATUS_INVALID_ARGUMENT);
    config = make_drive_config(&context, 0);
    config.geometry.sectors_per_track = 0U;
    assert(bm_floppy_drive_config_validate(&config) ==
           BM_STATUS_INVALID_ARGUMENT);
    config = make_drive_config(&context, 0);
    config.geometry.bytes_per_sector = 0U;
    assert(bm_floppy_drive_config_validate(&config) ==
           BM_STATUS_INVALID_ARGUMENT);

    config = make_drive_config(&context, 0);
    config.media.block_size += 1U;
    assert(bm_floppy_drive_config_validate(&config) ==
           BM_STATUS_INVALID_ARGUMENT);
    config = make_drive_config(&context, 0);
    config.media.block_count -= 1U;
    assert(bm_floppy_drive_config_validate(&config) ==
           BM_STATUS_INVALID_ARGUMENT);
    config = make_drive_config(&context, 0);
    config.media.read = NULL;
    assert(bm_floppy_drive_config_validate(&config) ==
           BM_STATUS_INVALID_ARGUMENT);

    config = make_drive_config(&context, 0);
    config.media_present = 0;
    memset(&config.media, 0, sizeof(config.media));
    assert(bm_floppy_drive_config_validate(&config) == BM_STATUS_OK);
}

static void
test_drive_access_and_state(void)
{
    static const uint8_t replacement[TEST_BLOCK_SIZE] = {
        0x11U, 0x22U, 0x33U, 0x44U
    };
    bm_host_services_t host = bm_null_host_services();
    test_media_t context;
    bm_floppy_drive_config_t config;
    bm_floppy_drive_t *drive = NULL;
    const bm_floppy_geometry_t *geometry;
    uint8_t bytes[TEST_BLOCK_SIZE];

    memset(&context, 0, sizeof(context));
    context.bytes[11U * TEST_BLOCK_SIZE] = 0x7eU;
    config = make_drive_config(&context, 0);

    assert(bm_floppy_drive_create(NULL, &config, &drive) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_floppy_drive_create(&host, &config, NULL) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_floppy_drive_create(&host, &config, &drive) == BM_STATUS_OK);
    assert(drive != NULL);
    assert(bm_floppy_drive_installed(drive));
    assert(bm_floppy_drive_media_present(drive));
    assert(!bm_floppy_drive_write_protected(drive));
    assert(bm_floppy_drive_changed(drive));
    geometry = bm_floppy_drive_geometry(drive);
    assert(geometry != NULL);
    assert(geometry->cylinders == 2U);
    assert(geometry->heads == 2U);
    assert(geometry->sectors_per_track == 3U);

    bm_floppy_drive_clear_changed(drive);
    assert(!bm_floppy_drive_changed(drive));
    assert(bm_floppy_drive_seek(drive, 1U) == BM_STATUS_OK);
    assert(bm_floppy_drive_cylinder(drive) == 1U);
    assert(bm_floppy_drive_seek(drive, 2U) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_floppy_drive_cylinder(drive) == 1U);
    bm_floppy_drive_reset(drive);
    assert(bm_floppy_drive_cylinder(drive) == 0U);

    assert(bm_floppy_drive_read_sector(drive, 1U, 1U, 3U, bytes,
                                       sizeof(bytes)) == BM_STATUS_OK);
    assert(context.last_block == 11U);
    assert(bytes[0] == 0x7eU);
    assert(bm_floppy_drive_read_sector(drive, 2U, 0U, 1U, bytes,
                                       sizeof(bytes)) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_floppy_drive_read_sector(drive, 0U, 2U, 1U, bytes,
                                       sizeof(bytes)) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_floppy_drive_read_sector(drive, 0U, 0U, 0U, bytes,
                                       sizeof(bytes)) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_floppy_drive_read_sector(drive, 0U, 0U, 1U, bytes,
                                       sizeof(bytes) - 1U) ==
           BM_STATUS_INVALID_ARGUMENT);

    assert(bm_floppy_drive_write_sector(drive, 0U, 1U, 1U, replacement,
                                        sizeof(replacement)) == BM_STATUS_OK);
    assert(context.last_block == 3U);
    assert(memcmp(&context.bytes[3U * TEST_BLOCK_SIZE], replacement,
                  sizeof(replacement)) == 0);

    context.read_status = BM_STATUS_DEVICE_ERROR;
    assert(bm_floppy_drive_read_sector(drive, 0U, 0U, 1U, bytes,
                                       sizeof(bytes)) ==
           BM_STATUS_DEVICE_ERROR);
    context.write_status = BM_STATUS_DEVICE_ERROR;
    assert(bm_floppy_drive_write_sector(drive, 0U, 0U, 1U, replacement,
                                        sizeof(replacement)) ==
           BM_STATUS_DEVICE_ERROR);

    bm_floppy_drive_destroy(drive);
}

static void
test_absent_and_read_only_drives(void)
{
    bm_host_services_t host = bm_null_host_services();
    test_media_t context;
    bm_floppy_drive_config_t config;
    bm_floppy_drive_t *drive = NULL;
    uint8_t bytes[TEST_BLOCK_SIZE] = { 0U };

    memset(&context, 0, sizeof(context));
    config = make_drive_config(&context, 0);
    config.media_present = 0;
    memset(&config.media, 0, sizeof(config.media));
    assert(bm_floppy_drive_create(&host, &config, &drive) == BM_STATUS_OK);
    assert(bm_floppy_drive_installed(drive));
    assert(!bm_floppy_drive_media_present(drive));
    assert(!bm_floppy_drive_changed(drive));
    assert(bm_floppy_drive_read_sector(drive, 0U, 0U, 1U, bytes,
                                       sizeof(bytes)) ==
           BM_STATUS_INVALID_STATE);
    bm_floppy_drive_destroy(drive);

    config = make_drive_config(&context, 1);
    assert(bm_floppy_drive_create(&host, &config, &drive) == BM_STATUS_OK);
    assert(bm_floppy_drive_write_protected(drive));
    assert(bm_floppy_drive_write_sector(drive, 0U, 0U, 1U, bytes,
                                        sizeof(bytes)) == BM_STATUS_READ_ONLY);
    assert(context.write_calls == 0U);
    bm_floppy_drive_destroy(drive);

    config = make_drive_config(&context, 0);
    config.write_protected = 1;
    assert(bm_floppy_drive_create(&host, &config, &drive) == BM_STATUS_OK);
    assert(bm_floppy_drive_write_protected(drive));
    assert(bm_floppy_drive_write_sector(drive, 0U, 0U, 1U, bytes,
                                        sizeof(bytes)) == BM_STATUS_READ_ONLY);
    assert(context.write_calls == 0U);
    bm_floppy_drive_destroy(drive);
}

int
main(void)
{
    test_block_media_validation();
    test_block_media_access();
    test_drive_configuration();
    test_drive_access_and_state();
    test_absent_and_read_only_drives();

    assert(!bm_floppy_drive_installed(NULL));
    assert(!bm_floppy_drive_media_present(NULL));
    assert(!bm_floppy_drive_write_protected(NULL));
    assert(!bm_floppy_drive_changed(NULL));
    assert(bm_floppy_drive_geometry(NULL) == NULL);
    assert(bm_floppy_drive_cylinder(NULL) == 0U);
    assert(bm_floppy_drive_seek(NULL, 0U) == BM_STATUS_INVALID_STATE);
    bm_floppy_drive_reset(NULL);
    bm_floppy_drive_clear_changed(NULL);
    bm_floppy_drive_destroy(NULL);
    return 0;
}
