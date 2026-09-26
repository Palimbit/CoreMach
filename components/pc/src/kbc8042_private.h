/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2023-2025 Miran Grca and EngiNerd
 * Copyright 2026 BluMach contributors
 * Instance layout moved from the attributed native implementation. */
#ifndef BM_KBC8042_PRIVATE_H
#define BM_KBC8042_PRIVATE_H
#include <blumach/components/kbc8042.h>
struct bm_kbc8042 {
    bm_host_services_t host;
    bm_kbc8042_config_t config;
    bm_kbc8042_state_t s;
    uint8_t input, pending_output, pulse_restore;
    int input_command, busy;
    void *clock_link; /* Borrowed sole clock owner; engine destroyed first. */
};
/* Private owner-only two-phase service. Caller bounds cycles by both peers'
 * next deadlines and preflights lifetime overflow. Elapse publishes nothing;
 * settle runs with this device busy, after both peers reached the boundary. */
typedef struct bm_kbc_edge { int input, output, pulse; } bm_kbc_edge_t;
void bm_kbc8042_elapse(bm_kbc8042_t *k, uint64_t cycles, bm_kbc_edge_t *edge);
bm_status_t bm_kbc8042_settle(bm_kbc8042_t *k, const bm_kbc_edge_t *edge);
#endif
