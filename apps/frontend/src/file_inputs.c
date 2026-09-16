/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/frontend/file_inputs.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static FILE *
open_file(const char *path, const char *mode)
{
    FILE *file = NULL;
#ifdef _MSC_VER
    if (fopen_s(&file, path, mode) != 0)
        file = NULL;
#else
    file = fopen(path, mode);
#endif
    return file;
}

int
bm_frontend_blob_read_exact(const char *path, size_t expected_size,
                            bm_frontend_blob_t *blob)
{
    FILE *file;
    uint8_t *data;
    size_t count;
    int extra;

    if ((path == NULL) || (blob == NULL) || (expected_size == 0U))
        return 0;
    *blob = (bm_frontend_blob_t) { NULL, 0U };
    file = open_file(path, "rb");
    if (file == NULL)
        return 0;
    data = malloc(expected_size);
    if (data == NULL) {
        fclose(file);
        return 0;
    }
    count = fread(data, 1U, expected_size, file);
    extra = fgetc(file);
    fclose(file);
    if ((count != expected_size) || (extra != EOF)) {
        free(data);
        return 0;
    }
    blob->data = data;
    blob->size = expected_size;
    return 1;
}

void
bm_frontend_blob_release(bm_frontend_blob_t *blob)
{
    if (blob == NULL)
        return;
    free(blob->data);
    *blob = (bm_frontend_blob_t) { NULL, 0U };
}

static bm_status_t
read_blocks(void *context, uint64_t first_block, uint32_t block_count,
            uint8_t *destination)
{
    bm_frontend_readonly_media_t *media = context;
    uint64_t offset;
    uint64_t byte_count;

    if ((media == NULL) || (media->file == NULL) || (destination == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    if ((first_block > media->media.block_count) ||
        ((uint64_t) block_count > media->media.block_count - first_block))
        return BM_STATUS_INVALID_ARGUMENT;
    offset = first_block * media->media.block_size;
    byte_count = (uint64_t) block_count * media->media.block_size;
    if ((offset > (uint64_t) LONG_MAX) || (byte_count > SIZE_MAX))
        return BM_STATUS_INVALID_ARGUMENT;
    if ((fseek(media->file, (long) offset, SEEK_SET) != 0) ||
        (fread(destination, 1U, (size_t) byte_count, media->file) !=
         (size_t) byte_count))
        return BM_STATUS_DEVICE_ERROR;
    return BM_STATUS_OK;
}

int
bm_frontend_readonly_media_open(const char *path, uint32_t block_size,
                                bm_frontend_readonly_media_t *media)
{
    FILE *file;
    long length;

    if ((path == NULL) || (media == NULL) || (block_size == 0U))
        return 0;
    memset(media, 0, sizeof(*media));
    file = open_file(path, "rb");
    if (file == NULL)
        return 0;
    if ((fseek(file, 0L, SEEK_END) != 0) || ((length = ftell(file)) <= 0L) ||
        (fseek(file, 0L, SEEK_SET) != 0) ||
        (((uint64_t) length % block_size) != 0U)) {
        fclose(file);
        return 0;
    }
    media->file = file;
    media->size = (size_t) length;
    media->media = (bm_block_media_t) {
        media, (uint64_t) media->size / block_size, block_size, 1,
        read_blocks, NULL
    };
    return 1;
}

void
bm_frontend_readonly_media_close(bm_frontend_readonly_media_t *media)
{
    if (media == NULL)
        return;
    if (media->file != NULL)
        fclose(media->file);
    memset(media, 0, sizeof(*media));
}
