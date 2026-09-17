/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_ENGINE_VIDEO_H
#define BLUMACH_ENGINE_VIDEO_H

#include <stddef.h>
#include <stdint.h>
#include <blumach/engine/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum bm_pixel_format {
    BM_PIXEL_XRGB8888 = 0
} bm_pixel_format_t;

typedef struct bm_video_geometry {
    uint32_t width;
    uint32_t height;
    bm_pixel_format_t format;
    /* Refresh frequency in hertz as an exact rational. Both fields are zero
     * when the machine cannot identify its programmed video clock. */
    uint64_t refresh_numerator;
    uint64_t refresh_denominator;
} bm_video_geometry_t;

typedef struct bm_video_framebuffer {
    uint32_t *pixels;
    size_t pixel_capacity;
    uint32_t stride;
    bm_video_geometry_t geometry;
} bm_video_framebuffer_t;

#ifdef __cplusplus
}
#endif

#endif
