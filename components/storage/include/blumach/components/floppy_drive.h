/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_COMPONENTS_FLOPPY_DRIVE_H
#define BLUMACH_COMPONENTS_FLOPPY_DRIVE_H

#include <stdint.h>
#include <blumach/engine/host.h>
#include <blumach/engine/storage.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bm_floppy_drive bm_floppy_drive_t;

typedef struct bm_floppy_geometry {
    uint16_t cylinders;
    uint8_t heads;
    uint8_t sectors_per_track;
    uint16_t bytes_per_sector;
} bm_floppy_geometry_t;

typedef struct bm_floppy_drive_config {
    int installed;
    int media_present;
    int write_protected;
    bm_floppy_geometry_t geometry;
    bm_block_media_t media;
} bm_floppy_drive_config_t;

typedef struct bm_floppy_drive_state {
    uint16_t cylinder;
    int installed;
    int media_present;
    int write_protected;
    int changed;
    uint64_t read_operations;
    uint64_t write_operations;
} bm_floppy_drive_state_t;

bm_status_t bm_floppy_drive_config_validate(
    const bm_floppy_drive_config_t *config);
bm_status_t bm_floppy_drive_create(const bm_host_services_t *host,
                                   const bm_floppy_drive_config_t *config,
                                   bm_floppy_drive_t **out_drive);
void bm_floppy_drive_destroy(bm_floppy_drive_t *drive);
void bm_floppy_drive_reset(bm_floppy_drive_t *drive);
int bm_floppy_drive_installed(const bm_floppy_drive_t *drive);
int bm_floppy_drive_media_present(const bm_floppy_drive_t *drive);
int bm_floppy_drive_write_protected(const bm_floppy_drive_t *drive);
int bm_floppy_drive_changed(const bm_floppy_drive_t *drive);
void bm_floppy_drive_clear_changed(bm_floppy_drive_t *drive);
bm_status_t bm_floppy_drive_replace_media(
    bm_floppy_drive_t *drive, const bm_floppy_geometry_t *geometry,
    const bm_block_media_t *media, int write_protected);
const bm_floppy_geometry_t *bm_floppy_drive_geometry(const bm_floppy_drive_t *drive);
bm_status_t bm_floppy_drive_seek(bm_floppy_drive_t *drive, uint16_t cylinder);
uint16_t bm_floppy_drive_cylinder(const bm_floppy_drive_t *drive);
bm_status_t bm_floppy_drive_state(const bm_floppy_drive_t *drive,
                                  bm_floppy_drive_state_t *state);
bm_status_t bm_floppy_drive_read_sector(bm_floppy_drive_t *drive,
                                        uint16_t cylinder,
                                        uint8_t head,
                                        uint8_t sector,
                                        uint8_t *destination,
                                        size_t destination_size);
bm_status_t bm_floppy_drive_write_sector(bm_floppy_drive_t *drive,
                                         uint16_t cylinder,
                                         uint8_t head,
                                         uint8_t sector,
                                         const uint8_t *source,
                                         size_t source_size);

#ifdef __cplusplus
}
#endif

#endif
