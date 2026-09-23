/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Draft AT keyboard protocol device, independently testable from the KBC.
 */
#ifndef BLUMACH_COMPONENTS_KEYBOARD_AT_H
#define BLUMACH_COMPONENTS_KEYBOARD_AT_H
#include <blumach/components/kbc8042.h>
#include <blumach/engine/input.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct bm_at_keyboard bm_at_keyboard_t;
typedef struct bm_at_keyboard_config {
    bm_at_keyboard_byte_fn send;
    void *send_context;
    bm_clock_rate_t clock;
    uint64_t bat_cycles, byte_cycles;
} bm_at_keyboard_config_t;
bm_status_t bm_at_keyboard_create(const bm_host_services_t *host,
                                   const bm_at_keyboard_config_t *config,
                                   bm_at_keyboard_t **out_keyboard);
void bm_at_keyboard_destroy(bm_at_keyboard_t *keyboard);
void bm_at_keyboard_reset(bm_at_keyboard_t *keyboard);
bm_status_t bm_at_keyboard_command(bm_at_keyboard_t *keyboard, uint8_t command);
bm_status_t bm_at_keyboard_input(bm_at_keyboard_t *keyboard,
                                  const bm_input_event_t *event);
bm_status_t bm_at_keyboard_leds(const bm_at_keyboard_t *keyboard,
                                 bm_keyboard_led_state_t *state);
bm_status_t bm_at_keyboard_set_inhibit(bm_at_keyboard_t *keyboard, int level);
bm_status_t bm_at_keyboard_advance(bm_at_keyboard_t *keyboard, uint64_t cycles);
bm_status_t bm_at_keyboard_next_deadline(const bm_at_keyboard_t *keyboard,
                                         uint64_t *cycles);
/* ACK, RESEND, BAT, scan enable, make/break, set selection, typematic and
 * guest LED commands belong here. Controller translation remains in 8042.
 * A rejected delivery retains the byte; callbacks cannot recurse into advance.
 * No host layout, host repeats, file I/O or host lock-state dependency. */
#ifdef __cplusplus
}
#endif
#endif
