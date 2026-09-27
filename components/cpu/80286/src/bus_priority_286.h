/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors */
#ifndef BLUMACH_BUS_PRIORITY_286_H
#define BLUMACH_BUS_PRIORITY_286_H
#include <blumach/engine/types.h>
#include <stdbool.h>

/* Experimental eligibility selector, Intel210760-002 table3-3.
 * One bit per READY request class, ordered highest to lowest.
 * A locked split continuation is classified LOCKED, not SPLIT.
 * This does not assert HLDA/LOCK, call devices or start physical bus phases.
 */
enum {
    BM_286_BUS_LOCKED = 1u,
    BM_286_BUS_SPLIT = 2u,
    BM_286_BUS_HOLD = 4u,
    BM_286_BUS_EXTENSION = 8u,
    BM_286_BUS_DATA = 16u,
    BM_286_BUS_PREFETCH = 32u
};

/* busy covers an in-flight transfer OR external ownership until relinquished.
 * lock_owned reserves the bus across gaps in a locked sequence: no ready
 * LOCKED request means idle, not a grant to HOLD or prefetch.
 * prefetch_inhibited is supplied by the future EU scheduler; this function
 * does not infer the documented two-clock advance notice from an opcode.
 * OK returns one class, IDLE returns zero; invalid arguments leave out intact.
 * No mutable global state; no elapsed timing or fairness approximation.
 */
bm_status_t bm_286_bus_select(unsigned ready, bool busy, bool lock_owned,
                            bool prefetch_inhibited, unsigned *out);
#endif
