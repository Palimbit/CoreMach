/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2018 Fred N. van Kempen
 * Copyright 2026 BluMach contributors
 */
#ifndef BLUMACH_COMPONENTS_RTC_MM58167_H
#define BLUMACH_COMPONENTS_RTC_MM58167_H

#include <stddef.h>
#include <stdint.h>
#include <blumach/components/bus.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bm_mm58167 bm_mm58167_t;
typedef void (*bm_mm58167_irq_fn)(void *context, int asserted);

#define BM_MM58167_STATE_SIZE 32U

typedef struct bm_mm58167_config {
    uint16_t interrupt_io_base;
    uint16_t counter_io_base;
    bm_mm58167_irq_fn interrupt;
    void *interrupt_context;
    const uint8_t *initial_state;
    size_t initial_state_size;
} bm_mm58167_config_t;

bm_status_t bm_mm58167_create(const bm_host_services_t *host,
                              bm_bus_t *bus,
                              const bm_mm58167_config_t *config,
                              bm_mm58167_t **out_rtc);
void bm_mm58167_destroy(bm_mm58167_t *rtc);
void bm_mm58167_reset(bm_mm58167_t *rtc);
void bm_mm58167_cold_reset(bm_mm58167_t *rtc);
bm_status_t bm_mm58167_advance_microseconds(bm_mm58167_t *rtc,
                                            uint64_t microseconds);
bm_status_t bm_mm58167_save_state(const bm_mm58167_t *rtc,
                                  uint8_t *state,
                                  size_t state_size);
bm_status_t bm_mm58167_load_state(bm_mm58167_t *rtc,
                                  const uint8_t *state,
                                  size_t state_size);
bm_status_t bm_mm58167_set_interrupt_status(bm_mm58167_t *rtc,
                                            uint8_t status);
uint8_t bm_mm58167_interrupt_control(const bm_mm58167_t *rtc);

#ifdef __cplusplus
}
#endif

#endif
