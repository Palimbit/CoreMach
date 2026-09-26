/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_COMPONENTS_PVGA1A_H
#define BLUMACH_COMPONENTS_PVGA1A_H

#include <stddef.h>
#include <stdint.h>
#include <blumach/components/bus.h>
#include <blumach/engine/video.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BM_PVGA1A_VRAM_SIZE (256U * 1024U)

typedef struct bm_pvga1a bm_pvga1a_t;

typedef struct bm_pvga1a_config {
    uint32_t vram_size;
} bm_pvga1a_config_t;

typedef enum bm_pvga1a_register_set {
    BM_PVGA1A_SEQUENCER = 0,
    BM_PVGA1A_GRAPHICS,
    BM_PVGA1A_CRTC,
    BM_PVGA1A_ATTRIBUTE
} bm_pvga1a_register_set_t;

bm_status_t bm_pvga1a_create(const bm_host_services_t *host,
                             bm_bus_t *bus,
                             const bm_pvga1a_config_t *config,
                             bm_pvga1a_t **out_video);
void bm_pvga1a_destroy(bm_pvga1a_t *video);
void bm_pvga1a_reset(bm_pvga1a_t *video);
/* Opt in to time-driven input status (including with zero ns), then advance
 * its raster. Reset returns to the legacy read-driven status used by existing
 * owners. Internal VGA clocks only; external clock selections are UNSUPPORTED
 * without changing phase. Functional CRTC model, not qualified board timing.
 * DEBUG reads preserve latches/indexes; DEBUG writes are rejected. */
bm_status_t bm_pvga1a_advance_ns(bm_pvga1a_t *video, uint64_t nanoseconds);
bm_status_t bm_pvga1a_inspect_register(const bm_pvga1a_t *video,
                                       bm_pvga1a_register_set_t set,
                                       uint8_t index,
                                       uint8_t *value);
bm_status_t bm_pvga1a_inspect_vram(const bm_pvga1a_t *video,
                                   unsigned int plane,
                                   uint16_t offset,
                                   uint8_t *value);
bm_status_t bm_pvga1a_video_geometry(const bm_pvga1a_t *video,
                                     bm_video_geometry_t *geometry);
/* ticks_per_second defines the machine's deterministic conversion from engine
 * ticks to video time. Rendering does not consult a host clock. */
bm_status_t bm_pvga1a_render(const bm_pvga1a_t *video,
                             bm_tick_t emulated_time,
                             uint64_t ticks_per_second,
                             bm_video_framebuffer_t *framebuffer);

#ifdef __cplusplus
}
#endif

#endif
