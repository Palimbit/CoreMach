/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Synthetic contract tests for documented V30 execution clocks, portable bus
 * wait reporting and instruction-queue invalidation. No firmware is used.
 */
#include "cpu_808x_test_harness.h"

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
    } cases[] = {
        { nop, sizeof(nop), 0x90U, 0U, 3U, 1U },
        { prefixed_nop, sizeof(prefixed_nop), 0x2eU, 1U, 5U, 2U }
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
        assert(capture.last.execution_clocks_known == 1U);
        assert(capture.last.execution_clocks == cases[index].clocks);
        assert(capture.last.logical_bus_transactions ==
               cases[index].bus_transactions);
        assert(capture.last.reported_wait_states == 0U);
        assert(capture.last.prefetch_queue_flushed == 0U);
        assert(capture.last.prefetch_pointer_known == 0U);
        cpu_808x_test_machine_destroy(&machine);
    }
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
    assert(capture.last.execution_clocks_known == 1U);
    assert(capture.last.execution_clocks == 14U);
    assert(capture.last.prefetch_queue_flushed == 1U);
    assert(capture.last.prefetch_pointer_known == 1U);
    assert(capture.last.prefetch_pointer == 2U);
    cpu_808x_test_machine_destroy(&machine);

    memset(&capture, 0, sizeof(capture));
    cpu_808x_test_machine_create(&machine, &config, program, sizeof(program));
    start_program(&machine);
    step_once(&machine, &capture);
    assert(capture.last.execution_clocks_known == 1U);
    assert(capture.last.execution_clocks == 4U);
    assert(capture.last.prefetch_queue_flushed == 0U);
    assert(capture.last.prefetch_pointer_known == 0U);
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

static void
test_bus_waits_are_reported_but_not_folded_into_execution_clocks(void)
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
    assert(capture.last.execution_clocks_known == 1U);
    assert(capture.last.execution_clocks == 9U);
    assert(capture.last.logical_bus_transactions == 3U);
    assert(capture.last.reported_wait_states == 3U);
    cpu_808x_test_machine_destroy(&machine);
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
    assert(capture.last.execution_clocks_known == 1U);
    assert(capture.last.execution_clocks == 9U);
    assert(capture.last.logical_bus_transactions == 3U);
    assert(cpu_808x_test_peek(&machine, 0x10040U) == 0x5aU);
    cpu_808x_test_machine_destroy(&machine);

    {
        static const uint8_t add_word[] = { 0x01U, 0x07U }; /* ADD [BW],AW. */
        unsigned int odd;

        for (odd = 0U; odd <= 1U; ++odd) {
            memset(&capture, 0, sizeof(capture));
            cpu_808x_test_machine_create(&machine, &config, add_word,
                                         sizeof(add_word));
            start_program(&machine);
            state = cpu_808x_test_get_state(&machine);
            state.ds = 0x1000U;
            state.bx = (uint16_t) (0x0040U + odd);
            state.ax = 1U;
            cpu_808x_test_set_state(&machine, &state);
            cpu_808x_test_poke(&machine, 0x10040U + odd, 2U);
            cpu_808x_test_poke(&machine, 0x10041U + odd, 0U);
            step_once(&machine, &capture);
            assert(capture.last.execution_clocks_known == 1U);
            assert(capture.last.execution_clocks == (odd ? 24U : 16U));
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
    assert(capture.last.execution_clocks_known == 1U);
    assert(capture.last.execution_clocks == 12U);
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
    assert(capture.last.execution_clocks_known == 1U);
    assert(capture.last.execution_clocks == 9U);
    cpu_808x_test_machine_destroy(&machine);

    memset(&capture, 0, sizeof(capture));
    cpu_808x_test_machine_create(&machine, &config, push, sizeof(push));
    start_program(&machine);
    state = cpu_808x_test_get_state(&machine);
    state.ss = 0x1000U;
    state.sp = 0x0101U;
    cpu_808x_test_set_state(&machine, &state);
    step_once(&machine, &capture);
    assert(capture.last.execution_clocks_known == 1U);
    assert(capture.last.execution_clocks == 12U);
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
    assert(capture.last.execution_clocks_known == 1U);
    assert(capture.last.execution_clocks == 20U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_data_dependent_timing_is_explicit(void)
{
    static const uint8_t program[] = { 0xf6U, 0xe8U }; /* IMUL AL. */
    timing_capture_t capture = { 0 };
    cpu_808x_test_config_t config = {
        .timing = capture_timing,
        .timing_context = &capture
    };
    cpu_808x_test_machine_t machine;

    cpu_808x_test_machine_create(&machine, &config, program, sizeof(program));
    start_program(&machine);
    step_once(&machine, &capture);
    assert(capture.last.execution_clocks_known == 0U);
    assert(capture.last.execution_clocks == 0U);
    cpu_808x_test_machine_destroy(&machine);
}

static bm_status_t
acknowledge_interrupt(void *context, uint8_t *vector)
{
    (void) context;
    *vector = 0x20U;
    return BM_STATUS_OK;
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
    assert(capture.last.execution_clocks_known == 0U);
    assert(capture.last.prefetch_queue_flushed == 1U);
    assert(capture.last.prefetch_pointer_known == 1U);
    assert(capture.last.prefetch_pointer == 0x1234U);
    assert(capture.last.logical_bus_transactions == 10U);
    cpu_808x_test_machine_destroy(&machine);
}

int
main(void)
{
    test_fixed_execution_clocks_and_prefix_cost();
    test_taken_branch_flushes_even_when_target_is_sequential();
    test_bus_waits_are_reported_but_not_folded_into_execution_clocks();
    test_memory_timing_uses_operand_form_and_alignment();
    test_counted_and_stack_timing();
    test_data_dependent_timing_is_explicit();
    test_interrupt_boundary_reports_queue_flush();
    return 0;
}
