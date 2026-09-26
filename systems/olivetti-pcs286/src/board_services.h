/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Private functional peripheral owner; not the public PCS286 factory. */
#ifndef BM_PCS286_BOARD_SERVICES_H
#define BM_PCS286_BOARD_SERVICES_H
#include "board_control.h"
#include <blumach/components/at_keyboard_pair.h>

typedef struct bm_pcs286_services bm_pcs286_services_t;
typedef struct bm_pcs286_services_config {
    bm_pcs286_control_t         *control; /* Borrowed initialized CPU/bus/PIC/memory owner. */
    bm_clock_rate_t              pit_clock;
    bm_at_rtc_config_t           rtc;
    bm_at_keyboard_pair_config_t keyboard;
    bm_at_line_fn                speaker; /* Optional digital level observer, never analog audio. */
    void                        *speaker_context;
} bm_pcs286_services_config_t;

typedef struct bm_pcs286_services_step {
    int                       cpu_completed, refresh_completed;
    bm_pcs286_control_event_t cpu; /* Valid only when cpu_completed. */
} bm_pcs286_services_step_t;
typedef struct bm_pcs286_services_state {
    bm_time_point_t time;
    bm_status_t     failure;
    int             refresh_pending, refdet, nmi, io_check_active;
    uint8_t         port61;
} bm_pcs286_services_state_t;

/* Owns engine (no registered CPU), PIT, RTC, keyboard pair, three clock links,
 * port61, checks and refresh. No public mutable child handles. Required RTC and
 * keyboard callbacks/contexts must be NULL: this owner wires them. Ports must
 * be RTC70, KBC60/64. Initial native profiles remain explicit; no host date.
 * Borrowed control must be quiescent/healthy, initial NMI low, and receivers
 * connected before create. Successful creation publishes initial outputs.
 * Failure clears out and releases all allocations; published receiver effects
 * cannot be rolled back. Host/recipients outlive destruction. No ROM ownership.
 * Reset/disconnect recipients before destroy if they need outputs lowered. */
bm_status_t bm_pcs286_services_create(const bm_host_services_t          *host,
                                      const bm_pcs286_services_config_t *config, bm_pcs286_services_t **out);
void        bm_pcs286_services_destroy(bm_pcs286_services_t *services);
/* Map byte resources40..43,60..61,64,70..71 through the existing board decoder.
 * No mirrors/port92. DEBUG reads pure/lazy, no clock or refresh service.
 * Normal I/O syncs all peers first, routes to existing link/port handler, then
 * services pending refresh. CPU access is permitted only through services_step;
 * recursive I/O and observer mutations reject. Caller result staged on failure.
 * Ordinary device register/transport rejections are not sticky; endpoint/clock
 * failures are. Invalid/unmapped transport rejects before synchronization. */
bm_status_t bm_pcs286_services_io(void *context, bm_bus_transaction_t *transaction);
/* Time and CPU execution are deliberately separate. Advance only the exact
 * peripheral engine; step performs at most one real functional CPU boundary.
 * No conversion from UNKNOWN CPU timing to nanoseconds/native clocks. */
bm_status_t bm_pcs286_services_advance(bm_pcs286_services_t *services, bm_tick_t ns);
bm_status_t bm_pcs286_services_step(bm_pcs286_services_t      *services,
                                    bm_pcs286_services_step_t *result);
bm_status_t bm_pcs286_services_input(bm_pcs286_services_t   *services,
                                     const bm_input_event_t *event);
/* Qualified external error sources only, never host endpoint failures. */
bm_status_t bm_pcs286_services_io_check(bm_pcs286_services_t *services, int active);
bm_status_t bm_pcs286_services_parity(bm_pcs286_services_t *services, int bad);
bm_status_t bm_pcs286_services_state(const bm_pcs286_services_t *services,
                                     bm_pcs286_services_state_t *state);
/* Pure diagnostics, including after failure: native RTC snapshot without clock
 * synchronization, callbacks, read-C/D effects or mutable child exposure.
 * CMOS size must match the configured64/128-byte device; outputs staged. */
bm_status_t bm_pcs286_services_inspect_rtc(const bm_pcs286_services_t *services,
                                          bm_at_rtc_state_t *state,
                                          uint8_t *cmos, size_t size);
/* CPU-only reset keeps the peripheral failure latch and time. On a healthy
 * peripheral owner it can explicitly recover a stopped CPU; it never repairs
 * a failed clock/device. KBC requests are serviced by step automatically. */
bm_status_t bm_pcs286_services_reset_cpu(bm_pcs286_services_t *services);
/* Explicit emulator epoch lifecycle: reset CPU/bus/PIC and owned peripherals,
 * retaining RAM/maps/A20's configured reset source, RTC calendar/CMOS/divider,
 * keyboard lifetime counters and external IO-check level. DMA registers and
 * external cards are NOT reset: reject any outstanding DMA/ISA bus request
 * before effects. Own pending refresh may be cancelled; LOCK is released by
 * CPU reset. This is not a full-machine/user reset or a hardware reset pulse.
 * recover=0 synchronizes healthy elapsed time first; any failure stops reset.
 * recover=1 explicitly abandons unfinished failed intervals at their retained
 * prefixes, clears failures and resets all links. No automatic replay. On
 * failure partial reset effects remain and owner stays stopped. */
bm_status_t bm_pcs286_services_reset_epoch(bm_pcs286_services_t *services, int recover);
#endif
