/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Synthetic V30 FPO1/FPO2/POLL contract tests. The encodings and CPU-side
 * effects follow the NEC 16-Bit V Series Instruction User's Manual. No
 * firmware, floating-point implementation or guest media is used here.
 */
#include "cpu_808x_test_harness.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct fake_coprocessor {
    bm_808x_fpo_request_t requests[16];
    size_t request_count;
    bm_status_t fpo_status;
    int poll_results[8];
    size_t poll_result_count;
    size_t poll_count;
    bm_status_t poll_status;
} fake_coprocessor_t;

static bm_status_t
record_fpo(void *context, const bm_808x_fpo_request_t *request)
{
    fake_coprocessor_t *coprocessor = context;

    assert(coprocessor != NULL);
    assert(request != NULL);
    assert(request->size == sizeof(*request));
    assert(coprocessor->request_count <
           sizeof(coprocessor->requests) / sizeof(coprocessor->requests[0]));
    coprocessor->requests[coprocessor->request_count++] = *request;
    return coprocessor->fpo_status;
}

static bm_status_t
poll_coprocessor(void *context, int *ready)
{
    fake_coprocessor_t *coprocessor = context;
    size_t index;

    assert(coprocessor != NULL);
    assert(ready != NULL);
    index = coprocessor->poll_count++;
    if (coprocessor->poll_status != BM_STATUS_OK)
        return coprocessor->poll_status;
    assert(index < coprocessor->poll_result_count);
    *ready = coprocessor->poll_results[index];
    return BM_STATUS_OK;
}

static bm_808x_arch_state_t
execution_state(cpu_808x_test_machine_t *machine)
{
    bm_808x_arch_state_t state = cpu_808x_test_get_state(machine);
    state.cs = 0xf000U;
    state.ds = 0x1000U;
    state.es = 0x2000U;
    state.ss = 0x3000U;
    state.ip = 0U;
    state.sp = 0x0100U;
    state.bp = 0x1230U;
    state.flags = 0xf8d7U;
    return state;
}

static void
step_ok(cpu_808x_test_machine_t *machine)
{
    bm_tick_t consumed = 0U;
    assert(cpu_808x_test_step(machine, &consumed) == BM_STATUS_OK);
    assert(consumed == 1U);
}

static void
test_all_register_encodings(void)
{
    unsigned int opcode;

    for (opcode = 0xd8U; opcode <= 0xdfU; ++opcode) {
        uint8_t program[] = { (uint8_t) opcode, 0xc5U };
        fake_coprocessor_t coprocessor;
        cpu_808x_test_config_t config;
        cpu_808x_test_machine_t machine;
        bm_808x_arch_state_t state;

        memset(&coprocessor, 0, sizeof(coprocessor));
        memset(&config, 0, sizeof(config));
        config.fpo = record_fpo;
        config.coprocessor_context = &coprocessor;
        cpu_808x_test_machine_create(&machine, &config, program,
                                     sizeof(program));
        state = execution_state(&machine);
        cpu_808x_test_set_state(&machine, &state);
        step_ok(&machine);
        state = cpu_808x_test_get_state(&machine);
        assert(state.ip == sizeof(program));
        assert(state.flags == 0xf8d7U);
        assert(coprocessor.request_count == 1U);
        assert(coprocessor.requests[0].family == BM_808X_FPO1);
        assert(coprocessor.requests[0].opcode == opcode);
        assert(coprocessor.requests[0].modrm == 0xc5U);
        assert(coprocessor.requests[0].memory_operand == 0U);
        assert(coprocessor.requests[0].physical_address == 0U);
        assert(coprocessor.requests[0].memory_value == 0U);
        cpu_808x_test_machine_destroy(&machine);
    }

    for (opcode = 0x66U; opcode <= 0x67U; ++opcode) {
        uint8_t program[] = { (uint8_t) opcode, 0xffU };
        fake_coprocessor_t coprocessor;
        cpu_808x_test_config_t config;
        cpu_808x_test_machine_t machine;
        bm_808x_arch_state_t state;

        memset(&coprocessor, 0, sizeof(coprocessor));
        memset(&config, 0, sizeof(config));
        config.fpo = record_fpo;
        config.coprocessor_context = &coprocessor;
        cpu_808x_test_machine_create(&machine, &config, program,
                                     sizeof(program));
        state = execution_state(&machine);
        cpu_808x_test_set_state(&machine, &state);
        step_ok(&machine);
        assert(coprocessor.request_count == 1U);
        assert(coprocessor.requests[0].family == BM_808X_FPO2);
        assert(coprocessor.requests[0].opcode == opcode);
        assert(coprocessor.requests[0].modrm == 0xffU);
        assert(coprocessor.requests[0].memory_operand == 0U);
        cpu_808x_test_machine_destroy(&machine);
    }
}

static void
test_memory_cycle_and_segment_override(void)
{
    static const uint8_t fpo1_direct[] = { 0xd9U, 0x06U, 0x35U, 0x12U };
    static const uint8_t fpo2_override[] = { 0x26U, 0x67U, 0x46U, 0x05U };
    static const uint8_t word[] = { 0x34U, 0x12U };
    fake_coprocessor_t coprocessor;
    cpu_808x_test_config_t config;
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;
    const bm_808x_fpo_request_t *request;

    memset(&coprocessor, 0, sizeof(coprocessor));
    memset(&config, 0, sizeof(config));
    config.fpo = record_fpo;
    config.coprocessor_context = &coprocessor;

    cpu_808x_test_machine_create(&machine, &config, fpo1_direct,
                                 sizeof(fpo1_direct));
    state = execution_state(&machine);
    cpu_808x_test_write(&machine, 0x11235U, word, sizeof(word));
    cpu_808x_test_set_state(&machine, &state);
    step_ok(&machine);
    assert(coprocessor.request_count == 1U);
    request = &coprocessor.requests[0];
    assert(request->family == BM_808X_FPO1);
    assert(request->memory_operand == 1U);
    assert(request->segment == 0x1000U);
    assert(request->offset == 0x1235U);
    assert(request->physical_address == 0x11235U);
    assert(request->memory_value == 0x1234U);
    cpu_808x_test_machine_destroy(&machine);

    memset(&coprocessor, 0, sizeof(coprocessor));
    cpu_808x_test_machine_create(&machine, &config, fpo2_override,
                                 sizeof(fpo2_override));
    state = execution_state(&machine);
    cpu_808x_test_write(&machine, 0x21235U, word, sizeof(word));
    cpu_808x_test_set_state(&machine, &state);
    step_ok(&machine);
    assert(coprocessor.request_count == 1U);
    request = &coprocessor.requests[0];
    assert(request->family == BM_808X_FPO2);
    assert(request->opcode == 0x67U);
    assert(request->memory_operand == 1U);
    assert(request->segment == 0x2000U);
    assert(request->offset == 0x1235U);
    assert(request->physical_address == 0x21235U);
    assert(request->memory_value == 0x1234U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_absent_and_failed_fpo(void)
{
    static const uint8_t memory_fpo[] = { 0xd8U, 0x06U, 0x00U, 0x20U };
    static const uint8_t register_fpo[] = { 0xd8U, 0xc0U };
    fake_coprocessor_t coprocessor;
    cpu_808x_test_config_t config;
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;
    bm_tick_t consumed = 0U;

    cpu_808x_test_machine_create(&machine, NULL, memory_fpo,
                                 sizeof(memory_fpo));
    state = execution_state(&machine);
    cpu_808x_test_poke(&machine, 0x12000U, 0xa5U);
    cpu_808x_test_set_state(&machine, &state);
    step_ok(&machine);
    state = cpu_808x_test_get_state(&machine);
    assert(state.ip == sizeof(memory_fpo));
    assert(state.flags == 0xf8d7U);
    cpu_808x_test_machine_destroy(&machine);

    memset(&coprocessor, 0, sizeof(coprocessor));
    memset(&config, 0, sizeof(config));
    coprocessor.fpo_status = BM_STATUS_DEVICE_ERROR;
    config.fpo = record_fpo;
    config.coprocessor_context = &coprocessor;
    cpu_808x_test_machine_create(&machine, &config, register_fpo,
                                 sizeof(register_fpo));
    state = execution_state(&machine);
    cpu_808x_test_set_state(&machine, &state);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_DEVICE_ERROR);
    assert(consumed == 0U);
    assert(coprocessor.request_count == 1U);
    assert(cpu_808x_test_get_state(&machine).ip == sizeof(register_fpo));
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_poll_contract(void)
{
    static const uint8_t poll[] = { 0x9bU };
    static const uint8_t locked_poll[] = { 0xf0U, 0x9bU };
    fake_coprocessor_t coprocessor;
    cpu_808x_test_config_t config;
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;
    bm_tick_t consumed = 0U;

    cpu_808x_test_machine_create(&machine, NULL, poll, sizeof(poll));
    state = execution_state(&machine);
    cpu_808x_test_set_state(&machine, &state);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_UNSUPPORTED);
    assert(consumed == 0U);
    assert(cpu_808x_test_get_state(&machine).ip == 1U);
    cpu_808x_test_machine_destroy(&machine);

    memset(&coprocessor, 0, sizeof(coprocessor));
    memset(&config, 0, sizeof(config));
    coprocessor.poll_results[0] = 0;
    coprocessor.poll_results[1] = 0;
    coprocessor.poll_results[2] = 1;
    coprocessor.poll_result_count = 3U;
    config.poll = poll_coprocessor;
    config.coprocessor_context = &coprocessor;
    cpu_808x_test_machine_create(&machine, &config, poll, sizeof(poll));
    state = execution_state(&machine);
    cpu_808x_test_set_state(&machine, &state);
    step_ok(&machine);
    assert(cpu_808x_test_get_state(&machine).ip == 0U);
    step_ok(&machine);
    assert(cpu_808x_test_get_state(&machine).ip == 0U);
    step_ok(&machine);
    assert(cpu_808x_test_get_state(&machine).ip == 1U);
    assert(coprocessor.poll_count == 3U);
    cpu_808x_test_machine_destroy(&machine);

    memset(&coprocessor, 0, sizeof(coprocessor));
    coprocessor.poll_results[0] = 1;
    coprocessor.poll_result_count = 1U;
    cpu_808x_test_machine_create(&machine, &config, locked_poll,
                                 sizeof(locked_poll));
    state = execution_state(&machine);
    cpu_808x_test_set_state(&machine, &state);
    consumed = 0U;
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_UNSUPPORTED);
    assert(consumed == 0U);
    assert(coprocessor.poll_count == 0U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_poll_errors(void)
{
    static const uint8_t poll[] = { 0x9bU };
    fake_coprocessor_t coprocessor;
    cpu_808x_test_config_t config;
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;
    bm_tick_t consumed = 0U;

    memset(&coprocessor, 0, sizeof(coprocessor));
    memset(&config, 0, sizeof(config));
    coprocessor.poll_status = BM_STATUS_DEVICE_ERROR;
    config.poll = poll_coprocessor;
    config.coprocessor_context = &coprocessor;
    cpu_808x_test_machine_create(&machine, &config, poll, sizeof(poll));
    state = execution_state(&machine);
    cpu_808x_test_set_state(&machine, &state);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_DEVICE_ERROR);
    assert(consumed == 0U);
    assert(coprocessor.poll_count == 1U);
    cpu_808x_test_machine_destroy(&machine);

    memset(&coprocessor, 0, sizeof(coprocessor));
    coprocessor.poll_results[0] = 2;
    coprocessor.poll_result_count = 1U;
    cpu_808x_test_machine_create(&machine, &config, poll, sizeof(poll));
    state = execution_state(&machine);
    cpu_808x_test_set_state(&machine, &state);
    consumed = 0U;
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_DEVICE_ERROR);
    assert(consumed == 0U);
    assert(coprocessor.poll_count == 1U);
    cpu_808x_test_machine_destroy(&machine);
}

int
main(void)
{
    test_all_register_encodings();
    test_memory_cycle_and_segment_override();
    test_absent_and_failed_fpo();
    test_poll_contract();
    test_poll_errors();
    return 0;
}
