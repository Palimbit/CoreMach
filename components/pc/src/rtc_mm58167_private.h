/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2018 Fred N. van Kempen
 * Copyright 2026 BluMach contributors
 */
#ifndef BLUMACH_COMPONENTS_RTC_MM58167_PRIVATE_H
#define BLUMACH_COMPONENTS_RTC_MM58167_PRIVATE_H

#include <blumach/components/rtc_mm58167.h>

typedef bm_status_t (*bm_mm58167_clock_sync_fn)(void *context);
typedef bm_status_t (*bm_mm58167_clock_changed_fn)(void *context);

struct bm_mm58167 {
    bm_host_services_t host;
    uint16_t interrupt_io_base;
    uint16_t counter_io_base;
    bm_mm58167_irq_fn interrupt;
    void *interrupt_context;
    bm_mm58167_clock_sync_fn clock_sync;
    bm_mm58167_clock_changed_fn clock_changed;
    void *clock_context;
    void *clock_binding;
    uint8_t registers[BM_MM58167_STATE_SIZE];
    uint16_t millisecond_count;
    uint16_t microsecond_remainder;
    uint16_t rollover_microseconds;
    uint8_t irq_asserted;
};

#endif
