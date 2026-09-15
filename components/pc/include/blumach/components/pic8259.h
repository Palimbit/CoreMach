/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2015-2020 Andrew Jenner
 * Copyright 2016-2020 Miran Grca
 * Copyright 2026 BluMach contributors
 */
#ifndef BLUMACH_COMPONENTS_PIC8259_H
#define BLUMACH_COMPONENTS_PIC8259_H

#include <stdint.h>
#include <blumach/components/bus.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bm_pic8259 bm_pic8259_t;
typedef void (*bm_pic8259_output_fn)(void *context, int asserted);

typedef struct bm_pic8259_config {
    uint16_t io_base;
    bm_pic8259_output_fn output;
    void *output_context;
} bm_pic8259_config_t;

typedef struct bm_pic8259_state {
    uint8_t interrupt_mask;
    uint8_t interrupt_requests;
    uint8_t in_service;
    uint8_t input_lines;
} bm_pic8259_state_t;

bm_status_t bm_pic8259_create(const bm_host_services_t *host,
                              bm_bus_t *bus,
                              const bm_pic8259_config_t *config,
                              bm_pic8259_t **out_pic);
void bm_pic8259_destroy(bm_pic8259_t *pic);
void bm_pic8259_reset(bm_pic8259_t *pic);
bm_status_t bm_pic8259_set_irq(bm_pic8259_t *pic, unsigned int line, int asserted);
int bm_pic8259_pending(const bm_pic8259_t *pic);
bm_status_t bm_pic8259_acknowledge(bm_pic8259_t *pic, uint8_t *vector);
bm_status_t bm_pic8259_state(const bm_pic8259_t *pic, bm_pic8259_state_t *state);

#ifdef __cplusplus
}
#endif

#endif
