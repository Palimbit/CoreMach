/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Draft ATA PIO contract. XTA is a different interface and is not substituted.
 */
#ifndef BLUMACH_COMPONENTS_ATA_PIO_H
#define BLUMACH_COMPONENTS_ATA_PIO_H
#include <blumach/components/at_bus.h>
#include <blumach/engine/storage.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct bm_ata_pio bm_ata_pio_t;
typedef struct bm_ata_drive_config {
    int installed;
    uint16_t cylinders;
    uint8_t heads, sectors_per_track;
    bm_block_media_t media;
} bm_ata_drive_config_t;
typedef struct bm_ata_pio_config {
    uint16_t command_base, control_port;
    bm_ata_drive_config_t drives[2];
    bm_at_line_fn irq;
    void *irq_context;
    bm_clock_rate_t clock;
    /* Functional scheduler delay, explicitly provisional until sourced.
     * Disk mechanics must not be presented as CPU/ISA wait states. */
    uint64_t command_cycles;
} bm_ata_pio_config_t;
bm_status_t bm_ata_pio_create(const bm_host_services_t *host,
                              const bm_ata_pio_config_t *config,
                              bm_ata_pio_t **out_ata);
void bm_ata_pio_destroy(bm_ata_pio_t *ata);
void bm_ata_pio_reset(bm_ata_pio_t *ata);
bm_status_t bm_ata_pio_io(void *context, bm_bus_transaction_t *transaction);
bm_status_t bm_ata_pio_advance(bm_ata_pio_t *ata, uint64_t cycles);
bm_status_t bm_ata_pio_next_deadline(const bm_ata_pio_t *ata, uint64_t *cycles);
bm_status_t bm_ata_pio_status(const bm_ata_pio_t *ata, unsigned int drive,
                              bm_storage_device_status_t *out_status);
/* First profile: CHS, 512-byte sectors, 16-bit data register, 8-bit task-file
 * registers, SRST/nIEN, alternate-status versus status IRQ acknowledgement,
 * BSY/DRQ/ERR and truthful IDENTIFY. Unsupported guest commands set ABRT;
 * do not return host unsupported or claim success. Validate geometry/media
 * capacity before creation; absent drive is an electrical/device response. */
#ifdef __cplusplus
}
#endif
#endif
