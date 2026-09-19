/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Derived rewrite of the inherited Vx0 bus interface unit. The original work
 * includes Copyright 2015-2020 Andrew Jenner and Copyright 2016-2020 Miran
 * Grca.
 */
#ifndef BLUMACH_V30_BCU_H
#define BLUMACH_V30_BCU_H

#include <blumach/components/bus.h>
#include <blumach/components/cpu_808x.h>

#include <stdint.h>

/*
 * Instance-owned state for the V30 bus control unit (BCU). This component
 * deliberately owns only state that is already modelled: the six-byte
 * instruction queue, its independent prefetch pointer, and bus-resource
 * observations. It does not yet claim to place transactions on an EXU/BCU
 * timeline; synchronous completion remains an explicit limitation.
 */
typedef struct bm_v30_bcu {
    uint16_t prefetch_pointer;
    uint8_t prefetch_queue[BM_808X_V30_PREFETCH_QUEUE_CAPACITY];
    uint8_t prefetch_head;
    uint8_t prefetch_count;
    uint64_t boundary_bus_transactions;
    uint64_t boundary_wait_states;
    uint64_t boundary_bus_active_clocks;
    uint64_t boundary_demand_prefetch_transactions;
    uint64_t boundary_demand_prefetch_bus_clocks;
    uint32_t boundary_instruction_queue_reads;
    int boundary_prefetch_flushed;
} bm_v30_bcu_t;

void bm_v30_bcu_reset(bm_v30_bcu_t *bcu, uint16_t instruction_pointer);
void bm_v30_bcu_begin_boundary(bm_v30_bcu_t *bcu);
void bm_v30_bcu_flush(bm_v30_bcu_t *bcu, uint16_t instruction_pointer);

uint8_t bm_v30_bcu_free_bytes(const bm_v30_bcu_t *bcu);
uint8_t bm_v30_bcu_queue_count(const bm_v30_bcu_t *bcu);
uint16_t bm_v30_bcu_prefetch_pointer(const bm_v30_bcu_t *bcu);

bm_status_t bm_v30_bcu_enqueue_byte(bm_v30_bcu_t *bcu, uint8_t value);
bm_status_t bm_v30_bcu_enqueue_word(bm_v30_bcu_t *bcu, uint16_t value);
bm_status_t bm_v30_bcu_dequeue_byte(bm_v30_bcu_t *bcu, uint8_t *value);

void bm_v30_bcu_record_transaction(
    bm_v30_bcu_t *bcu,
    const bm_bus_transaction_t *transaction,
    bm_status_t status,
    int demand_prefetch);

#endif
