/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_COMPONENTS_XTA_CLOCK_H
#define BLUMACH_COMPONENTS_XTA_CLOCK_H

#include <blumach/components/xta.h>
#include <blumach/engine/engine.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Attach a machine-selected service interval for deferred XTA DMA. The source
 * stays disarmed while the controller is idle and retries only while a DMA
 * command is waiting for a usable 8237 channel. This adapter preserves a
 * functional interval; it does not claim mechanical disk timing. */
bm_status_t bm_xta_attach_service_clock(
    bm_engine_t *engine,
    bm_xta_t *xta,
    const bm_clock_rate_t *rate,
    uint64_t service_interval_cycles,
    bm_timed_source_id_t *out_source_id);

#ifdef __cplusplus
}
#endif

#endif
