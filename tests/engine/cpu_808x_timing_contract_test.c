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
test_accumulator_test_has_documented_timing(void)
{
    static const uint8_t programs[][4] = {
        { 0xa8U, 0x55U, 0U, 0U },
        { 0xa9U, 0x55U, 0xaaU, 0U },
        { 0x2eU, 0xa8U, 0x55U, 0U }
    };
    const size_t sizes[] = { 2U, 3U, 3U };
    size_t index;

    for (index = 0U; index < sizeof(programs) / sizeof(programs[0]); ++index) {
        timing_capture_t capture = { 0 };
        cpu_808x_test_config_t config = {
            .timing = capture_timing,
            .timing_context = &capture
        };
        cpu_808x_test_machine_t machine;

        cpu_808x_test_machine_create(&machine, &config, programs[index],
                                     sizes[index]);
        start_program(&machine);
        step_once(&machine, &capture);
        assert_exact_execution_clocks(&capture, index == 2U ? 6U : 4U);
        assert(capture.last.boundary_clock_kind ==
               BM_808X_EXECUTION_CLOCKS_EXACT);
        assert(capture.last.operand_transactions == 0U);
        assert(capture.last.execution_timeline_complete == (index == 2U));
        cpu_808x_test_machine_destroy(&machine);
    }
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
        assert(capture.last.execution_timeline_complete == (index != 0U));
        assert(capture.last.execution_clocks_placed ==
               (index != 0U ? cases[index].clocks : 0U));
        assert(capture.last.operand_wait_states == 0U);
        cpu_808x_test_machine_destroy(&machine);
    }
}

static void
test_operand_timeline_can_finish_inflight_prefetch(void)
{
    static const uint8_t program[] = {
        0x90U,       /* NOP leaves three bytes queued and the BCU idle. */
        0x50U,       /* PUSH AW advances the PFP before its stack write. */
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
    assert_exact_boundary_clocks(&capture, 9U);
    assert(capture.last.logical_bus_transactions == 2U);
    assert(capture.last.prefetch_transactions == 1U);
    assert(capture.last.prefetch_phase_clocks == 5U);
    assert(capture.last.operand_transactions == 1U);
    assert(capture.last.operand_bus_clocks == 4U);
    assert(capture.last.prefetch_handoff_clocks == 0U);
    assert(capture.last.bus_active_clocks == 9U);
    assert(capture.last.prefetch_queue_count == 4U);
    assert(capture.last.prefetch_phase == BM_808X_PREFETCH_T2);
    assert(capture.last.execution_timeline_complete == 1U);
    assert(capture.last.execution_clocks_placed == 8U);
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
        /* Prefix execution finishes the pending prefetch before I/O needs the
         * bus, so it replaces the three-clock handoff seen by the unprefixed
         * form instead of extending that boundary. */
        assert_exact_boundary_clocks(&capture, 18U);
        assert(capture.last.execution_timeline_complete == 1U);
        assert(capture.last.execution_clocks_placed == 11U);
        cpu_808x_test_machine_destroy(&machine);
    }

}

static void
test_direct_memory_moves_have_a_complete_execution_timeline(void)
{
    const struct {
        uint8_t program[4];
        size_t size;
        uint16_t offset;
        uint32_t execution_clocks;
        uint64_t operand_transactions;
        int store;
        int word;
        int code_segment;
    } cases[] = {
        { { 0xa0U, 0x00U, 0x01U, 0U }, 3U, 0x0100U, 10U, 1U, 0, 0, 0 },
        { { 0xa1U, 0x00U, 0x01U, 0U }, 3U, 0x0100U, 10U, 1U, 0, 1, 0 },
        { { 0xa1U, 0x01U, 0x01U, 0U }, 3U, 0x0101U, 14U, 2U, 0, 1, 0 },
        { { 0xa2U, 0x00U, 0x01U, 0U }, 3U, 0x0100U, 9U, 1U, 1, 0, 0 },
        { { 0xa3U, 0x00U, 0x01U, 0U }, 3U, 0x0100U, 9U, 1U, 1, 1, 0 },
        { { 0xa3U, 0x01U, 0x01U, 0U }, 3U, 0x0101U, 13U, 2U, 1, 1, 0 },
        { { 0x2eU, 0xa0U, 0x00U, 0x01U }, 4U, 0x0100U, 12U, 1U,
          0, 0, 1 }
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
        uint64_t address = cases[index].offset;

        cpu_808x_test_machine_create(&machine, &config,
                                     cases[index].program,
                                     cases[index].size);
        start_program(&machine);
        state = cpu_808x_test_get_state(&machine);
        state.ax = 0xa55aU;
        cpu_808x_test_set_state(&machine, &state);
        if (cases[index].code_segment)
            address += UINT64_C(0xf0000);
        cpu_808x_test_poke(&machine, address,
                           cases[index].store ? 0U : 0x5aU);
        if (cases[index].word)
            cpu_808x_test_poke(&machine, address + 1U,
                               cases[index].store ? 0U : 0xa5U);

        step_once(&machine, &capture);
        assert_exact_execution_clocks(&capture,
                                      cases[index].execution_clocks);
        assert(capture.last.boundary_clock_kind ==
               BM_808X_EXECUTION_CLOCKS_EXACT);
        assert(capture.last.boundary_clocks_min ==
               capture.last.execution_clocks_min +
               capture.last.demand_prefetch_bus_clocks +
               capture.last.instruction_queue_reads +
               capture.last.prefetch_handoff_clocks);
        assert(capture.last.boundary_clocks_max ==
               capture.last.boundary_clocks_min);
        assert(capture.last.operand_transactions ==
               cases[index].operand_transactions);
        assert(capture.last.execution_timeline_complete == 1U);
        assert(capture.last.execution_clocks_placed ==
               cases[index].execution_clocks);
        assert(capture.last.operand_wait_states == 0U);
        if (cases[index].store) {
            assert(cpu_808x_test_peek(&machine, address) == 0x5aU);
            if (cases[index].word)
                assert(cpu_808x_test_peek(&machine, address + 1U) == 0xa5U);
        } else {
            state = cpu_808x_test_get_state(&machine);
            assert((state.ax & (cases[index].word ? 0xffffU : 0x00ffU)) ==
                   (cases[index].word ? 0xa55aU : 0x005aU));
        }
        cpu_808x_test_machine_destroy(&machine);
    }

    {
        static const uint8_t xlat[] = { 0xd7U };
        timing_capture_t capture = { 0 };
        cpu_808x_test_config_t config = {
            .timing = capture_timing,
            .timing_context = &capture
        };
        cpu_808x_test_machine_t machine;
        bm_808x_arch_state_t state;

        cpu_808x_test_machine_create(&machine, &config, xlat, sizeof(xlat));
        start_program(&machine);
        state = cpu_808x_test_get_state(&machine);
        state.bx = 0x0100U;
        state.ax = 0U;
        cpu_808x_test_set_state(&machine, &state);
        cpu_808x_test_poke(&machine, 0x0100U, 0x5aU);
        step_once(&machine, &capture);
        state = cpu_808x_test_get_state(&machine);
        assert((state.ax & 0x00ffU) == 0x005aU);
        assert_exact_execution_clocks(&capture, 9U);
        assert(capture.last.boundary_clock_kind ==
               BM_808X_EXECUTION_CLOCKS_EXACT);
        assert(capture.last.operand_transactions == 1U);
        assert(capture.last.execution_timeline_complete == 1U);
        assert(capture.last.execution_clocks_placed == 9U);
        assert(capture.last.operand_wait_states == 0U);
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
    assert_exact_boundary_clocks(&capture, 16U);
    assert(capture.last.logical_bus_transactions == 4U);
    assert(capture.last.operand_transactions == 1U);
    assert(capture.last.operand_bus_clocks == 4U);
    assert(capture.last.prefetch_handoff_clocks == 1U);
    assert(capture.last.execution_timeline_complete == 1U);
    assert(capture.last.execution_clocks_placed == 9U);
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
            assert_exact_boundary_clocks(&capture, odd ? 31U : 23U);
            assert(bus_capture.count == (odd ? 8U : 6U));
            assert(capture.last.bus_active_clocks == (odd ? 31U : 23U));
            assert(capture.last.demand_prefetch_transactions == 1U);
            assert(capture.last.demand_prefetch_bus_clocks == 4U);
            assert(capture.last.instruction_queue_reads == 2U);
            assert(capture.last.operand_transactions == (odd ? 4U : 2U));
            assert(capture.last.operand_bus_clocks == (odd ? 16U : 8U));
            assert(capture.last.prefetch_handoff_clocks == 1U);
            assert(capture.last.prefetch_transactions == 4U);
            assert(capture.last.execution_timeline_complete == 1U);
            assert(capture.last.execution_clocks_placed == (odd ? 24U : 16U));
            assert(bus_capture.transactions[0].operation == BM_BUS_FETCH);
            assert(bus_capture.transactions[0].size == 2U);
            assert(bus_capture.transactions[0].alignment == 2U);
            assert(bus_capture.transactions[1].operation == BM_BUS_FETCH);
            assert(bus_capture.transactions[1].size == 2U);
            assert(bus_capture.transactions[1].alignment == 2U);
            if (odd) {
                size_t index;

                for (index = 2U; index < 4U; ++index) {
                    assert(bus_capture.transactions[index].operation ==
                           BM_BUS_READ);
                    assert(bus_capture.transactions[index].size == 1U);
                    assert(bus_capture.transactions[index].alignment == 1U);
                    assert(bus_capture.transactions[index].address ==
                           0x10041U + ((index - 2U) & 1U));
                }
                assert(bus_capture.transactions[4].operation == BM_BUS_FETCH);
                for (index = 5U; index < 7U; ++index) {
                    assert(bus_capture.transactions[index].operation ==
                           BM_BUS_WRITE);
                    assert(bus_capture.transactions[index].size == 1U);
                    assert(bus_capture.transactions[index].alignment == 1U);
                    assert(bus_capture.transactions[index].address ==
                           0x10041U + ((index - 5U) & 1U));
                }
                assert(bus_capture.transactions[7].operation == BM_BUS_FETCH);
            } else {
                assert(bus_capture.transactions[2].operation == BM_BUS_READ);
                assert(bus_capture.transactions[3].operation == BM_BUS_FETCH);
                assert(bus_capture.transactions[4].operation == BM_BUS_WRITE);
                assert(bus_capture.transactions[5].operation == BM_BUS_FETCH);
                assert(bus_capture.transactions[2].address == 0x10040U);
                assert(bus_capture.transactions[4].address == 0x10040U);
                assert(bus_capture.transactions[2].size == 2U);
                assert(bus_capture.transactions[4].size == 2U);
                assert(bus_capture.transactions[2].alignment == 2U);
                assert(bus_capture.transactions[4].alignment == 2U);
            }
            cpu_808x_test_machine_destroy(&machine);
        }
    }
}

static void
test_modrm_moves_have_a_complete_execution_timeline(void)
{
    const struct {
        uint8_t program[4];
        size_t size;
        uint16_t bx;
        uint64_t address;
        uint32_t execution_clocks;
        uint64_t operand_transactions;
        int store;
        int word;
    } cases[] = {
        { { 0x89U, 0x07U, 0U, 0U }, 2U, 0x0100U, 0x0100U,
          9U, 1U, 1, 1 },
        { { 0x89U, 0x07U, 0U, 0U }, 2U, 0x0101U, 0x0101U,
          13U, 2U, 1, 1 },
        { { 0x8aU, 0x07U, 0U, 0U }, 2U, 0x0100U, 0x0100U,
          11U, 1U, 0, 0 },
        { { 0x8bU, 0x07U, 0U, 0U }, 2U, 0x0100U, 0x0100U,
          11U, 1U, 0, 1 },
        { { 0x8bU, 0x07U, 0U, 0U }, 2U, 0x0101U, 0x0101U,
          15U, 2U, 0, 1 },
        { { 0x8aU, 0x47U, 0x01U, 0U }, 3U, 0x0100U, 0x0101U,
          11U, 1U, 0, 0 },
        { { 0x2eU, 0x8aU, 0x07U, 0U }, 3U, 0x0100U, 0xf0100U,
          13U, 1U, 0, 0 }
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
        state.bx = cases[index].bx;
        state.ax = 0xa55aU;
        cpu_808x_test_set_state(&machine, &state);
        cpu_808x_test_poke(&machine, cases[index].address,
                           cases[index].store ? 0U : 0x5aU);
        if (cases[index].word)
            cpu_808x_test_poke(&machine, cases[index].address + 1U,
                               cases[index].store ? 0U : 0xa5U);

        step_once(&machine, &capture);
        assert_exact_execution_clocks(&capture,
                                      cases[index].execution_clocks);
        assert(capture.last.boundary_clock_kind ==
               BM_808X_EXECUTION_CLOCKS_EXACT);
        assert(capture.last.operand_transactions ==
               cases[index].operand_transactions);
        assert(capture.last.execution_timeline_complete == 1U);
        assert(capture.last.execution_clocks_placed ==
               cases[index].execution_clocks);
        if (cases[index].store) {
            assert(cpu_808x_test_peek(&machine, cases[index].address) ==
                   0x5aU);
            assert(cpu_808x_test_peek(&machine,
                                      cases[index].address + 1U) == 0xa5U);
        } else {
            state = cpu_808x_test_get_state(&machine);
            assert((state.ax & (cases[index].word ? 0xffffU : 0x00ffU)) ==
                   (cases[index].word ? 0xa55aU : 0x005aU));
        }
        cpu_808x_test_machine_destroy(&machine);
    }

    {
        static const uint8_t register_move[] = { 0x8bU, 0xc3U };
        timing_capture_t capture = { 0 };
        cpu_808x_test_config_t config = {
            .timing = capture_timing,
            .timing_context = &capture
        };
        cpu_808x_test_machine_t machine;
        bm_808x_arch_state_t state;

        cpu_808x_test_machine_create(&machine, &config, register_move,
                                     sizeof(register_move));
        start_program(&machine);
        state = cpu_808x_test_get_state(&machine);
        state.bx = 0xa55aU;
        cpu_808x_test_set_state(&machine, &state);
        step_once(&machine, &capture);
        state = cpu_808x_test_get_state(&machine);
        assert(state.ax == 0xa55aU);
        assert_exact_execution_clocks(&capture, 2U);
        assert(capture.last.operand_transactions == 0U);
        assert(capture.last.execution_timeline_complete == 0U);
        assert(capture.last.execution_clocks_placed == 0U);
        cpu_808x_test_machine_destroy(&machine);
    }
}

static void
test_modrm_alu_reads_have_a_complete_execution_timeline(void)
{
    const struct {
        uint8_t program[4];
        size_t size;
        uint16_t bx;
        uint64_t address;
        uint32_t execution_clocks;
        uint64_t operand_transactions;
        int word;
    } cases[] = {
        { { 0x02U, 0x07U, 0U, 0U }, 2U, 0x0100U, 0x0100U,
          11U, 1U, 0 },
        { { 0x03U, 0x07U, 0U, 0U }, 2U, 0x0100U, 0x0100U,
          11U, 1U, 1 },
        { { 0x03U, 0x07U, 0U, 0U }, 2U, 0x0101U, 0x0101U,
          15U, 2U, 1 },
        { { 0x32U, 0x07U, 0U, 0U }, 2U, 0x0100U, 0x0100U,
          11U, 1U, 0 },
        { { 0x3aU, 0x07U, 0U, 0U }, 2U, 0x0100U, 0x0100U,
          11U, 1U, 0 },
        { { 0x38U, 0x07U, 0U, 0U }, 2U, 0x0100U, 0x0100U,
          11U, 1U, 0 },
        { { 0x39U, 0x07U, 0U, 0U }, 2U, 0x0100U, 0x0100U,
          11U, 1U, 1 },
        { { 0x2eU, 0x3bU, 0x07U, 0U }, 3U, 0x0100U, 0xf0100U,
          13U, 1U, 1 }
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
        state.bx = cases[index].bx;
        state.ax = 1U;
        cpu_808x_test_set_state(&machine, &state);
        cpu_808x_test_poke(&machine, cases[index].address, 1U);
        if (cases[index].word)
            cpu_808x_test_poke(&machine, cases[index].address + 1U, 0U);

        step_once(&machine, &capture);
        assert_exact_execution_clocks(&capture,
                                      cases[index].execution_clocks);
        assert(capture.last.boundary_clock_kind ==
               BM_808X_EXECUTION_CLOCKS_EXACT);
        assert(capture.last.operand_transactions ==
               cases[index].operand_transactions);
        assert(capture.last.execution_timeline_complete == 1U);
        assert(capture.last.execution_clocks_placed ==
               cases[index].execution_clocks);
        assert(capture.last.operand_wait_states == 0U);
        cpu_808x_test_machine_destroy(&machine);
    }

    {
        static const uint8_t register_add[] = { 0x03U, 0xc3U };
        timing_capture_t capture = { 0 };
        cpu_808x_test_config_t config = {
            .timing = capture_timing,
            .timing_context = &capture
        };
        cpu_808x_test_machine_t machine;
        bm_808x_arch_state_t state;

        cpu_808x_test_machine_create(&machine, &config, register_add,
                                     sizeof(register_add));
        start_program(&machine);
        state = cpu_808x_test_get_state(&machine);
        state.ax = 1U;
        state.bx = 2U;
        cpu_808x_test_set_state(&machine, &state);
        step_once(&machine, &capture);
        state = cpu_808x_test_get_state(&machine);
        assert(state.ax == 3U);
        assert_exact_execution_clocks(&capture, 2U);
        assert(capture.last.operand_transactions == 0U);
        assert(capture.last.execution_timeline_complete == 0U);
        assert(capture.last.execution_clocks_placed == 0U);
        cpu_808x_test_machine_destroy(&machine);
    }
}

static void
test_modrm_alu_writes_have_a_complete_execution_timeline(void)
{
    const struct {
        uint8_t program[4];
        size_t size;
        uint64_t address;
        uint32_t execution_clocks;
    } cases[] = {
        { { 0x00U, 0x07U, 0U, 0U }, 2U, 0x0100U, 16U },
        { { 0x2eU, 0x00U, 0x07U, 0U }, 3U, 0xf0100U, 18U }
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
        state.bx = 0x0100U;
        state.ax = 1U;
        cpu_808x_test_set_state(&machine, &state);
        cpu_808x_test_poke(&machine, cases[index].address, 1U);

        step_once(&machine, &capture);
        assert(cpu_808x_test_peek(&machine, cases[index].address) == 2U);
        assert_exact_execution_clocks(&capture,
                                      cases[index].execution_clocks);
        assert(capture.last.boundary_clock_kind ==
               BM_808X_EXECUTION_CLOCKS_EXACT);
        assert(capture.last.operand_transactions == 2U);
        assert(capture.last.execution_timeline_complete == 1U);
        assert(capture.last.execution_clocks_placed ==
               cases[index].execution_clocks);
        assert(capture.last.operand_wait_states == 0U);
        cpu_808x_test_machine_destroy(&machine);
    }

    {
        static const uint8_t register_add[] = { 0x01U, 0xc3U };
        timing_capture_t capture = { 0 };
        cpu_808x_test_config_t config = {
            .timing = capture_timing,
            .timing_context = &capture
        };
        cpu_808x_test_machine_t machine;
        bm_808x_arch_state_t state;

        cpu_808x_test_machine_create(&machine, &config, register_add,
                                     sizeof(register_add));
        start_program(&machine);
        state = cpu_808x_test_get_state(&machine);
        state.ax = 1U;
        state.bx = 2U;
        cpu_808x_test_set_state(&machine, &state);
        step_once(&machine, &capture);
        state = cpu_808x_test_get_state(&machine);
        assert(state.bx == 3U);
        assert_exact_execution_clocks(&capture, 2U);
        assert(capture.last.operand_transactions == 0U);
        assert(capture.last.execution_timeline_complete == 0U);
        assert(capture.last.execution_clocks_placed == 0U);
        cpu_808x_test_machine_destroy(&machine);
    }
}

static void
test_modrm_test_and_exchange_have_a_complete_execution_timeline(void)
{
    const struct {
        uint8_t program[4];
        size_t size;
        uint16_t bx;
        uint64_t address;
        uint32_t execution_clocks;
        uint64_t operand_transactions;
        int word;
        int exchange;
    } cases[] = {
        { { 0x84U, 0x07U, 0U, 0U }, 2U, 0x0100U, 0x0100U,
          10U, 1U, 0, 0 },
        { { 0x85U, 0x07U, 0U, 0U }, 2U, 0x0100U, 0x0100U,
          10U, 1U, 1, 0 },
        { { 0x85U, 0x07U, 0U, 0U }, 2U, 0x0101U, 0x0101U,
          14U, 2U, 1, 0 },
        { { 0x2eU, 0x84U, 0x07U, 0U }, 3U, 0x0100U, 0xf0100U,
          12U, 1U, 0, 0 },
        { { 0x86U, 0x07U, 0U, 0U }, 2U, 0x0100U, 0x0100U,
          16U, 2U, 0, 1 },
        { { 0x87U, 0x07U, 0U, 0U }, 2U, 0x0100U, 0x0100U,
          16U, 2U, 1, 1 },
        { { 0x87U, 0x07U, 0U, 0U }, 2U, 0x0101U, 0x0101U,
          24U, 4U, 1, 1 },
        { { 0xf0U, 0x86U, 0x07U, 0U }, 3U, 0x0100U, 0x0100U,
          18U, 2U, 0, 1 }
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
        state.bx = cases[index].bx;
        state.ax = 0xa55aU;
        cpu_808x_test_set_state(&machine, &state);
        cpu_808x_test_poke(&machine, cases[index].address, 0x11U);
        if (cases[index].word)
            cpu_808x_test_poke(&machine, cases[index].address + 1U, 0x22U);

        step_once(&machine, &capture);
        assert_exact_execution_clocks(&capture,
                                      cases[index].execution_clocks);
        assert(capture.last.boundary_clock_kind ==
               BM_808X_EXECUTION_CLOCKS_EXACT);
        assert(capture.last.operand_transactions ==
               cases[index].operand_transactions);
        assert(capture.last.execution_timeline_complete == 1U);
        assert(capture.last.execution_clocks_placed ==
               cases[index].execution_clocks);
        if (cases[index].exchange) {
            assert(cpu_808x_test_peek(&machine, cases[index].address) ==
                   0x5aU);
            if (cases[index].word)
                assert(cpu_808x_test_peek(&machine,
                                          cases[index].address + 1U) == 0xa5U);
        } else {
            assert(cpu_808x_test_peek(&machine, cases[index].address) ==
                   0x11U);
        }
        cpu_808x_test_machine_destroy(&machine);
    }

    {
        static const uint8_t register_operations[][2] = {
            { 0x84U, 0xc3U },
            { 0x87U, 0xc3U }
        };

        for (index = 0U;
             index < sizeof(register_operations) / sizeof(register_operations[0]);
             ++index) {
            timing_capture_t capture = { 0 };
            cpu_808x_test_config_t config = {
                .timing = capture_timing,
                .timing_context = &capture
            };
            cpu_808x_test_machine_t machine;
            bm_808x_arch_state_t state;

            cpu_808x_test_machine_create(&machine, &config,
                                         register_operations[index], 2U);
            start_program(&machine);
            state = cpu_808x_test_get_state(&machine);
            state.ax = 1U;
            state.bx = 2U;
            cpu_808x_test_set_state(&machine, &state);
            step_once(&machine, &capture);
            assert_exact_execution_clocks(&capture, index == 0U ? 2U : 3U);
            assert(capture.last.operand_transactions == 0U);
            assert(capture.last.execution_timeline_complete == 0U);
            assert(capture.last.execution_clocks_placed == 0U);
            cpu_808x_test_machine_destroy(&machine);
        }
    }
}

static void
test_immediate_alu_groups_have_a_complete_execution_timeline(void)
{
    const struct {
        uint8_t program[5];
        size_t size;
        uint16_t bx;
        uint64_t address;
        uint32_t execution_clocks;
        uint64_t operand_transactions;
        int word;
        int compare;
    } cases[] = {
        { { 0x80U, 0x07U, 0x01U, 0U, 0U }, 3U, 0x0100U, 0x0100U,
          18U, 2U, 0, 0 },
        { { 0x80U, 0x3fU, 0x01U, 0U, 0U }, 3U, 0x0100U, 0x0100U,
          13U, 1U, 0, 1 },
        { { 0x82U, 0x07U, 0x01U, 0U, 0U }, 3U, 0x0100U, 0x0100U,
          18U, 2U, 0, 0 },
        { { 0x81U, 0x07U, 0x01U, 0x00U, 0U }, 4U, 0x0100U, 0x0100U,
          18U, 2U, 1, 0 },
        { { 0x81U, 0x07U, 0x01U, 0x00U, 0U }, 4U, 0x0101U, 0x0101U,
          26U, 4U, 1, 0 },
        { { 0x81U, 0x3fU, 0x01U, 0x00U, 0U }, 4U, 0x0100U, 0x0100U,
          13U, 1U, 1, 1 },
        { { 0x83U, 0x07U, 0x01U, 0U, 0U }, 3U, 0x0100U, 0x0100U,
          18U, 2U, 1, 0 },
        { { 0x83U, 0x3fU, 0x01U, 0U, 0U }, 3U, 0x0101U, 0x0101U,
          17U, 2U, 1, 1 },
        { { 0x2eU, 0x83U, 0x3fU, 0x01U, 0U }, 4U, 0x0100U, 0xf0100U,
          15U, 1U, 1, 1 }
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
        state.bx = cases[index].bx;
        cpu_808x_test_set_state(&machine, &state);
        cpu_808x_test_poke(&machine, cases[index].address, 1U);
        if (cases[index].word)
            cpu_808x_test_poke(&machine, cases[index].address + 1U, 0U);

        step_once(&machine, &capture);
        assert_exact_execution_clocks(&capture,
                                      cases[index].execution_clocks);
        assert(capture.last.boundary_clock_kind ==
               BM_808X_EXECUTION_CLOCKS_EXACT);
        assert(capture.last.operand_transactions ==
               cases[index].operand_transactions);
        assert(capture.last.execution_timeline_complete == 1U);
        assert(capture.last.execution_clocks_placed ==
               cases[index].execution_clocks);
        assert(cpu_808x_test_peek(&machine, cases[index].address) ==
               (cases[index].compare ? 1U : 2U));
        cpu_808x_test_machine_destroy(&machine);
    }

    {
        static const uint8_t register_add[] = { 0x80U, 0xc3U, 0x01U };
        timing_capture_t capture = { 0 };
        cpu_808x_test_config_t config = {
            .timing = capture_timing,
            .timing_context = &capture
        };
        cpu_808x_test_machine_t machine;
        bm_808x_arch_state_t state;

        cpu_808x_test_machine_create(&machine, &config, register_add,
                                     sizeof(register_add));
        start_program(&machine);
        state = cpu_808x_test_get_state(&machine);
        state.bx = 1U;
        cpu_808x_test_set_state(&machine, &state);
        step_once(&machine, &capture);
        state = cpu_808x_test_get_state(&machine);
        assert((state.bx & 0x00ffU) == 2U);
        assert_exact_execution_clocks(&capture, 4U);
        assert(capture.last.operand_transactions == 0U);
        assert(capture.last.execution_timeline_complete == 0U);
        assert(capture.last.execution_clocks_placed == 0U);
        cpu_808x_test_machine_destroy(&machine);
    }
}

static void
test_special_moves_have_a_complete_execution_timeline(void)
{
    const struct {
        uint8_t program[5];
        size_t size;
        uint16_t bx;
        uint64_t address;
        uint32_t execution_clocks;
        uint64_t operand_transactions;
        uint16_t initial_es;
        uint16_t memory_word;
        uint8_t expected_byte;
        int load_ds;
        int word;
    } cases[] = {
        { { 0x8cU, 0x07U, 0U, 0U, 0U }, 2U, 0x0100U, 0x0100U,
          10U, 1U, 0x2211U, 0U, 0x11U, 0, 1 },
        { { 0x8cU, 0x07U, 0U, 0U, 0U }, 2U, 0x0101U, 0x0101U,
          14U, 2U, 0x2211U, 0U, 0x11U, 0, 1 },
        { { 0x8eU, 0x1fU, 0U, 0U, 0U }, 2U, 0x0100U, 0x0100U,
          11U, 1U, 0U, 0x4433U, 0x33U, 1, 1 },
        { { 0x8eU, 0x1fU, 0U, 0U, 0U }, 2U, 0x0101U, 0x0101U,
          15U, 2U, 0U, 0x4433U, 0x33U, 1, 1 },
        { { 0x2eU, 0x8eU, 0x1fU, 0U, 0U }, 3U, 0x0100U, 0xf0100U,
          13U, 1U, 0U, 0x4433U, 0x33U, 1, 1 },
        { { 0xc6U, 0x07U, 0x5aU, 0U, 0U }, 3U, 0x0100U, 0x0100U,
          11U, 1U, 0U, 0U, 0x5aU, 0, 0 },
        { { 0x2eU, 0xc6U, 0x07U, 0x5aU, 0U }, 4U, 0x0100U, 0xf0100U,
          13U, 1U, 0U, 0U, 0x5aU, 0, 0 },
        { { 0xc7U, 0x07U, 0x5aU, 0xa5U, 0U }, 4U, 0x0100U, 0x0100U,
          11U, 1U, 0U, 0U, 0x5aU, 0, 1 },
        { { 0xc7U, 0x07U, 0x5aU, 0xa5U, 0U }, 4U, 0x0101U, 0x0101U,
          15U, 2U, 0U, 0U, 0x5aU, 0, 1 }
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
        state.bx = cases[index].bx;
        state.es = cases[index].initial_es;
        cpu_808x_test_set_state(&machine, &state);
        cpu_808x_test_poke(&machine, cases[index].address,
                           (uint8_t) cases[index].memory_word);
        if (cases[index].word)
            cpu_808x_test_poke(&machine, cases[index].address + 1U,
                               (uint8_t) (cases[index].memory_word >> 8U));

        step_once(&machine, &capture);
        assert_exact_execution_clocks(&capture,
                                      cases[index].execution_clocks);
        assert(capture.last.boundary_clock_kind ==
               BM_808X_EXECUTION_CLOCKS_EXACT);
        assert(capture.last.operand_transactions ==
               cases[index].operand_transactions);
        assert(capture.last.execution_timeline_complete == 1U);
        assert(capture.last.execution_clocks_placed ==
               cases[index].execution_clocks);
        state = cpu_808x_test_get_state(&machine);
        if (cases[index].load_ds) {
            assert(state.ds == cases[index].memory_word);
        } else {
            assert(cpu_808x_test_peek(&machine, cases[index].address) ==
                   cases[index].expected_byte);
            if (cases[index].word)
                assert(cpu_808x_test_peek(&machine,
                                          cases[index].address + 1U) ==
                       (cases[index].initial_es != 0U ? 0x22U : 0xa5U));
        }
        cpu_808x_test_machine_destroy(&machine);
    }

    {
        static const struct {
            uint8_t program[4];
            size_t size;
            uint32_t execution_clocks;
        } register_cases[] = {
            { { 0x8cU, 0xc0U, 0U, 0U }, 2U, 2U },
            { { 0x8eU, 0xd8U, 0U, 0U }, 2U, 2U },
            { { 0xc7U, 0xc3U, 0x5aU, 0xa5U }, 4U, 4U }
        };

        for (index = 0U;
             index < sizeof(register_cases) / sizeof(register_cases[0]);
             ++index) {
            timing_capture_t capture = { 0 };
            cpu_808x_test_config_t config = {
                .timing = capture_timing,
                .timing_context = &capture
            };
            cpu_808x_test_machine_t machine;
            bm_808x_arch_state_t state;

            cpu_808x_test_machine_create(&machine, &config,
                                         register_cases[index].program,
                                         register_cases[index].size);
            start_program(&machine);
            state = cpu_808x_test_get_state(&machine);
            state.ax = 0x4433U;
            state.es = 0x2211U;
            cpu_808x_test_set_state(&machine, &state);
            step_once(&machine, &capture);
            assert_exact_execution_clocks(
                &capture, register_cases[index].execution_clocks);
            assert(capture.last.operand_transactions == 0U);
            assert(capture.last.execution_timeline_complete == 0U);
            assert(capture.last.execution_clocks_placed == 0U);
            cpu_808x_test_machine_destroy(&machine);
        }
    }
}

static void
test_group3_memory_operands_have_a_placed_timeline(void)
{
    const struct {
        uint8_t program[5];
        size_t size;
        uint16_t bx;
        uint64_t address;
        uint32_t execution_clocks;
        uint64_t operand_transactions;
        uint16_t initial_value;
        uint16_t expected_value;
        int word;
    } cases[] = {
        { { 0xf6U, 0x07U, 0x0fU, 0U, 0U }, 3U, 0x0100U, 0x0100U,
          11U, 1U, 0x00f0U, 0x00f0U, 0 },
        { { 0xf7U, 0x07U, 0x0fU, 0x00U, 0U }, 4U, 0x0100U, 0x0100U,
          11U, 1U, 0x00f0U, 0x00f0U, 1 },
        { { 0xf7U, 0x07U, 0x0fU, 0x00U, 0U }, 4U, 0x0101U, 0x0101U,
          15U, 2U, 0x00f0U, 0x00f0U, 1 },
        { { 0xf6U, 0x17U, 0U, 0U, 0U }, 2U, 0x0100U, 0x0100U,
          16U, 2U, 0x000fU, 0x00f0U, 0 },
        { { 0xf6U, 0x1fU, 0U, 0U, 0U }, 2U, 0x0100U, 0x0100U,
          16U, 2U, 0x0002U, 0x00feU, 0 },
        { { 0xf7U, 0x17U, 0U, 0U, 0U }, 2U, 0x0100U, 0x0100U,
          16U, 2U, 0x00ffU, 0xff00U, 1 },
        { { 0xf7U, 0x1fU, 0U, 0U, 0U }, 2U, 0x0101U, 0x0101U,
          24U, 4U, 0x0002U, 0xfffeU, 1 },
        { { 0x2eU, 0xf6U, 0x17U, 0U, 0U }, 3U, 0x0100U, 0xf0100U,
          18U, 2U, 0x000fU, 0x00f0U, 0 }
    };
    size_t index;

    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        timing_capture_t capture = { 0 };
        cpu_808x_test_config_t config = {
            .timing = capture_timing,
            .timing_context = &capture
        };
        cpu_808x_test_machine_t machine;
        uint16_t value;

        cpu_808x_test_machine_create(&machine, &config,
                                     cases[index].program,
                                     cases[index].size);
        start_program(&machine);
        {
            bm_808x_arch_state_t state = cpu_808x_test_get_state(&machine);
            state.bx = cases[index].bx;
            cpu_808x_test_set_state(&machine, &state);
        }
        cpu_808x_test_poke(&machine, cases[index].address,
                           (uint8_t) cases[index].initial_value);
        if (cases[index].word)
            cpu_808x_test_poke(&machine, cases[index].address + 1U,
                               (uint8_t) (cases[index].initial_value >> 8U));

        step_once(&machine, &capture);
        assert_exact_execution_clocks(&capture,
                                      cases[index].execution_clocks);
        assert(capture.last.boundary_clock_kind ==
               BM_808X_EXECUTION_CLOCKS_EXACT);
        assert(capture.last.operand_transactions ==
               cases[index].operand_transactions);
        assert(capture.last.execution_timeline_complete == 1U);
        assert(capture.last.execution_clocks_placed ==
               cases[index].execution_clocks);
        value = cpu_808x_test_peek(&machine, cases[index].address);
        if (cases[index].word)
            value |= (uint16_t) cpu_808x_test_peek(
                &machine, cases[index].address + 1U) << 8U;
        assert(value == cases[index].expected_value);
        cpu_808x_test_machine_destroy(&machine);
    }

    {
        const struct {
            uint8_t program[2];
            uint16_t bx;
            uint32_t execution_clocks;
            uint64_t operand_transactions;
            int word;
        } divide_cases[] = {
            { { 0xf6U, 0x37U }, 0x0100U, 25U, 1U, 0 },
            { { 0xf7U, 0x37U }, 0x0100U, 30U, 1U, 1 },
            { { 0xf7U, 0x37U }, 0x0101U, 34U, 2U, 1 }
        };

        for (index = 0U;
             index < sizeof(divide_cases) / sizeof(divide_cases[0]);
             ++index) {
            timing_capture_t capture = { 0 };
            cpu_808x_test_config_t config = {
                .timing = capture_timing,
                .timing_context = &capture
            };
            cpu_808x_test_machine_t machine;
            bm_808x_arch_state_t state;

            cpu_808x_test_machine_create(&machine, &config,
                                         divide_cases[index].program, 2U);
            start_program(&machine);
            state = cpu_808x_test_get_state(&machine);
            state.ax = 8U;
            state.dx = 0U;
            state.bx = divide_cases[index].bx;
            cpu_808x_test_set_state(&machine, &state);
            cpu_808x_test_poke(&machine, divide_cases[index].bx, 2U);
            if (divide_cases[index].word)
                cpu_808x_test_poke(&machine,
                                   divide_cases[index].bx + 1U, 0U);
            step_once(&machine, &capture);
            state = cpu_808x_test_get_state(&machine);
            assert(state.ax == 4U && state.dx == 0U);
            assert_exact_execution_clocks(
                &capture, divide_cases[index].execution_clocks);
            assert(capture.last.boundary_clock_kind ==
                   BM_808X_EXECUTION_CLOCKS_EXACT);
            assert(capture.last.operand_transactions ==
                   divide_cases[index].operand_transactions);
            assert(capture.last.execution_timeline_complete == 1U);
            assert(capture.last.execution_clocks_placed ==
                   divide_cases[index].execution_clocks);
            cpu_808x_test_machine_destroy(&machine);
        }
    }

    {
        static const uint8_t multiply[] = { 0xf6U, 0x27U };
        timing_capture_t capture = { 0 };
        cpu_808x_test_config_t config = {
            .timing = capture_timing,
            .timing_context = &capture
        };
        cpu_808x_test_machine_t machine;
        bm_808x_arch_state_t state;

        cpu_808x_test_machine_create(&machine, &config, multiply,
                                     sizeof(multiply));
        start_program(&machine);
        state = cpu_808x_test_get_state(&machine);
        state.ax = 2U;
        state.bx = 0x0100U;
        cpu_808x_test_set_state(&machine, &state);
        cpu_808x_test_poke(&machine, 0x0100U, 3U);
        step_once(&machine, &capture);
        state = cpu_808x_test_get_state(&machine);
        assert(state.ax == 6U);
        assert_exact_execution_clocks(&capture, 28U);
        assert(capture.last.boundary_clock_kind ==
               BM_808X_EXECUTION_CLOCKS_EXACT);
        assert(capture.last.operand_transactions == 1U);
        assert(capture.last.execution_timeline_complete == 1U);
        assert(capture.last.execution_clocks_placed == 28U);
        cpu_808x_test_machine_destroy(&machine);
    }

    {
        static const uint8_t register_not[] = { 0xf6U, 0xd0U };
        timing_capture_t capture = { 0 };
        cpu_808x_test_config_t config = {
            .timing = capture_timing,
            .timing_context = &capture
        };
        cpu_808x_test_machine_t machine;

        cpu_808x_test_machine_create(&machine, &config, register_not,
                                     sizeof(register_not));
        start_program(&machine);
        step_once(&machine, &capture);
        assert_exact_execution_clocks(&capture, 2U);
        assert(capture.last.operand_transactions == 0U);
        assert(capture.last.execution_timeline_complete == 0U);
        assert(capture.last.execution_clocks_placed == 0U);
        cpu_808x_test_machine_destroy(&machine);
    }
}

static void
test_inc_dec_memory_operands_have_a_complete_timeline(void)
{
    const struct {
        uint8_t program[3];
        size_t size;
        uint16_t bx;
        uint64_t address;
        uint32_t execution_clocks;
        uint64_t operand_transactions;
        uint16_t initial_value;
        uint16_t expected_value;
        int word;
    } cases[] = {
        { { 0xfeU, 0x07U, 0U }, 2U, 0x0100U, 0x0100U,
          16U, 2U, 0x0001U, 0x0002U, 0 },
        { { 0xfeU, 0x0fU, 0U }, 2U, 0x0100U, 0x0100U,
          16U, 2U, 0x0002U, 0x0001U, 0 },
        { { 0x2eU, 0xfeU, 0x07U }, 3U, 0x0100U, 0xf0100U,
          18U, 2U, 0x0001U, 0x0002U, 0 },
        { { 0xffU, 0x07U, 0U }, 2U, 0x0100U, 0x0100U,
          16U, 2U, 0x0001U, 0x0002U, 1 },
        { { 0xffU, 0x0fU, 0U }, 2U, 0x0101U, 0x0101U,
          24U, 4U, 0x0002U, 0x0001U, 1 }
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
        uint16_t value;

        cpu_808x_test_machine_create(&machine, &config,
                                     cases[index].program,
                                     cases[index].size);
        start_program(&machine);
        state = cpu_808x_test_get_state(&machine);
        state.bx = cases[index].bx;
        state.flags |= 1U;
        cpu_808x_test_set_state(&machine, &state);
        cpu_808x_test_poke(&machine, cases[index].address,
                           (uint8_t) cases[index].initial_value);
        if (cases[index].word)
            cpu_808x_test_poke(&machine, cases[index].address + 1U,
                               (uint8_t) (cases[index].initial_value >> 8U));

        step_once(&machine, &capture);
        state = cpu_808x_test_get_state(&machine);
        assert((state.flags & 1U) != 0U);
        assert_exact_execution_clocks(&capture,
                                      cases[index].execution_clocks);
        assert(capture.last.boundary_clock_kind ==
               BM_808X_EXECUTION_CLOCKS_EXACT);
        assert(capture.last.operand_transactions ==
               cases[index].operand_transactions);
        assert(capture.last.execution_timeline_complete == 1U);
        assert(capture.last.execution_clocks_placed ==
               cases[index].execution_clocks);
        value = cpu_808x_test_peek(&machine, cases[index].address);
        if (cases[index].word)
            value |= (uint16_t) cpu_808x_test_peek(
                &machine, cases[index].address + 1U) << 8U;
        assert(value == cases[index].expected_value);
        cpu_808x_test_machine_destroy(&machine);
    }

    {
        static const uint8_t register_operations[][2] = {
            { 0xfeU, 0xc0U },
            { 0xffU, 0xc0U }
        };

        for (index = 0U;
             index < sizeof(register_operations) / sizeof(register_operations[0]);
             ++index) {
            timing_capture_t capture = { 0 };
            cpu_808x_test_config_t config = {
                .timing = capture_timing,
                .timing_context = &capture
            };
            cpu_808x_test_machine_t machine;

            cpu_808x_test_machine_create(&machine, &config,
                                         register_operations[index], 2U);
            start_program(&machine);
            step_once(&machine, &capture);
            assert_exact_execution_clocks(&capture, 2U);
            assert(capture.last.operand_transactions == 0U);
            assert(capture.last.execution_timeline_complete == 0U);
            assert(capture.last.execution_clocks_placed == 0U);
            cpu_808x_test_machine_destroy(&machine);
        }
    }
}

static void
test_single_word_stack_operations_have_a_complete_timeline(void)
{
    const struct {
        uint8_t program[3];
        size_t size;
        uint16_t sp;
        uint32_t execution_clocks;
        uint64_t operand_transactions;
        uint16_t expected_value;
    } push_cases[] = {
        { { 0x50U, 0U, 0U }, 1U, 0x0100U, 8U, 1U, 0x1234U },
        { { 0x50U, 0U, 0U }, 1U, 0x0101U, 12U, 2U, 0x1234U },
        { { 0x2eU, 0x50U, 0U }, 2U, 0x0100U, 10U, 1U, 0x1234U },
        { { 0x06U, 0U, 0U }, 1U, 0x0100U, 8U, 1U, 0x2345U },
        { { 0x68U, 0xa5U, 0x5aU }, 3U, 0x0100U, 8U, 1U, 0x5aa5U },
        { { 0x6aU, 0xffU, 0U }, 2U, 0x0100U, 7U, 1U, 0xffffU }
    };
    const struct {
        uint8_t opcode;
        uint16_t sp;
        uint32_t execution_clocks;
        uint64_t operand_transactions;
        int segment;
    } pop_cases[] = {
        { 0x58U, 0x0100U, 8U, 1U, 0 },
        { 0x58U, 0x0101U, 12U, 2U, 0 },
        { 0x07U, 0x0100U, 8U, 1U, 1 },
        { 0x9dU, 0x0100U, 8U, 1U, 2 }
    };
    size_t index;

    for (index = 0U;
         index < sizeof(push_cases) / sizeof(push_cases[0]); ++index) {
        timing_capture_t capture = { 0 };
        cpu_808x_test_config_t config = {
            .timing = capture_timing,
            .timing_context = &capture
        };
        cpu_808x_test_machine_t machine;
        bm_808x_arch_state_t state;
        uint16_t address;
        uint16_t value;

        cpu_808x_test_machine_create(&machine, &config,
                                     push_cases[index].program,
                                     push_cases[index].size);
        start_program(&machine);
        state = cpu_808x_test_get_state(&machine);
        state.sp = push_cases[index].sp;
        state.ax = 0x1234U;
        state.es = 0x2345U;
        cpu_808x_test_set_state(&machine, &state);
        step_once(&machine, &capture);
        state = cpu_808x_test_get_state(&machine);
        address = (uint16_t) (push_cases[index].sp - 2U);
        value = cpu_808x_test_peek(&machine, address);
        value |= (uint16_t) cpu_808x_test_peek(&machine,
                                               (uint16_t) (address + 1U)) << 8U;
        assert(state.sp == address);
        assert(value == push_cases[index].expected_value);
        assert_exact_execution_clocks(&capture,
                                      push_cases[index].execution_clocks);
        assert(capture.last.boundary_clock_kind ==
               BM_808X_EXECUTION_CLOCKS_EXACT);
        assert(capture.last.operand_transactions ==
               push_cases[index].operand_transactions);
        assert(capture.last.execution_timeline_complete == 1U);
        assert(capture.last.execution_clocks_placed ==
               push_cases[index].execution_clocks);
        cpu_808x_test_machine_destroy(&machine);
    }

    {
        static const uint8_t pushf[] = { 0x9cU };
        timing_capture_t capture = { 0 };
        cpu_808x_test_config_t config = {
            .timing = capture_timing,
            .timing_context = &capture
        };
        cpu_808x_test_machine_t machine;
        bm_808x_arch_state_t state;

        cpu_808x_test_machine_create(&machine, &config, pushf,
                                     sizeof(pushf));
        start_program(&machine);
        state = cpu_808x_test_get_state(&machine);
        state.sp = 0x0100U;
        cpu_808x_test_set_state(&machine, &state);
        step_once(&machine, &capture);
        state = cpu_808x_test_get_state(&machine);
        assert(state.sp == 0x00feU);
        assert_exact_execution_clocks(&capture, 8U);
        assert(capture.last.boundary_clock_kind ==
               BM_808X_EXECUTION_CLOCKS_EXACT);
        assert(capture.last.operand_transactions == 1U);
        assert(capture.last.execution_timeline_complete == 1U);
        assert(capture.last.execution_clocks_placed == 8U);
        cpu_808x_test_machine_destroy(&machine);
    }

    for (index = 0U;
         index < sizeof(pop_cases) / sizeof(pop_cases[0]); ++index) {
        uint8_t program[] = { pop_cases[index].opcode };
        timing_capture_t capture = { 0 };
        cpu_808x_test_config_t config = {
            .timing = capture_timing,
            .timing_context = &capture
        };
        cpu_808x_test_machine_t machine;
        bm_808x_arch_state_t state;

        cpu_808x_test_machine_create(&machine, &config, program,
                                     sizeof(program));
        start_program(&machine);
        state = cpu_808x_test_get_state(&machine);
        state.sp = pop_cases[index].sp;
        cpu_808x_test_set_state(&machine, &state);
        cpu_808x_test_poke(&machine, pop_cases[index].sp, 0x78U);
        cpu_808x_test_poke(&machine,
                           (uint16_t) (pop_cases[index].sp + 1U), 0x56U);
        step_once(&machine, &capture);
        state = cpu_808x_test_get_state(&machine);
        assert(state.sp == (uint16_t) (pop_cases[index].sp + 2U));
        if (pop_cases[index].segment == 0)
            assert(state.ax == 0x5678U);
        else if (pop_cases[index].segment == 1)
            assert(state.es == 0x5678U);
        assert_exact_execution_clocks(&capture,
                                      pop_cases[index].execution_clocks);
        assert(capture.last.boundary_clock_kind ==
               BM_808X_EXECUTION_CLOCKS_EXACT);
        assert(capture.last.operand_transactions ==
               pop_cases[index].operand_transactions);
        assert(capture.last.execution_timeline_complete == 1U);
        assert(capture.last.execution_clocks_placed ==
               pop_cases[index].execution_clocks);
        cpu_808x_test_machine_destroy(&machine);
    }
}

static void
test_far_pointer_loads_have_a_complete_timeline(void)
{
    const struct {
        uint8_t program[5];
        size_t size;
        uint16_t address;
        uint32_t execution_clocks;
        uint64_t operand_transactions;
        int load_ds;
    } cases[] = {
        { { 0xc4U, 0x06U, 0x00U, 0x01U, 0U }, 4U,
          0x0100U, 18U, 2U, 0 },
        { { 0xc5U, 0x06U, 0x01U, 0x01U, 0U }, 4U,
          0x0101U, 26U, 4U, 1 },
        { { 0x2eU, 0xc4U, 0x06U, 0x00U, 0x01U }, 5U,
          0x0100U, 20U, 2U, 0 }
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
        uint64_t physical = index == 2U ?
            0xf0000U + cases[index].address : cases[index].address;

        cpu_808x_test_machine_create(&machine, &config,
                                     cases[index].program,
                                     cases[index].size);
        start_program(&machine);
        cpu_808x_test_poke(&machine, physical, 0x34U);
        cpu_808x_test_poke(&machine, physical + 1U, 0x12U);
        cpu_808x_test_poke(&machine, physical + 2U, 0x78U);
        cpu_808x_test_poke(&machine, physical + 3U, 0x56U);

        step_once(&machine, &capture);
        state = cpu_808x_test_get_state(&machine);
        assert(state.ax == 0x1234U);
        if (cases[index].load_ds)
            assert(state.ds == 0x5678U);
        else
            assert(state.es == 0x5678U);
        assert_exact_execution_clocks(&capture,
                                      cases[index].execution_clocks);
        assert(capture.last.boundary_clock_kind ==
               BM_808X_EXECUTION_CLOCKS_EXACT);
        assert(capture.last.operand_transactions ==
               cases[index].operand_transactions);
        assert(capture.last.execution_timeline_complete == 1U);
        assert(capture.last.execution_clocks_placed ==
               cases[index].execution_clocks);
        cpu_808x_test_machine_destroy(&machine);
    }
}

static void
test_near_call_and_return_have_a_complete_timeline(void)
{
    static const uint8_t program[] = {
        0xe8U, 0x00U, 0x00U, /* CALL +0. */
        0xc3U                /* RET. */
    };
    const uint16_t initial_stacks[] = { 0x0100U, 0x0101U };
    size_t index;

    for (index = 0U;
         index < sizeof(initial_stacks) / sizeof(initial_stacks[0]);
         ++index) {
        timing_capture_t capture = { 0 };
        cpu_808x_test_config_t config = {
            .timing = capture_timing,
            .timing_context = &capture
        };
        cpu_808x_test_machine_t machine;
        bm_808x_arch_state_t state;
        uint32_t call_clocks = index == 0U ? 16U : 20U;
        uint32_t return_clocks = index == 0U ? 15U : 19U;
        uint64_t transactions = index == 0U ? 1U : 2U;

        cpu_808x_test_machine_create(&machine, &config, program,
                                     sizeof(program));
        start_program(&machine);
        state = cpu_808x_test_get_state(&machine);
        state.ss = 0x1000U;
        state.sp = initial_stacks[index];
        cpu_808x_test_set_state(&machine, &state);

        step_once(&machine, &capture);
        state = cpu_808x_test_get_state(&machine);
        assert(state.ip == 3U);
        assert(state.sp == (uint16_t) (initial_stacks[index] - 2U));
        assert_exact_execution_clocks(&capture, call_clocks);
        assert(capture.last.boundary_clock_kind ==
               BM_808X_EXECUTION_CLOCKS_EXACT);
        assert(capture.last.operand_transactions == transactions);
        assert(capture.last.prefetch_queue_flushed == 1U);
        assert(capture.last.execution_timeline_complete == 1U);
        assert(capture.last.execution_clocks_placed == call_clocks);

        capture.count = 0U;
        step_once(&machine, &capture);
        state = cpu_808x_test_get_state(&machine);
        assert(state.ip == 3U);
        assert(state.sp == initial_stacks[index]);
        assert_exact_execution_clocks(&capture, return_clocks);
        assert(capture.last.boundary_clock_kind ==
               BM_808X_EXECUTION_CLOCKS_EXACT);
        assert(capture.last.operand_transactions == transactions);
        assert(capture.last.prefetch_queue_flushed == 1U);
        assert(capture.last.execution_timeline_complete == 1U);
        assert(capture.last.execution_clocks_placed == return_clocks);
        cpu_808x_test_machine_destroy(&machine);
    }
}

static void
test_indirect_near_call_has_a_complete_timeline(void)
{
    const struct {
        uint8_t program[5];
        size_t size;
        uint16_t pointer;
        uint16_t stack;
        uint32_t clocks;
        uint64_t transactions;
        int register_operand;
    } cases[] = {
        { { 0xffU, 0x16U, 0x00U, 0x01U, 0U }, 4U,
          0x0100U, 0x0200U, 23U, 2U, 0 },
        { { 0xffU, 0x16U, 0x01U, 0x01U, 0U }, 4U,
          0x0101U, 0x0200U, 27U, 3U, 0 },
        { { 0xffU, 0x16U, 0x00U, 0x01U, 0U }, 4U,
          0x0100U, 0x0201U, 27U, 3U, 0 },
        { { 0xffU, 0x16U, 0x01U, 0x01U, 0U }, 4U,
          0x0101U, 0x0201U, 31U, 4U, 0 },
        { { 0x2eU, 0xffU, 0x16U, 0x00U, 0x01U }, 5U,
          0x0100U, 0x0200U, 25U, 2U, 0 },
        { { 0xffU, 0xd0U, 0U, 0U, 0U }, 2U,
          0U, 0x0200U, 14U, 1U, 1 },
        { { 0xffU, 0xd0U, 0U, 0U, 0U }, 2U,
          0U, 0x0201U, 18U, 2U, 1 }
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
        uint64_t pointer_physical = index == 4U ?
            0xf0000U + cases[index].pointer : cases[index].pointer;

        cpu_808x_test_machine_create(&machine, &config,
                                     cases[index].program,
                                     cases[index].size);
        start_program(&machine);
        state = cpu_808x_test_get_state(&machine);
        state.ss = 0U;
        state.sp = cases[index].stack;
        if (cases[index].register_operand) {
            state.ax = 0x1234U;
        } else {
            cpu_808x_test_poke(&machine, pointer_physical, 0x34U);
            cpu_808x_test_poke(&machine, pointer_physical + 1U, 0x12U);
        }
        cpu_808x_test_set_state(&machine, &state);

        step_once(&machine, &capture);
        state = cpu_808x_test_get_state(&machine);
        assert(state.ip == 0x1234U);
        assert(state.sp == (uint16_t) (cases[index].stack - 2U));
        assert_exact_execution_clocks(&capture, cases[index].clocks);
        assert(capture.last.boundary_clock_kind ==
               BM_808X_EXECUTION_CLOCKS_EXACT);
        assert(capture.last.operand_transactions ==
               cases[index].transactions);
        assert(capture.last.prefetch_queue_flushed == 1U);
        assert(capture.last.execution_timeline_complete == 1U);
        assert(capture.last.execution_clocks_placed == cases[index].clocks);
        cpu_808x_test_machine_destroy(&machine);
    }
}

static void
test_clocked_step_requires_an_exact_scalar_boundary(void)
{
    static const uint8_t exact_program[] = { 0x90U };
    static const uint8_t ranged_program[] = { 0xf6U, 0xe8U };
    timing_capture_t capture = { 0 };
    cpu_808x_test_config_t config = {
        .timing = capture_timing,
        .timing_context = &capture
    };
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;
    uint64_t cycles = UINT64_MAX;

    cpu_808x_test_machine_create(&machine, &config, exact_program,
                                 sizeof(exact_program));
    start_program(&machine);
    assert(bm_808x_step_clocked(machine.cpu.context, 123U, &cycles) ==
           BM_STATUS_OK);
    assert(capture.count == 1U);
    assert(capture.last.boundary_clock_kind ==
           BM_808X_EXECUTION_CLOCKS_EXACT);
    assert(cycles == capture.last.boundary_clocks_min);
    assert(cycles == capture.last.boundary_clocks_max);
    cpu_808x_test_machine_destroy(&machine);

    memset(&capture, 0, sizeof(capture));
    cpu_808x_test_machine_create(&machine, &config, ranged_program,
                                 sizeof(ranged_program));
    start_program(&machine);
    state = cpu_808x_test_get_state(&machine);
    state.ax = 2U;
    cpu_808x_test_set_state(&machine, &state);
    cycles = UINT64_MAX;
    assert(bm_808x_step_clocked(machine.cpu.context, 456U, &cycles) ==
           BM_STATUS_UNSUPPORTED);
    assert(cycles == 0U);
    assert(capture.count == 1U);
    assert(capture.last.boundary_clock_kind ==
           BM_808X_EXECUTION_CLOCKS_RANGE);
    cpu_808x_test_machine_destroy(&machine);

    assert(bm_808x_step_clocked(NULL, 0U, &cycles) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_808x_step_clocked((void *) 1, 0U, NULL) ==
           BM_STATUS_INVALID_ARGUMENT);
}

static void
test_software_interrupt_has_a_complete_timeline(void)
{
    static const uint8_t program[] = { 0xcdU, 0x20U }; /* INT 20h. */
    const uint16_t initial_stacks[] = { 0x0100U, 0x0101U };
    size_t index;

    for (index = 0U;
         index < sizeof(initial_stacks) / sizeof(initial_stacks[0]);
         ++index) {
        timing_capture_t capture = { 0 };
        cpu_808x_test_config_t config = {
            .timing = capture_timing,
            .timing_context = &capture
        };
        cpu_808x_test_machine_t machine;
        bm_808x_arch_state_t state;
        uint32_t clocks = index == 0U ? 38U : 50U;
        uint64_t transactions = index == 0U ? 5U : 8U;

        cpu_808x_test_machine_create(&machine, &config, program,
                                     sizeof(program));
        start_program(&machine);
        cpu_808x_test_poke(&machine, 0x0080U, 0x34U);
        cpu_808x_test_poke(&machine, 0x0081U, 0x12U);
        cpu_808x_test_poke(&machine, 0x0082U, 0x00U);
        cpu_808x_test_poke(&machine, 0x0083U, 0x20U);
        state = cpu_808x_test_get_state(&machine);
        state.ss = 0U;
        state.sp = initial_stacks[index];
        state.flags = (uint16_t) (state.flags | TEST_FLAG_IF);
        cpu_808x_test_set_state(&machine, &state);

        step_once(&machine, &capture);
        state = cpu_808x_test_get_state(&machine);
        assert(state.ip == 0x1234U);
        assert(state.cs == 0x2000U);
        assert(state.sp == (uint16_t) (initial_stacks[index] - 6U));
        assert((state.flags & TEST_FLAG_IF) == 0U);
        assert_exact_execution_clocks(&capture, clocks);
        assert(capture.last.boundary_clock_kind ==
               BM_808X_EXECUTION_CLOCKS_EXACT);
        assert(capture.last.operand_transactions == transactions);
        assert(capture.last.prefetch_queue_flushed == 1U);
        assert(capture.last.execution_timeline_complete == 1U);
        assert(capture.last.execution_clocks_placed == clocks);
        cpu_808x_test_machine_destroy(&machine);
    }
}

static void
test_interrupt_latches_vector_before_writing_stack(void)
{
    static const uint8_t program[] = { 0xcdU, 0x20U }; /* INT 20h. */
    timing_capture_t capture = { 0 };
    cpu_808x_test_config_t config = {
        .timing = capture_timing,
        .timing_context = &capture
    };
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;

    cpu_808x_test_machine_create(&machine, &config, program, sizeof(program));
    start_program(&machine);
    cpu_808x_test_poke(&machine, 0x0080U, 0x34U);
    cpu_808x_test_poke(&machine, 0x0081U, 0x12U);
    cpu_808x_test_poke(&machine, 0x0082U, 0x00U);
    cpu_808x_test_poke(&machine, 0x0083U, 0x20U);
    state = cpu_808x_test_get_state(&machine);
    state.ss = 0U;
    state.sp = 0x0082U; /* The first push overwrites vector 20h's offset. */
    cpu_808x_test_set_state(&machine, &state);

    step_once(&machine, &capture);
    state = cpu_808x_test_get_state(&machine);
    assert(state.ip == 0x1234U);
    assert(state.cs == 0x2000U);
    assert(state.sp == 0x007cU);
    assert_exact_execution_clocks(&capture, 38U);
    assert(capture.last.boundary_clock_kind ==
           BM_808X_EXECUTION_CLOCKS_EXACT);
    assert(capture.last.execution_timeline_complete == 1U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_interrupt_return_has_a_complete_timeline(void)
{
    static const uint8_t program[] = { 0xcfU }; /* IRET. */
    const uint16_t initial_stacks[] = { 0x0100U, 0x0101U };
    size_t index;

    for (index = 0U;
         index < sizeof(initial_stacks) / sizeof(initial_stacks[0]);
         ++index) {
        timing_capture_t capture = { 0 };
        cpu_808x_test_config_t config = {
            .timing = capture_timing,
            .timing_context = &capture
        };
        cpu_808x_test_machine_t machine;
        bm_808x_arch_state_t state;
        uint16_t stack = initial_stacks[index];
        uint32_t clocks = index == 0U ? 27U : 39U;
        uint64_t transactions = index == 0U ? 3U : 6U;

        cpu_808x_test_machine_create(&machine, &config, program,
                                     sizeof(program));
        start_program(&machine);
        state = cpu_808x_test_get_state(&machine);
        state.ss = 0U;
        state.sp = stack;
        cpu_808x_test_set_state(&machine, &state);
        cpu_808x_test_poke(&machine, stack, 0x34U);
        cpu_808x_test_poke(&machine, (uint16_t) (stack + 1U), 0x12U);
        cpu_808x_test_poke(&machine, (uint16_t) (stack + 2U), 0x00U);
        cpu_808x_test_poke(&machine, (uint16_t) (stack + 3U), 0x20U);
        cpu_808x_test_poke(&machine, (uint16_t) (stack + 4U), 0x02U);
        cpu_808x_test_poke(&machine, (uint16_t) (stack + 5U), 0x72U);

        step_once(&machine, &capture);
        state = cpu_808x_test_get_state(&machine);
        assert(state.ip == 0x1234U);
        assert(state.cs == 0x2000U);
        assert(state.sp == (uint16_t) (stack + 6U));
        assert_exact_execution_clocks(&capture, clocks);
        assert(capture.last.boundary_clock_kind ==
               BM_808X_EXECUTION_CLOCKS_EXACT);
        assert(capture.last.operand_transactions == transactions);
        assert(capture.last.prefetch_queue_flushed == 1U);
        assert(capture.last.execution_timeline_complete == 1U);
        assert(capture.last.execution_clocks_placed == clocks);
        cpu_808x_test_machine_destroy(&machine);
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
    assert(capture.last.boundary_clock_kind ==
           BM_808X_EXECUTION_CLOCKS_EXACT);
    assert(capture.last.operand_transactions == 1U);
    assert(capture.last.prefetch_queue_flushed == 1U);
    assert(capture.last.execution_timeline_complete == 1U);
    assert(capture.last.execution_clocks_placed == 20U);
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
        if (index == 1U)
            assert_boundary_clock_range(&capture, 44U, 49U);
        else if (index == 3U)
            assert_boundary_clock_range(&capture, 37U, 43U);
        else if (index == 4U)
            assert_boundary_clock_range(&capture, 49U, 55U);
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

static void
test_unsigned_multiply_resolves_its_data_dependent_clock(void)
{
    const struct {
        uint8_t program[2];
        uint16_t ax;
        uint16_t dx;
        uint32_t execution_clocks;
        int memory_operand;
    } cases[] = {
        { { 0xf6U, 0xe3U }, 0x0002U, 0U, 22U, 0 },
        { { 0xf6U, 0xe3U }, 0x0080U, 0U, 21U, 0 },
        { { 0xf6U, 0x27U }, 0x0002U, 0U, 28U, 1 },
        { { 0xf6U, 0x27U }, 0x0080U, 0U, 27U, 1 },
        { { 0xf7U, 0xe3U }, 0x0002U, 0U, 30U, 0 },
        { { 0xf7U, 0xe3U }, 0x8000U, 0U, 29U, 0 }
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
                                     sizeof(cases[index].program));
        start_program(&machine);
        state = cpu_808x_test_get_state(&machine);
        state.ax = cases[index].ax;
        state.bx = cases[index].memory_operand ? 0x0100U : 2U;
        state.dx = cases[index].dx;
        cpu_808x_test_set_state(&machine, &state);
        if (cases[index].memory_operand)
            cpu_808x_test_poke(&machine, 0x0100U, 2U);
        step_once(&machine, &capture);
        assert_exact_execution_clocks(&capture,
                                      cases[index].execution_clocks);
        assert(capture.last.boundary_clock_kind ==
               BM_808X_EXECUTION_CLOCKS_EXACT);
        cpu_808x_test_machine_destroy(&machine);
    }
}

static void
test_stos_scas_have_a_complete_timeline(void)
{
    const struct {
        uint8_t program[2];
        size_t size;
        uint16_t cx;
        uint16_t di;
        uint16_t ax;
        uint32_t execution_clocks;
        uint64_t operand_transactions;
        int comparison;
    } cases[] = {
        { { 0xabU, 0U }, 1U, 5U, 0x0041U, 0x1234U, 11U, 2U, 0 },
        { { 0xf3U, 0xabU }, 2U, 3U, 0x0040U, 0x1234U, 19U, 3U, 0 },
        { { 0xafU, 0U }, 1U, 5U, 0x0041U, 0x1234U, 11U, 2U, 1 },
        { { 0xf3U, 0xaeU }, 2U, 3U, 0x0040U, 0x0034U, 17U, 1U, 1 },
        { { 0xf3U, 0xabU }, 2U, 0U, 0x0040U, 0x1234U, 7U, 0U, 0 }
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
        state.es = 0x2000U;
        state.cx = cases[index].cx;
        state.di = cases[index].di;
        state.ax = cases[index].ax;
        cpu_808x_test_set_state(&machine, &state);
        if (cases[index].comparison) {
            cpu_808x_test_poke(&machine,
                               0x20000U + cases[index].di, 0x78U);
            cpu_808x_test_poke(&machine,
                               0x20000U + cases[index].di + 1U, 0x56U);
        }

        step_once(&machine, &capture);
        assert_exact_execution_clocks(&capture,
                                      cases[index].execution_clocks);
        assert(capture.last.boundary_clock_kind ==
               BM_808X_EXECUTION_CLOCKS_EXACT);
        assert(capture.last.operand_transactions ==
               cases[index].operand_transactions);
        assert(capture.last.execution_timeline_complete == 1U);
        assert(capture.last.execution_clocks_placed ==
               cases[index].execution_clocks);
        cpu_808x_test_machine_destroy(&machine);
    }
}

static void
test_movs_lods_have_a_complete_timeline(void)
{
    const struct {
        uint8_t program[3];
        size_t size;
        uint16_t cx;
        uint16_t si;
        uint16_t di;
        uint32_t execution_clocks;
        uint64_t operand_transactions;
        int loads_accumulator;
    } cases[] = {
        { { 0xa4U, 0U, 0U }, 1U, 7U, 0x0040U, 0x0060U,
          11U, 2U, 0 },
        { { 0xf3U, 0xa4U, 0U }, 2U, 3U, 0x0040U, 0x0060U,
          35U, 6U, 0 },
        { { 0xf3U, 0xa5U, 0U }, 2U, 2U, 0x0041U, 0x0060U,
          35U, 6U, 0 },
        { { 0x2eU, 0xf3U, 0xa4U }, 3U, 2U, 0x0040U, 0x0060U,
          29U, 4U, 0 },
        { { 0xadU, 0U, 0U }, 1U, 7U, 0x0041U, 0x0060U,
          11U, 2U, 1 },
        { { 0xf3U, 0xadU, 0U }, 2U, 2U, 0x0041U, 0x0060U,
          33U, 4U, 1 },
        { { 0xf3U, 0xa4U, 0U }, 2U, 0U, 0x0040U, 0x0060U,
          11U, 0U, 0 }
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
        state.cs = 0xf000U;
        state.ds = 0x1000U;
        state.es = 0x2000U;
        state.cx = cases[index].cx;
        state.si = cases[index].si;
        state.di = cases[index].di;
        cpu_808x_test_set_state(&machine, &state);
        cpu_808x_test_poke(&machine, 0x10000U + cases[index].si, 0x34U);
        cpu_808x_test_poke(&machine,
                           0x10000U + cases[index].si + 1U, 0x12U);
        cpu_808x_test_poke(&machine,
                           0x10000U + cases[index].si + 2U, 0x34U);
        cpu_808x_test_poke(&machine,
                           0x10000U + cases[index].si + 3U, 0x12U);
        cpu_808x_test_poke(&machine, 0xf0000U + cases[index].si, 0x34U);
        cpu_808x_test_poke(&machine,
                           0xf0000U + cases[index].si + 1U, 0x34U);

        step_once(&machine, &capture);
        state = cpu_808x_test_get_state(&machine);
        if (cases[index].loads_accumulator)
            assert(state.ax == 0x1234U);
        assert_exact_execution_clocks(&capture,
                                      cases[index].execution_clocks);
        assert(capture.last.boundary_clock_kind ==
               BM_808X_EXECUTION_CLOCKS_EXACT);
        assert(capture.last.operand_transactions ==
               cases[index].operand_transactions);
        assert(capture.last.execution_timeline_complete == 1U);
        assert(capture.last.execution_clocks_placed ==
               cases[index].execution_clocks);
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
    const struct {
        bm_808x_signal_t signal;
        uint16_t sp;
        uint32_t clocks;
        uint64_t transactions;
        uint64_t bus_clocks;
    } cases[] = {
        { BM_808X_SIGNAL_INT, 0x0100U, 49U, 5U, 20U },
        { BM_808X_SIGNAL_INT, 0x0101U, 61U, 8U, 32U },
        { BM_808X_SIGNAL_NMI, 0x0100U, 38U, 5U, 20U },
        { BM_808X_SIGNAL_NMI, 0x0101U, 50U, 8U, 32U }
    };
    size_t index;

    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        timing_capture_t capture = { 0 };
        cpu_808x_test_config_t config = {
            .timing = capture_timing,
            .timing_context = &capture,
            .interrupt_ack = acknowledge_interrupt
        };
        cpu_808x_test_machine_t machine;
        bm_808x_arch_state_t state;
        uint64_t cycles = 0U;
        uint16_t vector_address =
            cases[index].signal == BM_808X_SIGNAL_INT ? 0x0080U : 0x0008U;

        cpu_808x_test_machine_create(&machine, &config, program,
                                     sizeof(program));
        start_program(&machine);
        cpu_808x_test_poke(&machine, vector_address, 0x34U);
        cpu_808x_test_poke(&machine, vector_address + 1U, 0x12U);
        cpu_808x_test_poke(&machine, vector_address + 2U, 0x00U);
        cpu_808x_test_poke(&machine, vector_address + 3U, 0x20U);
        state = cpu_808x_test_get_state(&machine);
        state.ss = 0x1000U;
        state.sp = cases[index].sp;
        state.flags |= TEST_FLAG_IF;
        cpu_808x_test_set_state(&machine, &state);
        assert(bm_engine_signal_cpu(machine.engine, 0U,
                                    cases[index].signal, 1) == BM_STATUS_OK);
        assert(bm_808x_step_clocked(machine.cpu.context, 0U, &cycles) ==
               BM_STATUS_OK);
        assert(capture.count == 1U);
        assert(capture.last.kind == BM_808X_BOUNDARY_INTERRUPT);
        assert_exact_execution_clocks(&capture, cases[index].clocks);
        assert(capture.last.boundary_clock_kind ==
               BM_808X_EXECUTION_CLOCKS_EXACT);
        assert(cycles == capture.last.boundary_clocks_min);
        assert(cycles == cases[index].clocks +
                         capture.last.prefetch_handoff_clocks);
        assert(capture.last.prefetch_queue_flushed == 1U);
        assert(capture.last.prefetch_pointer_known == 1U);
        assert(capture.last.prefetch_pointer == 0x1236U);
        assert(capture.last.operand_transactions ==
               cases[index].transactions);
        assert(capture.last.operand_bus_clocks == cases[index].bus_clocks);
        assert(capture.last.demand_prefetch_transactions == 0U);
        assert(capture.last.demand_prefetch_bus_clocks == 0U);
        assert(capture.last.instruction_queue_reads == 0U);
        assert(capture.last.prefetch_queue_count == 2U);
        assert(capture.last.prefetch_phase == BM_808X_PREFETCH_IDLE);
        assert(capture.last.execution_timeline_complete == 1U);
        assert(capture.last.execution_clocks_placed == cases[index].clocks);
        cpu_808x_test_machine_destroy(&machine);
    }
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
    test_accumulator_test_has_documented_timing();
    test_fixed_execution_clocks_and_prefix_cost();
    test_operand_timeline_can_finish_inflight_prefetch();
    test_taken_branch_flushes_even_when_target_is_sequential();
    test_bus_waits_extend_boundary_but_not_execution_clocks();
    test_direct_io_has_a_complete_execution_timeline();
    test_direct_memory_moves_have_a_complete_execution_timeline();
    test_memory_timing_uses_operand_form_and_alignment();
    test_modrm_moves_have_a_complete_execution_timeline();
    test_modrm_alu_reads_have_a_complete_execution_timeline();
    test_modrm_alu_writes_have_a_complete_execution_timeline();
    test_modrm_test_and_exchange_have_a_complete_execution_timeline();
    test_immediate_alu_groups_have_a_complete_execution_timeline();
    test_special_moves_have_a_complete_execution_timeline();
    test_group3_memory_operands_have_a_placed_timeline();
    test_inc_dec_memory_operands_have_a_complete_timeline();
    test_single_word_stack_operations_have_a_complete_timeline();
    test_counted_and_stack_timing();
    test_data_dependent_arithmetic_reports_documented_ranges();
    test_unsigned_multiply_resolves_its_data_dependent_clock();
    test_divide_error_does_not_claim_normal_execution_clocks();
    test_scalar_formula_and_condition_timing();
    test_nec_extension_timing();
    test_standard_string_formula_timing();
    test_io_string_formula_timing();
    test_stos_scas_have_a_complete_timeline();
    test_movs_lods_have_a_complete_timeline();
    test_interrupted_string_timing_remains_unknown();
    test_far_pointer_loads_have_a_complete_timeline();
    test_near_call_and_return_have_a_complete_timeline();
    test_indirect_near_call_has_a_complete_timeline();
    test_clocked_step_requires_an_exact_scalar_boundary();
    test_software_interrupt_has_a_complete_timeline();
    test_interrupt_latches_vector_before_writing_stack();
    test_interrupt_return_has_a_complete_timeline();
    test_interrupt_boundary_reports_queue_flush();
    test_prefetched_byte_survives_later_memory_write();
    test_odd_prefetch_and_pointer_wrap();
    test_taken_branch_discards_sequential_prefetch();
    return 0;
}
