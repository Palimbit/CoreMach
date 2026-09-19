/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_COMPONENTS_PIT8253_CLOCK_H
#define BLUMACH_COMPONENTS_PIT8253_CLOCK_H

#include <blumach/components/pit8253.h>
#include <blumach/engine/engine.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Attach the PIT input clock to an exact engine clock domain. The PIT remains
 * independent of the scheduler; this optional adapter advances one physical
 * input edge per timed-source activation. The PIT must remain alive while the
 * engine can execute or reset the source. Registration is subject to the
 * engine's time-zero construction rule. */
bm_status_t bm_pit8253_attach_clock(bm_engine_t *engine,
                                    bm_pit8253_t *pit,
                                    const bm_clock_rate_t *rate,
                                    bm_timed_source_id_t *out_source_id);

#ifdef __cplusplus
}
#endif

#endif
