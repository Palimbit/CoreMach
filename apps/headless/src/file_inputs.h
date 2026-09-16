/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_HEADLESS_FILE_INPUTS_H
#define BLUMACH_HEADLESS_FILE_INPUTS_H

#include <blumach/engine/storage.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct headless_blob {
    uint8_t *data;
    size_t size;
} headless_blob_t;

typedef struct headless_readonly_media {
    FILE *file;
    size_t size;
    bm_block_media_t media;
} headless_readonly_media_t;

int headless_blob_read_exact(const char *path, size_t expected_size,
                             headless_blob_t *blob);
void headless_blob_release(headless_blob_t *blob);
int headless_readonly_media_open(const char *path, uint32_t block_size,
                                 headless_readonly_media_t *media);
void headless_readonly_media_close(headless_readonly_media_t *media);

#endif
