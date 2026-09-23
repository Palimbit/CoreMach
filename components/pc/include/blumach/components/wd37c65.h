/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Draft WD37C65B contract. Existing 765 is a reuse candidate, not a full model.
 */
#ifndef BLUMACH_COMPONENTS_WD37C65_H
#define BLUMACH_COMPONENTS_WD37C65_H
#include <blumach/components/at_bus.h>
#include <blumach/components/floppy_drive.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct bm_wd37c65 bm_wd37c65_t;
typedef struct bm_wd37c65_config {
    uint16_t io_base;
    bm_floppy_drive_t *drives[4]; /* borrowed, installed != media present */
    bm_at_line_fn irq;
    bm_at_line_fn dreq;
    void *output_context;
    bm_clock_rate_t clock;
} bm_wd37c65_config_t;
bm_status_t bm_wd37c65_create(const bm_host_services_t *host,
                              const bm_wd37c65_config_t *config,
                              bm_wd37c65_t **out_fdc);
void bm_wd37c65_destroy(bm_wd37c65_t *fdc);
void bm_wd37c65_reset(bm_wd37c65_t *fdc);
/* Board decodes only owned ports (3F6h belongs to ATA at the standard base).
 * Commands/results, DOR, DIR, data-rate selection, DMA/non-DMA and reset-sense
 * sequencing must be proved against the WD part, not merely renamed. */
bm_status_t bm_wd37c65_io(void *context, bm_bus_transaction_t *transaction);
bm_status_t bm_wd37c65_dma_read(bm_wd37c65_t *fdc, uint8_t *value);
bm_status_t bm_wd37c65_dma_write(bm_wd37c65_t *fdc, uint8_t value);
bm_status_t bm_wd37c65_set_terminal_count(bm_wd37c65_t *fdc, int level);
bm_status_t bm_wd37c65_advance(bm_wd37c65_t *fdc, uint64_t cycles);
bm_status_t bm_wd37c65_next_deadline(const bm_wd37c65_t *fdc, uint64_t *cycles);
/* No disk, write protection and normal seek/read failures produce guest
 * status/result bytes and appropriate IRQ, not a fatal runtime error.
 * Sector buffers are bounded before media callbacks. No host file handles. */
#ifdef __cplusplus
}
#endif
#endif
