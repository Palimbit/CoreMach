/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Draft MC146818-compatible register contract; exact package is unverified.
 */
#ifndef BLUMACH_COMPONENTS_RTC_AT_H
#define BLUMACH_COMPONENTS_RTC_AT_H
#include <blumach/components/at_bus.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct bm_at_rtc bm_at_rtc_t;
#define BM_AT_RTC_CMOS_BYTES 128U
typedef struct bm_at_rtc_config {
    uint16_t io_base;
    const uint8_t *initial_cmos; /* copied; exactly 128 bytes or NULL/depleted */
    size_t initial_cmos_size;
    int battery_valid;
    bm_at_line_fn irq;
    bm_at_line_fn nmi_mask;
    void *output_context;
} bm_at_rtc_config_t;
bm_status_t bm_at_rtc_create(const bm_host_services_t *host,
                             const bm_at_rtc_config_t *config,
                             bm_at_rtc_t **out_rtc);
void bm_at_rtc_destroy(bm_at_rtc_t *rtc);
/* Warm board reset preserves battery-backed bytes/calendar. Cold power and
 * depleted battery are explicit configuration, never host time at reset. */
void bm_at_rtc_reset(bm_at_rtc_t *rtc);
bm_status_t bm_at_rtc_io(void *context, bm_bus_transaction_t *transaction);
/* Native oscillator edges at 32768 Hz. UIP, SET, binary/BCD, 12/24 h, alarms,
 * periodic/update IRQs and status-C read-to-clear must be tested explicitly. */
bm_status_t bm_at_rtc_advance(bm_at_rtc_t *rtc, uint64_t cycles);
bm_status_t bm_at_rtc_next_deadline(const bm_at_rtc_t *rtc, uint64_t *cycles);
bm_status_t bm_at_rtc_export_cmos(const bm_at_rtc_t *rtc,
                                  uint8_t *bytes, size_t size);
#ifdef __cplusplus
}
#endif
#endif
