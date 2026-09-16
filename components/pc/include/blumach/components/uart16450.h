/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2016-2025 Miran Grca
 * Copyright 2017-2020 Fred N. van Kempen
 * Copyright 2026 BluMach contributors
 */
#ifndef BLUMACH_COMPONENTS_UART16450_H
#define BLUMACH_COMPONENTS_UART16450_H

#include <blumach/engine/host.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bm_uart16450 bm_uart16450_t;

typedef void (*bm_uart16450_transmit_fn)(void *context, uint8_t value);
typedef void (*bm_uart16450_irq_fn)(void *context, int asserted);

typedef struct bm_uart16450_config {
    bm_uart16450_transmit_fn transmit;
    bm_uart16450_irq_fn irq;
    void *context;
} bm_uart16450_config_t;

typedef struct bm_uart16450_state {
    uint16_t divisor;
    uint8_t interrupt_enable;
    uint8_t interrupt_identification;
    uint8_t line_control;
    uint8_t modem_control;
    uint8_t line_status;
    uint8_t modem_status;
    uint8_t scratch;
} bm_uart16450_state_t;

bm_status_t bm_uart16450_create(const bm_host_services_t *host,
                                const bm_uart16450_config_t *configuration,
                                bm_uart16450_t **out_uart);
void bm_uart16450_destroy(bm_uart16450_t *uart);
void bm_uart16450_reset(bm_uart16450_t *uart);
bm_status_t bm_uart16450_read(bm_uart16450_t *uart,
                              unsigned int register_index,
                              uint8_t *value);
bm_status_t bm_uart16450_write(bm_uart16450_t *uart,
                               unsigned int register_index,
                               uint8_t value);
bm_status_t bm_uart16450_receive(bm_uart16450_t *uart, uint8_t value);
bm_status_t bm_uart16450_state(const bm_uart16450_t *uart,
                               bm_uart16450_state_t *state);

#ifdef __cplusplus
}
#endif

#endif
