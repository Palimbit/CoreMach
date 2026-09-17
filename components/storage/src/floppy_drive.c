/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/components/floppy_drive.h>

#include <string.h>

struct bm_floppy_drive {
    bm_host_services_t host;
    bm_floppy_geometry_t geometry;
    bm_block_media_t media;
    uint16_t cylinder;
    int installed;
    int media_present;
    int write_protected;
    int changed;
    uint64_t read_operations;
    uint64_t write_operations;
};

bm_status_t
bm_floppy_drive_config_validate(const bm_floppy_drive_config_t *config)
{
    uint64_t required_blocks;

    if (config == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    if (!config->installed)
        return config->media_present ? BM_STATUS_INVALID_ARGUMENT : BM_STATUS_OK;
    if ((config->geometry.cylinders == 0U) ||
        (config->geometry.heads == 0U) ||
        (config->geometry.sectors_per_track == 0U) ||
        (config->geometry.bytes_per_sector == 0U))
        return BM_STATUS_INVALID_ARGUMENT;
    if (!config->media_present)
        return BM_STATUS_OK;
    if (bm_block_media_validate(&config->media) != BM_STATUS_OK)
        return BM_STATUS_INVALID_ARGUMENT;
    required_blocks = (uint64_t) config->geometry.cylinders *
                      config->geometry.heads *
                      config->geometry.sectors_per_track;
    if ((config->media.block_size != config->geometry.bytes_per_sector) ||
        (config->media.block_count < required_blocks))
        return BM_STATUS_INVALID_ARGUMENT;
    return BM_STATUS_OK;
}

static bm_status_t
sector_block(const bm_floppy_drive_t *drive,
             uint16_t cylinder,
             uint8_t head,
             uint8_t sector,
             uint64_t *block)
{
    if ((drive == NULL) || (block == NULL) || !drive->installed ||
        !drive->media_present)
        return BM_STATUS_INVALID_STATE;
    if ((cylinder >= drive->geometry.cylinders) ||
        (head >= drive->geometry.heads) || (sector == 0U) ||
        (sector > drive->geometry.sectors_per_track))
        return BM_STATUS_INVALID_ARGUMENT;
    *block = ((uint64_t) cylinder * drive->geometry.heads + head) *
             drive->geometry.sectors_per_track + (sector - 1U);
    return BM_STATUS_OK;
}

bm_status_t
bm_floppy_drive_create(const bm_host_services_t *host,
                       const bm_floppy_drive_config_t *config,
                       bm_floppy_drive_t **out_drive)
{
    bm_floppy_drive_t *drive;
    bm_status_t status;

    if ((bm_host_services_validate(host) != BM_STATUS_OK) || (out_drive == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    *out_drive = NULL;
    status = bm_floppy_drive_config_validate(config);
    if (status != BM_STATUS_OK)
        return status;
    drive = host->allocate(host->context, sizeof(*drive));
    if (drive == NULL)
        return BM_STATUS_OUT_OF_MEMORY;
    memset(drive, 0, sizeof(*drive));
    drive->host = *host;
    drive->installed = !!config->installed;
    drive->media_present = !!config->media_present;
    drive->write_protected = !!config->write_protected || config->media.read_only;
    drive->geometry = config->geometry;
    drive->media = config->media;
    drive->changed = drive->media_present;
    *out_drive = drive;
    return BM_STATUS_OK;
}

void
bm_floppy_drive_destroy(bm_floppy_drive_t *drive)
{
    if (drive != NULL)
        drive->host.release(drive->host.context, drive);
}

void
bm_floppy_drive_reset(bm_floppy_drive_t *drive)
{
    if (drive != NULL) {
        drive->cylinder = 0U;
        drive->read_operations = 0U;
        drive->write_operations = 0U;
    }
}

int
bm_floppy_drive_installed(const bm_floppy_drive_t *drive)
{
    return (drive != NULL) && drive->installed;
}

int
bm_floppy_drive_media_present(const bm_floppy_drive_t *drive)
{
    return (drive != NULL) && drive->installed && drive->media_present;
}

int
bm_floppy_drive_write_protected(const bm_floppy_drive_t *drive)
{
    return (drive != NULL) && drive->write_protected;
}

int
bm_floppy_drive_changed(const bm_floppy_drive_t *drive)
{
    return (drive != NULL) && drive->changed;
}

void
bm_floppy_drive_clear_changed(bm_floppy_drive_t *drive)
{
    if (drive != NULL)
        drive->changed = 0;
}

bm_status_t
bm_floppy_drive_replace_media(bm_floppy_drive_t *drive,
                              const bm_floppy_geometry_t *geometry,
                              const bm_block_media_t *media,
                              int write_protected)
{
    bm_floppy_drive_config_t replacement;
    bm_status_t status;

    if ((drive == NULL) || !drive->installed)
        return BM_STATUS_INVALID_STATE;
    if ((geometry == NULL) != (media == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    if (media == NULL) {
        memset(&drive->media, 0, sizeof(drive->media));
        drive->media_present = 0;
        drive->write_protected = 0;
        drive->changed = 1;
        return BM_STATUS_OK;
    }
    memset(&replacement, 0, sizeof(replacement));
    replacement.installed = 1;
    replacement.media_present = 1;
    replacement.write_protected = !!write_protected;
    replacement.geometry = *geometry;
    replacement.media = *media;
    status = bm_floppy_drive_config_validate(&replacement);
    if (status != BM_STATUS_OK)
        return status;
    drive->geometry = *geometry;
    drive->media = *media;
    drive->media_present = 1;
    drive->write_protected = !!write_protected || media->read_only;
    drive->changed = 1;
    if (drive->cylinder >= geometry->cylinders)
        drive->cylinder = (uint16_t) (geometry->cylinders - 1U);
    return BM_STATUS_OK;
}

const bm_floppy_geometry_t *
bm_floppy_drive_geometry(const bm_floppy_drive_t *drive)
{
    return drive != NULL ? &drive->geometry : NULL;
}

bm_status_t
bm_floppy_drive_seek(bm_floppy_drive_t *drive, uint16_t cylinder)
{
    if ((drive == NULL) || !drive->installed)
        return BM_STATUS_INVALID_STATE;
    if (cylinder >= drive->geometry.cylinders)
        return BM_STATUS_INVALID_ARGUMENT;
    drive->cylinder = cylinder;
    drive->changed = 0;
    return BM_STATUS_OK;
}

uint16_t
bm_floppy_drive_cylinder(const bm_floppy_drive_t *drive)
{
    return drive != NULL ? drive->cylinder : 0U;
}

bm_status_t
bm_floppy_drive_state(const bm_floppy_drive_t *drive,
                      bm_floppy_drive_state_t *state)
{
    if ((drive == NULL) || (state == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    state->cylinder = drive->cylinder;
    state->installed = drive->installed;
    state->media_present = drive->media_present;
    state->write_protected = drive->write_protected;
    state->changed = drive->changed;
    state->read_operations = drive->read_operations;
    state->write_operations = drive->write_operations;
    return BM_STATUS_OK;
}

bm_status_t
bm_floppy_drive_read_sector(bm_floppy_drive_t *drive,
                            uint16_t cylinder,
                            uint8_t head,
                            uint8_t sector,
                            uint8_t *destination,
                            size_t destination_size)
{
    uint64_t block;
    bm_status_t status = sector_block(drive, cylinder, head, sector, &block);
    if (status != BM_STATUS_OK)
        return status;
    if ((destination == NULL) ||
        (destination_size != drive->geometry.bytes_per_sector))
        return BM_STATUS_INVALID_ARGUMENT;
    status = bm_block_media_read(&drive->media, block, 1U, destination);
    if (status == BM_STATUS_OK)
        ++drive->read_operations;
    return status;
}

bm_status_t
bm_floppy_drive_write_sector(bm_floppy_drive_t *drive,
                             uint16_t cylinder,
                             uint8_t head,
                             uint8_t sector,
                             const uint8_t *source,
                             size_t source_size)
{
    uint64_t block;
    bm_status_t status = sector_block(drive, cylinder, head, sector, &block);
    if (status != BM_STATUS_OK)
        return status;
    if ((source == NULL) || (source_size != drive->geometry.bytes_per_sector))
        return BM_STATUS_INVALID_ARGUMENT;
    if (drive->write_protected)
        return BM_STATUS_READ_ONLY;
    status = bm_block_media_write(&drive->media, block, 1U, source);
    if (status == BM_STATUS_OK)
        ++drive->write_operations;
    return status;
}
