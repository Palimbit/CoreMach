/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_COMPONENTS_RTC_MSM6242_H
#define BLUMACH_COMPONENTS_RTC_MSM6242_H

#include <stddef.h>
#include <stdint.h>
#include <blumach/components/bus.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BM_MSM6242_STATE_SIZE 16U

typedef struct bm_msm6242 bm_msm6242_t;

typedef struct bm_msm6242_config {
    uint16_t io_base;
    const uint8_t *initial_state;
    size_t initial_state_size;
} bm_msm6242_config_t;

/* All register and calendar state belongs to this instance. Without an
 * initial_state, the deterministic default is 1980-01-01 00:00:00 (Tuesday)
 * in 24-hour mode. The device never reads the host clock. */
bm_status_t bm_msm6242_create(const bm_host_services_t *host, bm_bus_t *bus,
                              const bm_msm6242_config_t *config,
                              bm_msm6242_t **out_rtc);
void bm_msm6242_destroy(bm_msm6242_t *rtc);
/* Advance one second of virtual chip time. HOLD, STOP and REST gate counting.
 * Interrupt output, pulse rates, BUSY timing and TEST mode are not modeled. */
bm_status_t bm_msm6242_advance_second(bm_msm6242_t *rtc);
bm_status_t bm_msm6242_save_state(const bm_msm6242_t *rtc,
                                  uint8_t *state, size_t size);

#ifdef __cplusplus
}
#endif

#endif
