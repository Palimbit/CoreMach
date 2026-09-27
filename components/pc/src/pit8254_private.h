/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors */
#ifndef BM_PIT8254_PRIVATE_H
#define BM_PIT8254_PRIVATE_H
#include <blumach/components/pit8254.h>
#include "pit8253_exact.h"
struct bm_pit8254 {
    bm_host_services_t host;
    bm_pit8254_config_t config;
    bm_pit_exact_device_t exact;
    int busy;
    void *clock_link; /* Single clock owner; released after engine destruction. */
};
#endif
