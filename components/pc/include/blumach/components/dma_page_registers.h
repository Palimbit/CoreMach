/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2008-2020 Sarah Walker
 * Copyright 2016-2020 Miran Grca
 * Copyright 2017-2020 Fred N. van Kempen
 * Copyright 2026 BluMach contributors
 */
#ifndef BLUMACH_COMPONENTS_DMA_PAGE_REGISTERS_H
#define BLUMACH_COMPONENTS_DMA_PAGE_REGISTERS_H

#include <stdint.h>
#include <blumach/components/dma8237.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bm_dma_page_registers bm_dma_page_registers_t;

typedef struct bm_dma_page_registers_config {
    uint16_t io_base;
    uint8_t page_mask; /* Wired address bits, not the readable latch width. */
    bm_dma8237_t *dma;
} bm_dma_page_registers_config_t;

bm_status_t bm_dma_page_registers_create(
    const bm_host_services_t *host,
    bm_bus_t *bus,
    const bm_dma_page_registers_config_t *config,
    bm_dma_page_registers_t **out_registers);
void bm_dma_page_registers_destroy(bm_dma_page_registers_t *registers);
void bm_dma_page_registers_reset(bm_dma_page_registers_t *registers);

#ifdef __cplusplus
}
#endif

#endif
