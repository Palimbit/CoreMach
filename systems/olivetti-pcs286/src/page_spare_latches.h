/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Private PCS286 board-owned latches in the AT DMA-page register bank.
 */
#ifndef BLUMACH_PCS286_PAGE_SPARE_LATCHES_H
#define BLUMACH_PCS286_PAGE_SPARE_LATCHES_H
#include <blumach/components/at_bus.h>

typedef struct bm_pcs286_page_spare_latches {
    uint8_t value[16];
} bm_pcs286_page_spare_latches_t;

void bm_pcs286_page_spare_latches_initialize(
    bm_pcs286_page_spare_latches_t *latches);
bm_status_t bm_pcs286_page_spare_latches_io(
    void *context, bm_bus_transaction_t *transaction);

#endif
