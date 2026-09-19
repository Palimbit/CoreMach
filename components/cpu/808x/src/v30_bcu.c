/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Derived rewrite of the inherited Vx0 bus interface unit. The original work
 * includes Copyright 2015-2020 Andrew Jenner and Copyright 2016-2020 Miran
 * Grca.
 */
#include "v30_bcu.h"

#include <string.h>

static uint8_t
queue_tail(const bm_v30_bcu_t *bcu)
{
    return (uint8_t) ((bcu->prefetch_head + bcu->prefetch_count) %
                      BM_808X_V30_PREFETCH_QUEUE_CAPACITY);
}

void
bm_v30_bcu_reset(bm_v30_bcu_t *bcu, uint16_t instruction_pointer)
{
    memset(bcu, 0, sizeof(*bcu));
    bcu->prefetch_pointer = instruction_pointer;
}

void
bm_v30_bcu_begin_boundary(bm_v30_bcu_t *bcu)
{
    bcu->boundary_bus_transactions = 0U;
    bcu->boundary_wait_states = 0U;
    bcu->boundary_bus_active_clocks = 0U;
    bcu->boundary_demand_prefetch_transactions = 0U;
    bcu->boundary_demand_prefetch_bus_clocks = 0U;
    bcu->boundary_instruction_queue_reads = 0U;
    bcu->boundary_prefetch_flushed = 0;
}

void
bm_v30_bcu_flush(bm_v30_bcu_t *bcu, uint16_t instruction_pointer)
{
    bcu->prefetch_head = 0U;
    bcu->prefetch_count = 0U;
    bcu->prefetch_pointer = instruction_pointer;
    bcu->boundary_prefetch_flushed = 1;
}

uint8_t
bm_v30_bcu_free_bytes(const bm_v30_bcu_t *bcu)
{
    return (uint8_t) (BM_808X_V30_PREFETCH_QUEUE_CAPACITY -
                      bcu->prefetch_count);
}

uint8_t
bm_v30_bcu_queue_count(const bm_v30_bcu_t *bcu)
{
    return bcu->prefetch_count;
}

uint16_t
bm_v30_bcu_prefetch_pointer(const bm_v30_bcu_t *bcu)
{
    return bcu->prefetch_pointer;
}

bm_status_t
bm_v30_bcu_enqueue_byte(bm_v30_bcu_t *bcu, uint8_t value)
{
    if (bcu->prefetch_count == BM_808X_V30_PREFETCH_QUEUE_CAPACITY)
        return BM_STATUS_INVALID_STATE;
    bcu->prefetch_queue[queue_tail(bcu)] = value;
    ++bcu->prefetch_count;
    ++bcu->prefetch_pointer;
    return BM_STATUS_OK;
}

bm_status_t
bm_v30_bcu_enqueue_word(bm_v30_bcu_t *bcu, uint16_t value)
{
    uint8_t tail;

    if (bm_v30_bcu_free_bytes(bcu) < 2U)
        return BM_STATUS_INVALID_STATE;
    tail = queue_tail(bcu);
    bcu->prefetch_queue[tail] = (uint8_t) value;
    tail = (uint8_t) ((tail + 1U) % BM_808X_V30_PREFETCH_QUEUE_CAPACITY);
    bcu->prefetch_queue[tail] = (uint8_t) (value >> 8U);
    bcu->prefetch_count = (uint8_t) (bcu->prefetch_count + 2U);
    bcu->prefetch_pointer = (uint16_t) (bcu->prefetch_pointer + 2U);
    return BM_STATUS_OK;
}

bm_status_t
bm_v30_bcu_dequeue_byte(bm_v30_bcu_t *bcu, uint8_t *value)
{
    if (value == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    if (bcu->prefetch_count == 0U)
        return BM_STATUS_INVALID_STATE;
    *value = bcu->prefetch_queue[bcu->prefetch_head];
    bcu->prefetch_head = (uint8_t)
        ((bcu->prefetch_head + 1U) % BM_808X_V30_PREFETCH_QUEUE_CAPACITY);
    --bcu->prefetch_count;
    ++bcu->boundary_instruction_queue_reads;
    return BM_STATUS_OK;
}

void
bm_v30_bcu_record_transaction(bm_v30_bcu_t *bcu,
                              const bm_bus_transaction_t *transaction,
                              bm_status_t status,
                              int demand_prefetch)
{
    uint64_t clocks;

    if ((status != BM_STATUS_OK) || (transaction == NULL))
        return;
    clocks = 4U + (uint64_t) transaction->wait_states;
    ++bcu->boundary_bus_transactions;
    bcu->boundary_wait_states += transaction->wait_states;
    bcu->boundary_bus_active_clocks += clocks;
    if (demand_prefetch) {
        ++bcu->boundary_demand_prefetch_transactions;
        bcu->boundary_demand_prefetch_bus_clocks += clocks;
    }
}
