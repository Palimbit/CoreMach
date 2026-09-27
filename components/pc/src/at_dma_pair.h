/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Private Intel 8237A byte-pair transport; not a DMA service entry point.
 */
#ifndef BLUMACH_PRIVATE_AT_DMA_PAIR_H
#define BLUMACH_PRIVATE_AT_DMA_PAIR_H
#include <blumach/components/at_dma.h>

typedef enum bm_at_dma_pair_phase {
    BM_AT_DMA_PAIR_STOPPED = 0,
    BM_AT_DMA_PAIR_READ,
    BM_AT_DMA_PAIR_WRITE,
    BM_AT_DMA_PAIR_COMPLETE,
    BM_AT_DMA_PAIR_BUSY
} bm_at_dma_pair_phase_t;

typedef struct bm_at_dma_pair {
    bm_at_access_fn memory;
    void *context;
    bm_clock_rate_t clock;
    const uint8_t *eop; /* borrowed semantic level; NULL means deasserted */
    uint32_t source, destination;
    bm_at_dma_pair_phase_t phase;
    uint8_t temporary, read_complete, write_complete;
    uint8_t source_eop, destination_eop;
    uint64_t completed_clocks; /* successful phases only, not elapsed pin time */
} bm_at_dma_pair_t;

/* Prepare one byte pair in a shared 64K page (the value of IBM port83h for
 * a lower-controller copy, not a page-port address). No effects or allocation.
 * The caller owns grant, register/counter/TC/autoinit policy and lifetime of
 * callback/EOP storage. It may change the EOP byte, but must not mutate or
 * reprepare this object from a callback. Preparation is for a NEW operation;
 * it does not authorize recovery/replay after a host failure.
 * This private primitive neither inspects nor changes a bm_at_dma_t instance;
 * public mem2mem service is disabled by default; the explicit matched-count
 * profile is its bounded caller. Not a supported board-facing ABI. */
bm_status_t bm_at_dma_pair_prepare(bm_at_dma_pair_t *pair, bm_at_access_fn memory,
    void *context, bm_clock_rate_t clock, uint8_t page, uint16_t source,
    uint16_t destination, const uint8_t *eop);

/* One call performs just READ or WRITE, then returns to its caller. Intel
 * 231466-005 pp4/6, figure12: S11-S14 then S21-S24, four clocks each plus
 * independent callback EXTRA waits. Always DMA8; no device/DACK/TC callbacks.
 * Successful READ captures one byte in TEMP before WRITE can start. Each
 * successful phase samples EOP independently at its functional completion;
 * no pulse queue, electrical sampling claim or termination decision here.
 * Host failure retains prior successful phase/TEMP/clocks and external partial
 * effects, consumes the operation and returns zero clocks for the failed phase.
 * IDLE under the caller's promised grant is INVALID_STATE, never retryable.
 * COMPLETE/STOPPED/BUSY cannot execute; reentry is rejected without consuming
 * the outer call. The caller must not resume CPU/another master between phases
 * without applying the complete controller/board policy (not implemented here).
 * Inspect read_complete/write_complete on failure; zero completed-pair claims
 * must not hide a completed source read or a partially effective failed write. */
bm_status_t bm_at_dma_pair_step(bm_at_dma_pair_t *pair, uint64_t *clocks);
#endif
