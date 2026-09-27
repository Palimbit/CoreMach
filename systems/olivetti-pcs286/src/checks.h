/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Private functional AT error capture/NMI routing; no parity-bit RAM model. */
#ifndef BM_PCS286_CHECKS_H
#define BM_PCS286_CHECKS_H
#include <blumach/components/at_bus.h>

typedef bm_status_t (*bm_pcs286_nmi_fn)(void *context, int asserted);
typedef struct bm_pcs286_checks_state {
    int ram_enabled, io_enabled, masked;
    int ram_latched, io_latched, io_active, nmi;
    bm_status_t failure;
} bm_pcs286_checks_state_t;
typedef struct bm_pcs286_checks {
    bm_pcs286_nmi_fn output;
    void *context;
    bm_pcs286_checks_state_t state;
    int notifying;
} bm_pcs286_checks_t;

/* Unused caller-owned storage, copied callback and borrowed context. Required
 * output forwards semantic NMI level to the CPU, never recursively executes it.
 * No publication/callback/allocation. CPU NMI starts low; global mask starts
 * asserted. Check enables start true to match the existing zero port61 latch:
 * deterministic model reset policy, not IBM/PCS286 power-on certification. */
bm_status_t bm_pcs286_checks_initialize(bm_pcs286_checks_t *checks,
                                       bm_pcs286_nmi_fn output, void *context);
/* Board reset: restore enables/mask, clear RAM latch, retain external IO level
 * (and capture it if active). Publish NMI low, forcing a resync after failure.
 * CPU-only reset does NOT call this: owner re-presents current NMI level to the
 * reset CPU separately. Reset/disconnect before destroying callback recipients. */
bm_status_t bm_pcs286_checks_reset(bm_pcs286_checks_t *checks);
/* Callback-compatible port61 endpoint: semantic enables = inverse bits2/3.
 * Disabling clears the corresponding stored indication. An external IO check
 * still asserted remains visible at bit6 even while disabled, but cannot drive
 * NMI until enabled. It relatches on enable. No analog set/clear race is modeled. */
bm_status_t bm_pcs286_checks_enable(void *context, int ram_enabled, int io_enabled);
/* Pure status bits6/7 ONLY, for the board aggregator to OR with refresh bit4.
 * Inspection works after host failure, clears nothing and emits no signal. */
bm_status_t bm_pcs286_checks_status(void *context, uint8_t *bits);
/* One qualified guest RAM-read parity observation; bad parity latches only
 * while enabled. A good read cannot clear a prior error. DEBUG/refresh/non-RAM
 * reads must not call this; host access failures are NEVER parity observations.
 * Parity generation/storage and memory-controller qualification are separate. */
bm_status_t bm_pcs286_checks_memory_sample(bm_pcs286_checks_t *checks, int bad);
/* Semantic external IO-check level (1 means physical /IOCHCK asserted low).
 * Source owns deassertion; disabling checking does not repair an adapter. */
bm_status_t bm_pcs286_checks_io_input(bm_pcs286_checks_t *checks, int active);
/* Global mask, as future RTC address-port bit7: 1 blocks NMI, not capture or
 * status. No RTC/70h decode is implemented here. Mask changes cannot retract
 * an edge already accepted by the CPU. */
bm_status_t bm_pcs286_checks_mask(bm_pcs286_checks_t *checks, int masked);
bm_status_t bm_pcs286_checks_state(const bm_pcs286_checks_t *checks,
                                  bm_pcs286_checks_state_t *state);
/* Changed-level publication only; repeated errors do not pulse NMI. Failed
 * publication retains accepted state and first host error until explicit reset;
 * no rollback/retry or host-to-guest conversion. IDLE from output is invalid.
 * Mutator callback reentry rejects; pure inspection is allowed. Single owner. */
#endif
