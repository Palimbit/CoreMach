/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Derived rewrite of the inherited Vx0 bus interface unit. The original work
 * includes Copyright 2015-2020 Andrew Jenner and Copyright 2016-2020 Miran
 * Grca.
 */
#include "cpu_808x_biu.h"

#include <string.h>

static uint8_t
queue_tail(const bm_808x_biu_t *bcu)
{
    return (uint8_t) ((bcu->prefetch_head + bcu->prefetch_count) %
                      bcu->prefetch_capacity);
}

static uint32_t
prefetch_physical_address(uint16_t code_segment, uint16_t offset)
{
    return ((((uint32_t) code_segment << 4U) + offset) & 0xfffffU);
}

static int
can_begin_prefetch(const bm_808x_biu_t *bcu)
{
    const uint8_t required = bcu->fetch_width == 1U ? 1U :
        ((bcu->prefetch_pointer & 1U) == 0U ? 2U : 1U);

    return bm_808x_biu_free_bytes(bcu) >= required;
}

static void
abort_pending_prefetch(bm_808x_biu_t *bcu)
{
    bcu->prefetch_phase = BM_808X_BIU_PHASE_IDLE;
    memset(&bcu->pending_prefetch, 0, sizeof(bcu->pending_prefetch));
    bcu->pending_wait_clocks = 0U;
    bcu->pending_prefetch_valid = 0;
    bcu->pending_prefetch_demand = 0;
}

void
bm_808x_biu_reset(bm_808x_biu_t *bcu, uint16_t instruction_pointer,
                  uint8_t prefetch_capacity, uint8_t fetch_width)
{
    memset(bcu, 0, sizeof(*bcu));
    bcu->prefetch_pointer = instruction_pointer;
    bcu->prefetch_capacity = prefetch_capacity;
    bcu->fetch_width = fetch_width;
}

void
bm_808x_biu_begin_boundary(bm_808x_biu_t *bcu)
{
    bcu->boundary_bus_transactions = 0U;
    bcu->boundary_wait_states = 0U;
    bcu->boundary_bus_active_clocks = 0U;
    bcu->boundary_demand_prefetch_transactions = 0U;
    bcu->boundary_demand_prefetch_bus_clocks = 0U;
    bcu->boundary_prefetch_transactions = 0U;
    bcu->boundary_prefetch_phase_clocks = 0U;
    bcu->boundary_operand_transactions = 0U;
    bcu->boundary_operand_bus_clocks = 0U;
    bcu->boundary_prefetch_handoff_clocks = 0U;
    bcu->boundary_instruction_queue_reads = 0U;
    bcu->boundary_prefetch_flushed = 0;
}

void
bm_808x_biu_suspend_prefetch(bm_808x_biu_t *bcu)
{
    abort_pending_prefetch(bcu);
}

void
bm_808x_biu_flush(bm_808x_biu_t *bcu, uint16_t instruction_pointer)
{
    abort_pending_prefetch(bcu);
    bcu->prefetch_head = 0U;
    bcu->prefetch_count = 0U;
    bcu->prefetch_pointer = instruction_pointer;
    bcu->boundary_prefetch_flushed = 1;
}

uint8_t
bm_808x_biu_free_bytes(const bm_808x_biu_t *bcu)
{
    return (uint8_t) (bcu->prefetch_capacity - bcu->prefetch_count);
}

uint8_t
bm_808x_biu_queue_count(const bm_808x_biu_t *bcu)
{
    return bcu->prefetch_count;
}

uint16_t
bm_808x_biu_prefetch_pointer(const bm_808x_biu_t *bcu)
{
    return bcu->prefetch_pointer;
}

bm_status_t
bm_808x_biu_enqueue_byte(bm_808x_biu_t *bcu, uint8_t value)
{
    if (bcu->prefetch_count == bcu->prefetch_capacity)
        return BM_STATUS_INVALID_STATE;
    bcu->prefetch_queue[queue_tail(bcu)] = value;
    ++bcu->prefetch_count;
    ++bcu->prefetch_pointer;
    return BM_STATUS_OK;
}

bm_status_t
bm_808x_biu_enqueue_word(bm_808x_biu_t *bcu, uint16_t value)
{
    uint8_t tail;

    if (bm_808x_biu_free_bytes(bcu) < 2U)
        return BM_STATUS_INVALID_STATE;
    tail = queue_tail(bcu);
    bcu->prefetch_queue[tail] = (uint8_t) value;
    tail = (uint8_t) ((tail + 1U) % bcu->prefetch_capacity);
    bcu->prefetch_queue[tail] = (uint8_t) (value >> 8U);
    bcu->prefetch_count = (uint8_t) (bcu->prefetch_count + 2U);
    bcu->prefetch_pointer = (uint16_t) (bcu->prefetch_pointer + 2U);
    return BM_STATUS_OK;
}

bm_status_t
bm_808x_biu_dequeue_byte(bm_808x_biu_t *bcu, uint8_t *value)
{
    if (value == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    if (bcu->prefetch_count == 0U)
        return BM_STATUS_INVALID_STATE;
    *value = bcu->prefetch_queue[bcu->prefetch_head];
    bcu->prefetch_head = (uint8_t)
        ((bcu->prefetch_head + 1U) % bcu->prefetch_capacity);
    --bcu->prefetch_count;
    ++bcu->boundary_instruction_queue_reads;
    return BM_STATUS_OK;
}

bm_status_t
bm_808x_biu_step_prefetch(bm_808x_biu_t *bcu,
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

    if (bcu->prefetch_phase == BM_808X_BIU_PHASE_IDLE) {
        uint32_t width;

        if (!can_begin_prefetch(bcu))
            return BM_STATUS_IDLE;
        width = bcu->fetch_width == 1U ? 1U :
            ((bcu->prefetch_pointer & 1U) == 0U ? 2U : 1U);
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
        bcu->prefetch_phase = BM_808X_BIU_PHASE_T1;
    }

    ++bcu->total_phase_clocks;
    ++bcu->boundary_bus_active_clocks;
    ++bcu->boundary_prefetch_phase_clocks;
    if (bcu->pending_prefetch_demand)
        ++bcu->boundary_demand_prefetch_bus_clocks;
    switch (bcu->prefetch_phase) {
        case BM_808X_BIU_PHASE_T1:
            bcu->prefetch_phase = BM_808X_BIU_PHASE_T2;
            break;
        case BM_808X_BIU_PHASE_T2:
            bcu->prefetch_phase = BM_808X_BIU_PHASE_T3;
            break;
        case BM_808X_BIU_PHASE_T3:
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
                BM_808X_BIU_PHASE_TW : BM_808X_BIU_PHASE_T4;
            break;
        case BM_808X_BIU_PHASE_TW:
            --bcu->pending_wait_clocks;
            if (bcu->pending_wait_clocks == 0U)
                bcu->prefetch_phase = BM_808X_BIU_PHASE_T4;
            break;
        case BM_808X_BIU_PHASE_T4:
            if (!bcu->pending_prefetch_valid) {
                abort_pending_prefetch(bcu);
                return BM_STATUS_INVALID_STATE;
            }
            if (bcu->pending_prefetch.size == 2U)
                status = bm_808x_biu_enqueue_word(
                    bcu, (uint16_t) bcu->pending_prefetch.value);
            else
                status = bm_808x_biu_enqueue_byte(
                    bcu, (uint8_t) bcu->pending_prefetch.value);
            abort_pending_prefetch(bcu);
            break;
        case BM_808X_BIU_PHASE_IDLE:
        default:
            return BM_STATUS_INVALID_STATE;
    }
    return status;
}

bm_status_t
bm_808x_biu_advance_prefetch(bm_808x_biu_t *bcu,
                            bm_bus_t *bus,
                            uint16_t code_segment,
                            uint32_t transaction_attributes,
                            uint32_t clock_budget)
{
    uint32_t clock;

    if ((bcu == NULL) || (bus == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    for (clock = 0U; clock < clock_budget; ++clock) {
        bm_status_t status = bm_808x_biu_step_prefetch(
            bcu, bus, code_segment, transaction_attributes, 0);

        if (status == BM_STATUS_IDLE)
            return BM_STATUS_OK;
        if (status != BM_STATUS_OK)
            return status;
    }
    return BM_STATUS_OK;
}

bm_status_t
bm_808x_biu_fill_on_demand(bm_808x_biu_t *bcu,
                          bm_bus_t *bus,
                          uint16_t code_segment,
                          uint32_t transaction_attributes)
{
    bm_status_t status;

    if ((bcu == NULL) || (bus == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    while (bm_808x_biu_queue_count(bcu) == 0U) {
        /* A speculative fetch may have started in an earlier boundary. Once
         * an empty queue blocks the EXU, every remaining phase is demand
         * latency even though the transaction itself was already issued. */
        if (bcu->prefetch_phase != BM_808X_BIU_PHASE_IDLE)
            bcu->pending_prefetch_demand = 1;
        status = bm_808x_biu_step_prefetch(
            bcu, bus, code_segment, transaction_attributes, 1);
        if (status != BM_STATUS_OK)
            return status;
    }
    return BM_STATUS_OK;
}

bm_status_t
bm_808x_biu_transact(bm_808x_biu_t *bcu,
                    bm_bus_t *bus,
                    bm_bus_transaction_t *transaction)
{
    uint64_t handoff_start;
    uint64_t clocks;
    bm_status_t status;

    if ((bcu == NULL) || (bus == NULL) || (transaction == NULL))
        return BM_STATUS_INVALID_ARGUMENT;

    /* NEC bus requests do not discard an instruction fetch already in
     * progress. Finish only that transfer; a new prefetch must not win the
     * bus after the operand request exists. */
    handoff_start = bcu->boundary_prefetch_phase_clocks;
    while (bcu->prefetch_phase != BM_808X_BIU_PHASE_IDLE) {
        status = bm_808x_biu_step_prefetch(bcu, bus, 0U, 0U, 0);
        if (status != BM_STATUS_OK)
            return status;
    }
    bcu->boundary_prefetch_handoff_clocks +=
        bcu->boundary_prefetch_phase_clocks - handoff_start;

    /* T1 and T2 precede the portable access, which occurs at T3. Tw clocks
     * reported by the mapped device follow T3, then T4 closes the cycle. */
    bcu->total_phase_clocks += 3U;
    bcu->boundary_bus_active_clocks += 3U;
    bcu->boundary_operand_bus_clocks += 3U;
    status = bm_bus_transact(bus, transaction);
    if (status != BM_STATUS_OK)
        return status;

    clocks = 4U + (uint64_t) transaction->wait_states;
    ++bcu->boundary_bus_transactions;
    ++bcu->boundary_operand_transactions;
    bcu->boundary_wait_states += transaction->wait_states;
    bcu->total_phase_clocks += clocks - 3U;
    bcu->boundary_bus_active_clocks += clocks - 3U;
    bcu->boundary_operand_bus_clocks += clocks - 3U;
    return BM_STATUS_OK;
}
