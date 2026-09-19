/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2019-2020 Miran Grca
 * Copyright 2022-2026 Daniel Balsom
 * Copyright 2026 Clara
 * Copyright 2026 BluMach contributors
 */
#ifndef BLUMACH_COMPONENTS_PIT8253_PRIVATE_H
#define BLUMACH_COMPONENTS_PIT8253_PRIVATE_H

#include <blumach/components/pit8253.h>

#include "pit8253_exact.h"

typedef bm_status_t (*bm_pit8253_clock_sync_fn)(void *context);
typedef bm_status_t (*bm_pit8253_clock_changed_fn)(void *context);

struct bm_pit8253 {
    bm_host_services_t host;
    uint16_t io_base;
    bm_pit8253_output_fn output;
    void *output_context;
    bm_pit8253_clock_sync_fn clock_sync;
    bm_pit8253_clock_changed_fn clock_changed;
    void *clock_context;
    void *clock_binding;
    bm_pit_exact_device_t exact;
};

#endif
