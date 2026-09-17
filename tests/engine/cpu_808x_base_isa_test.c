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
    FLAG_PF = 0x0004,
    FLAG_AF = 0x0010,
    FLAG_TF = 0x0100,
    FLAG_IF = 0x0200,
    FLAG_OF = 0x0800
};

typedef struct arithmetic_case {
    uint8_t program[3];
    size_t program_size;
    uint16_t initial_ax;
    uint16_t initial_cx;
    uint16_t initial_flags;
    uint16_t expected_ax;
    uint16_t expected_flags;
} arithmetic_case_t;

static void
run_arithmetic_case(const arithmetic_case_t *test)
{
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;
    bm_tick_t consumed = 0U;

    cpu_808x_test_machine_create(&machine, NULL, test->program,
                                 test->program_size);
    state = cpu_808x_test_get_state(&machine);
    state.cs = 0xf000U;
    state.ip = 0U;
    state.ax = test->initial_ax;
    state.cx = test->initial_cx;
    state.flags = test->initial_flags;
    cpu_808x_test_set_state(&machine, &state);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(consumed == 1U);
    assert(state.ax == test->expected_ax);
    assert(state.flags == test->expected_flags);
    assert(state.ip == test->program_size);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_adc_sbb_primary_forms(void)
{
    static const arithmetic_case_t cases[] = {
        { { 0x10U, 0xc8U }, 2U, 0x007fU, 0x0000U, 0xf003U,
          0x0080U, 0xf892U },
        { { 0x12U, 0xc1U }, 2U, 0x007fU, 0x0000U, 0xf003U,
          0x0080U, 0xf892U },
        { { 0x14U, 0x00U }, 2U, 0x007fU, 0U, 0xf003U,
          0x0080U, 0xf892U },
        { { 0x15U, 0x00U, 0x00U }, 3U, 0x7fffU, 0U, 0xf003U,
          0x8000U, 0xf896U },
        { { 0x18U, 0xc8U }, 2U, 0x0000U, 0x0000U, 0xf003U,
          0x00ffU, 0xf097U },
        { { 0x1aU, 0xc1U }, 2U, 0x0000U, 0x0000U, 0xf003U,
          0x00ffU, 0xf097U },
        { { 0x1cU, 0x00U }, 2U, 0x0000U, 0U, 0xf003U,
          0x00ffU, 0xf097U },
        { { 0x1dU, 0x00U, 0x00U }, 3U, 0x0000U, 0U, 0xf003U,
          0xffffU, 0xf097U },
        { { 0x82U, 0xd0U, 0x00U }, 3U, 0x007fU, 0U, 0xf003U,
          0x0080U, 0xf892U }
    };
    size_t index;

    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index)
        run_arithmetic_case(&cases[index]);
}

static bm_808x_arch_state_t
run_single(const uint8_t *program, size_t size, uint16_t ax, uint16_t flags)
{
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;
    bm_tick_t consumed = 0U;

    cpu_808x_test_machine_create(&machine, NULL, program, size);
    state = cpu_808x_test_get_state(&machine);
    state.cs = 0xf000U;
    state.ip = 0U;
    state.ax = ax;
    state.flags = flags;
    cpu_808x_test_set_state(&machine, &state);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    assert(consumed == 1U);
    state = cpu_808x_test_get_state(&machine);
    cpu_808x_test_machine_destroy(&machine);
    return state;
}

static void
test_adjust_and_cwd(void)
{
    static const uint8_t daa[] = { 0x27U };
    static const uint8_t das[] = { 0x2fU };
    static const uint8_t aaa[] = { 0x37U };
    static const uint8_t aas[] = { 0x3fU };
    static const uint8_t cwd[] = { 0x99U };
    bm_808x_arch_state_t state;

    state = run_single(daa, sizeof(daa), 0x369eU, 0xfc96U);
    assert(state.ax == 0x36a4U && state.flags == 0xf492U);
    state = run_single(das, sizeof(das), 0x3612U, 0xf013U);
    assert((state.ax & 0xff00U) == 0x3600U);
    assert((state.flags & (FLAG_CF | FLAG_AF)) ==
           (FLAG_CF | FLAG_AF));
    state = run_single(aaa, sizeof(aaa), 0x72ffU, 0xf493U);
    assert(state.ax == 0x7305U);
    assert((state.flags & (FLAG_CF | FLAG_AF)) ==
           (FLAG_CF | FLAG_AF));
    state = run_single(aas, sizeof(aas), 0x1202U, 0xf012U);
    assert(state.ax == 0x110cU);
    assert((state.flags & (FLAG_CF | FLAG_AF)) ==
           (FLAG_CF | FLAG_AF));

    state = run_single(cwd, sizeof(cwd), 0x8000U, 0xf002U);
    assert(state.dx == 0xffffU && state.ax == 0x8000U);
    state = run_single(cwd, sizeof(cwd), 0x7fffU, 0xf002U);
    assert(state.dx == 0U && state.ax == 0x7fffU);
}

static void
test_int3_and_into(void)
{
    static const uint8_t int3[] = { 0xccU };
    static const uint8_t into[] = { 0xceU };
    static const uint8_t vector3[] = { 0x34U, 0x12U, 0x78U, 0x56U };
    static const uint8_t vector4[] = { 0xbcU, 0x9aU, 0xf0U, 0xdeU };
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;
    bm_tick_t consumed = 0U;

    cpu_808x_test_machine_create(&machine, NULL, int3, sizeof(int3));
    cpu_808x_test_write(&machine, 3U * 4U, vector3, sizeof(vector3));
    state = cpu_808x_test_get_state(&machine);
    state.cs = 0xf000U;
    state.ss = 0x2000U;
    state.sp = 0x0100U;
    state.ip = 0U;
    state.flags = (uint16_t) (0xf002U | FLAG_IF | FLAG_TF);
    cpu_808x_test_set_state(&machine, &state);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.cs == 0x5678U && state.ip == 0x1234U);
    assert(state.sp == 0x00faU);
    assert((state.flags & (FLAG_IF | FLAG_TF)) == 0U);
    assert(cpu_808x_test_peek(&machine, 0x200faU) == 0x01U);
    assert(cpu_808x_test_peek(&machine, 0x200fcU) == 0x00U);
    assert(cpu_808x_test_peek(&machine, 0x200fdU) == 0xf0U);
    cpu_808x_test_machine_destroy(&machine);

    cpu_808x_test_machine_create(&machine, NULL, into, sizeof(into));
    cpu_808x_test_write(&machine, 4U * 4U, vector4, sizeof(vector4));
    state = cpu_808x_test_get_state(&machine);
    state.cs = 0xf000U;
    state.ss = 0x2000U;
    state.sp = 0x0100U;
    state.ip = 0U;
    state.flags = (uint16_t) (0xf002U | FLAG_OF);
    cpu_808x_test_set_state(&machine, &state);
    consumed = 0U;
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.cs == 0xdef0U && state.ip == 0x9abcU);
    assert(state.sp == 0x00faU);
    cpu_808x_test_machine_destroy(&machine);

    state = run_single(into, sizeof(into), 0U, 0xf002U);
    assert(state.cs == 0xf000U && state.ip == 1U);
}

int
main(void)
{
    test_adc_sbb_primary_forms();
    test_adjust_and_cwd();
    test_int3_and_into();
    return 0;
}
