/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Private functional wiring, not a qualified PCS286 machine/clock profile. */
#ifndef BM_PCS286_BOARD_CONTROL_H
#define BM_PCS286_BOARD_CONTROL_H
#include "headland_at_memory.h"
#include <blumach/components/at_pic.h>
#include <blumach/components/cpu_80286.h>

typedef struct bm_pcs286_control_config {
    bm_cpu_t                *cpu;
    bm_at_bus_t             *bus;
    bm_at_pic_t             *pic;
    bm_headland_at_memory_t *memory;
} bm_pcs286_control_config_t;

typedef struct bm_pcs286_control_state {
    int         reset_level, reset_pending, nmi;
    bm_status_t failure;
} bm_pcs286_control_state_t;

typedef struct bm_pcs286_control {
    bm_pcs286_control_config_t config;
    bm_pcs286_control_state_t  state;
    int                        busy;
} bm_pcs286_control_t;

typedef enum bm_pcs286_control_event_kind {
    BM_PCS286_CONTROL_CPU = 0,
    BM_PCS286_CONTROL_RESET
} bm_pcs286_control_event_kind_t;
typedef struct bm_pcs286_control_event {
    bm_pcs286_control_event_kind_t kind;
    bm_286_boundary_t              cpu;           /* Meaningful only for CPU kind. */
    int                            reset_applied; /* Can follow a successful CPU boundary. */
} bm_pcs286_control_event_t;

/* Unused caller storage; initialized children borrowed, no callbacks/reset.
 * Connect PIC INTR and bus HOLD plus CPU LOCK/HLDA to the adapters below;
 * NMI starts low and is subsequently supplied by the checks endpoint.
 * Pair's required IRQ/A20/reset callbacks use the same control context.
 * The CPU must be exclusively executed/reset through this owner once connected.
 * Initialize before any publication; synchronize initial outputs explicitly.
 * Single-threaded. Owner and recipients outlive callbacks/CPU destruction. */
bm_status_t bm_pcs286_control_initialize(bm_pcs286_control_t              *control,
                                         const bm_pcs286_control_config_t *config);
bm_status_t bm_pcs286_control_irq1(void *context, int level);
bm_status_t bm_pcs286_control_a20(void *context, int enabled);
/* Rising semantic assertion latches; deassertion cannot erase an unserviced
 * pulse. Duplicate high does not request another reset. No execution here. */
bm_status_t bm_pcs286_control_reset_line(void *context, int asserted);
bm_status_t bm_pcs286_control_nmi(void *context, int asserted);
void        bm_pcs286_control_intr(void *context, int asserted);
void        bm_pcs286_control_hold(void *context, int asserted);
void        bm_pcs286_control_hlda(void *context, int asserted);
void        bm_pcs286_control_lock(void *context, int asserted);

/* Functional boundary only, never a native-clock engine callback. At entry a
 * pending reset emits RESET without fetch; held reset returns IDLE thereafter.
 * A request raised inside a successful CPU boundary applies after it returns;
 * the event preserves that boundary and reports reset_applied. A host error
 * stops first, retains pending reset and effects, and never auto-recovers.
 * Result unchanged on IDLE/error. No peripheral clocks advanced here. */
bm_status_t bm_pcs286_control_step(bm_pcs286_control_t       *control,
                                   bm_pcs286_control_event_t *event);
/* Explicit CPU-only recovery at an idle owner boundary, including sticky host
 * errors. Does not recover failed peripherals: owner repairs those separately.
 * Clears CPU stop/REP/HLDA/LOCK, then re-presents actual PIC INTR, bus HOLD and
 * stored NMI. High NMI is presented as a fresh input to the reset CPU (functional
 * policy, not pin timing). RAM, maps, A20, PIC, DMA, CMOS, keyboard and engine
 * epoch are retained. An asserted reset still prevents subsequent fetch.
 * Reentrant reset/step reject. Ordinary line callbacks during CPU execution
 * are allowed; A20 during an active memory transfer rejects, never deferred.
 * First host error is sticky (including void pin adapters); no retries/guest
 * conversion. Explicit reset preserves any new pin-adapter failure. */
bm_status_t bm_pcs286_control_reset_cpu(bm_pcs286_control_t *control);
bm_status_t bm_pcs286_control_state(const bm_pcs286_control_t *control,
                                    bm_pcs286_control_state_t *state);
#endif
