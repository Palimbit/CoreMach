/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Derived rewrite of the inherited Vx0 bus interface unit. The original work
 * includes Copyright 2015-2020 Andrew Jenner and Copyright 2016-2020 Miran
 * Grca.
 */
#ifndef BLUMACH_CPU_808X_BIU_H
#define BLUMACH_CPU_808X_BIU_H

#include <blumach/components/bus.h>
#include <blumach/components/cpu_808x.h>

#include <stdint.h>

/*
 * Instance-owned state for an 808x bus interface unit. This component owns
 * the model-selected instruction queue, its independent prefetch pointer,
 * T1/T2/T3/Tw/T4 bus phases and bus-resource observations. Operand
 * transactions are synchronous at their semantic access point, but share
 * this resource and complete an in-flight prefetch before acquiring it. Their
 * exact EXU clock offsets remain subsequent executor work.
 */
typedef struct bm_808x_biu {
    uint16_t prefetch_pointer;
    uint8_t prefetch_queue[BM_808X_V30_PREFETCH_QUEUE_CAPACITY];
    uint8_t prefetch_head;
    uint8_t prefetch_count;
    uint8_t prefetch_capacity;
    uint8_t fetch_width;
    uint64_t boundary_bus_transactions;
    uint64_t boundary_wait_states;
    uint64_t boundary_bus_active_clocks;
    uint64_t boundary_demand_prefetch_transactions;
    uint64_t boundary_demand_prefetch_bus_clocks;
    uint64_t boundary_prefetch_transactions;
    uint64_t boundary_prefetch_phase_clocks;
    uint64_t boundary_operand_transactions;
    uint64_t boundary_operand_bus_clocks;
    uint64_t boundary_prefetch_handoff_clocks;
    uint32_t boundary_instruction_queue_reads;
    int boundary_prefetch_flushed;
    /* Phase to execute on the next BCU clock. */
    enum {
        BM_808X_BIU_PHASE_IDLE = 0,
        BM_808X_BIU_PHASE_T1,
        BM_808X_BIU_PHASE_T2,
        BM_808X_BIU_PHASE_T3,
        BM_808X_BIU_PHASE_TW,
        BM_808X_BIU_PHASE_T4
    } prefetch_phase;
    bm_bus_transaction_t pending_prefetch;
    uint32_t pending_wait_clocks;
    uint64_t total_phase_clocks;
    int pending_prefetch_valid;
    int pending_prefetch_demand;
} bm_808x_biu_t;

void bm_808x_biu_reset(bm_808x_biu_t *bcu, uint16_t instruction_pointer,
                       uint8_t prefetch_capacity, uint8_t fetch_width);
void bm_808x_biu_begin_boundary(bm_808x_biu_t *bcu);
/* Stop an in-flight prefetch without discarding already queued bytes. */
void bm_808x_biu_suspend_prefetch(bm_808x_biu_t *bcu);
void bm_808x_biu_flush(bm_808x_biu_t *bcu, uint16_t instruction_pointer);

uint8_t bm_808x_biu_free_bytes(const bm_808x_biu_t *bcu);
uint8_t bm_808x_biu_queue_count(const bm_808x_biu_t *bcu);
uint16_t bm_808x_biu_prefetch_pointer(const bm_808x_biu_t *bcu);

bm_status_t bm_808x_biu_enqueue_byte(bm_808x_biu_t *bcu, uint8_t value);
bm_status_t bm_808x_biu_enqueue_word(bm_808x_biu_t *bcu, uint16_t value);
bm_status_t bm_808x_biu_dequeue_byte(bm_808x_biu_t *bcu, uint8_t *value);

/* Advance the prefetch state machine by one V30 clock. IDLE is returned only
 * when the queue has insufficient room to begin the fetch selected by PFP. */
bm_status_t bm_808x_biu_step_prefetch(
    bm_808x_biu_t *bcu,
    bm_bus_t *bus,
    uint16_t code_segment,
    uint32_t transaction_attributes,
    int demand_prefetch);

/* Run whole T1/T2/T3/Tw/T4 phases until at least one byte is queued. */
bm_status_t bm_808x_biu_fill_on_demand(
    bm_808x_biu_t *bcu,
    bm_bus_t *bus,
    uint16_t code_segment,
    uint32_t transaction_attributes);

/* Let an in-flight or newly eligible prefetch consume at most clock_budget
 * BCU clocks. A full queue is a successful no-op. */
bm_status_t bm_808x_biu_advance_prefetch(
    bm_808x_biu_t *bcu,
    bm_bus_t *bus,
    uint16_t code_segment,
    uint32_t transaction_attributes,
    uint32_t clock_budget);

/* Acquire the shared external bus for one operand or I/O transaction. An
 * already-started instruction prefetch completes first; no new prefetch is
 * started while the request waits. The transaction itself advances through
 * T1/T2/T3/Tw/T4 and is presented to the portable bus at T3. */
bm_status_t bm_808x_biu_transact(
    bm_808x_biu_t *bcu,
    bm_bus_t *bus,
    bm_bus_transaction_t *transaction);

#endif
