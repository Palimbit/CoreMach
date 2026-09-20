/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "cpu_808x_test_harness.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define FLAG_CF 0x0001U
#define FLAG_IF 0x0200U

typedef struct bus_event {
    bm_bus_operation_t operation;
    uint64_t address;
    uint32_t attributes;
} bus_event_t;

typedef struct bus_capture {
    bus_event_t events[64];
    size_t count;
    bm_cpu_t *cpu;
    uint64_t assert_interrupt_on_write;
    int interrupt_asserted;
} bus_capture_t;

static void
capture_bus(void *context, const bm_bus_transaction_t *transaction)
{
    bus_capture_t *capture = context;

    assert(capture->count < sizeof(capture->events) /
                            sizeof(capture->events[0]));
    capture->events[capture->count++] = (bus_event_t) {
        transaction->operation, transaction->address, transaction->attributes
    };
    if (!capture->interrupt_asserted && (capture->cpu != NULL) &&
        (transaction->operation == BM_BUS_WRITE) &&
        (transaction->address == capture->assert_interrupt_on_write)) {
        assert(capture->cpu->ops.signal(capture->cpu->context, 0U, 1) ==
               BM_STATUS_OK);
        capture->interrupt_asserted = 1;
    }
}

static bm_status_t
acknowledge_interrupt(void *context, uint8_t *vector)
{
    (void) context;
    assert(vector != NULL);
    *vector = 0x20U;
    return BM_STATUS_OK;
}

static void
set_native_state(cpu_808x_test_machine_t *machine)
{
    bm_808x_arch_state_t state = cpu_808x_test_get_state(machine);

    state.cs = 0xf000U;
    state.ip = 0U;
    state.ss = 0U;
    state.sp = 0x0800U;
    state.ds = 0U;
    state.es = 0U;
    state.flags = 0xf002U;
    state.halted = 0U;
    state.interrupt_inhibit = 0U;
    cpu_808x_test_set_state(machine, &state);
}

static void
test_buslock_marks_following_instruction(void)
{
    static const uint8_t program[] = {
        0xbbU, 0x00U, 0x01U,       /* MOV BX,0100h. */
        0xf0U, 0xfeU, 0x07U,       /* BUSLOCK INC byte ptr [BX]. */
        0xf4U
    };
    cpu_808x_test_machine_t machine;
    bus_capture_t capture = { 0 };
    bm_tick_t consumed = 0U;
    size_t index;

    cpu_808x_test_machine_create(&machine, NULL, program, sizeof(program));
    set_native_state(&machine);
    cpu_808x_test_poke(&machine, 0x0100U, 0x2aU);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    assert(consumed == 1U);

    bm_bus_set_observer(machine.bus, capture_bus, &capture);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    assert(consumed == 1U);
    assert(cpu_808x_test_peek(&machine, 0x0100U) == 0x2bU);
    /* The pending instruction fetch started while the preceding MOV executed
     * and completes as the locked instruction consumes its queue. The placed
     * BUSLOCK prefix interval then admits the next speculative fetch before
     * the operand request. Instruction fetch never belongs to the BUSLOCK
     * window; only the operand cycles do. */
    assert(capture.count == 4U);
    assert(capture.events[0].operation == BM_BUS_FETCH &&
           capture.events[0].address == 0xf0006U &&
           capture.events[0].attributes == 0U);
    assert(capture.events[1].operation == BM_BUS_FETCH &&
           capture.events[1].address == 0xf0008U &&
           capture.events[1].attributes == 0U);
    for (index = 2U; index < capture.count; ++index)
        assert((capture.events[index].attributes &
                BM_BUS_TRANSACTION_LOCKED) != 0U);
    assert(capture.events[2].operation == BM_BUS_READ &&
           capture.events[2].address == 0x0100U);
    assert(capture.events[3].operation == BM_BUS_WRITE &&
           capture.events[3].address == 0x0100U);

    memset(&capture, 0, sizeof(capture));
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_IDLE);
    assert(consumed == 1U && capture.count == 1U);
    assert(capture.events[0].operation == BM_BUS_FETCH &&
           capture.events[0].address == 0xf000aU &&
           capture.events[0].attributes == 0U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_buslock_covers_repeated_block(void)
{
    static const uint8_t program[] = { 0xf0U, 0xf3U, 0xa4U, 0x90U };
    static const uint8_t source[] = { 0x11U, 0x22U, 0x33U };
    static const uint8_t vector[] = { 0x00U, 0x01U, 0x00U, 0xf0U };
    cpu_808x_test_config_t config = { 0 };
    cpu_808x_test_machine_t machine;
    bus_capture_t capture = { 0 };
    bm_808x_arch_state_t state;
    bm_tick_t consumed = 0U;
    size_t index;

    config.interrupt_ack = acknowledge_interrupt;
    cpu_808x_test_machine_create(&machine, &config, program, sizeof(program));
    cpu_808x_test_write(&machine, 0x0100U, source, sizeof(source));
    cpu_808x_test_write(&machine, 0x0080U, vector, sizeof(vector));
    set_native_state(&machine);
    state = cpu_808x_test_get_state(&machine);
    state.cx = 3U;
    state.si = 0x0100U;
    state.di = 0x0200U;
    state.flags |= FLAG_IF;
    cpu_808x_test_set_state(&machine, &state);

    capture.cpu = &machine.cpu;
    capture.assert_interrupt_on_write = 0x0200U;
    bm_bus_set_observer(machine.bus, capture_bus, &capture);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(consumed == 1U && capture.interrupt_asserted);
    assert(state.ip == 3U && state.cx == 0U);
    assert(state.si == 0x0103U && state.di == 0x0203U);
    assert(cpu_808x_test_peek(&machine, 0x0200U) == 0x11U);
    assert(cpu_808x_test_peek(&machine, 0x0201U) == 0x22U);
    assert(cpu_808x_test_peek(&machine, 0x0202U) == 0x33U);
    assert(capture.events[0].address == 0xf0000U &&
           capture.events[0].attributes == 0U);
    for (index = 0U; index < capture.count; ++index) {
        if (capture.events[index].operation == BM_BUS_FETCH)
            assert(capture.events[index].attributes == 0U);
        else
            assert((capture.events[index].attributes &
                    BM_BUS_TRANSACTION_LOCKED) != 0U);
    }

    memset(&capture, 0, sizeof(capture));
    bm_bus_set_observer(machine.bus, capture_bus, &capture);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(consumed == 1U && state.cs == 0xf000U && state.ip == 0x0100U);
    for (index = 0U; index < capture.count; ++index)
        assert((capture.events[index].attributes &
                BM_BUS_TRANSACTION_LOCKED) == 0U);
    cpu_808x_test_machine_destroy(&machine);
}

static bm_808x_arch_state_t
run_carry_repeat(const uint8_t *program,
                 size_t program_size,
                 const uint8_t *source,
                 const uint8_t *destination,
                 uint16_t ax)
{
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;
    bm_tick_t consumed = 0U;

    cpu_808x_test_machine_create(&machine, NULL, program, program_size);
    cpu_808x_test_write(&machine, 0x0100U, source, 4U);
    cpu_808x_test_write(&machine, 0x0200U, destination, 4U);
    set_native_state(&machine);
    state = cpu_808x_test_get_state(&machine);
    state.ax = ax;
    state.cx = 4U;
    state.si = 0x0100U;
    state.di = 0x0200U;
    cpu_808x_test_set_state(&machine, &state);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    assert(consumed == 1U);
    state = cpu_808x_test_get_state(&machine);
    cpu_808x_test_machine_destroy(&machine);
    return state;
}

static void
test_repc_and_repnc_compare_conditions(void)
{
    static const uint8_t repc_cmps[] = { 0x65U, 0xa6U };
    static const uint8_t repnc_cmps[] = { 0x64U, 0xa6U };
    static const uint8_t repc_scas[] = { 0x65U, 0xaeU };
    static const uint8_t repc_source[] = { 1U, 2U, 5U, 6U };
    static const uint8_t repc_destination[] = { 2U, 3U, 4U, 7U };
    static const uint8_t repnc_source[] = { 3U, 4U, 1U, 0U };
    static const uint8_t repnc_destination[] = { 2U, 3U, 2U, 1U };
    static const uint8_t unused_source[] = { 0U, 0U, 0U, 0U };
    static const uint8_t scas_destination[] = { 2U, 3U, 0U, 4U };
    bm_808x_arch_state_t state = run_carry_repeat(
        repc_cmps, sizeof(repc_cmps), repc_source, repc_destination, 0U);

    assert(state.cx == 1U && state.si == 0x0103U && state.di == 0x0203U);
    assert((state.flags & FLAG_CF) == 0U);

    state = run_carry_repeat(repnc_cmps, sizeof(repnc_cmps), repnc_source,
                             repnc_destination, 0U);
    assert(state.cx == 1U && state.si == 0x0103U && state.di == 0x0203U);
    assert((state.flags & FLAG_CF) != 0U);

    state = run_carry_repeat(repc_scas, sizeof(repc_scas), unused_source,
                             scas_destination, 1U);
    assert(state.cx == 1U && state.si == 0x0100U && state.di == 0x0203U);
    assert((state.flags & FLAG_CF) == 0U);
}

static void
test_prefix_selection_and_rejections(void)
{
    static const uint8_t accepted_last_rep[] = { 0x65U, 0xf3U, 0xa4U };
    static const uint8_t rejected_last_repc[] = { 0xf3U, 0x65U, 0xa4U };
    static const uint8_t rejected_repc_movs[] = { 0x65U, 0xa4U };
    static const uint8_t rejected_repnc_nop[] = { 0x64U, 0x90U };
    static const uint8_t rejected_undefined[] = { 0xf1U };
    static const uint8_t source[] = { 0x5aU, 0xa5U };
    const struct {
        const uint8_t *program;
        size_t size;
        uint16_t expected_ip;
    } rejected[] = {
        { rejected_last_repc, sizeof(rejected_last_repc), 3U },
        { rejected_repc_movs, sizeof(rejected_repc_movs), 2U },
        { rejected_repnc_nop, sizeof(rejected_repnc_nop), 2U },
        { rejected_undefined, sizeof(rejected_undefined), 1U }
    };
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;
    bm_tick_t consumed = 99U;
    size_t index;

    cpu_808x_test_machine_create(&machine, NULL, accepted_last_rep,
                                  sizeof(accepted_last_rep));
    cpu_808x_test_write(&machine, 0x0100U, source, sizeof(source));
    set_native_state(&machine);
    state = cpu_808x_test_get_state(&machine);
    state.cx = 2U;
    state.si = 0x0100U;
    state.di = 0x0200U;
    cpu_808x_test_set_state(&machine, &state);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    assert(consumed == 1U && cpu_808x_test_peek(&machine, 0x0200U) == 0x5aU);
    assert(cpu_808x_test_peek(&machine, 0x0201U) == 0xa5U);
    cpu_808x_test_machine_destroy(&machine);

    for (index = 0U; index < sizeof(rejected) / sizeof(rejected[0]); ++index) {
        cpu_808x_test_machine_create(&machine, NULL, rejected[index].program,
                                      rejected[index].size);
        set_native_state(&machine);
        state = cpu_808x_test_get_state(&machine);
        state.cx = 2U;
        state.si = 0x0100U;
        state.di = 0x0200U;
        cpu_808x_test_set_state(&machine, &state);
        consumed = 99U;
        assert(cpu_808x_test_step(&machine, &consumed) ==
               BM_STATUS_UNSUPPORTED);
        state = cpu_808x_test_get_state(&machine);
        assert(consumed == 0U && state.ip == rejected[index].expected_ip);
        assert(state.cx == 2U && state.si == 0x0100U && state.di == 0x0200U);
        cpu_808x_test_machine_destroy(&machine);
    }
}

int
main(void)
{
    test_buslock_marks_following_instruction();
    test_buslock_covers_repeated_block();
    test_repc_and_repnc_compare_conditions();
    test_prefix_selection_and_rejections();
    return 0;
}
