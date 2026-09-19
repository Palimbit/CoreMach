/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Synthetic tests for the instance-owned V30 bus control unit state. No
 * firmware or host timing is involved.
 */
#include "v30_bcu.h"

#include <assert.h>
#include <stdint.h>

static void
test_queue_order_and_wrap(void)
{
    bm_v30_bcu_t bcu;
    uint8_t value = 0U;

    bm_v30_bcu_reset(&bcu, 0xfffcU);
    assert(bm_v30_bcu_prefetch_pointer(&bcu) == 0xfffcU);
    assert(bm_v30_bcu_queue_count(&bcu) == 0U);
    assert(bm_v30_bcu_free_bytes(&bcu) == 6U);

    assert(bm_v30_bcu_enqueue_word(&bcu, 0x2211U) == BM_STATUS_OK);
    assert(bm_v30_bcu_enqueue_word(&bcu, 0x4433U) == BM_STATUS_OK);
    assert(bm_v30_bcu_enqueue_word(&bcu, 0x6655U) == BM_STATUS_OK);
    assert(bm_v30_bcu_prefetch_pointer(&bcu) == 0x0002U);
    assert(bm_v30_bcu_enqueue_byte(&bcu, 0x77U) ==
           BM_STATUS_INVALID_STATE);

    assert(bm_v30_bcu_dequeue_byte(&bcu, &value) == BM_STATUS_OK);
    assert(value == 0x11U);
    assert(bm_v30_bcu_dequeue_byte(&bcu, &value) == BM_STATUS_OK);
    assert(value == 0x22U);
    assert(bm_v30_bcu_enqueue_word(&bcu, 0x8877U) == BM_STATUS_OK);

    assert(bm_v30_bcu_dequeue_byte(&bcu, &value) == BM_STATUS_OK);
    assert(value == 0x33U);
    assert(bm_v30_bcu_dequeue_byte(&bcu, &value) == BM_STATUS_OK);
    assert(value == 0x44U);
    assert(bm_v30_bcu_dequeue_byte(&bcu, &value) == BM_STATUS_OK);
    assert(value == 0x55U);
    assert(bm_v30_bcu_dequeue_byte(&bcu, &value) == BM_STATUS_OK);
    assert(value == 0x66U);
    assert(bm_v30_bcu_dequeue_byte(&bcu, &value) == BM_STATUS_OK);
    assert(value == 0x77U);
    assert(bm_v30_bcu_dequeue_byte(&bcu, &value) == BM_STATUS_OK);
    assert(value == 0x88U);
    assert(bm_v30_bcu_dequeue_byte(&bcu, &value) ==
           BM_STATUS_INVALID_STATE);
    assert(bcu.boundary_instruction_queue_reads == 8U);
}

static void
test_boundary_accounting_and_flush(void)
{
    bm_v30_bcu_t bcu;
    bm_bus_transaction_t transaction = {
        BM_ADDRESS_MEMORY, BM_BUS_FETCH, 0xffff0U, 0U,
        2U, 2U, 3U, BM_ENDIAN_LITTLE, 0U
    };

    bm_v30_bcu_reset(&bcu, 0U);
    assert(bm_v30_bcu_enqueue_byte(&bcu, 0x90U) == BM_STATUS_OK);
    bm_v30_bcu_record_transaction(&bcu, &transaction, BM_STATUS_OK, 1);
    assert(bcu.boundary_bus_transactions == 1U);
    assert(bcu.boundary_wait_states == 3U);
    assert(bcu.boundary_bus_active_clocks == 7U);
    assert(bcu.boundary_demand_prefetch_transactions == 1U);
    assert(bcu.boundary_demand_prefetch_bus_clocks == 7U);

    bm_v30_bcu_flush(&bcu, 0x1234U);
    assert(bm_v30_bcu_queue_count(&bcu) == 0U);
    assert(bm_v30_bcu_prefetch_pointer(&bcu) == 0x1234U);
    assert(bcu.boundary_prefetch_flushed != 0);

    bm_v30_bcu_begin_boundary(&bcu);
    assert(bm_v30_bcu_prefetch_pointer(&bcu) == 0x1234U);
    assert(bcu.boundary_bus_transactions == 0U);
    assert(bcu.boundary_wait_states == 0U);
    assert(bcu.boundary_bus_active_clocks == 0U);
    assert(bcu.boundary_demand_prefetch_transactions == 0U);
    assert(bcu.boundary_demand_prefetch_bus_clocks == 0U);
    assert(bcu.boundary_instruction_queue_reads == 0U);
    assert(bcu.boundary_prefetch_flushed == 0);

    bm_v30_bcu_record_transaction(&bcu, &transaction,
                                  BM_STATUS_INVALID_STATE, 1);
    bm_v30_bcu_record_transaction(&bcu, NULL, BM_STATUS_OK, 1);
    assert(bcu.boundary_bus_transactions == 0U);
}

int
main(void)
{
    test_queue_order_and_wrap();
    test_boundary_accounting_and_flush();
    return 0;
}
