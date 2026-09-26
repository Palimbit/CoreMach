/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors */
#ifndef BM_RTC_AT_PRIVATE_H
#define BM_RTC_AT_PRIVATE_H
#include <blumach/components/rtc_at.h>
struct bm_at_rtc {
    bm_host_services_t host;
    bm_at_rtc_config_t config;
    bm_at_rtc_state_t state;
    uint8_t regs[BM_AT_RTC_CMOS_BYTES];
    int busy;
    void *clock_link; /* One borrowed clock owner; engine destroyed first. */
};
#endif
