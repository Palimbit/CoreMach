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
