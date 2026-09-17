/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * NEC-specific expectations are derived from hardware-generated V20 vectors:
 * https://github.com/SingleStepTests/v20/tree/9efbd02b8ec1a3aad347c2b59672ad25f3bcdb21/v1_native
 */
#include "cpu_808x_test_harness.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

enum {
    FLAG_CF = 0x0001,
    FLAG_OF = 0x0800
};

static void
set_execution_state(cpu_808x_test_machine_t *machine,
                    bm_808x_arch_state_t *state)
{
    *state = cpu_808x_test_get_state(machine);
    state->cs = 0xf000U;
    state->ss = 0x2000U;
    state->ip = 0U;
}

static void
test_pusha_popa(void)
{
    static const uint8_t pusha[] = { 0x60U };
    static const uint8_t popa[] = { 0x61U };
    static const uint8_t stack[] = {
        0x88U, 0x88U, 0x77U, 0x77U, 0x66U, 0x66U, 0x55U, 0x55U,
        0x44U, 0x44U, 0x33U, 0x33U, 0x22U, 0x22U, 0x11U, 0x11U
    };
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;
    bm_tick_t consumed = 0U;

    cpu_808x_test_machine_create(&machine, NULL, pusha, sizeof(pusha));
    set_execution_state(&machine, &state);
    state.ax = 0x1111U;
    state.cx = 0x2222U;
    state.dx = 0x3333U;
    state.bx = 0x4444U;
    state.sp = 0x0100U;
    state.bp = 0x6666U;
    state.si = 0x7777U;
    state.di = 0x8888U;
    cpu_808x_test_set_state(&machine, &state);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(consumed == 1U && state.sp == 0x00f0U);
    assert(cpu_808x_test_peek(&machine, 0x200f0U) == 0x88U);
    assert(cpu_808x_test_peek(&machine, 0x200f6U) == 0x00U);
    assert(cpu_808x_test_peek(&machine, 0x200f7U) == 0x01U);
    assert(cpu_808x_test_peek(&machine, 0x200feU) == 0x11U);
    cpu_808x_test_machine_destroy(&machine);

    cpu_808x_test_machine_create(&machine, NULL, popa, sizeof(popa));
    cpu_808x_test_write(&machine, 0x20100U, stack, sizeof(stack));
    set_execution_state(&machine, &state);
    state.sp = 0x0100U;
    cpu_808x_test_set_state(&machine, &state);
    consumed = 0U;
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(consumed == 1U && state.sp == 0x0110U);
    assert(state.di == 0x8888U && state.si == 0x7777U);
    assert(state.bp == 0x6666U && state.bx == 0x4444U);
    assert(state.dx == 0x3333U && state.cx == 0x2222U);
    assert(state.ax == 0x1111U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_push_immediates(void)
{
    static const uint8_t push_word[] = { 0x68U, 0x34U, 0x12U };
    static const uint8_t push_byte[] = { 0x6aU, 0x80U };
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;
    bm_tick_t consumed = 0U;

    cpu_808x_test_machine_create(&machine, NULL, push_word, sizeof(push_word));
    set_execution_state(&machine, &state);
    state.sp = 0x0100U;
    cpu_808x_test_set_state(&machine, &state);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    assert(cpu_808x_test_peek(&machine, 0x200feU) == 0x34U);
    assert(cpu_808x_test_peek(&machine, 0x200ffU) == 0x12U);
    cpu_808x_test_machine_destroy(&machine);

    cpu_808x_test_machine_create(&machine, NULL, push_byte, sizeof(push_byte));
    set_execution_state(&machine, &state);
    state.sp = 0x0100U;
    cpu_808x_test_set_state(&machine, &state);
    consumed = 0U;
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    assert(cpu_808x_test_peek(&machine, 0x200feU) == 0x80U);
    assert(cpu_808x_test_peek(&machine, 0x200ffU) == 0xffU);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_imul_immediates(void)
{
    static const uint8_t imul_word[] = { 0x69U, 0xc1U, 0x02U, 0x00U };
    static const uint8_t imul_byte[] = { 0x6bU, 0xd1U, 0x02U };
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;
    bm_tick_t consumed = 0U;

    cpu_808x_test_machine_create(&machine, NULL, imul_word, sizeof(imul_word));
    set_execution_state(&machine, &state);
    state.cx = 0xfed4U;
    state.flags = 0xf803U;
    cpu_808x_test_set_state(&machine, &state);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.ax == 0xfda8U);
    assert((state.flags & (FLAG_CF | FLAG_OF)) == 0U);
    cpu_808x_test_machine_destroy(&machine);

    cpu_808x_test_machine_create(&machine, NULL, imul_byte, sizeof(imul_byte));
    set_execution_state(&machine, &state);
    state.cx = 20000U;
    state.flags = 0xf002U;
    cpu_808x_test_set_state(&machine, &state);
    consumed = 0U;
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.dx == 40000U);
    assert((state.flags & (FLAG_CF | FLAG_OF)) ==
           (FLAG_CF | FLAG_OF));
    cpu_808x_test_machine_destroy(&machine);
}

int
main(void)
{
    test_pusha_popa();
    test_push_immediates();
    test_imul_immediates();
    return 0;
}
