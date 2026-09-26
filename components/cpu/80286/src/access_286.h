/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 BluMach contributors
 */
#ifndef BM_ACCESS_286_H
#define BM_ACCESS_286_H

#include "descriptor_286.h"

/* Private cached-segment accesses, not an engine ABI or PE dispatch bypass. */
typedef enum bm_286_pm_access_kind {
    BM_286_PM_FETCH, BM_286_PM_READ, BM_286_PM_WRITE
} bm_286_pm_access_kind_t;

typedef struct bm_286_pm_access_check {
    bool allowed;
    uint8_t fault_vector; /* #GP(0), or #SS(0) for an SS range violation. */
} bm_286_pm_access_check_t;

/* reg: 0=ES, 1=CS, 2=SS, 3=DS. FETCH requires CS. Checks a complete,
 * nonempty, non-wrapping range using the hidden cache. No table lookup, A-bit
 * write or repeated load-time privilege/presence checks. Unusable DS/ES cause
 * #GP(0); inconsistent usable caches are host INVALID_STATE, never #NP.
 * CS/SS must already be usable. A valid cache's visible selector is not used
 * to infer usability (in particular after the real-to-protected transition).
 * Raw 82h in an ordinary hidden cache represents retained real-mode writable
 * data, including CS fetch; table access 82h still denotes an LDT descriptor.
 * Wide offsets/lengths allow callers to preflight aggregate stack operands.
 * Inputs must not alias result. No CPU changes, bus access or fault delivery. */
bm_status_t bm_286_pm_check_access(const bm_286_arch_state_t *arch,
    unsigned reg, bm_286_pm_access_kind_t kind, uint32_t offset,
    uint32_t length, bm_286_pm_access_check_t *result);

typedef struct bm_286_pm_access_state {
    /* Instance-owned, clear only on reset/stopped-state import, never retry. */
    bool stopped;
} bm_286_pm_access_state_t;

typedef struct bm_286_pm_access_result {
    bool completed;
    uint8_t fault_vector; /* Error code is always zero. */
    uint64_t waits;       /* Successful endpoint transfers only. */
    uint8_t bytes[10];    /* READ/FETCH published only on complete success. */
} bm_286_pm_access_result_t;

/* Preflight then transfer 1..10 bytes. FETCH covers only requested instruction
 * bytes, never speculative prefetch. DATA uses aligned words or odd split
 * bytes; physical addresses wrap at 24 bits, A20 belongs to the board.
 * locked only marks DATA transfers: caller owns the surrounding lock and its
 * release. Callbacks may latch signal inputs (including a pending NMI), but
 * may not reset/import/execute or alter operand caches/registers/state/buffers.
 * CPU registers are never changed. On host failure completed writes/endpoint
 * effects survive, no partial read is published, and state latches stopped.
 * Guest faults perform no transfers and do not latch stopped. Caller owns
 * multi-operand preflight, restart IP, instruction commit/unwind and delivery.
 * All inputs/outputs must be serialized and non-aliasing. */
bm_status_t bm_286_pm_access(const bm_286_arch_state_t *arch,
    unsigned reg, bm_286_pm_access_kind_t kind, uint32_t offset,
    unsigned length, bool locked, const uint8_t *write_bytes,
    bm_bus_access_fn access, void *context, bm_286_pm_access_state_t *state,
    bm_286_pm_access_result_t *result);

#endif
