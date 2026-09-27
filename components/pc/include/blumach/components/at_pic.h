/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Cascaded 8259A boundary contract; independent of the single-PIC XT core.
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
/* Implements non-buffered 8086 mode, ICW3 cascade selection, fully nested
 * priorities, edge/level input, EOI/AEOI (1985+ silicon), rotation and special
 * masks. MCS-80/85, buffered, SFNM and poll commands return UNSUPPORTED without
 * mutation. No physical pin delays or timing claim. Reset is an emulator
 * lifecycle operation: masks all, clears inputs and requires initialization;
 * it is not an invented hardware RESET pin/default vector. Creation registers
 * no bus maps and emits no callback. Destroy does not call external contexts;
 * reset/disconnect output first if a live consumer needs it lowered.
 * Callbacks may signal a CPU but must not reenter/mutate/destroy this object. */
bm_status_t bm_at_pic_create(const bm_host_services_t *host,
                             const bm_at_pic_config_t *config,
                             bm_at_pic_t **out_pic);
void bm_at_pic_destroy(bm_at_pic_t *pic);
void bm_at_pic_reset(bm_at_pic_t *pic);
bm_status_t bm_at_pic_io(void *context, bm_bus_transaction_t *transaction);
/* IRQ0..7 are master pins, IRQ8..15 slave pins. The configured master cascade
 * input combines the slave INT and the external pin with OR. Normal AT board
 * wiring must not drive that pin independently; raw-pin diagnostics can model
 * a cascade request without a slave request (spurious slave IRQ7).
 * Falling inputs withdraw requests not yet acknowledged. Phase 0 freezes the
 * chosen master/slave levels; later edges cannot replace that vector. */
bm_status_t bm_at_pic_set_irq(bm_at_pic_t *pic, unsigned int irq, int asserted);
/* phase 0 latches the selected request; phase 1 delivers the vector. Correct
 * cascade/ICW3, separate EOIs, priority, level/edge mode and spurious IRQ7/15
 * are modeled. No vector-base hardcoding and no slave ack for master IRQs.
 * vector is untouched in phase 0; phase 1 completes AEOI. Calls out of order
 * fail without mutation. An ICW3-selected absent/mismatched slave returns
 * UNSUPPORTED before acknowledgement, not a fabricated open-bus vector.
 * I/O writes during the pair return INVALID_STATE. Reads (including DEBUG)
 * are pure; DEBUG writes are rejected. Only byte I/O is accepted and existing
 * wait_states is preserved. Board converts waits; this core does not own time. */
bm_status_t bm_at_pic_acknowledge(bm_at_pic_t *pic, unsigned int phase,
                                  uint8_t *vector);
bm_status_t bm_at_pic_state(const bm_at_pic_t *pic, bm_at_pic_state_t *out_state);
#ifdef __cplusplus
}
#endif
#endif
