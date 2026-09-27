/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2016-2023 Miran Grca
 * Copyright 2017-2023 Fred N. van Kempen
 * Copyright 2026 BluMach contributors
 * Instance layout moved from the attributed native implementation. */
#ifndef BM_KEYBOARD_AT_PRIVATE_H
#define BM_KEYBOARD_AT_PRIVATE_H
#include <blumach/components/keyboard_at.h>
struct bm_at_keyboard {
    bm_host_services_t      host;
    bm_at_keyboard_config_t config;
    bm_at_keyboard_state_t  s;
    uint64_t                delays[4], periods[32];
    uint8_t                 scans[16], replies[3], actions[3], modes[256], down[256], scanned[256];
    int                     busy, sending, identifying, resend_pending;
    uint8_t                 resend_byte;
    void *clock_link; /* Borrowed sole clock owner; engine destroyed first. */
};
/* Same private two-phase contract as the KBC; no public partial-advance API. */
typedef struct bm_keyboard_edge {
    int phase, tx, repeat;
    bm_at_keyboard_phase_t old_phase;
} bm_keyboard_edge_t;
void bm_at_keyboard_elapse(bm_at_keyboard_t *k, uint64_t cycles, bm_keyboard_edge_t *edge);
bm_status_t bm_at_keyboard_settle(bm_at_keyboard_t *k, const bm_keyboard_edge_t *edge);
#endif
