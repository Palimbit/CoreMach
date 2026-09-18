/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/frontend/file_inputs.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

int
main(int argc, char **argv)
{
    bm_frontend_readonly_media_t media = { 0 };
    uint8_t zero[512] = { 0 };
    uint8_t pattern[512];
    uint8_t observed[512];
    FILE *file;
    size_t index;

    assert(argc == 2);
    file = fopen(argv[1], "wb");
    assert(file != NULL);
    assert(fwrite(zero, 1U, sizeof(zero), file) == sizeof(zero));
    assert(fwrite(zero, 1U, sizeof(zero), file) == sizeof(zero));
    assert(fclose(file) == 0);

    assert(bm_frontend_readonly_media_open(argv[1], 512U, &media));
    assert(media.media.read_only);
    assert(media.media.write == NULL);
    assert(bm_block_media_write(&media.media, 0U, 1U, zero) ==
           BM_STATUS_READ_ONLY);
    bm_frontend_readonly_media_close(&media);

    for (index = 0U; index < sizeof(pattern); ++index)
        pattern[index] = (uint8_t) (index ^ 0xa5U);
    assert(bm_frontend_working_media_open(argv[1], 512U, &media));
    assert(!media.media.read_only);
    assert(media.media.write != NULL);
    assert(bm_block_media_write(&media.media, 1U, 1U, pattern) == BM_STATUS_OK);
    bm_frontend_readonly_media_close(&media);

    file = fopen(argv[1], "rb");
    assert(file != NULL);
    assert(fseek(file, 512L, SEEK_SET) == 0);
    assert(fread(observed, 1U, sizeof(observed), file) == sizeof(observed));
    assert(fclose(file) == 0);
    assert(memcmp(observed, pattern, sizeof(pattern)) == 0);
    assert(remove(argv[1]) == 0);
    return 0;
}
