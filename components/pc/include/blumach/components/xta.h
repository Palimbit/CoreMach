/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright 2017-2018 Fred N. van Kempen
 * Copyright 2026 rtzor
 * Copyright 2026 BluMach contributors
 */
#ifndef BLUMACH_COMPONENTS_XTA_H
#define BLUMACH_COMPONENTS_XTA_H

#include <stdint.h>
#include <blumach/components/bus.h>
#include <blumach/components/dma8237.h>
#include <blumach/engine/storage.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bm_xta bm_xta_t;
typedef void (*bm_xta_irq_fn)(void *context, int asserted);

typedef struct bm_xta_geometry {
    uint16_t cylinders;
    uint8_t heads;
    uint8_t sectors_per_track;
} bm_xta_geometry_t;

typedef struct bm_xta_config {
    uint16_t io_base;
    uint8_t option_switches;
    unsigned int dma_channel;
    bm_dma8237_t *dma;
    bm_xta_irq_fn irq;
    void *irq_context;
    int drive_present;
    bm_xta_geometry_t geometry;
    bm_block_media_t media;
} bm_xta_config_t;

typedef struct bm_xta_state {
    uint8_t status;
    uint8_t sense;
    uint8_t completion;
    uint8_t interrupt_mask;
    uint16_t cylinder;
    int enabled;
    int interrupt_asserted;
} bm_xta_state_t;

bm_status_t bm_xta_config_validate(const bm_xta_config_t *config);
bm_status_t bm_xta_create(const bm_host_services_t *host,
                          bm_bus_t *bus,
                          const bm_xta_config_t *config,
                          bm_xta_t **out_xta);
void bm_xta_destroy(bm_xta_t *xta);
void bm_xta_reset(bm_xta_t *xta);
void bm_xta_set_enabled(bm_xta_t *xta, int enabled);
bm_status_t bm_xta_state(const bm_xta_t *xta, bm_xta_state_t *state);

#ifdef __cplusplus
}
#endif

#endif
