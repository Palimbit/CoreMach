/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_ENGINE_STORAGE_H
#define BLUMACH_ENGINE_STORAGE_H

#include <stddef.h>
#include <stdint.h>
#include <blumach/engine/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef bm_status_t (*bm_block_media_read_fn)(void *context,
                                               uint64_t first_block,
                                               uint32_t block_count,
                                               uint8_t *destination);
typedef bm_status_t (*bm_block_media_write_fn)(void *context,
                                                uint64_t first_block,
                                                uint32_t block_count,
                                                const uint8_t *source);

/* Caller-owned random-access media. The callbacks and their context must
 * remain valid for the lifetime of every configured component that uses it. */
typedef struct bm_block_media {
    void *context;
    uint64_t block_count;
    uint32_t block_size;
    int read_only;
    bm_block_media_read_fn read;
    bm_block_media_write_fn write;
} bm_block_media_t;

typedef enum bm_storage_device_kind {
    BM_STORAGE_DEVICE_FLOPPY = 0,
    BM_STORAGE_DEVICE_HARD_DISK = 1
} bm_storage_device_kind_t;

/* Frontend-neutral, read-only device telemetry. Operation counters are
 * cumulative since the most recent machine reset. */
typedef struct bm_storage_device_status {
    bm_storage_device_kind_t kind;
    uint32_t unit;
    int installed;
    int media_present;
    int write_protected;
    int motor_active;
    uint64_t read_operations;
    uint64_t write_operations;
} bm_storage_device_status_t;

/* Caller-owned replacement media. A present medium and its callback context
 * must remain valid until it is replaced again or the session is destroyed. */
typedef struct bm_storage_media_change {
    int media_present;
    int write_protected;
    bm_block_media_t media;
} bm_storage_media_change_t;

bm_status_t bm_block_media_validate(const bm_block_media_t *media);
bm_status_t bm_block_media_read(const bm_block_media_t *media,
                                uint64_t first_block,
                                uint32_t block_count,
                                uint8_t *destination);
bm_status_t bm_block_media_write(const bm_block_media_t *media,
                                 uint64_t first_block,
                                 uint32_t block_count,
                                 const uint8_t *source);

#ifdef __cplusplus
}
#endif

#endif
