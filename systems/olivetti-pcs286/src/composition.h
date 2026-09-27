/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Private shared experimental board composition; diagnostic fields observational.
 */
#ifndef BM_PCS286_COMPOSITION_H
#define BM_PCS286_COMPOSITION_H
#include "board_services.h"
#include "board_io.h"
#include "dma_coordinator.h"
#include "page_spare_latches.h"
#include <blumach/components/lpt_spp.h>
#include <blumach/components/pvga1a.h>
#include <blumach/components/wd37c65.h>
#define TRACE_SIZE 64U
#define FLOPPY_1440_BYTES 1474560U
typedef struct io_event {
    uint64_t step, address, value;
    uint32_t pc, size;
    int op, status;
} io_event_t;
typedef struct cpu_event {
    uint64_t step;
    bm_286_boundary_t b;
} cpu_event_t;
typedef struct video_status_site {
    uint32_t pc;
    uint64_t port, reads, first_step, last_step, transitions;
    uint8_t first_value, last_value, value_or, value_and;
    uint8_t sequencer1, crtc0, crtc1, crtc6, crtc7, crtc10, crtc11, crtc12;
    uint8_t attribute0, attribute10, attribute12, attribute14, attribute15;
} video_status_site_t;
typedef struct probe {
    bm_pcs286_control_t control;
    bm_gc103_memory_t routes;
    bm_headland_at_memory_t memory;
    bm_pcs286_io_t io;
    bm_ioc02_legacy_registers_t ioc;
    bm_pcs286_memory_t *bytes;
    bm_cpu_t cpu;
    bm_at_bus_t *bus;
    bm_at_pic_t *pic;
    bm_at_dma_t *dma;
    bm_pcs286_dma_coordinator_t dma_coordinator;
    bm_wd37c65_t *fdc;
    bm_floppy_drive_t *floppy;
    const uint8_t *floppy_image;
    size_t floppy_size;
    bm_lpt_spp_t *lpt;
    bm_bus_t *video_bus;
    bm_pvga1a_t *video;
    uint64_t video_ns;
    bm_pcs286_services_t *services;
    bm_pcs286_page_spare_latches_t page_spares;
    void (*diagnostic)(void *, const char *, uint64_t, uint32_t, unsigned);
    void *diagnostic_context;
    uint64_t step, boundaries, io_count, memory_calls, post_count;
    /* Optional benchmark observer; NULL in product/probe. Inclusive host
     * durations, not guest time. Timer overhead perturbs these measurements. */
    uint64_t (*profile_now)(void);
    uint64_t profile_memory_ns, profile_io_ns;
    uint32_t pc, first_fetch;
    int fetched, last_post;
    io_event_t first_io[TRACE_SIZE], recent_io[TRACE_SIZE], failed_access;
    io_event_t kbc_io[TRACE_SIZE];
    io_event_t video_status_io[TRACE_SIZE];
    io_event_t pit_io[TRACE_SIZE];
    io_event_t board_diagnostic_io[TRACE_SIZE];
    uint64_t kbc_count, video_status_count, pit_count, board_diagnostic_count;
    video_status_site_t video_status_sites[TRACE_SIZE];
    unsigned video_status_site_count;
    uint64_t video_status_site_overflow;
    cpu_event_t recent_cpu[TRACE_SIZE];
} bm_pcs286_composition_t;


bm_status_t bm_pcs286_composition_initialize(bm_pcs286_composition_t *p, const bm_host_services_t *host, const uint8_t *image, unsigned ram_mib,
 int ff, int classic_rtc, int cmos_mode, const uint8_t *external_cmos, int video,
 const uint8_t *floppy_image, size_t floppy_size, const bm_floppy_drive_config_t *drive);
void bm_pcs286_composition_destroy(bm_pcs286_composition_t *p);
#endif
