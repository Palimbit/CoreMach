/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2008-2020 Sarah Walker
 * Copyright 2016-2020 Miran Grca
 * Copyright 2017-2020 Fred N. van Kempen
 * Copyright 2026 BluMach contributors
 */
#ifndef BLUMACH_COMPONENTS_DMA8237_H
#define BLUMACH_COMPONENTS_DMA8237_H

#include <stdint.h>
#include <blumach/components/bus.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bm_dma8237 bm_dma8237_t;

typedef struct bm_dma8237_config {
    uint16_t io_base;
} bm_dma8237_config_t;

typedef struct bm_dma8237_channel_state {
    uint16_t base_address;
    uint16_t current_address;
    uint16_t base_count;
    uint16_t current_count;
    uint8_t mode;
    uint8_t page;
    uint32_t current_physical_address;
    int masked;
    int requested;
    int terminal_count;
} bm_dma8237_channel_state_t;

bm_status_t bm_dma8237_create(const bm_host_services_t *host,
                              bm_bus_t *bus,
                              const bm_dma8237_config_t *config,
                              bm_dma8237_t **out_dma);
void bm_dma8237_destroy(bm_dma8237_t *dma);
void bm_dma8237_reset(bm_dma8237_t *dma);
bm_status_t bm_dma8237_set_dreq(bm_dma8237_t *dma,
                                unsigned int channel,
                                int asserted);
bm_status_t bm_dma8237_set_page(bm_dma8237_t *dma,
                                unsigned int channel,
                                uint8_t page);
bm_status_t bm_dma8237_channel_state(const bm_dma8237_t *dma,
                                     unsigned int channel,
                                     bm_dma8237_channel_state_t *out_state);
/* Device-facing DACK operations. A device must assert DREQ first. The write
 * direction moves one byte from the device into guest memory; the read
 * direction moves one byte from guest memory into the device. */
bm_status_t bm_dma8237_device_write(bm_dma8237_t *dma,
                                    unsigned int channel,
                                    uint8_t value,
                                    int *terminal_count);
bm_status_t bm_dma8237_device_read(bm_dma8237_t *dma,
                                   unsigned int channel,
                                   uint8_t *value,
                                   int *terminal_count);
uint8_t bm_dma8237_command(const bm_dma8237_t *dma);
uint8_t bm_dma8237_mask(const bm_dma8237_t *dma);

#ifdef __cplusplus
}
#endif

#endif
