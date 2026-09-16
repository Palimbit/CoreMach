/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2008-2020 Sarah Walker
 * Copyright 2016-2020 Miran Grca
 * Copyright 2018-2020 Fred N. van Kempen
 * Copyright 2025 Toni Riikonen
 * Copyright 2026 BluMach contributors
 */
#ifndef BLUMACH_COMPONENTS_FDC765_H
#define BLUMACH_COMPONENTS_FDC765_H

#include <stdint.h>
#include <blumach/components/bus.h>
#include <blumach/components/dma8237.h>
#include <blumach/components/floppy_drive.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bm_fdc765 bm_fdc765_t;
typedef void (*bm_fdc765_irq_fn)(void *context, int asserted);

/* Maximum sector payload accepted by this controller implementation. */
#define BM_FDC765_MAX_SECTOR_SIZE 4096U

typedef struct bm_fdc765_config {
    uint16_t io_base;
    unsigned int dma_channel;
    int disk_change_active_low;
    bm_dma8237_t *dma;
    bm_floppy_drive_t *drives[4];
    bm_fdc765_irq_fn irq;
    void *irq_context;
} bm_fdc765_config_t;

typedef struct bm_fdc765_state {
    uint8_t digital_output;
    uint8_t main_status;
    uint8_t data_rate;
    uint8_t command;
    uint8_t selected_drive;
    uint8_t reset_sense_remaining;
    uint8_t result_count;
    int interrupt_pending;
    int interrupt_asserted;
} bm_fdc765_state_t;

bm_status_t bm_fdc765_create(const bm_host_services_t *host,
                             bm_bus_t *bus,
                             const bm_fdc765_config_t *config,
                             bm_fdc765_t **out_fdc);
void bm_fdc765_destroy(bm_fdc765_t *fdc);
void bm_fdc765_reset(bm_fdc765_t *fdc);
bm_status_t bm_fdc765_state(const bm_fdc765_t *fdc,
                            bm_fdc765_state_t *state);

#ifdef __cplusplus
}
#endif

#endif
