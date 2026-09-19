/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2022-2026 Daniel Balsom
 * Copyright 2026 Clara
 * Copyright 2026 BluMach contributors
 */
#ifndef BLUMACH_COMPONENTS_PIT8253_EXACT_H
#define BLUMACH_COMPONENTS_PIT8253_EXACT_H

#include <stdbool.h>
#include <stdint.h>

typedef enum bm_pit_exact_state {
    BM_PIT_WAIT_COUNT = 0,
    BM_PIT_WAIT_TRIGGER,
    BM_PIT_WAIT_GATE,
    BM_PIT_LOAD_NEXT,
    BM_PIT_COUNTING,
    BM_PIT_RELOAD_NEXT,
    BM_PIT_STROBE_RECOVER,
    BM_PIT_DONE
} bm_pit_exact_state_t;

typedef struct bm_pit_exact_channel {
    uint8_t control;
    uint8_t mode_raw;
    uint8_t mode;
    uint8_t rw_mode;
    bool bcd;
    uint8_t write_phase;
    uint8_t read_phase;
    bool count_latched;
    uint16_t output_latch;
    uint16_t count_register;
    uint32_t counting_element;
    bool null_count;
    bool gate;
    bool output;
    bool armed;
    bool initial_load;
    bool incomplete_reload;
    bool ce_undefined;
    bool toggle_on_reload;
    bm_pit_exact_state_t state;
    uint64_t clocks;
} bm_pit_exact_channel_t;

typedef struct bm_pit_exact_device {
    bm_pit_exact_channel_t channel[3];
    uint8_t last_control;
} bm_pit_exact_device_t;

void bm_pit_exact_reset(bm_pit_exact_device_t *pit);
void bm_pit_exact_control_write(bm_pit_exact_device_t *pit, uint8_t value);
void bm_pit_exact_data_write(bm_pit_exact_device_t *pit, unsigned int channel, uint8_t value);
uint8_t bm_pit_exact_data_read(bm_pit_exact_device_t *pit, unsigned int channel);
void bm_pit_exact_set_gate(bm_pit_exact_device_t *pit, unsigned int channel, bool gate);
void bm_pit_exact_tick(bm_pit_exact_device_t *pit);
uint32_t bm_pit_exact_cycles_until_output_change(
    const bm_pit_exact_device_t *pit);
uint32_t bm_pit_exact_advance_until_output_change(
    bm_pit_exact_device_t *pit, uint32_t maximum_ticks);
uint16_t bm_pit_exact_get_count(const bm_pit_exact_device_t *pit, unsigned int channel);
bool bm_pit_exact_get_output(const bm_pit_exact_device_t *pit, unsigned int channel);

#endif
