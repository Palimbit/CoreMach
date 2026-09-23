/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_COMPONENTS_V6355D_H
#define BLUMACH_COMPONENTS_V6355D_H

#include <blumach/components/bus.h>
#include <blumach/engine/video.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BM_V6355D_VRAM_SIZE 16384U
#define BM_V6355D_FONT_SIZE 2048U

typedef struct bm_v6355d bm_v6355d_t;

/* The caller owns the 256 x 8 glyph blob for the lifetime of create().
 * Only the first 128 glyphs are supplied by the known M15 BIOS location;
 * the other glyphs must not be silently attributed to that firmware. */
typedef struct bm_v6355d_config {
    const uint8_t *font;
    size_t font_size;
    bm_tick_t (*time_now)(void *context); /* Optional emulated-time source. */
    void *time_context;
} bm_v6355d_config_t;

bm_status_t bm_v6355d_create(const bm_host_services_t *host, bm_bus_t *bus,
                             const bm_v6355d_config_t *config,
                             bm_v6355d_t **out_video);
void bm_v6355d_destroy(bm_v6355d_t *video);
void bm_v6355d_reset(bm_v6355d_t *video);
bm_status_t bm_v6355d_geometry(const bm_v6355d_t *video,
                               bm_video_geometry_t *geometry);
/* Output is one 4-bit RGBI palette index per pixel. LCD phosphor/panel
 * presentation is deliberately outside the controller. */
bm_status_t bm_v6355d_render_indices(const bm_v6355d_t *video,
                                     bm_tick_t emulated_time,
                                     uint8_t *pixels, size_t capacity,
                                     uint32_t stride);
uint8_t bm_v6355d_status_at(bm_tick_t emulated_time);

#ifdef __cplusplus
}
#endif
#endif
