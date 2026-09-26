/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors */
#ifndef BLUMACH_COMPONENTS_AT_KEYBOARD_PAIR_H
#define BLUMACH_COMPONENTS_AT_KEYBOARD_PAIR_H
#include <blumach/components/at_clock.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct bm_at_keyboard_pair bm_at_keyboard_pair_t;
typedef struct bm_at_keyboard_pair_config {
    bm_kbc8042_config_t     controller;
    bm_at_keyboard_config_t keyboard;
} bm_at_keyboard_pair_config_t;
typedef struct bm_at_keyboard_pair_state {
    bm_kbc8042_state_t     controller;
    bm_at_keyboard_state_t keyboard;
    bm_status_t            failure;
} bm_at_keyboard_pair_state_t;
/* Owns both existing native devices, internal byte/command/inhibit wiring and
 * one optional shared clock link. Both configs must use the same logical native
 * service rate (equivalent fractions allowed); this is not a shared physical
 * oscillator claim. Different service rates explicitly reject. Internal link
 * callbacks/contexts must be NULL; IRQ/A20/CPU-reset remain required external
 * outputs. Creation publishes no callbacks, starts with keyboard inhibited.
 * No native handles escape: all mutation goes through this owner.
 * Advance jumps to the earliest deadline: elapse BOTH devices first, then
 * settle KBC, then keyboard. This is a declared same-time functional policy.
 * Host failures retain completed effects/common elapsed prefix, stop the pair,
 * and never become a guest reply or a retry. DEBUG/state inspection stays pure.
 * Native access while clock-attached requires sync then access then changed;
 * prefer link_io/clock_input. Never advance an attached pair directly.
 * Healthy peripheral reset: sync/pair_reset/changed. Engine reset: engine_reset
 * then link_reset for every link. CPU-only reset does not reset this pair.
 * Destroy engine first, then link, then pair. Mutating reentry is forbidden. */
bm_status_t bm_at_keyboard_pair_create(const bm_host_services_t           *host,
                                       const bm_at_keyboard_pair_config_t *config,
                                       bm_at_keyboard_pair_t             **out_pair);
void        bm_at_keyboard_pair_destroy(bm_at_keyboard_pair_t *pair);
bm_status_t bm_at_keyboard_pair_reset(bm_at_keyboard_pair_t *pair);
bm_status_t bm_at_keyboard_pair_io(void *pair, bm_bus_transaction_t *transaction);
bm_status_t bm_at_keyboard_pair_input(bm_at_keyboard_pair_t *pair, const bm_input_event_t *event);
bm_status_t bm_at_keyboard_pair_advance(bm_at_keyboard_pair_t *pair, uint64_t cycles);
bm_status_t bm_at_keyboard_pair_next_deadline(const bm_at_keyboard_pair_t *pair, uint64_t *cycles);
bm_status_t bm_at_keyboard_pair_state(const bm_at_keyboard_pair_t *pair, bm_at_keyboard_pair_state_t *state);
bm_status_t bm_at_keyboard_pair_attach_clock(const bm_host_services_t *host, bm_engine_t *engine,
                                             bm_at_keyboard_pair_t *pair, bm_at_clock_link_t **out_link);
bm_status_t bm_at_keyboard_pair_clock_input(bm_at_clock_link_t *link, const bm_input_event_t *event);
#ifdef __cplusplus
}
#endif
#endif
