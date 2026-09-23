/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Draft cascaded 8259A contract. Existing single-PIC code is not AT-complete.
 */
#ifndef BLUMACH_COMPONENTS_AT_PIC_H
#define BLUMACH_COMPONENTS_AT_PIC_H
#include <blumach/components/at_bus.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct bm_at_pic bm_at_pic_t;
typedef struct bm_at_pic_config {
    uint16_t master_base;
    uint16_t slave_base;
    uint8_t cascade_line;
    bm_at_line_fn intr;
    void *intr_context;
} bm_at_pic_config_t;
typedef struct bm_at_pic_state {
    uint8_t irr[2], isr[2], imr[2], input_lines[2];
    uint8_t vector_base[2];
    uint8_t acknowledge_phase;
    int intr;
} bm_at_pic_state_t;
bm_status_t bm_at_pic_create(const bm_host_services_t *host,
                             const bm_at_pic_config_t *config,
                             bm_at_pic_t **out_pic);
void bm_at_pic_destroy(bm_at_pic_t *pic);
void bm_at_pic_reset(bm_at_pic_t *pic);
bm_status_t bm_at_pic_io(void *context, bm_bus_transaction_t *transaction);
bm_status_t bm_at_pic_set_irq(bm_at_pic_t *pic, unsigned int irq, int asserted);
/* phase 0 latches the selected request; phase 1 delivers the vector. Correct
 * cascade/ICW3, separate EOIs, priority, level/edge mode and spurious IRQ7/15
 * are required. No vector-base hardcoding and no slave ack for master IRQs.
 * The board adapts wait counts to CPU clocks; this core does not own time. */
bm_status_t bm_at_pic_acknowledge(bm_at_pic_t *pic, unsigned int phase,
                                  uint8_t *vector);
bm_status_t bm_at_pic_state(const bm_at_pic_t *pic, bm_at_pic_state_t *out_state);
#ifdef __cplusplus
}
#endif
#endif
