/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/engine/storage.h>

#include <limits.h>

static bm_status_t
validate_range(const bm_block_media_t *media,
               uint64_t first_block,
               uint32_t block_count)
{
    if ((bm_block_media_validate(media) != BM_STATUS_OK) ||
        (block_count == 0U) || (first_block >= media->block_count) ||
        ((uint64_t) block_count > media->block_count - first_block))
        return BM_STATUS_INVALID_ARGUMENT;
    if ((size_t) block_count > SIZE_MAX / media->block_size)
        return BM_STATUS_INVALID_ARGUMENT;
    return BM_STATUS_OK;
}

bm_status_t
bm_block_media_validate(const bm_block_media_t *media)
{
    if ((media == NULL) || (media->block_count == 0U) ||
        (media->block_size == 0U) || (media->read == NULL) ||
        (!media->read_only && (media->write == NULL)))
        return BM_STATUS_INVALID_ARGUMENT;
    return BM_STATUS_OK;
}

bm_status_t
bm_block_media_read(const bm_block_media_t *media,
                    uint64_t first_block,
                    uint32_t block_count,
                    uint8_t *destination)
{
    bm_status_t status = validate_range(media, first_block, block_count);
    if ((status != BM_STATUS_OK) || (destination == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    return media->read(media->context, first_block, block_count, destination);
}

bm_status_t
bm_block_media_write(const bm_block_media_t *media,
                     uint64_t first_block,
                     uint32_t block_count,
                     const uint8_t *source)
{
    bm_status_t status = validate_range(media, first_block, block_count);
    if ((status != BM_STATUS_OK) || (source == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    if (media->read_only || (media->write == NULL))
        return BM_STATUS_READ_ONLY;
    return media->write(media->context, first_block, block_count, source);
}
