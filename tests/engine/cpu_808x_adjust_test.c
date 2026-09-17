/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * NEC-specific expectations are derived from hardware-generated V20 vectors:
 * https://github.com/SingleStepTests/v20/tree/9efbd02b8ec1a3aad347c2b59672ad25f3bcdb21/v1_native
 * The V20 and V30 share these native architectural instruction results; V20
 * cycle traces are deliberately not used as V30 timing evidence.
 */
#include "cpu_808x_test_harness.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

enum {
    FLAG_CF = 0x0001,
    FLAG_PF = 0x0004,
    FLAG_AF = 0x0010,
    FLAG_ZF = 0x0040,
    FLAG_SF = 0x0080,
    FLAG_OF = 0x0800,
    DEFINED_ADJUST_FLAGS = FLAG_PF | FLAG_ZF | FLAG_SF
};

typedef struct adjust_case {
    uint8_t opcode;
    uint8_t encoded_base;
    uint16_t input_ax;
    uint16_t expected_ax;
    uint16_t expected_defined_flags;
} adjust_case_t;

static void
run_adjust_case(const adjust_case_t *test)
{
    uint8_t program[2];
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;
    bm_tick_t consumed = 0U;

    program[0] = test->opcode;
    program[1] = test->encoded_base;
    cpu_808x_test_machine_create(&machine, NULL, program, sizeof(program));
    state = cpu_808x_test_get_state(&machine);
    state.ax = test->input_ax;
    state.cs = 0xf000U;
    state.ip = 0U;
    state.flags = 0xffffU;
    cpu_808x_test_set_state(&machine, &state);

    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    assert(consumed == 1U);
    state = cpu_808x_test_get_state(&machine);
    assert(state.ax == test->expected_ax);
    assert(state.ip == 2U);
    assert((state.flags & DEFINED_ADJUST_FLAGS) ==
           test->expected_defined_flags);
    assert((state.flags & (FLAG_CF | FLAG_AF | FLAG_OF)) == 0U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_arch_state_contract(void)
{
    static const uint8_t program[] = { 0x90U };
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;
    bm_808x_arch_state_t observed;
    bm_808x_arch_state_t invalid;
    bm_cpu_t foreign_cpu = { 0 };
    bm_tick_t consumed = 0U;

    cpu_808x_test_machine_create(&machine, NULL, program, sizeof(program));
    state = cpu_808x_test_get_state(&machine);
    assert(state.size == sizeof(state));
    assert(state.version == BM_808X_ARCH_STATE_VERSION);
    assert(state.model == BM_808X_NEC_V30);
    assert(state.cs == 0xffffU && state.ip == 0U && state.flags == 0xf002U);

    state.ax = 0x1234U;
    state.cx = 0x5678U;
    state.dx = 0x9abcU;
    state.bx = 0xdef0U;
    state.sp = 0x1357U;
    state.bp = 0x2468U;
    state.si = 0xaaaaU;
    state.di = 0x5555U;
    state.es = 0x1000U;
    state.cs = 0xf000U;
    state.ss = 0x2000U;
    state.ds = 0x3000U;
    state.ip = 0U;
    state.flags = 0xffd7U;
    state.halted = 0U;
    state.interrupt_inhibit = 1U;
    state.boundary_inhibit = 1U;
    state.nmi_pending = 1U;
    state.trap_pending = 1U;
    cpu_808x_test_set_state(&machine, &state);
    observed = cpu_808x_test_get_state(&machine);
    assert(observed.ax == state.ax && observed.cx == state.cx);
    assert(observed.dx == state.dx && observed.bx == state.bx);
    assert(observed.sp == state.sp && observed.bp == state.bp);
    assert(observed.si == state.si && observed.di == state.di);
    assert(observed.es == state.es && observed.cs == state.cs);
    assert(observed.ss == state.ss && observed.ds == state.ds);
    assert(observed.ip == state.ip && observed.flags == state.flags);
    assert(observed.interrupt_inhibit == 1U);
    assert(observed.boundary_inhibit == 1U);
    assert(observed.nmi_pending == 1U && observed.trap_pending == 1U);

    state = observed;
    state.boundary_inhibit = 0U;
    state.nmi_pending = 0U;
    state.trap_pending = 0U;
    cpu_808x_test_set_state(&machine, &state);

    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    assert(consumed == 1U);
    observed = cpu_808x_test_get_state(&machine);
    assert(observed.ip == 1U && observed.ax == state.ax);
    assert(observed.interrupt_inhibit == 0U);
    assert(observed.boundary_inhibit == 0U && observed.nmi_pending == 0U);
    assert(observed.trap_pending == 1U);

    invalid = state;
    invalid.size = 0U;
    assert(bm_808x_set_arch_state(&machine.cpu, &invalid) ==
           BM_STATUS_INVALID_ARGUMENT);
    invalid = state;
    invalid.version = BM_808X_ARCH_STATE_VERSION + 1U;
    assert(bm_808x_set_arch_state(&machine.cpu, &invalid) ==
           BM_STATUS_INVALID_ARGUMENT);
    invalid = state;
    invalid.halted = 2U;
    assert(bm_808x_set_arch_state(&machine.cpu, &invalid) ==
           BM_STATUS_INVALID_ARGUMENT);
    invalid = state;
    invalid.interrupt_inhibit = 2U;
    assert(bm_808x_set_arch_state(&machine.cpu, &invalid) ==
           BM_STATUS_INVALID_ARGUMENT);
    invalid = state;
    invalid.boundary_inhibit = 2U;
    assert(bm_808x_set_arch_state(&machine.cpu, &invalid) ==
           BM_STATUS_INVALID_ARGUMENT);
    invalid = state;
    invalid.nmi_pending = 2U;
    assert(bm_808x_set_arch_state(&machine.cpu, &invalid) ==
           BM_STATUS_INVALID_ARGUMENT);
    invalid = state;
    invalid.trap_pending = 2U;
    assert(bm_808x_set_arch_state(&machine.cpu, &invalid) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_808x_get_arch_state(NULL, &observed) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_808x_get_arch_state(&foreign_cpu, &observed) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_808x_step(&foreign_cpu, &consumed) ==
           BM_STATUS_INVALID_ARGUMENT);
    cpu_808x_test_machine_destroy(&machine);
}

int
main(void)
{
    static const adjust_case_t cases[] = {
        /* AAM honors arbitrary non-zero bases on measured V20 hardware. */
        { 0xd4U, 10U, 0xa17bU, 0x0c03U, FLAG_PF },
        { 0xd4U, 75U, 0x52a1U, 0x020bU, 0U },
        { 0xd4U, 1U, 0x00ffU, 0xff00U, FLAG_PF | FLAG_ZF },
        /* Base zero is a NEC special case, not an interrupt-zero fault. */
        { 0xd4U, 0U, 0xe837U, 0xff37U, 0U },
        /* AAD ignores the encoded base and always multiplies AH by ten. */
        { 0xd5U, 0xe2U, 0x634cU, 0x002aU, 0U },
        { 0xd5U, 10U, 0x0937U, 0x0091U, FLAG_SF }
    };
    size_t index;

    test_arch_state_contract();
    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index)
        run_adjust_case(&cases[index]);
    return 0;
}
