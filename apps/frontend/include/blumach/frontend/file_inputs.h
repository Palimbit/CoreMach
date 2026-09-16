/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_FRONTEND_FILE_INPUTS_H
#define BLUMACH_FRONTEND_FILE_INPUTS_H

#include <blumach/engine/storage.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bm_frontend_blob {
    uint8_t *data;
    size_t size;
} bm_frontend_blob_t;

typedef struct bm_frontend_readonly_media {
    FILE *file;
    size_t size;
    bm_block_media_t media;
} bm_frontend_readonly_media_t;

int bm_frontend_blob_read_exact(const char *path, size_t expected_size,
                                bm_frontend_blob_t *blob);
void bm_frontend_blob_release(bm_frontend_blob_t *blob);
int bm_frontend_readonly_media_open(const char *path, uint32_t block_size,
                                    bm_frontend_readonly_media_t *media);
void bm_frontend_readonly_media_close(bm_frontend_readonly_media_t *media);

#ifdef __cplusplus
}
#endif

#endif
