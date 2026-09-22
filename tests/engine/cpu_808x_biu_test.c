/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Synthetic tests for the instance-owned 808x bus interface state. No
 * firmware or host timing is involved.
 */
#include "cpu_808x_biu.h"

#include <blumach/platforms/null_host.h>

#include <assert.h>
#include <stdint.h>

typedef struct prefetch_fixture {
    bm_status_t status;
    uint64_t value;
    uint32_t wait_states;
    unsigned int calls;
    bm_bus_transaction_t last;
} prefetch_fixture_t;

typedef struct phase_capture {
    bm_808x_bus_phase_observation_t observations[16];
    size_t count;
} phase_capture_t;

static void
capture_phase(void *context,
              const bm_808x_bus_phase_observation_t *observation)
{
    phase_capture_t *capture = context;

    assert(capture != NULL);
    assert(observation != NULL);
    assert(capture->count < 16U);
    capture->observations[capture->count++] = *observation;
}

static bm_status_t
prefetch_access(void *context, bm_bus_transaction_t *transaction)
{
    prefetch_fixture_t *fixture = context;

    assert(fixture != NULL);
    assert(transaction != NULL);
    ++fixture->calls;
    fixture->last = *transaction;
    if (fixture->status != BM_STATUS_OK)
        return fixture->status;
    transaction->value = fixture->value;
    transaction->wait_states += fixture->wait_states;
    return BM_STATUS_OK;
}

static void
test_queue_order_and_wrap(void)
{
    bm_808x_biu_t bcu;
    uint8_t value = 0U;

    bm_808x_biu_reset(&bcu, 0xfffcU,
                      BM_808X_V30_PREFETCH_QUEUE_CAPACITY, 2U);
    assert(bm_808x_biu_prefetch_pointer(&bcu) == 0xfffcU);
    assert(bm_808x_biu_queue_count(&bcu) == 0U);
    assert(bm_808x_biu_free_bytes(&bcu) == 6U);

    assert(bm_808x_biu_enqueue_word(&bcu, 0x2211U) == BM_STATUS_OK);
    assert(bm_808x_biu_enqueue_word(&bcu, 0x4433U) == BM_STATUS_OK);
    assert(bm_808x_biu_enqueue_word(&bcu, 0x6655U) == BM_STATUS_OK);
    assert(bm_808x_biu_prefetch_pointer(&bcu) == 0x0002U);
    assert(bm_808x_biu_enqueue_byte(&bcu, 0x77U) ==
           BM_STATUS_INVALID_STATE);

    assert(bm_808x_biu_dequeue_byte(&bcu, &value) == BM_STATUS_OK);
    assert(value == 0x11U);
    assert(bm_808x_biu_dequeue_byte(&bcu, &value) == BM_STATUS_OK);
    assert(value == 0x22U);
    assert(bm_808x_biu_enqueue_word(&bcu, 0x8877U) == BM_STATUS_OK);

    assert(bm_808x_biu_dequeue_byte(&bcu, &value) == BM_STATUS_OK);
    assert(value == 0x33U);
    assert(bm_808x_biu_dequeue_byte(&bcu, &value) == BM_STATUS_OK);
    assert(value == 0x44U);
    assert(bm_808x_biu_dequeue_byte(&bcu, &value) == BM_STATUS_OK);
    assert(value == 0x55U);
    assert(bm_808x_biu_dequeue_byte(&bcu, &value) == BM_STATUS_OK);
    assert(value == 0x66U);
    assert(bm_808x_biu_dequeue_byte(&bcu, &value) == BM_STATUS_OK);
    assert(value == 0x77U);
    assert(bm_808x_biu_dequeue_byte(&bcu, &value) == BM_STATUS_OK);
    assert(value == 0x88U);
    assert(bm_808x_biu_dequeue_byte(&bcu, &value) ==
           BM_STATUS_INVALID_STATE);
    assert(bcu.boundary_instruction_queue_reads == 8U);
}

static void
test_boundary_accounting_and_flush(void)
{
    bm_808x_biu_t bcu;

    bm_808x_biu_reset(&bcu, 0U,
                      BM_808X_V30_PREFETCH_QUEUE_CAPACITY, 2U);
    assert(bm_808x_biu_enqueue_byte(&bcu, 0x90U) == BM_STATUS_OK);
    assert(bcu.boundary_bus_transactions == 0U);
    assert(bcu.boundary_wait_states == 0U);
    assert(bcu.boundary_bus_active_clocks == 0U);
    assert(bcu.boundary_demand_prefetch_transactions == 0U);
    assert(bcu.boundary_demand_prefetch_bus_clocks == 0U);

    bm_808x_biu_flush(&bcu, 0x1234U);
    assert(bm_808x_biu_queue_count(&bcu) == 0U);
    assert(bm_808x_biu_prefetch_pointer(&bcu) == 0x1234U);
    assert(bcu.boundary_prefetch_flushed != 0);

    bm_808x_biu_begin_boundary(&bcu);
    assert(bm_808x_biu_prefetch_pointer(&bcu) == 0x1234U);
    assert(bcu.boundary_bus_transactions == 0U);
    assert(bcu.boundary_wait_states == 0U);
    assert(bcu.boundary_bus_active_clocks == 0U);
    assert(bcu.boundary_demand_prefetch_transactions == 0U);
    assert(bcu.boundary_demand_prefetch_bus_clocks == 0U);
    assert(bcu.boundary_operand_transactions == 0U);
    assert(bcu.boundary_operand_bus_clocks == 0U);
    assert(bcu.boundary_prefetch_handoff_clocks == 0U);
    assert(bcu.boundary_instruction_queue_reads == 0U);
    assert(bcu.boundary_prefetch_flushed == 0);
}

static void
test_prefetch_phases_and_waits(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_bus_t *bus = NULL;
    bm_808x_biu_t bcu;
    prefetch_fixture_t fixture = {
        BM_STATUS_OK, 0x2211U, 2U, 0U, { 0 }
    };
    phase_capture_t capture = { 0 };
    uint8_t value = 0U;

    assert(bm_bus_create(&host, 1U, &bus) == BM_STATUS_OK);
    assert(bm_bus_map(bus, BM_ADDRESS_MEMORY, 0U, 0xfffffU,
                      prefetch_access, &fixture) == BM_STATUS_OK);
    bm_808x_biu_reset(&bcu, 0U,
                      BM_808X_V30_PREFETCH_QUEUE_CAPACITY, 2U);
    bm_808x_biu_set_phase_observer(&bcu, capture_phase, &capture);
    bm_808x_biu_begin_boundary(&bcu);

    assert(bm_808x_biu_step_prefetch(&bcu, bus, 0xffffU, 0U, 1) ==
           BM_STATUS_OK);
    assert(bcu.prefetch_phase == BM_808X_BIU_PHASE_T2);
    assert(fixture.calls == 0U);
    assert(bm_808x_biu_step_prefetch(&bcu, bus, 0xffffU, 0U, 1) ==
           BM_STATUS_OK);
    assert(bcu.prefetch_phase == BM_808X_BIU_PHASE_T3);
    assert(bm_808x_biu_step_prefetch(&bcu, bus, 0xffffU, 0U, 1) ==
           BM_STATUS_OK);
    assert(bcu.prefetch_phase == BM_808X_BIU_PHASE_TW);
    assert(fixture.calls == 1U);
    assert(fixture.last.address == 0xffff0U);
    assert(fixture.last.size == 2U);
    assert(bm_808x_biu_step_prefetch(&bcu, bus, 0xffffU, 0U, 1) ==
           BM_STATUS_OK);
    assert(bcu.prefetch_phase == BM_808X_BIU_PHASE_TW);
    assert(bm_808x_biu_step_prefetch(&bcu, bus, 0xffffU, 0U, 1) ==
           BM_STATUS_OK);
    assert(bcu.prefetch_phase == BM_808X_BIU_PHASE_T4);
    assert(bm_808x_biu_queue_count(&bcu) == 0U);
    assert(bm_808x_biu_step_prefetch(&bcu, bus, 0xffffU, 0U, 1) ==
           BM_STATUS_OK);
    assert(bcu.prefetch_phase == BM_808X_BIU_PHASE_IDLE);
    assert(bcu.total_phase_clocks == 6U);
    assert(bm_808x_biu_queue_count(&bcu) == 2U);
    assert(bcu.boundary_bus_transactions == 1U);
    assert(bcu.boundary_wait_states == 2U);
    assert(bcu.boundary_bus_active_clocks == 6U);
    assert(bcu.boundary_demand_prefetch_transactions == 1U);
    assert(bcu.boundary_demand_prefetch_bus_clocks == 6U);
    assert(bcu.boundary_operand_transactions == 0U);
    assert(bcu.boundary_operand_bus_clocks == 0U);
    assert(bcu.boundary_prefetch_handoff_clocks == 0U);
    assert(capture.count == 6U);
    assert(capture.observations[0].phase == BM_808X_BUS_PHASE_T1);
    assert(capture.observations[1].phase == BM_808X_BUS_PHASE_T2);
    assert(capture.observations[2].phase == BM_808X_BUS_PHASE_T3);
    assert(capture.observations[3].phase == BM_808X_BUS_PHASE_TW);
    assert(capture.observations[4].phase == BM_808X_BUS_PHASE_TW);
    assert(capture.observations[5].phase == BM_808X_BUS_PHASE_T4);
    assert(capture.observations[0].bus_active_clock_index == 0U);
    assert(capture.observations[5].bus_active_clock_index == 5U);
    assert(capture.observations[1].response_valid == 0U);
    assert(capture.observations[2].response_valid == 1U);
    assert(capture.observations[5].response_valid == 1U);
    assert(capture.observations[2].transaction.operation == BM_BUS_FETCH);
    assert(capture.observations[2].transaction.address == 0xffff0U);
    assert(capture.observations[2].transaction.value == 0x2211U);
    assert(capture.observations[2].transaction.wait_states == 2U);
    assert(bm_808x_biu_dequeue_byte(&bcu, &value) == BM_STATUS_OK);
    assert(value == 0x11U);
    assert(bm_808x_biu_dequeue_byte(&bcu, &value) == BM_STATUS_OK);
    assert(value == 0x22U);

    fixture.value = 0xaaU;
    fixture.wait_states = 0U;
    bm_808x_biu_reset(&bcu, 1U,
                      BM_808X_V30_PREFETCH_QUEUE_CAPACITY, 2U);
    assert(bm_808x_biu_fill_on_demand(&bcu, bus, 0U,
                                     BM_BUS_TRANSACTION_LOCKED) ==
           BM_STATUS_OK);
    assert(bcu.total_phase_clocks == 4U);
    assert(fixture.last.address == 1U);
    assert(fixture.last.size == 1U);
    assert(fixture.last.attributes == BM_BUS_TRANSACTION_LOCKED);
    assert(bm_808x_biu_prefetch_pointer(&bcu) == 2U);
    assert(bm_808x_biu_dequeue_byte(&bcu, &value) == BM_STATUS_OK);
    assert(value == 0xaaU);

    assert(bm_808x_biu_step_prefetch(&bcu, bus, 0U,
                                    BM_BUS_TRANSACTION_DEBUG, 0) ==
           BM_STATUS_INVALID_ARGUMENT);
    bm_bus_destroy(bus);
}

static void
test_operand_waits_for_inflight_prefetch(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_bus_t *bus = NULL;
    bm_808x_biu_t bcu;
    prefetch_fixture_t fixture = {
        BM_STATUS_OK, 0x2211U, 1U, 0U, { 0 }
    };
    phase_capture_t capture = { 0 };
    bm_bus_transaction_t operand = {
        BM_ADDRESS_MEMORY, BM_BUS_READ, 0x0040U, 0U,
        1U, 1U, 0U, BM_ENDIAN_LITTLE, 0U
    };

    assert(bm_bus_create(&host, 1U, &bus) == BM_STATUS_OK);
    assert(bm_bus_map(bus, BM_ADDRESS_MEMORY, 0U, 0xfffffU,
                      prefetch_access, &fixture) == BM_STATUS_OK);
    bm_808x_biu_reset(&bcu, 0U,
                      BM_808X_V30_PREFETCH_QUEUE_CAPACITY, 2U);
    bm_808x_biu_set_phase_observer(&bcu, capture_phase, &capture);
    bm_808x_biu_set_clocked_timeline(&bcu, 1);
    bm_808x_biu_begin_boundary(&bcu);

    /* The operand request arrives after T1 of a word prefetch. It must let
     * T2/T3/Tw/T4 finish, then acquire a fresh T1 boundary for itself. */
    assert(bm_808x_biu_step_prefetch(&bcu, bus, 0U, 0U, 0) ==
           BM_STATUS_OK);
    assert(bcu.prefetch_phase == BM_808X_BIU_PHASE_T2);
    assert(bm_808x_biu_transact(&bcu, bus, &operand) == BM_STATUS_OK);

    assert(fixture.calls == 2U);
    assert(fixture.last.operation == BM_BUS_READ);
    assert(fixture.last.address == 0x0040U);
    assert(operand.value == 0x2211U);
    assert(operand.wait_states == 1U);
    assert(bcu.prefetch_phase == BM_808X_BIU_PHASE_IDLE);
    assert(bm_808x_biu_queue_count(&bcu) == 2U);
    assert(bcu.total_phase_clocks == 10U);
    assert(bcu.boundary_bus_transactions == 2U);
    assert(bcu.boundary_wait_states == 2U);
    assert(bcu.boundary_bus_active_clocks == 10U);
    assert(bcu.boundary_prefetch_transactions == 1U);
    assert(bcu.boundary_prefetch_phase_clocks == 5U);
    assert(bcu.boundary_operand_transactions == 1U);
    assert(bcu.boundary_operand_bus_clocks == 5U);
    assert(bcu.boundary_prefetch_handoff_clocks == 4U);
    assert(bcu.clocked_cpu_cycles == 10U);
    assert(capture.count == 10U);
    for (size_t index = 0U; index < capture.count; ++index) {
        assert(capture.observations[index].cpu_clock_known == 1U);
        assert(capture.observations[index].cpu_clock_index == index);
    }
    assert(capture.observations[5].phase == BM_808X_BUS_PHASE_T1);
    assert(capture.observations[6].phase == BM_808X_BUS_PHASE_T2);
    assert(capture.observations[7].phase == BM_808X_BUS_PHASE_T3);
    assert(capture.observations[8].phase == BM_808X_BUS_PHASE_TW);
    assert(capture.observations[9].phase == BM_808X_BUS_PHASE_T4);

    bm_bus_destroy(bus);
}

static void
test_inflight_prefetch_becomes_demand_when_queue_empties(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_bus_t *bus = NULL;
    bm_808x_biu_t bcu;
    prefetch_fixture_t fixture = {
        BM_STATUS_OK, 0x2211U, 0U, 0U, { 0 }
    };

    assert(bm_bus_create(&host, 1U, &bus) == BM_STATUS_OK);
    assert(bm_bus_map(bus, BM_ADDRESS_MEMORY, 0U, 0xfffffU,
                      prefetch_access, &fixture) == BM_STATUS_OK);
    bm_808x_biu_reset(&bcu, 0U,
                      BM_808X_V30_PREFETCH_QUEUE_CAPACITY, 2U);

    assert(bm_808x_biu_step_prefetch(&bcu, bus, 0U, 0U, 0) ==
           BM_STATUS_OK);
    assert(bm_808x_biu_step_prefetch(&bcu, bus, 0U, 0U, 0) ==
           BM_STATUS_OK);
    assert(bm_808x_biu_step_prefetch(&bcu, bus, 0U, 0U, 0) ==
           BM_STATUS_OK);
    assert(bcu.prefetch_phase == BM_808X_BIU_PHASE_T4);
    assert(bm_808x_biu_queue_count(&bcu) == 0U);

    bm_808x_biu_begin_boundary(&bcu);
    assert(bm_808x_biu_fill_on_demand(&bcu, bus, 0U, 0U) == BM_STATUS_OK);
    assert(bm_808x_biu_queue_count(&bcu) == 2U);
    assert(bcu.boundary_demand_prefetch_bus_clocks == 1U);
    assert(bcu.boundary_demand_prefetch_transactions == 0U);
    assert(bcu.boundary_prefetch_phase_clocks == 1U);
    assert(bcu.boundary_prefetch_handoff_clocks == 0U);

    bm_bus_destroy(bus);
}

static void
test_prefetch_abort_and_bus_error(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_bus_t *bus = NULL;
    bm_808x_biu_t bcu;
    prefetch_fixture_t fixture = {
        BM_STATUS_UNMAPPED, 0U, 0U, 0U, { 0 }
    };
    phase_capture_t capture = { 0 };

    assert(bm_bus_create(&host, 1U, &bus) == BM_STATUS_OK);
    assert(bm_bus_map(bus, BM_ADDRESS_MEMORY, 0U, 0xfffffU,
                      prefetch_access, &fixture) == BM_STATUS_OK);
    bm_808x_biu_reset(&bcu, 0U,
                      BM_808X_V30_PREFETCH_QUEUE_CAPACITY, 2U);
    bm_808x_biu_set_phase_observer(&bcu, capture_phase, &capture);
    assert(bm_808x_biu_step_prefetch(&bcu, bus, 0U, 0U, 0) ==
           BM_STATUS_OK);
    assert(bm_808x_biu_step_prefetch(&bcu, bus, 0U, 0U, 0) ==
           BM_STATUS_OK);
    assert(bcu.prefetch_phase == BM_808X_BIU_PHASE_T3);
    assert(bm_808x_biu_step_prefetch(&bcu, bus, 0U, 0U, 0) ==
           BM_STATUS_UNMAPPED);
    assert(bcu.prefetch_phase == BM_808X_BIU_PHASE_IDLE);
    assert(bm_808x_biu_queue_count(&bcu) == 0U);
    assert(bcu.boundary_bus_transactions == 0U);
    assert(capture.count == 3U);
    assert(capture.observations[2].phase == BM_808X_BUS_PHASE_T3);
    assert(capture.observations[2].response_valid == 0U);

    fixture.status = BM_STATUS_OK;
    assert(bm_808x_biu_step_prefetch(&bcu, bus, 0U, 0U, 0) ==
           BM_STATUS_OK);
    assert(bcu.prefetch_phase == BM_808X_BIU_PHASE_T2);
    bm_808x_biu_suspend_prefetch(&bcu);
    assert(bcu.prefetch_phase == BM_808X_BIU_PHASE_IDLE);
    assert(bm_808x_biu_prefetch_pointer(&bcu) == 0U);
    assert(bm_808x_biu_queue_count(&bcu) == 0U);
    assert(bm_808x_biu_step_prefetch(&bcu, bus, 0U, 0U, 0) ==
           BM_STATUS_OK);
    assert(bcu.prefetch_phase == BM_808X_BIU_PHASE_T2);
    bm_808x_biu_flush(&bcu, 0x4567U);
    assert(bcu.prefetch_phase == BM_808X_BIU_PHASE_IDLE);
    assert(bm_808x_biu_prefetch_pointer(&bcu) == 0x4567U);
    assert(bm_808x_biu_queue_count(&bcu) == 0U);
    bm_bus_destroy(bus);
}

static void
test_8088_byte_fetch_and_four_byte_queue(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_bus_t *bus = NULL;
    bm_808x_biu_t biu;
    prefetch_fixture_t fixture = { 0 };
    uint8_t value = 0U;
    unsigned int index;

    assert(bm_bus_create(&host, 1U, &bus) == BM_STATUS_OK);
    assert(bm_bus_map(bus, BM_ADDRESS_MEMORY, 0U, 0xfffffU,
                      prefetch_access, &fixture) == BM_STATUS_OK);
    bm_808x_biu_reset(&biu, 0U,
                      BM_808X_8088_PREFETCH_QUEUE_CAPACITY, 1U);
    for (index = 0U; index < BM_808X_8088_PREFETCH_QUEUE_CAPACITY; ++index)
        assert(bm_808x_biu_fill_on_demand(&biu, bus, 0U, 0U) ==
               BM_STATUS_OK &&
               bm_808x_biu_dequeue_byte(&biu, &value) ==
               BM_STATUS_OK);

    bm_808x_biu_reset(&biu, 0U,
                      BM_808X_8088_PREFETCH_QUEUE_CAPACITY, 1U);
    for (index = 0U; index < 16U; ++index)
        assert(bm_808x_biu_step_prefetch(&biu, bus, 0U, 0U, 0) ==
               BM_STATUS_OK);
    assert(bm_808x_biu_queue_count(&biu) ==
           BM_808X_8088_PREFETCH_QUEUE_CAPACITY);
    assert(bm_808x_biu_free_bytes(&biu) == 0U);
    assert(fixture.calls >= BM_808X_8088_PREFETCH_QUEUE_CAPACITY);
    assert(fixture.last.size == 1U);
    bm_808x_biu_flush(&biu, 0x1234U);
    assert(bm_808x_biu_queue_count(&biu) == 0U);
    assert(bm_808x_biu_prefetch_pointer(&biu) == 0x1234U);
    bm_bus_destroy(bus);
}

int
main(void)
{
    test_queue_order_and_wrap();
    test_boundary_accounting_and_flush();
    test_prefetch_phases_and_waits();
    test_operand_waits_for_inflight_prefetch();
    test_inflight_prefetch_becomes_demand_when_queue_empties();
    test_prefetch_abort_and_bus_error();
    test_8088_byte_fetch_and_four_byte_queue();
    return 0;
}
