/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Synthetic contract tests for documented V30 execution clocks, portable bus
 * wait reporting and instruction-queue invalidation. No firmware is used.
 */
#include "cpu_808x_test_harness.h"

#include <blumach/components/bus.h>

#include <assert.h>
#include <stdint.h>
#include <string.h>

enum {
    TEST_FLAG_IF = 0x0200,
    TEST_FLAG_ZF = 0x0040
};

typedef struct timing_capture {
    bm_808x_timing_observation_t last;
    unsigned int count;
} timing_capture_t;

typedef struct timing_io_fixture {
    cpu_808x_test_machine_t *machine;
    unsigned int event_count;
    unsigned int signal_after;
    uint8_t next_value;
} timing_io_fixture_t;

typedef struct bus_capture {
    bm_bus_transaction_t transactions[16];
    size_t count;
} bus_capture_t;

static void
capture_bus_transaction(void *context,
                        const bm_bus_transaction_t *transaction)
{
    bus_capture_t *capture = context;

    assert(capture != NULL);
    assert(transaction != NULL);
    assert(capture->count <
           sizeof(capture->transactions) / sizeof(capture->transactions[0]));
    capture->transactions[capture->count++] = *transaction;
}

static bm_status_t
timing_io_access(void *context, bm_bus_transaction_t *transaction)
{
    timing_io_fixture_t *fixture = context;

    assert(fixture != NULL);
    assert(transaction->size == 1U);
    ++fixture->event_count;
    if (transaction->operation == BM_BUS_READ) {
        transaction->value = fixture->next_value++;
    } else {
        assert(transaction->operation == BM_BUS_WRITE);
    }
    if ((fixture->signal_after != 0U) &&
        (fixture->event_count == fixture->signal_after))
        assert(bm_engine_signal_cpu(fixture->machine->engine, 0U,
                                    BM_808X_SIGNAL_INT, 1) == BM_STATUS_OK);
    return BM_STATUS_OK;
}

static void
capture_timing(void *context,
               const bm_808x_timing_observation_t *observation)
{
    timing_capture_t *capture = context;

    assert(capture != NULL);
    assert(observation != NULL);
    assert(observation->size == sizeof(*observation));
    assert(observation->version == BM_808X_TIMING_OBSERVATION_VERSION);
    capture->last = *observation;
    ++capture->count;
}

static void
start_program(cpu_808x_test_machine_t *machine)
{
    bm_808x_arch_state_t state = cpu_808x_test_get_state(machine);

    state.cs = 0xf000U;
    state.ip = 0U;
    state.sp = 0x0100U;
    cpu_808x_test_set_state(machine, &state);
}

static void
step_once(cpu_808x_test_machine_t *machine, timing_capture_t *capture)
{
    bm_tick_t consumed = 99U;

    assert(cpu_808x_test_step(machine, &consumed) == BM_STATUS_OK);
    assert(consumed == 1U);
    assert(capture->count == 1U);
    assert(capture->last.kind == BM_808X_BOUNDARY_INSTRUCTION);
    assert(capture->last.prefetch_queue_capacity ==
           BM_808X_V30_PREFETCH_QUEUE_CAPACITY);
}

static void
assert_exact_execution_clocks(const timing_capture_t *capture,
                              uint32_t clocks)
{
    assert(capture->last.execution_clock_kind ==
           BM_808X_EXECUTION_CLOCKS_EXACT);
    assert(capture->last.execution_clocks_min == clocks);
    assert(capture->last.execution_clocks_max == clocks);
}

static void
assert_execution_clock_range(const timing_capture_t *capture,
                             uint32_t minimum, uint32_t maximum)
{
    assert(capture->last.execution_clock_kind ==
           BM_808X_EXECUTION_CLOCKS_RANGE);
    assert(capture->last.execution_clocks_min == minimum);
    assert(capture->last.execution_clocks_max == maximum);
}

static void
assert_unknown_execution_clocks(const timing_capture_t *capture)
{
    assert(capture->last.execution_clock_kind ==
           BM_808X_EXECUTION_CLOCKS_UNKNOWN);
    assert(capture->last.execution_clocks_min == 0U);
    assert(capture->last.execution_clocks_max == 0U);
}

static void
assert_exact_boundary_clocks(const timing_capture_t *capture, uint64_t clocks)
{
    assert(capture->last.boundary_clock_kind ==
           BM_808X_EXECUTION_CLOCKS_EXACT);
    assert(capture->last.boundary_clocks_min == clocks);
    assert(capture->last.boundary_clocks_max == clocks);
}

static void
assert_unknown_boundary_clocks(const timing_capture_t *capture)
{
    assert(capture->last.boundary_clock_kind ==
           BM_808X_EXECUTION_CLOCKS_UNKNOWN);
    assert(capture->last.boundary_clocks_min == 0U);
    assert(capture->last.boundary_clocks_max == 0U);
}

static void
assert_boundary_clock_range(const timing_capture_t *capture,
                            uint64_t minimum, uint64_t maximum)
{
    assert(capture->last.boundary_clock_kind ==
           BM_808X_EXECUTION_CLOCKS_RANGE);
    assert(capture->last.boundary_clocks_min == minimum);
    assert(capture->last.boundary_clocks_max == maximum);
}

static void
test_fixed_execution_clocks_and_prefix_cost(void)
{
    static const uint8_t nop[] = { 0x90U };
    static const uint8_t prefixed_nop[] = { 0x2eU, 0x90U };
    const struct {
        const uint8_t *program;
        size_t size;
        uint8_t opcode;
        uint8_t prefix_count;
        uint32_t clocks;
        uint64_t bus_transactions;
        uint64_t bus_clocks;
        uint16_t prefetch_pointer;
        uint8_t queue_count;
        bm_808x_prefetch_phase_t prefetch_phase;
    } cases[] = {
        { nop, sizeof(nop), 0x90U, 0U, 3U, 2U, 8U, 4U, 3U,
          BM_808X_PREFETCH_IDLE },
        { prefixed_nop, sizeof(prefixed_nop), 0x2eU, 1U, 5U, 3U, 11U, 4U,
          2U, BM_808X_PREFETCH_T4 }
    };
    size_t index;

    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        timing_capture_t capture = { 0 };
        cpu_808x_test_config_t config = {
            .timing = capture_timing,
            .timing_context = &capture
        };
        cpu_808x_test_machine_t machine;

        cpu_808x_test_machine_create(&machine, &config,
                                     cases[index].program,
                                     cases[index].size);
        start_program(&machine);
        step_once(&machine, &capture);
        assert(capture.last.opcode == cases[index].opcode);
        assert(capture.last.effective_opcode == 0x90U);
        assert(capture.last.prefix_count == cases[index].prefix_count);
        assert_exact_execution_clocks(&capture, cases[index].clocks);
        assert_exact_boundary_clocks(
            &capture, index == 0U ? UINT64_C(8) : UINT64_C(11));
        assert(capture.last.logical_bus_transactions ==
               cases[index].bus_transactions);
        assert(capture.last.reported_wait_states == 0U);
        assert(capture.last.bus_active_clocks == cases[index].bus_clocks);
        assert(capture.last.demand_prefetch_transactions == 1U);
        assert(capture.last.demand_prefetch_bus_clocks == 4U);
        assert(capture.last.instruction_queue_reads ==
               cases[index].prefix_count + 1U);
        assert(capture.last.prefetch_queue_flushed == 0U);
        assert(capture.last.prefetch_pointer_known == 1U);
        assert(capture.last.prefetch_pointer ==
               cases[index].prefetch_pointer);
        assert(capture.last.prefetch_queue_count == cases[index].queue_count);
        assert(capture.last.prefetch_phase == cases[index].prefetch_phase);
        assert(capture.last.prefetch_transactions == index + 2U);
        assert(capture.last.prefetch_phase_clocks == cases[index].bus_clocks);
        assert(capture.last.operand_transactions == 0U);
        assert(capture.last.operand_bus_clocks == 0U);
        assert(capture.last.prefetch_handoff_clocks == 0U);
        assert(capture.last.execution_timeline_complete == 0U);
        assert(capture.last.execution_clocks_placed == 0U);
        assert(capture.last.operand_wait_states == 0U);
        cpu_808x_test_machine_destroy(&machine);
    }
}

static void
test_operand_request_completes_inflight_prefetch(void)
{
    static const uint8_t program[] = {
        0x90U,       /* NOP leaves three bytes queued and the BCU idle. */
        0x50U,       /* PUSH AW requests the operand bus. */
        0x90U, 0x90U
    };
    timing_capture_t capture = { 0 };
    cpu_808x_test_config_t config = {
        .timing = capture_timing,
        .timing_context = &capture
    };
    cpu_808x_test_machine_t machine;

    cpu_808x_test_machine_create(&machine, &config, program, sizeof(program));
    start_program(&machine);
    step_once(&machine, &capture);
    assert(capture.last.prefetch_phase == BM_808X_PREFETCH_IDLE);

    memset(&capture, 0, sizeof(capture));
    step_once(&machine, &capture);
    assert_exact_execution_clocks(&capture, 8U);
    assert_unknown_boundary_clocks(&capture);
    assert(capture.last.logical_bus_transactions == 2U);
    assert(capture.last.prefetch_transactions == 1U);
    assert(capture.last.prefetch_phase_clocks == 4U);
    assert(capture.last.operand_transactions == 1U);
    assert(capture.last.operand_bus_clocks == 4U);
    assert(capture.last.prefetch_handoff_clocks == 3U);
    assert(capture.last.bus_active_clocks == 8U);
    assert(capture.last.prefetch_queue_count == 4U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_taken_branch_flushes_even_when_target_is_sequential(void)
{
    static const uint8_t program[] = { 0x74U, 0x00U }; /* BZ +0. */
    timing_capture_t capture = { 0 };
    cpu_808x_test_config_t config = {
        .timing = capture_timing,
        .timing_context = &capture
    };
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;

    cpu_808x_test_machine_create(&machine, &config, program, sizeof(program));
    start_program(&machine);
    state = cpu_808x_test_get_state(&machine);
    state.flags |= TEST_FLAG_ZF;
    cpu_808x_test_set_state(&machine, &state);
    step_once(&machine, &capture);
    assert_exact_execution_clocks(&capture, 14U);
    assert_exact_boundary_clocks(&capture, 20U);
    assert(capture.last.prefetch_queue_flushed == 1U);
    assert(capture.last.prefetch_pointer_known == 1U);
    assert(capture.last.prefetch_pointer == 2U);
    assert(capture.last.prefetch_queue_count == 0U);
    assert(capture.last.prefetch_phase == BM_808X_PREFETCH_IDLE);
    cpu_808x_test_machine_destroy(&machine);

    memset(&capture, 0, sizeof(capture));
    cpu_808x_test_machine_create(&machine, &config, program, sizeof(program));
    start_program(&machine);
    step_once(&machine, &capture);
    assert_exact_execution_clocks(&capture, 4U);
    assert_exact_boundary_clocks(&capture, 10U);
    assert(capture.last.prefetch_queue_flushed == 0U);
    assert(capture.last.prefetch_pointer_known == 1U);
    assert(capture.last.prefetch_pointer == 4U);
    assert(capture.last.prefetch_queue_count == 2U);
    assert(capture.last.prefetch_phase == BM_808X_PREFETCH_T3);
    cpu_808x_test_machine_destroy(&machine);
}

static bm_status_t
wait_io_access(void *context, bm_bus_transaction_t *transaction)
{
    (void) context;
    assert(transaction->space == BM_ADDRESS_IO);
    assert(transaction->operation == BM_BUS_READ);
    assert(transaction->address == 0x42U);
    assert(transaction->size == 1U);
    transaction->value = 0xa5U;
    transaction->wait_states += 3U;
    return BM_STATUS_OK;
}

static bm_status_t
timeline_io_access(void *context, bm_bus_transaction_t *transaction)
{
    (void) context;
    assert(transaction->address >= 0x40U);
    assert(transaction->address <= 0x44U);
    assert((transaction->size == 1U) || (transaction->size == 2U));
    if (transaction->operation == BM_BUS_READ)
        transaction->value = transaction->size == 1U ? 0x5aU : 0xa55aU;
    else
        assert(transaction->operation == BM_BUS_WRITE);
    return BM_STATUS_OK;
}

static void
test_bus_waits_extend_boundary_but_not_execution_clocks(void)
{
    static const uint8_t program[] = { 0xe4U, 0x42U }; /* IN AL,42h. */
    timing_capture_t capture = { 0 };
    cpu_808x_test_config_t config = {
        .bus_capacity = 2U,
        .timing = capture_timing,
        .timing_context = &capture
    };
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;

    cpu_808x_test_machine_create(&machine, &config, program, sizeof(program));
    start_program(&machine);
    assert(bm_bus_map(machine.bus, BM_ADDRESS_IO, 0x42U, 0x42U,
                      wait_io_access, NULL) == BM_STATUS_OK);
    step_once(&machine, &capture);
    state = cpu_808x_test_get_state(&machine);
    assert((state.ax & 0x00ffU) == 0x00a5U);
    assert_exact_execution_clocks(&capture, 9U);
    assert_exact_boundary_clocks(&capture, 21U);
    assert(capture.last.logical_bus_transactions == 4U);
    assert(capture.last.reported_wait_states == 3U);
    assert(capture.last.bus_active_clocks == 21U);
    assert(capture.last.demand_prefetch_transactions == 1U);
    assert(capture.last.demand_prefetch_bus_clocks == 4U);
    assert(capture.last.instruction_queue_reads == 2U);
    assert(capture.last.prefetch_transactions == 3U);
    assert(capture.last.prefetch_phase_clocks == 14U);
    assert(capture.last.prefetch_phase == BM_808X_PREFETCH_T3);
    assert(capture.last.operand_transactions == 1U);
    assert(capture.last.operand_bus_clocks == 7U);
    assert(capture.last.prefetch_handoff_clocks == 3U);
    assert(capture.last.execution_timeline_complete == 1U);
    assert(capture.last.execution_clocks_placed == 9U);
    assert(capture.last.operand_wait_states == 3U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_direct_io_has_a_complete_execution_timeline(void)
{
    const struct {
        uint8_t program[2];
        size_t size;
        uint16_t dx;
        uint32_t execution_clocks;
        uint64_t boundary_clocks;
        uint64_t operand_transactions;
        uint64_t handoff_clocks;
    } cases[] = {
        { { 0xe4U, 0x42U }, 2U, 0U, 9U, 18U, 1U, 3U },
        { { 0xe5U, 0x42U }, 2U, 0U, 9U, 18U, 1U, 3U },
        { { 0xe5U, 0x43U }, 2U, 0U, 13U, 22U, 2U, 3U },
        { { 0xe6U, 0x42U }, 2U, 0U, 8U, 14U, 1U, 0U },
        { { 0xe7U, 0x42U }, 2U, 0U, 8U, 14U, 1U, 0U },
        { { 0xe7U, 0x43U }, 2U, 0U, 12U, 18U, 2U, 0U },
        { { 0xecU, 0U }, 1U, 0x42U, 8U, 14U, 1U, 1U },
        { { 0xedU, 0U }, 1U, 0x42U, 8U, 14U, 1U, 1U },
        { { 0xedU, 0U }, 1U, 0x43U, 12U, 18U, 2U, 1U },
        { { 0xeeU, 0U }, 1U, 0x42U, 8U, 15U, 1U, 2U },
        { { 0xefU, 0U }, 1U, 0x42U, 8U, 15U, 1U, 2U },
        { { 0xefU, 0U }, 1U, 0x43U, 12U, 19U, 2U, 2U }
    };
    size_t index;

    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        timing_capture_t capture = { 0 };
        cpu_808x_test_config_t config = {
            .bus_capacity = 2U,
            .timing = capture_timing,
            .timing_context = &capture
        };
        cpu_808x_test_machine_t machine;
        bm_808x_arch_state_t state;

        cpu_808x_test_machine_create(&machine, &config,
                                     cases[index].program,
                                     cases[index].size);
        start_program(&machine);
        state = cpu_808x_test_get_state(&machine);
        state.dx = cases[index].dx;
        state.ax = 0xa55aU;
        cpu_808x_test_set_state(&machine, &state);
        assert(bm_bus_map(machine.bus, BM_ADDRESS_IO, 0x40U, 0x44U,
                          timeline_io_access, NULL) == BM_STATUS_OK);
        step_once(&machine, &capture);
        assert_exact_execution_clocks(&capture,
                                      cases[index].execution_clocks);
        assert_exact_boundary_clocks(&capture, cases[index].boundary_clocks);
        assert(capture.last.operand_transactions ==
               cases[index].operand_transactions);
        assert(capture.last.prefetch_handoff_clocks ==
               cases[index].handoff_clocks);
        assert(capture.last.execution_timeline_complete == 1U);
        assert(capture.last.execution_clocks_placed ==
               cases[index].execution_clocks);
        assert(capture.last.operand_wait_states == 0U);
        cpu_808x_test_machine_destroy(&machine);
    }

    {
        static const uint8_t prefixed_in[] = { 0x2eU, 0xe4U, 0x42U };
        timing_capture_t capture = { 0 };
        cpu_808x_test_config_t config = {
            .bus_capacity = 2U,
            .timing = capture_timing,
            .timing_context = &capture
        };
        cpu_808x_test_machine_t machine;

        cpu_808x_test_machine_create(&machine, &config, prefixed_in,
                                     sizeof(prefixed_in));
        start_program(&machine);
        assert(bm_bus_map(machine.bus, BM_ADDRESS_IO, 0x40U, 0x44U,
                          timeline_io_access, NULL) == BM_STATUS_OK);
        step_once(&machine, &capture);
        assert_exact_execution_clocks(&capture, 11U);
        assert_unknown_boundary_clocks(&capture);
        assert(capture.last.execution_timeline_complete == 0U);
        assert(capture.last.execution_clocks_placed == 0U);
        cpu_808x_test_machine_destroy(&machine);
    }
}

static void
test_memory_timing_uses_operand_form_and_alignment(void)
{
    static const uint8_t program[] = { 0x88U, 0x07U }; /* MOV [BW],AL. */
    timing_capture_t capture = { 0 };
    cpu_808x_test_config_t config = {
        .timing = capture_timing,
        .timing_context = &capture
    };
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;

    cpu_808x_test_machine_create(&machine, &config, program, sizeof(program));
    start_program(&machine);
    state = cpu_808x_test_get_state(&machine);
    state.ds = 0x1000U;
    state.bx = 0x0040U;
    state.ax = 0x005aU;
    cpu_808x_test_set_state(&machine, &state);
    step_once(&machine, &capture);
    assert_exact_execution_clocks(&capture, 9U);
    assert_unknown_boundary_clocks(&capture);
    assert(capture.last.logical_bus_transactions == 3U);
    assert(capture.last.operand_transactions == 1U);
    assert(capture.last.operand_bus_clocks == 4U);
    assert(capture.last.prefetch_handoff_clocks == 2U);
    assert(cpu_808x_test_peek(&machine, 0x10040U) == 0x5aU);
    cpu_808x_test_machine_destroy(&machine);

    {
        static const uint8_t add_word[] = { 0x01U, 0x07U }; /* ADD [BW],AW. */
        unsigned int odd;

        for (odd = 0U; odd <= 1U; ++odd) {
            bus_capture_t bus_capture = { 0 };

            memset(&capture, 0, sizeof(capture));
            cpu_808x_test_machine_create(&machine, &config, add_word,
                                         sizeof(add_word));
            bm_bus_set_observer(machine.bus, capture_bus_transaction,
                                &bus_capture);
            start_program(&machine);
            state = cpu_808x_test_get_state(&machine);
            state.ds = 0x1000U;
            state.bx = (uint16_t) (0x0040U + odd);
            state.ax = 1U;
            cpu_808x_test_set_state(&machine, &state);
            cpu_808x_test_poke(&machine, 0x10040U + odd, 2U);
            cpu_808x_test_poke(&machine, 0x10041U + odd, 0U);
            bus_capture.count = 0U;
            step_once(&machine, &capture);
            assert_exact_execution_clocks(&capture, odd ? 24U : 16U);
            assert_unknown_boundary_clocks(&capture);
            assert(bus_capture.count == (odd ? 6U : 4U));
            assert(capture.last.bus_active_clocks == (odd ? 24U : 16U));
            assert(capture.last.demand_prefetch_transactions == 1U);
            assert(capture.last.demand_prefetch_bus_clocks == 4U);
            assert(capture.last.instruction_queue_reads == 2U);
            assert(capture.last.operand_transactions == (odd ? 4U : 2U));
            assert(capture.last.operand_bus_clocks == (odd ? 16U : 8U));
            assert(capture.last.prefetch_handoff_clocks == 2U);
            assert(bus_capture.transactions[0].operation == BM_BUS_FETCH);
            assert(bus_capture.transactions[0].size == 2U);
            assert(bus_capture.transactions[0].alignment == 2U);
            assert(bus_capture.transactions[1].operation == BM_BUS_FETCH);
            assert(bus_capture.transactions[1].size == 2U);
            assert(bus_capture.transactions[1].alignment == 2U);
            if (odd) {
                size_t index;

                for (index = 2U; index < bus_capture.count; ++index) {
                    assert(bus_capture.transactions[index].size == 1U);
                    assert(bus_capture.transactions[index].alignment == 1U);
                    assert(bus_capture.transactions[index].address ==
                           0x10041U + ((index - 2U) & 1U));
                }
            } else {
                assert(bus_capture.transactions[2].operation == BM_BUS_READ);
                assert(bus_capture.transactions[3].operation == BM_BUS_WRITE);
                assert(bus_capture.transactions[2].address == 0x10040U);
                assert(bus_capture.transactions[3].address == 0x10040U);
                assert(bus_capture.transactions[2].size == 2U);
                assert(bus_capture.transactions[3].size == 2U);
                assert(bus_capture.transactions[2].alignment == 2U);
                assert(bus_capture.transactions[3].alignment == 2U);
            }
            cpu_808x_test_machine_destroy(&machine);
        }
    }
}

static void
test_counted_and_stack_timing(void)
{
    static const uint8_t shift[] = { 0xc1U, 0xe0U, 0x05U }; /* SHL AW,5. */
    static const uint8_t shift_cl[] = { 0xd2U, 0xe1U }; /* SHL CL,CL. */
    static const uint8_t push[] = { 0x50U }; /* PUSH AW. */
    static const uint8_t ret_adjust[] = { 0xc2U, 0x01U, 0x00U };
    timing_capture_t capture = { 0 };
    cpu_808x_test_config_t config = {
        .timing = capture_timing,
        .timing_context = &capture
    };
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;

    cpu_808x_test_machine_create(&machine, &config, shift, sizeof(shift));
    start_program(&machine);
    step_once(&machine, &capture);
    assert_exact_execution_clocks(&capture, 12U);
    cpu_808x_test_machine_destroy(&machine);

    memset(&capture, 0, sizeof(capture));
    cpu_808x_test_machine_create(&machine, &config, shift_cl,
                                 sizeof(shift_cl));
    start_program(&machine);
    state = cpu_808x_test_get_state(&machine);
    state.cx = 2U;
    cpu_808x_test_set_state(&machine, &state);
    step_once(&machine, &capture);
    state = cpu_808x_test_get_state(&machine);
    assert((state.cx & 0x00ffU) == 8U);
    assert_exact_execution_clocks(&capture, 9U);
    cpu_808x_test_machine_destroy(&machine);

    memset(&capture, 0, sizeof(capture));
    cpu_808x_test_machine_create(&machine, &config, push, sizeof(push));
    start_program(&machine);
    state = cpu_808x_test_get_state(&machine);
    state.ss = 0x1000U;
    state.sp = 0x0101U;
    cpu_808x_test_set_state(&machine, &state);
    step_once(&machine, &capture);
    assert_exact_execution_clocks(&capture, 12U);
    cpu_808x_test_machine_destroy(&machine);

    /* The cleanup makes the final SP odd; timing follows the initial stack
     * transfer address, which was even. */
    memset(&capture, 0, sizeof(capture));
    cpu_808x_test_machine_create(&machine, &config, ret_adjust,
                                 sizeof(ret_adjust));
    start_program(&machine);
    state = cpu_808x_test_get_state(&machine);
    state.ss = 0x1000U;
    state.sp = 0x0100U;
    cpu_808x_test_set_state(&machine, &state);
    cpu_808x_test_poke(&machine, 0x10100U, 0x03U);
    cpu_808x_test_poke(&machine, 0x10101U, 0x00U);
    step_once(&machine, &capture);
    assert_exact_execution_clocks(&capture, 20U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_data_dependent_arithmetic_reports_documented_ranges(void)
{
    static const struct {
        uint8_t program[5];
        size_t size;
        uint16_t bx;
        uint32_t minimum;
        uint32_t maximum;
    } cases[] = {
        { { 0xf6U, 0xe0U }, 2U, 0U, 21U, 22U }, /* MULU AL. */
        { { 0xf6U, 0x2fU }, 2U, 0x0040U, 39U, 45U }, /* MUL [BW]. */
        { { 0xf7U, 0xf8U }, 2U, 0U, 38U, 43U }, /* DIV AW. */
        { { 0xf7U, 0x6fU }, 2U, 0x0041U, 51U, 57U }, /* MUL [BW]. */
        { { 0x6bU, 0xc0U, 0x02U }, 3U, 0U, 28U, 34U },
        { { 0x2eU, 0x69U, 0xc0U, 0x02U, 0x00U },
          5U, 0U, 38U, 44U }
    };
    size_t index;

    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        timing_capture_t capture = { 0 };
        cpu_808x_test_config_t config = {
            .timing = capture_timing,
            .timing_context = &capture
        };
        cpu_808x_test_machine_t machine;
        bm_808x_arch_state_t state;

        cpu_808x_test_machine_create(&machine, &config, cases[index].program,
                                     cases[index].size);
        start_program(&machine);
        state = cpu_808x_test_get_state(&machine);
        state.ds = 0x1000U;
        state.bx = cases[index].bx;
        state.ax = 2U;
        state.dx = 0U;
        cpu_808x_test_set_state(&machine, &state);
        if (cases[index].bx != 0U) {
            cpu_808x_test_poke(&machine, 0x10000U + cases[index].bx, 2U);
            cpu_808x_test_poke(&machine, 0x10001U + cases[index].bx, 0U);
        }
        step_once(&machine, &capture);
        assert_execution_clock_range(&capture, cases[index].minimum,
                                     cases[index].maximum);
        if (index == 0U)
            assert_boundary_clock_range(&capture, 27U, 28U);
        else if (index == 2U)
            assert_boundary_clock_range(&capture, 44U, 49U);
        else if (index == 4U)
            assert_boundary_clock_range(&capture, 37U, 43U);
        else if (index == 5U)
            assert_boundary_clock_range(&capture, 51U, 57U);
        else
            assert_unknown_boundary_clocks(&capture);
        cpu_808x_test_machine_destroy(&machine);
    }
}

static void
test_divide_error_does_not_claim_normal_execution_clocks(void)
{
    static const uint8_t program[] = { 0xf6U, 0xf0U }; /* DIVU AL, AL=0. */
    timing_capture_t capture = { 0 };
    cpu_808x_test_config_t config = {
        .timing = capture_timing,
        .timing_context = &capture
    };
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;

    cpu_808x_test_machine_create(&machine, &config, program, sizeof(program));
    start_program(&machine);
    state = cpu_808x_test_get_state(&machine);
    state.ss = 0x1000U;
    state.sp = 0x0100U;
    state.ax = 0U;
    cpu_808x_test_set_state(&machine, &state);
    cpu_808x_test_poke(&machine, 0U, 0x34U);
    cpu_808x_test_poke(&machine, 1U, 0x12U);
    cpu_808x_test_poke(&machine, 2U, 0x00U);
    cpu_808x_test_poke(&machine, 3U, 0x20U);
    step_once(&machine, &capture);
    assert_unknown_execution_clocks(&capture);
    assert(capture.last.prefetch_queue_flushed == 1U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_scalar_formula_and_condition_timing(void)
{
    static const uint8_t cwd[] = { 0x99U };
    static const uint8_t prepare_zero[] = { 0xc8U, 0x00U, 0x00U, 0x00U };
    static const uint8_t prepare_three[] = { 0xc8U, 0x00U, 0x00U, 0x03U };
    static const uint8_t chkind[] = { 0x62U, 0x06U, 0x40U, 0x00U };
    timing_capture_t capture = { 0 };
    cpu_808x_test_config_t config = {
        .timing = capture_timing,
        .timing_context = &capture
    };
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;
    unsigned int odd;

    cpu_808x_test_machine_create(&machine, &config, cwd, sizeof(cwd));
    start_program(&machine);
    step_once(&machine, &capture);
    assert_execution_clock_range(&capture, 4U, 5U);
    cpu_808x_test_machine_destroy(&machine);

    for (odd = 0U; odd <= 1U; ++odd) {
        memset(&capture, 0, sizeof(capture));
        cpu_808x_test_machine_create(&machine, &config, prepare_zero,
                                     sizeof(prepare_zero));
        start_program(&machine);
        state = cpu_808x_test_get_state(&machine);
        state.ss = 0x1000U;
        state.sp = (uint16_t) (0x0100U + odd);
        state.bp = 0x0200U;
        cpu_808x_test_set_state(&machine, &state);
        step_once(&machine, &capture);
        assert_exact_execution_clocks(&capture, odd ? 16U : 12U);
        cpu_808x_test_machine_destroy(&machine);

        memset(&capture, 0, sizeof(capture));
        cpu_808x_test_machine_create(&machine, &config, prepare_three,
                                     sizeof(prepare_three));
        start_program(&machine);
        state = cpu_808x_test_get_state(&machine);
        state.ss = 0x1000U;
        state.sp = (uint16_t) (0x0100U + odd);
        state.bp = 0x0200U;
        cpu_808x_test_set_state(&machine, &state);
        step_once(&machine, &capture);
        assert_exact_execution_clocks(&capture, odd ? 53U : 33U);
        cpu_808x_test_machine_destroy(&machine);
    }

    memset(&capture, 0, sizeof(capture));
    cpu_808x_test_machine_create(&machine, &config, chkind, sizeof(chkind));
    start_program(&machine);
    state = cpu_808x_test_get_state(&machine);
    state.ds = 0x1000U;
    state.ax = 5U;
    cpu_808x_test_set_state(&machine, &state);
    cpu_808x_test_poke(&machine, 0x10040U, 1U);
    cpu_808x_test_poke(&machine, 0x10041U, 0U);
    cpu_808x_test_poke(&machine, 0x10042U, 10U);
    cpu_808x_test_poke(&machine, 0x10043U, 0U);
    step_once(&machine, &capture);
    assert_exact_execution_clocks(&capture, 18U);
    cpu_808x_test_machine_destroy(&machine);

    memset(&capture, 0, sizeof(capture));
    cpu_808x_test_machine_create(&machine, &config, chkind, sizeof(chkind));
    start_program(&machine);
    state = cpu_808x_test_get_state(&machine);
    state.ds = 0x1000U;
    state.ss = 0x2000U;
    state.sp = 0x0100U;
    state.ax = 20U;
    cpu_808x_test_set_state(&machine, &state);
    cpu_808x_test_poke(&machine, 0x10040U, 1U);
    cpu_808x_test_poke(&machine, 0x10041U, 0U);
    cpu_808x_test_poke(&machine, 0x10042U, 10U);
    cpu_808x_test_poke(&machine, 0x10043U, 0U);
    cpu_808x_test_poke(&machine, 0x0014U, 0x34U);
    cpu_808x_test_poke(&machine, 0x0015U, 0x12U);
    cpu_808x_test_poke(&machine, 0x0016U, 0x00U);
    cpu_808x_test_poke(&machine, 0x0017U, 0x30U);
    step_once(&machine, &capture);
    assert_execution_clock_range(&capture, 53U, 56U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_nec_extension_timing(void)
{
    static const uint8_t test1_register[] = { 0x0fU, 0x10U, 0xc0U };
    static const uint8_t clr1_word_odd[] = {
        0x0fU, 0x13U, 0x06U, 0x41U, 0x00U
    };
    static const uint8_t set1_immediate_memory[] = {
        0x0fU, 0x1cU, 0x06U, 0x40U, 0x00U, 0x03U
    };
    static const uint8_t bcd_add[] = { 0x0fU, 0x20U };
    static const uint8_t rol4_register[] = { 0x0fU, 0x28U, 0xc3U };
    static const uint8_t ror4_memory[] = {
        0x0fU, 0x2aU, 0x06U, 0x40U, 0x00U
    };
    static const uint8_t ext_even[] = { 0x0fU, 0x33U, 0xd1U };
    static const uint8_t ext_odd[] = { 0x0fU, 0x33U, 0xd1U };
    static const uint8_t brkem[] = { 0x0fU, 0xffU, 0x20U };
    timing_capture_t capture = { 0 };
    cpu_808x_test_config_t config = {
        .timing = capture_timing,
        .timing_context = &capture
    };
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;

    cpu_808x_test_machine_create(&machine, &config, test1_register,
                                 sizeof(test1_register));
    start_program(&machine);
    step_once(&machine, &capture);
    assert_exact_execution_clocks(&capture, 3U);
    cpu_808x_test_machine_destroy(&machine);

    memset(&capture, 0, sizeof(capture));
    cpu_808x_test_machine_create(&machine, &config, clr1_word_odd,
                                 sizeof(clr1_word_odd));
    start_program(&machine);
    state = cpu_808x_test_get_state(&machine);
    state.ds = 0x1000U;
    cpu_808x_test_set_state(&machine, &state);
    step_once(&machine, &capture);
    assert_exact_execution_clocks(&capture, 22U);
    cpu_808x_test_machine_destroy(&machine);

    memset(&capture, 0, sizeof(capture));
    cpu_808x_test_machine_create(&machine, &config, set1_immediate_memory,
                                 sizeof(set1_immediate_memory));
    start_program(&machine);
    state = cpu_808x_test_get_state(&machine);
    state.ds = 0x1000U;
    cpu_808x_test_set_state(&machine, &state);
    step_once(&machine, &capture);
    assert_exact_execution_clocks(&capture, 14U);
    cpu_808x_test_machine_destroy(&machine);

    memset(&capture, 0, sizeof(capture));
    cpu_808x_test_machine_create(&machine, &config, bcd_add,
                                 sizeof(bcd_add));
    start_program(&machine);
    state = cpu_808x_test_get_state(&machine);
    state.cx = 3U;
    state.ds = 0x1000U;
    state.es = 0x2000U;
    state.si = state.di = 0x0040U;
    cpu_808x_test_set_state(&machine, &state);
    step_once(&machine, &capture);
    assert_exact_execution_clocks(&capture, 45U);
    cpu_808x_test_machine_destroy(&machine);

    memset(&capture, 0, sizeof(capture));
    cpu_808x_test_machine_create(&machine, &config, rol4_register,
                                 sizeof(rol4_register));
    start_program(&machine);
    step_once(&machine, &capture);
    assert_exact_execution_clocks(&capture, 13U);
    cpu_808x_test_machine_destroy(&machine);

    memset(&capture, 0, sizeof(capture));
    cpu_808x_test_machine_create(&machine, &config, ror4_memory,
                                 sizeof(ror4_memory));
    start_program(&machine);
    state = cpu_808x_test_get_state(&machine);
    state.ds = 0x1000U;
    cpu_808x_test_set_state(&machine, &state);
    step_once(&machine, &capture);
    assert_exact_execution_clocks(&capture, 32U);
    cpu_808x_test_machine_destroy(&machine);

    memset(&capture, 0, sizeof(capture));
    cpu_808x_test_machine_create(&machine, &config, ext_even,
                                 sizeof(ext_even));
    start_program(&machine);
    state = cpu_808x_test_get_state(&machine);
    state.cx = 6U;
    state.dx = 3U;
    state.ds = 0x1000U;
    state.si = 0x0040U;
    cpu_808x_test_set_state(&machine, &state);
    step_once(&machine, &capture);
    assert_execution_clock_range(&capture, 31U, 117U);
    cpu_808x_test_machine_destroy(&machine);

    memset(&capture, 0, sizeof(capture));
    cpu_808x_test_machine_create(&machine, &config, ext_odd,
                                 sizeof(ext_odd));
    start_program(&machine);
    state = cpu_808x_test_get_state(&machine);
    state.cx = 6U;
    state.dx = 3U;
    state.ds = 0x1000U;
    state.si = 0x0041U;
    cpu_808x_test_set_state(&machine, &state);
    step_once(&machine, &capture);
    assert_execution_clock_range(&capture, 35U, 133U);
    cpu_808x_test_machine_destroy(&machine);

    memset(&capture, 0, sizeof(capture));
    cpu_808x_test_machine_create(&machine, &config, brkem, sizeof(brkem));
    start_program(&machine);
    state = cpu_808x_test_get_state(&machine);
    state.ss = 0x2000U;
    state.sp = 0x0100U;
    cpu_808x_test_set_state(&machine, &state);
    cpu_808x_test_poke(&machine, 0x0080U, 0x34U);
    cpu_808x_test_poke(&machine, 0x0081U, 0x12U);
    cpu_808x_test_poke(&machine, 0x0082U, 0x00U);
    cpu_808x_test_poke(&machine, 0x0083U, 0x30U);
    step_once(&machine, &capture);
    assert_exact_execution_clocks(&capture, 38U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_standard_string_formula_timing(void)
{
    static const uint8_t movsb[] = { 0xa4U };
    static const uint8_t rep_movsb[] = { 0xf3U, 0xa4U };
    static const uint8_t rep_zero_movsb[] = { 0xf3U, 0xa4U };
    static const uint8_t rep_movsw[] = { 0xf3U, 0xa5U };
    static const uint8_t repe_cmpsb[] = { 0xf3U, 0xa6U };
    static const uint8_t stosw[] = { 0xabU };
    static const uint8_t rep_lodsw[] = { 0xf3U, 0xadU };
    static const uint8_t overridden_rep_movsb[] = { 0x2eU, 0xf3U, 0xa4U };
    const struct {
        const uint8_t *program;
        size_t size;
        uint16_t cx;
        uint16_t si;
        uint16_t di;
        uint32_t clocks;
    } cases[] = {
        { movsb, sizeof(movsb), 7U, 0x0040U, 0x0060U, 11U },
        { rep_movsb, sizeof(rep_movsb), 3U, 0x0040U, 0x0060U, 35U },
        { rep_zero_movsb, sizeof(rep_zero_movsb), 0U,
          0x0040U, 0x0060U, 11U },
        { rep_movsw, sizeof(rep_movsw), 2U, 0x0041U, 0x0060U, 35U },
        { stosw, sizeof(stosw), 9U, 0x0040U, 0x0061U, 11U },
        { rep_lodsw, sizeof(rep_lodsw), 2U, 0x0041U, 0x0060U, 33U },
        { overridden_rep_movsb, sizeof(overridden_rep_movsb), 2U,
          0x0040U, 0x0060U, 29U }
    };
    size_t index;

    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        timing_capture_t capture = { 0 };
        cpu_808x_test_config_t config = {
            .timing = capture_timing,
            .timing_context = &capture
        };
        cpu_808x_test_machine_t machine;
        bm_808x_arch_state_t state;

        cpu_808x_test_machine_create(&machine, &config,
                                     cases[index].program,
                                     cases[index].size);
        start_program(&machine);
        state = cpu_808x_test_get_state(&machine);
        state.ds = 0x1000U;
        state.es = 0x2000U;
        state.cx = cases[index].cx;
        state.si = cases[index].si;
        state.di = cases[index].di;
        cpu_808x_test_set_state(&machine, &state);
        step_once(&machine, &capture);
        assert_exact_execution_clocks(&capture, cases[index].clocks);
        cpu_808x_test_machine_destroy(&machine);
    }

    {
        timing_capture_t capture = { 0 };
        cpu_808x_test_config_t config = {
            .timing = capture_timing,
            .timing_context = &capture
        };
        cpu_808x_test_machine_t machine;
        bm_808x_arch_state_t state;

        cpu_808x_test_machine_create(&machine, &config, repe_cmpsb,
                                     sizeof(repe_cmpsb));
        start_program(&machine);
        state = cpu_808x_test_get_state(&machine);
        state.ds = 0x1000U;
        state.es = 0x2000U;
        state.cx = 3U;
        state.si = 0x0040U;
        state.di = 0x0060U;
        cpu_808x_test_set_state(&machine, &state);
        cpu_808x_test_poke(&machine, 0x10040U, 0x12U);
        cpu_808x_test_poke(&machine, 0x20060U, 0x34U);
        step_once(&machine, &capture);
        assert_exact_execution_clocks(&capture, 21U);
        state = cpu_808x_test_get_state(&machine);
        assert(state.cx == 2U);
        cpu_808x_test_machine_destroy(&machine);
    }
}

static void
test_io_string_formula_timing(void)
{
    static const uint8_t insb[] = { 0x6cU };
    static const uint8_t rep_insw[] = { 0xf3U, 0x6dU };
    static const uint8_t rep_outsw[] = { 0xf3U, 0x6fU };
    const struct {
        const uint8_t *program;
        size_t size;
        uint16_t cx;
        uint16_t dx;
        uint16_t index;
        uint32_t clocks;
    } cases[] = {
        { insb, sizeof(insb), 5U, 0x0080U, 0x0040U, 10U },
        { rep_insw, sizeof(rep_insw), 2U, 0x0081U, 0x0041U, 41U },
        { rep_outsw, sizeof(rep_outsw), 2U, 0x0081U, 0x0040U, 33U }
    };
    size_t case_index;

    for (case_index = 0U;
         case_index < sizeof(cases) / sizeof(cases[0]); ++case_index) {
        timing_capture_t capture = { 0 };
        cpu_808x_test_config_t config = {
            .bus_capacity = 2U,
            .timing = capture_timing,
            .timing_context = &capture
        };
        cpu_808x_test_machine_t machine;
        bm_808x_arch_state_t state;
        timing_io_fixture_t io = { .next_value = 0x40U };

        cpu_808x_test_machine_create(&machine, &config,
                                     cases[case_index].program,
                                     cases[case_index].size);
        io.machine = &machine;
        assert(bm_bus_map(machine.bus, BM_ADDRESS_IO, 0U, 0xffffU,
                          timing_io_access, &io) == BM_STATUS_OK);
        start_program(&machine);
        state = cpu_808x_test_get_state(&machine);
        state.ds = 0x1000U;
        state.es = 0x2000U;
        state.cx = cases[case_index].cx;
        state.dx = cases[case_index].dx;
        state.si = cases[case_index].index;
        state.di = cases[case_index].index;
        cpu_808x_test_set_state(&machine, &state);
        step_once(&machine, &capture);
        assert_exact_execution_clocks(&capture, cases[case_index].clocks);
        cpu_808x_test_machine_destroy(&machine);
    }
}

static bm_status_t
acknowledge_interrupt(void *context, uint8_t *vector)
{
    (void) context;
    *vector = 0x20U;
    return BM_STATUS_OK;
}

static void
test_interrupted_string_timing_remains_unknown(void)
{
    static const uint8_t rep_outsb[] = { 0xf3U, 0x6eU };
    timing_capture_t capture = { 0 };
    timing_io_fixture_t io = {
        .signal_after = 1U,
        .next_value = 0x40U
    };
    cpu_808x_test_config_t config = {
        .bus_capacity = 2U,
        .interrupt_ack = acknowledge_interrupt,
        .timing = capture_timing,
        .timing_context = &capture
    };
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;

    cpu_808x_test_machine_create(&machine, &config, rep_outsb,
                                 sizeof(rep_outsb));
    io.machine = &machine;
    assert(bm_bus_map(machine.bus, BM_ADDRESS_IO, 0U, 0xffffU,
                      timing_io_access, &io) == BM_STATUS_OK);
    start_program(&machine);
    state = cpu_808x_test_get_state(&machine);
    state.ds = 0x1000U;
    state.ss = 0U;
    state.sp = 0x0400U;
    state.flags = (uint16_t) (state.flags | TEST_FLAG_IF);
    state.cx = 3U;
    state.dx = 0x0080U;
    state.si = 0x0040U;
    cpu_808x_test_set_state(&machine, &state);
    step_once(&machine, &capture);
    assert_unknown_execution_clocks(&capture);
    state = cpu_808x_test_get_state(&machine);
    assert(state.cx == 2U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_interrupt_boundary_reports_queue_flush(void)
{
    static const uint8_t program[] = { 0x90U };
    timing_capture_t capture = { 0 };
    cpu_808x_test_config_t config = {
        .timing = capture_timing,
        .timing_context = &capture,
        .interrupt_ack = acknowledge_interrupt
    };
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;
    bm_tick_t consumed = 99U;

    cpu_808x_test_machine_create(&machine, &config, program, sizeof(program));
    start_program(&machine);
    cpu_808x_test_poke(&machine, 0x0080U, 0x34U);
    cpu_808x_test_poke(&machine, 0x0081U, 0x12U);
    cpu_808x_test_poke(&machine, 0x0082U, 0x00U);
    cpu_808x_test_poke(&machine, 0x0083U, 0x20U);
    state = cpu_808x_test_get_state(&machine);
    state.ss = 0x1000U;
    state.sp = 0x0100U;
    state.flags |= TEST_FLAG_IF;
    cpu_808x_test_set_state(&machine, &state);
    assert(bm_engine_signal_cpu(machine.engine, 0U, BM_808X_SIGNAL_INT, 1) ==
           BM_STATUS_OK);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    assert(consumed == 1U && capture.count == 1U);
    assert(capture.last.kind == BM_808X_BOUNDARY_INTERRUPT);
    assert_unknown_execution_clocks(&capture);
    assert(capture.last.prefetch_queue_flushed == 1U);
    assert(capture.last.prefetch_pointer_known == 1U);
    assert(capture.last.prefetch_pointer == 0x1234U);
    assert(capture.last.logical_bus_transactions == 5U);
    assert(capture.last.bus_active_clocks == 20U);
    assert(capture.last.demand_prefetch_transactions == 0U);
    assert(capture.last.demand_prefetch_bus_clocks == 0U);
    assert(capture.last.instruction_queue_reads == 0U);
    assert(capture.last.prefetch_transactions == 0U);
    assert(capture.last.prefetch_phase_clocks == 0U);
    assert(capture.last.prefetch_phase == BM_808X_PREFETCH_IDLE);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_prefetched_byte_survives_later_memory_write(void)
{
    static const uint8_t program[] = { 0x90U, 0x90U, 0xf4U };
    timing_capture_t capture = { 0 };
    cpu_808x_test_config_t config = {
        .timing = capture_timing,
        .timing_context = &capture
    };
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;
    bm_tick_t consumed = 99U;

    cpu_808x_test_machine_create(&machine, &config, program, sizeof(program));
    start_program(&machine);
    step_once(&machine, &capture);
    assert(capture.last.prefetch_pointer == 4U);
    assert(capture.last.prefetch_queue_count == 3U);
    assert(capture.last.logical_bus_transactions == 2U);
    assert(capture.last.bus_active_clocks == 8U);
    assert(capture.last.prefetch_phase == BM_808X_PREFETCH_IDLE);
    assert(capture.last.prefetch_transactions == 2U);
    assert(capture.last.prefetch_phase_clocks == 8U);

    cpu_808x_test_poke(&machine, 0xf0001U, 0xf4U);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    assert(consumed == 1U && capture.count == 2U);
    state = cpu_808x_test_get_state(&machine);
    assert(state.ip == 2U);
    assert(state.halted == 0U);
    assert(capture.last.prefetch_pointer == 6U);
    assert(capture.last.prefetch_queue_count == 4U);
    assert(capture.last.logical_bus_transactions == 1U);
    assert(capture.last.bus_active_clocks == 4U);
    assert(capture.last.demand_prefetch_transactions == 0U);
    assert(capture.last.demand_prefetch_bus_clocks == 0U);
    assert(capture.last.instruction_queue_reads == 1U);
    assert(capture.last.prefetch_phase == BM_808X_PREFETCH_IDLE);
    assert(capture.last.prefetch_transactions == 1U);
    assert(capture.last.prefetch_phase_clocks == 4U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_odd_prefetch_and_pointer_wrap(void)
{
    static const uint8_t program[] = { 0x90U, 0x90U };
    timing_capture_t capture = { 0 };
    cpu_808x_test_config_t config = {
        .timing = capture_timing,
        .timing_context = &capture
    };
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;

    cpu_808x_test_machine_create(&machine, &config, program, sizeof(program));
    start_program(&machine);
    state = cpu_808x_test_get_state(&machine);
    state.ip = 1U;
    cpu_808x_test_set_state(&machine, &state);
    step_once(&machine, &capture);
    assert(capture.last.prefetch_pointer == 4U);
    assert(capture.last.prefetch_queue_count == 2U);
    assert(capture.last.logical_bus_transactions == 2U);
    assert(capture.last.bus_active_clocks == 8U);
    assert(capture.last.demand_prefetch_transactions == 1U);
    assert(capture.last.demand_prefetch_bus_clocks == 4U);
    assert(capture.last.instruction_queue_reads == 1U);
    assert(capture.last.prefetch_phase == BM_808X_PREFETCH_IDLE);
    assert(capture.last.prefetch_transactions == 2U);
    assert(capture.last.prefetch_phase_clocks == 8U);

    memset(&capture, 0, sizeof(capture));
    state = cpu_808x_test_get_state(&machine);
    state.ip = 0xffffU;
    cpu_808x_test_set_state(&machine, &state);
    cpu_808x_test_poke(&machine, 0xfffffU, 0x90U);
    step_once(&machine, &capture);
    assert(capture.last.prefetch_pointer == 2U);
    assert(capture.last.prefetch_queue_count == 2U);
    assert(capture.last.logical_bus_transactions == 2U);
    assert(capture.last.bus_active_clocks == 8U);
    assert(capture.last.demand_prefetch_transactions == 1U);
    assert(capture.last.demand_prefetch_bus_clocks == 4U);
    assert(capture.last.instruction_queue_reads == 1U);
    assert(capture.last.prefetch_phase == BM_808X_PREFETCH_IDLE);
    assert(capture.last.prefetch_transactions == 2U);
    assert(capture.last.prefetch_phase_clocks == 8U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_taken_branch_discards_sequential_prefetch(void)
{
    static const uint8_t program[] = {
        0x90U, 0xebU, 0x01U, 0xf4U, 0x90U, 0xf4U
    };
    timing_capture_t capture = { 0 };
    cpu_808x_test_config_t config = {
        .timing = capture_timing,
        .timing_context = &capture
    };
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;
    bm_tick_t consumed = 99U;

    cpu_808x_test_machine_create(&machine, &config, program, sizeof(program));
    start_program(&machine);
    state = cpu_808x_test_get_state(&machine);
    state.ip = 1U;
    cpu_808x_test_set_state(&machine, &state);
    step_once(&machine, &capture);
    assert(capture.last.prefetch_queue_flushed == 1U);
    assert(capture.last.prefetch_pointer == 4U);
    assert(capture.last.prefetch_queue_count == 0U);

    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    assert(consumed == 1U && capture.count == 2U);
    state = cpu_808x_test_get_state(&machine);
    assert(state.ip == 5U);
    assert(state.halted == 0U);
    cpu_808x_test_machine_destroy(&machine);
}

int
main(void)
{
    test_fixed_execution_clocks_and_prefix_cost();
    test_operand_request_completes_inflight_prefetch();
    test_taken_branch_flushes_even_when_target_is_sequential();
    test_bus_waits_extend_boundary_but_not_execution_clocks();
    test_direct_io_has_a_complete_execution_timeline();
    test_memory_timing_uses_operand_form_and_alignment();
    test_counted_and_stack_timing();
    test_data_dependent_arithmetic_reports_documented_ranges();
    test_divide_error_does_not_claim_normal_execution_clocks();
    test_scalar_formula_and_condition_timing();
    test_nec_extension_timing();
    test_standard_string_formula_timing();
    test_io_string_formula_timing();
    test_interrupted_string_timing_remains_unknown();
    test_interrupt_boundary_reports_queue_flush();
    test_prefetched_byte_survives_later_memory_write();
    test_odd_prefetch_and_pointer_wrap();
    test_taken_branch_discards_sequential_prefetch();
    return 0;
}
