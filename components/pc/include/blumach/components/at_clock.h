/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Draft scheduler adapters, kept out of device register implementations.
 */
#ifndef BLUMACH_COMPONENTS_AT_CLOCK_H
#define BLUMACH_COMPONENTS_AT_CLOCK_H
#include <blumach/components/pit8254.h>
#include <blumach/components/rtc_at.h>
#include <blumach/components/kbc8042.h>
#include <blumach/components/keyboard_at.h>
#include <blumach/components/wd37c65.h>
#include <blumach/components/ata_pio.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct bm_at_clock_link bm_at_clock_link_t;
/* Attach at engine time zero. Rate must match the device's configured native
 * rate. Query exact elapsed native edges before I/O/state access; rearm only
 * at observable transitions. An adapter must also rearm when I/O changes a
 * previously idle device. Map bm_at_clock_link_io, not the raw device I/O.
 * The engine retains callbacks: destroy it before links and devices.
 * Failure must leave no live callback referencing released component state.
 * Existing engine has no unregister API: failed machine construction must
 * destroy the engine first; no pretend detach or borrowed stack context.
 * sync owns the single last-serviced cycle cursor; callers must not also
 * advance the attached device directly. Before external input, sync; after
 * input, changed rearms from the new device state. No clock phase is reset. */
/* DEBUG access bypasses synchronization/advance and uses pure device inspection.
 * Host allocation is explicit so adapters need not inspect opaque chip state. */
bm_status_t bm_at_clock_link_io(void *context, bm_bus_transaction_t *transaction);
bm_status_t bm_at_clock_link_sync(bm_at_clock_link_t *link);
bm_status_t bm_at_clock_link_changed(bm_at_clock_link_t *link);
/* Only after engine destruction. Releases adapter state, not the device. */
void bm_at_clock_link_destroy(bm_at_clock_link_t *link);
bm_status_t bm_pit8254_attach_clock(const bm_host_services_t *host,
                                    bm_engine_t *engine, bm_pit8254_t *pit,
                                    const bm_clock_rate_t *rate,
                                    bm_at_clock_link_t **out_link);
bm_status_t bm_at_rtc_attach_clock(const bm_host_services_t *host,
                                   bm_engine_t *engine, bm_at_rtc_t *rtc,
                                   bm_at_clock_link_t **out_link);
bm_status_t bm_kbc8042_attach_clock(const bm_host_services_t *host,
                                    bm_engine_t *engine, bm_kbc8042_t *kbc,
                                    const bm_clock_rate_t *rate,
                                    bm_at_clock_link_t **out_link);
bm_status_t bm_at_keyboard_attach_clock(const bm_host_services_t *host,
                                        bm_engine_t *engine,
                                        bm_at_keyboard_t *keyboard,
                                        const bm_clock_rate_t *rate,
                                        bm_at_clock_link_t **out_link);
bm_status_t bm_wd37c65_attach_clock(const bm_host_services_t *host,
                                    bm_engine_t *engine, bm_wd37c65_t *fdc,
                                    const bm_clock_rate_t *rate,
                                    bm_at_clock_link_t **out_link);
bm_status_t bm_ata_pio_attach_clock(const bm_host_services_t *host,
                                    bm_engine_t *engine, bm_ata_pio_t *ata,
                                    const bm_clock_rate_t *rate,
                                    bm_at_clock_link_t **out_link);
#ifdef __cplusplus
}
#endif
#endif
