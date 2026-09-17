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

static bm_808x_arch_state_t
run_register_instruction(const uint8_t *program, size_t size,
                         uint16_t ax, uint16_t cx, uint16_t dx,
                         uint16_t flags)
{
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;
    bm_tick_t consumed = 0U;

    cpu_808x_test_machine_create(&machine, NULL, program, size);
    state = cpu_808x_test_get_state(&machine);
    state.cs = 0xf000U;
    state.ip = 0U;
    state.ax = ax;
    state.cx = cx;
    state.dx = dx;
    state.flags = flags;
    cpu_808x_test_set_state(&machine, &state);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    assert(consumed == 1U);
    state = cpu_808x_test_get_state(&machine);
    assert(state.ip == size);
    cpu_808x_test_machine_destroy(&machine);
    return state;
}

static void
test_test_aliases(void)
{
    static const uint8_t test_al_alias[] = { 0xf6U, 0xc8U, 0x0fU };
    static const uint8_t test_ax_alias[] = { 0xf7U, 0xc8U, 0x0fU, 0x0fU };
    bm_808x_arch_state_t state;

    state = run_register_instruction(test_al_alias, sizeof(test_al_alias),
                                     0x55a5U, 0U, 0U, 0xf803U);
    assert(state.ax == 0x55a5U);
    assert(state.flags == 0xf006U);
    state = run_register_instruction(test_ax_alias, sizeof(test_ax_alias),
                                     0xa5a5U, 0U, 0U, 0xf803U);
    assert(state.ax == 0xa5a5U);
    assert(state.flags == 0xf006U);
}

static void
test_signed_multiply(void)
{
    static const uint8_t imul_cl[] = { 0xf6U, 0xe9U };
    static const uint8_t imul_cx[] = { 0xf7U, 0xe9U };
    bm_808x_arch_state_t state;

    state = run_register_instruction(imul_cl, sizeof(imul_cl),
                                     0x00fbU, 0x0007U, 0U, 0xf803U);
    assert(state.ax == 0xffddU);
    assert((state.flags & (FLAG_CF | FLAG_OF)) == 0U);
    state = run_register_instruction(imul_cl, sizeof(imul_cl),
                                     0x009cU, 0x0002U, 0U, 0xf002U);
    assert(state.ax == 0xff38U);
    assert((state.flags & (FLAG_CF | FLAG_OF)) ==
           (FLAG_CF | FLAG_OF));

    state = run_register_instruction(imul_cx, sizeof(imul_cx),
                                     0xfed4U, 0x0007U, 0U, 0xf803U);
    assert(state.dx == 0xffffU && state.ax == 0xf7ccU);
    assert((state.flags & (FLAG_CF | FLAG_OF)) == 0U);
    state = run_register_instruction(imul_cx, sizeof(imul_cx),
                                     20000U, 2U, 0U, 0xf002U);
    assert(state.dx == 0U && state.ax == 40000U);
    assert((state.flags & (FLAG_CF | FLAG_OF)) ==
           (FLAG_CF | FLAG_OF));
}

static void
test_signed_divide(void)
{
    static const uint8_t idiv_cl[] = { 0xf6U, 0xf9U };
    static const uint8_t idiv_cx[] = { 0xf7U, 0xf9U };
    bm_808x_arch_state_t state;

    state = run_register_instruction(idiv_cl, sizeof(idiv_cl),
                                     0xfff9U, 3U, 0U, 0xf002U);
    assert(state.ax == 0xfffeU);
    state = run_register_instruction(idiv_cx, sizeof(idiv_cx),
                                     0xfff9U, 3U, 0xffffU, 0xf002U);
    assert(state.ax == 0xfffeU && state.dx == 0xffffU);
}

static void
test_signed_divide_fault(void)
{
    static const uint8_t idiv_cl[] = { 0xf6U, 0xf9U };
    static const uint8_t idiv_cx[] = { 0xf7U, 0xf9U };
    static const uint8_t vector[] = { 0x00U, 0x04U, 0x00U, 0x00U };
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;
    bm_tick_t consumed = 0U;

    cpu_808x_test_machine_create(&machine, NULL, idiv_cx, sizeof(idiv_cx));
    cpu_808x_test_write(&machine, 0U, vector, sizeof(vector));
    state = cpu_808x_test_get_state(&machine);
    state.cs = 0xf000U;
    state.ss = 0x2000U;
    state.sp = 0x0100U;
    state.ip = 0U;
    state.ax = 0U;
    state.dx = 0x8000U;
    state.cx = 0xffffU;
    state.flags = 0xf302U;
    cpu_808x_test_set_state(&machine, &state);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(consumed == 1U);
    assert(state.cs == 0U && state.ip == 0x0400U);
    assert(state.sp == 0x00faU);
    assert(state.ax == 0U && state.dx == 0x8000U);
    assert(cpu_808x_test_peek(&machine, 0x200faU) == 0x02U);
    assert(cpu_808x_test_peek(&machine, 0x200fbU) == 0x00U);
    assert(cpu_808x_test_peek(&machine, 0x200fcU) == 0x00U);
    assert(cpu_808x_test_peek(&machine, 0x200fdU) == 0xf0U);
    assert(cpu_808x_test_peek(&machine, 0x200feU) == 0x02U);
    assert(cpu_808x_test_peek(&machine, 0x200ffU) == 0xf3U);
    cpu_808x_test_machine_destroy(&machine);

    /* The observed NEC V20 raises INT 0 for the byte quotient -128. */
    cpu_808x_test_machine_create(&machine, NULL, idiv_cl, sizeof(idiv_cl));
    cpu_808x_test_write(&machine, 0U, vector, sizeof(vector));
    state = cpu_808x_test_get_state(&machine);
    state.cs = 0xf000U;
    state.ss = 0x2000U;
    state.sp = 0x0100U;
    state.ip = 0U;
    state.ax = 0xff00U;
    state.cx = 2U;
    state.flags = 0xf302U;
    cpu_808x_test_set_state(&machine, &state);
    consumed = 0U;
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(consumed == 1U);
    assert(state.cs == 0U && state.ip == 0x0400U);
    assert(state.sp == 0x00faU && state.ax == 0xff00U);
    cpu_808x_test_machine_destroy(&machine);
}

int
main(void)
{
    test_test_aliases();
    test_signed_multiply();
    test_signed_divide();
    test_signed_divide_fault();
    return 0;
}
