/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_COMPONENTS_RTC_MM58167_CLOCK_H
#define BLUMACH_COMPONENTS_RTC_MM58167_CLOCK_H

#include <blumach/components/rtc_mm58167.h>
#include <blumach/engine/engine.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Attach the RTC's microsecond timebase to an exact engine clock domain. The
 * optional adapter advances lazily to the next millisecond or 150 us rollover
 * boundary and synchronizes before component I/O and state operations. The
 * engine must be destroyed before the RTC because it retains the source
 * callback. Registration is subject to the engine's time-zero construction
 * rule, and an RTC can have at most one clock attachment. */
bm_status_t bm_mm58167_attach_clock(bm_engine_t *engine,
                                    bm_mm58167_t *rtc,
                                    bm_timed_source_id_t *out_source_id);

#ifdef __cplusplus
}
#endif

#endif
