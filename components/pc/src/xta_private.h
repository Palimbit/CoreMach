/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 */
#ifndef BLUMACH_COMPONENTS_XTA_PRIVATE_H
#define BLUMACH_COMPONENTS_XTA_PRIVATE_H

#include <blumach/components/xta.h>

typedef bm_status_t (*bm_xta_service_changed_fn)(void *context);

struct bm_xta {
    bm_host_services_t host;
    bm_dma8237_t *dma;
    bm_block_media_t media;
    bm_xta_geometry_t physical_geometry;
    bm_xta_geometry_t active_geometry;
    bm_xta_irq_fn irq;
    void *irq_context;
    bm_xta_service_changed_fn service_changed;
    void *service_context;
    void *service_binding;
    unsigned int dma_channel;
    uint8_t option_switches;
    uint8_t phase;
    uint8_t status;
    uint8_t sense;
    uint8_t completion;
    uint8_t interrupt_mask;
    uint8_t dcb[6];
    uint8_t buffer[512];
    uint8_t sector_buffer[512];
    size_t buffer_index;
    size_t buffer_length;
    uint16_t cylinder;
    uint8_t head;
    uint8_t sector;
    unsigned int remaining;
    uint64_t read_operations;
    uint64_t write_operations;
    uint8_t active_command;
    int drive_present;
    int enabled;
    int interrupt_asserted;
};

#endif
