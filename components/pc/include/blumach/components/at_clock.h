/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * PIT8254, AT RTC, KBC and keyboard scheduler adapters.
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
 * input, changed rearms from the new device state at that same boundary. It
 * rejects unsynchronized input rather than advancing under the new state.
 * No clock phase is reset. Only one link may attach to a device, which must not
 * have advanced native pulses yet; programming at time zero is allowed.
 * Non-debug rejected I/O still synchronizes elapsed time. Host clock/device
 * failures latch until explicit full reset; register errors do not poison the
 * link. Completed output/register effects survive scheduling failure, caller
 * transaction results are staged. No retry or guest exception is synthesized.
 * Synchronous output callbacks may inspect via DEBUG but must not mutate the
 * link; status-returning reentry rejects, destroy during callback is ignored. */
/* DEBUG access bypasses synchronization/advance and uses pure device inspection.
 * Host allocation is explicit so adapters need not inspect opaque chip state. */
bm_status_t bm_at_clock_link_io(void *context, bm_bus_transaction_t *transaction);
bm_status_t bm_at_clock_link_sync(bm_at_clock_link_t *link);
bm_status_t bm_at_clock_link_changed(bm_at_clock_link_t *link);
/* Full-machine reset: call engine_reset, then reset EVERY link before any run,
 * access or signal. Requires exact engine time zero, resets device and cursor.
 * Engine reset does not reset these borrowed peripherals. Do not infer reset
 * from a decreasing clock: an unobserved epoch can also overtake the old cursor.
 * RTC: synchronize BEFORE resetting a healthy engine to preserve elapsed time.
 * Link reset rebases only its engine cursor; RTC warm reset preserves calendar,
 * integer divider phase, update state and lifetime cycle count. The new engine
 * epoch restarts fractional oscillator phase: this is an emulator lifecycle
 * policy, not a physical board-reset action. Never discard an epoch to emulate
 * a warm board reset: sync; bm_at_rtc_reset; changed at the same boundary.
 * A failed sync/reset must stop the owner, never replay a partially sent interval.
 * CPU-only warm reset does not reset engine, link, PIT or RTC. Single-threaded owner
 * operation only, never from a CPU/event/device callback. */
bm_status_t bm_at_clock_link_reset(bm_at_clock_link_t *link);
/* Only after engine destruction. Releases adapter state, not the device. */
void bm_at_clock_link_destroy(bm_at_clock_link_t *link);
bm_status_t bm_pit8254_attach_clock(const bm_host_services_t *host,
                                    bm_engine_t *engine, bm_pit8254_t *pit,
                                    const bm_clock_rate_t *rate,
                                    bm_at_clock_link_t **out_link);
/* RTC uses its fixed 32768/1 Hz rate. Ordinary read-C reactivates the next PF
 * event even if the previous sticky flag let the source become idle. */
bm_status_t bm_at_rtc_attach_clock(const bm_host_services_t *host,
                                   bm_engine_t *engine, bm_at_rtc_t *rtc,
                                   bm_at_clock_link_t **out_link);
/* KBC/keyboard use their configured native rate: mathematically equivalent
 * rational rates are accepted, mismatches reject before allocation. */
bm_status_t bm_kbc8042_attach_clock(const bm_host_services_t *host,
                                    bm_engine_t *engine, bm_kbc8042_t *kbc,
                                    const bm_clock_rate_t *rate,
                                    bm_at_clock_link_t **out_link);
bm_status_t bm_at_keyboard_attach_clock(const bm_host_services_t *host,
                                        bm_engine_t *engine,
                                        bm_at_keyboard_t *keyboard,
                                        const bm_clock_rate_t *rate,
                                        bm_at_clock_link_t **out_link);
/* Typed external inputs: sync elapsed pulses, apply the input, rearm even after
 * an ordinary rejected input. A wrong device-link type rejects unchanged.
 * Keyboard has no I/O ports: link_io returns UNSUPPORTED (DEBUG stays pure).
 * These are owner-boundary operations, NOT recursive peer callback bridges.
 * Independently attached KBC and keyboard require a board coordinator to order
 * coincident peer events. Do not directly cross-wire these wrappers or mutate
 * an unsynchronized peer; native direct wiring alone is not clock composition.
 * Native inhibit feedback during keyboard send remains permitted, but must be
 * applied by an owner that has already synchronized the communicating devices.
 * Physical peripheral reset: sync/native reset/changed. Full engine epoch reset:
 * engine_reset then link_reset on every link. KBC/keyboard lifetime counters
 * survive peripheral reset; pending protocol work does not. CPU-only reset
 * must not reset either device. Destroy engine, then links, then devices. */
bm_status_t bm_kbc8042_clock_receive(bm_at_clock_link_t *link, uint8_t value);
bm_status_t bm_at_keyboard_clock_command(bm_at_clock_link_t *link, uint8_t value);
bm_status_t bm_at_keyboard_clock_inhibit(bm_at_clock_link_t *link, int level);
bm_status_t bm_at_keyboard_clock_input(bm_at_clock_link_t *link, const bm_input_event_t *event);
/* Remaining attachments are draft declarations, not linkable implementations. */
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
