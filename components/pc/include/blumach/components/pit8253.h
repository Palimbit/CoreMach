/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2019-2020 Miran Grca
 * Copyright 2026 BluMach contributors
 */
#ifndef BLUMACH_COMPONENTS_PIT8253_H
#define BLUMACH_COMPONENTS_PIT8253_H

#include <stdint.h>
#include <blumach/components/bus.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bm_pit8253 bm_pit8253_t;
typedef void (*bm_pit8253_output_fn)(void *context, unsigned int channel, int output);

typedef struct bm_pit8253_config {
    uint16_t io_base;
    bm_pit8253_output_fn output;
    void *output_context;
} bm_pit8253_config_t;

bm_status_t bm_pit8253_create(const bm_host_services_t *host,
                              bm_bus_t *bus,
                              const bm_pit8253_config_t *config,
                              bm_pit8253_t **out_pit);
void bm_pit8253_destroy(bm_pit8253_t *pit);
void bm_pit8253_reset(bm_pit8253_t *pit);
bm_status_t bm_pit8253_set_gate(bm_pit8253_t *pit, unsigned int channel, int asserted);
bm_status_t bm_pit8253_advance(bm_pit8253_t *pit, uint32_t input_ticks);
bm_status_t bm_pit8253_count(const bm_pit8253_t *pit,
                             unsigned int channel,
                             uint16_t *count);
bm_status_t bm_pit8253_output(const bm_pit8253_t *pit,
                              unsigned int channel,
                              int *output);

#ifdef __cplusplus
}
#endif

#endif
