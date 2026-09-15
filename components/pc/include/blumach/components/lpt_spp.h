/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright holders: Sarah Walker
 * Copyright 2026 BluMach contributors
 */
#ifndef BLUMACH_COMPONENTS_LPT_SPP_H
#define BLUMACH_COMPONENTS_LPT_SPP_H

#include <blumach/engine/host.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bm_lpt_spp bm_lpt_spp_t;

typedef void (*bm_lpt_spp_output_fn)(void *context,
                                      uint8_t data,
                                      uint8_t control);
typedef void (*bm_lpt_spp_irq_fn)(void *context, int asserted);

typedef struct bm_lpt_spp_config {
    bm_lpt_spp_output_fn output;
    bm_lpt_spp_irq_fn irq;
    void *context;
} bm_lpt_spp_config_t;

typedef struct bm_lpt_spp_state {
    uint8_t data;
    uint8_t status;
    uint8_t control;
} bm_lpt_spp_state_t;

bm_status_t bm_lpt_spp_create(const bm_host_services_t *host,
                              const bm_lpt_spp_config_t *configuration,
                              bm_lpt_spp_t **out_lpt);
void bm_lpt_spp_destroy(bm_lpt_spp_t *lpt);
void bm_lpt_spp_reset(bm_lpt_spp_t *lpt);
bm_status_t bm_lpt_spp_read(bm_lpt_spp_t *lpt,
                            unsigned int register_index,
                            uint8_t *value);
bm_status_t bm_lpt_spp_write(bm_lpt_spp_t *lpt,
                             unsigned int register_index,
                             uint8_t value);
bm_status_t bm_lpt_spp_set_status(bm_lpt_spp_t *lpt, uint8_t status);
bm_status_t bm_lpt_spp_state(const bm_lpt_spp_t *lpt,
                             bm_lpt_spp_state_t *state);

#ifdef __cplusplus
}
#endif

#endif
