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

static uint32_t
prefetch_physical_address(uint16_t code_segment, uint16_t offset)
{
    return ((((uint32_t) code_segment << 4U) + offset) & 0xfffffU);
}

static int
can_begin_prefetch(const bm_v30_bcu_t *bcu)
{
    const uint8_t required =
        (bcu->prefetch_pointer & 1U) == 0U ? 2U : 1U;

    return bm_v30_bcu_free_bytes(bcu) >= required;
}

static void
abort_pending_prefetch(bm_v30_bcu_t *bcu)
{
    bcu->prefetch_phase = BM_V30_BCU_PHASE_IDLE;
    memset(&bcu->pending_prefetch, 0, sizeof(bcu->pending_prefetch));
    bcu->pending_wait_clocks = 0U;
    bcu->pending_prefetch_valid = 0;
    bcu->pending_prefetch_demand = 0;
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
    bcu->boundary_prefetch_transactions = 0U;
    bcu->boundary_prefetch_phase_clocks = 0U;
    bcu->boundary_instruction_queue_reads = 0U;
    bcu->boundary_prefetch_flushed = 0;
}

void
bm_v30_bcu_flush(bm_v30_bcu_t *bcu, uint16_t instruction_pointer)
{
    abort_pending_prefetch(bcu);
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

bm_status_t
bm_v30_bcu_step_prefetch(bm_v30_bcu_t *bcu,
                         bm_bus_t *bus,
                         uint16_t code_segment,
                         uint32_t transaction_attributes,
                         int demand_prefetch)
{
    bm_status_t status = BM_STATUS_OK;

    if ((bcu == NULL) || (bus == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    if ((transaction_attributes & ~BM_BUS_TRANSACTION_LOCKED) != 0U)
        return BM_STATUS_INVALID_ARGUMENT;

    if (bcu->prefetch_phase == BM_V30_BCU_PHASE_IDLE) {
        uint32_t width;

        if (!can_begin_prefetch(bcu))
            return BM_STATUS_IDLE;
        width = (bcu->prefetch_pointer & 1U) == 0U ? 2U : 1U;
        bcu->pending_prefetch = (bm_bus_transaction_t) {
            BM_ADDRESS_MEMORY,
            BM_BUS_FETCH,
            prefetch_physical_address(code_segment,
                                      bcu->prefetch_pointer),
            0U,
            width,
            width,
            0U,
            BM_ENDIAN_LITTLE,
            transaction_attributes
        };
        bcu->pending_prefetch_valid = 0;
        bcu->pending_prefetch_demand = !!demand_prefetch;
        bcu->prefetch_phase = BM_V30_BCU_PHASE_T1;
    }

    ++bcu->total_phase_clocks;
    ++bcu->boundary_bus_active_clocks;
    ++bcu->boundary_prefetch_phase_clocks;
    if (bcu->pending_prefetch_demand)
        ++bcu->boundary_demand_prefetch_bus_clocks;
    switch (bcu->prefetch_phase) {
        case BM_V30_BCU_PHASE_T1:
            bcu->prefetch_phase = BM_V30_BCU_PHASE_T2;
            break;
        case BM_V30_BCU_PHASE_T2:
            bcu->prefetch_phase = BM_V30_BCU_PHASE_T3;
            break;
        case BM_V30_BCU_PHASE_T3:
            status = bm_bus_transact(bus, &bcu->pending_prefetch);
            if (status != BM_STATUS_OK) {
                abort_pending_prefetch(bcu);
                return status;
            }
            bcu->pending_prefetch_valid = 1;
            bcu->pending_wait_clocks =
                bcu->pending_prefetch.wait_states;
            ++bcu->boundary_bus_transactions;
            ++bcu->boundary_prefetch_transactions;
            bcu->boundary_wait_states +=
                bcu->pending_prefetch.wait_states;
            if (bcu->pending_prefetch_demand)
                ++bcu->boundary_demand_prefetch_transactions;
            bcu->prefetch_phase = bcu->pending_wait_clocks != 0U ?
                BM_V30_BCU_PHASE_TW : BM_V30_BCU_PHASE_T4;
            break;
        case BM_V30_BCU_PHASE_TW:
            --bcu->pending_wait_clocks;
            if (bcu->pending_wait_clocks == 0U)
                bcu->prefetch_phase = BM_V30_BCU_PHASE_T4;
            break;
        case BM_V30_BCU_PHASE_T4:
            if (!bcu->pending_prefetch_valid) {
                abort_pending_prefetch(bcu);
                return BM_STATUS_INVALID_STATE;
            }
            if (bcu->pending_prefetch.size == 2U)
                status = bm_v30_bcu_enqueue_word(
                    bcu, (uint16_t) bcu->pending_prefetch.value);
            else
                status = bm_v30_bcu_enqueue_byte(
                    bcu, (uint8_t) bcu->pending_prefetch.value);
            abort_pending_prefetch(bcu);
            break;
        case BM_V30_BCU_PHASE_IDLE:
        default:
            return BM_STATUS_INVALID_STATE;
    }
    return status;
}

bm_status_t
bm_v30_bcu_advance_prefetch(bm_v30_bcu_t *bcu,
                            bm_bus_t *bus,
                            uint16_t code_segment,
                            uint32_t transaction_attributes,
                            uint32_t clock_budget)
{
    uint32_t clock;

    if ((bcu == NULL) || (bus == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    for (clock = 0U; clock < clock_budget; ++clock) {
        bm_status_t status = bm_v30_bcu_step_prefetch(
            bcu, bus, code_segment, transaction_attributes, 0);

        if (status == BM_STATUS_IDLE)
            return BM_STATUS_OK;
        if (status != BM_STATUS_OK)
            return status;
    }
    return BM_STATUS_OK;
}

bm_status_t
bm_v30_bcu_fill_on_demand(bm_v30_bcu_t *bcu,
                          bm_bus_t *bus,
                          uint16_t code_segment,
                          uint32_t transaction_attributes)
{
    bm_status_t status;

    if ((bcu == NULL) || (bus == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    while (bm_v30_bcu_queue_count(bcu) == 0U) {
        status = bm_v30_bcu_step_prefetch(
            bcu, bus, code_segment, transaction_attributes, 1);
        if (status != BM_STATUS_OK)
            return status;
    }
    return BM_STATUS_OK;
}

void
bm_v30_bcu_record_transaction(bm_v30_bcu_t *bcu,
                              const bm_bus_transaction_t *transaction,
                              bm_status_t status)
{
    uint64_t clocks;

    if ((status != BM_STATUS_OK) || (transaction == NULL))
        return;
    clocks = 4U + (uint64_t) transaction->wait_states;
    ++bcu->boundary_bus_transactions;
    bcu->boundary_wait_states += transaction->wait_states;
    bcu->boundary_bus_active_clocks += clocks;
}
