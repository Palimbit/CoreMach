/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PSW and MD semantics follow the NEC V20/V30 User's Manual (October 1986)
 * and the NEC 16-Bit V Series Instruction User's Manual. Firmware and
 * hardware-vector corpora are deliberately not test inputs here.
 */
#include "cpu_808x_test_harness.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

static uint16_t
peek_word(const cpu_808x_test_machine_t *machine, uint64_t address)
{
    return (uint16_t) (cpu_808x_test_peek(machine, address) |
                       ((uint16_t) cpu_808x_test_peek(machine, address + 1U) << 8U));
}

static void
poke_word(cpu_808x_test_machine_t *machine, uint64_t address, uint16_t value)
{
    cpu_808x_test_poke(machine, address, (uint8_t) value);
    cpu_808x_test_poke(machine, address + 1U, (uint8_t) (value >> 8U));
}

static bm_808x_arch_state_t
execution_state(cpu_808x_test_machine_t *machine)
{
    bm_808x_arch_state_t state = cpu_808x_test_get_state(machine);
    state.cs = 0xf000U;
    state.ss = 0x2000U;
    state.ip = 0U;
    state.sp = 0x0100U;
    return state;
}

static void
test_reset_and_state_contract(void)
{
    static const uint8_t nop[] = { 0x90U };
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;
    bm_808x_arch_state_t rejected;

    cpu_808x_test_machine_create(&machine, NULL, nop, sizeof(nop));
    state = cpu_808x_test_get_state(&machine);
    assert(state.flags == 0xf002U);
    assert(state.md_write_enabled == 0U);
    state.flags = 0x8028U;
    cpu_808x_test_set_state(&machine, &state);
    state = cpu_808x_test_get_state(&machine);
    assert(state.flags == 0xf002U);
    state.flags = 0xffffU;
    cpu_808x_test_set_state(&machine, &state);
    state = cpu_808x_test_get_state(&machine);
    assert(state.flags == 0xffd7U);
    assert(cpu_808x_test_inspect(&machine, "flags") == 0xffd7U);

    rejected = state;
    rejected.flags &= 0x7fffU;
    assert(bm_808x_set_arch_state(&machine.cpu, &rejected) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(cpu_808x_test_get_state(&machine).flags == 0xffd7U);

    rejected.md_write_enabled = 1U;
    assert(bm_808x_set_arch_state(&machine.cpu, &rejected) == BM_STATUS_OK);
    rejected = cpu_808x_test_get_state(&machine);
    assert(rejected.flags == 0x7fd7U && rejected.md_write_enabled == 1U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_pushf_and_popf_images(void)
{
    static const uint8_t pushf[] = { 0x9cU };
    static const uint8_t popf[] = { 0x9dU };
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;
    bm_tick_t consumed = 0U;

    cpu_808x_test_machine_create(&machine, NULL, pushf, sizeof(pushf));
    state = execution_state(&machine);
    state.flags = 0xffffU;
    cpu_808x_test_set_state(&machine, &state);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(consumed == 1U && state.sp == 0x00feU);
    assert(peek_word(&machine, 0x200feU) == 0xffd7U);
    cpu_808x_test_machine_destroy(&machine);

    cpu_808x_test_machine_create(&machine, NULL, popf, sizeof(popf));
    state = execution_state(&machine);
    state.flags = 0xf002U;
    poke_word(&machine, 0x20100U, 0x0badU);
    cpu_808x_test_set_state(&machine, &state);
    consumed = 0U;
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(consumed == 1U && state.sp == 0x0102U);
    assert(state.flags == 0xfb87U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_iret_preserves_locked_md(void)
{
    static const uint8_t iret[] = { 0xcfU };
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;
    bm_tick_t consumed = 0U;

    cpu_808x_test_machine_create(&machine, NULL, iret, sizeof(iret));
    state = execution_state(&machine);
    poke_word(&machine, 0x20100U, 0x1234U);
    poke_word(&machine, 0x20102U, 0x5678U);
    poke_word(&machine, 0x20104U, 0x0badU);
    cpu_808x_test_set_state(&machine, &state);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(consumed == 1U && state.sp == 0x0106U);
    assert(state.cs == 0x5678U && state.ip == 0x1234U);
    assert(state.flags == 0xfb87U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_interrupt_psw_image(void)
{
    static const uint8_t int3[] = { 0xccU };
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;
    bm_tick_t consumed = 0U;

    cpu_808x_test_machine_create(&machine, NULL, int3, sizeof(int3));
    poke_word(&machine, 3U * 4U, 0x1234U);
    poke_word(&machine, 3U * 4U + 2U, 0x5678U);
    state = execution_state(&machine);
    state.flags = 0xffffU;
    cpu_808x_test_set_state(&machine, &state);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(consumed == 1U && state.sp == 0x00faU);
    assert(peek_word(&machine, 0x200feU) == 0xffd7U);
    assert(state.cs == 0x5678U && state.ip == 0x1234U);
    assert(state.flags == 0xfcd7U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_brkem_enters_emulation_mode(void)
{
    static const uint8_t brkem[] = { 0x0fU, 0xffU, 0x12U };
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;
    bm_tick_t consumed = 99U;

    cpu_808x_test_machine_create(&machine, NULL, brkem, sizeof(brkem));
    state = execution_state(&machine);
    poke_word(&machine, 0x12U * 4U, 0x3456U);
    poke_word(&machine, 0x12U * 4U + 2U, 0x1234U);
    cpu_808x_test_set_state(&machine, &state);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(consumed == 1U && state.cs == 0x1234U && state.ip == 0x3456U);
    assert(state.sp == 0x00faU && state.flags == 0x7002U);
    assert(state.md_write_enabled == 1U);
    assert(peek_word(&machine, 0x200faU) == 3U);
    assert(peek_word(&machine, 0x200fcU) == 0xf000U);
    assert(peek_word(&machine, 0x200feU) == 0xf002U);
    cpu_808x_test_machine_destroy(&machine);
}

int
main(void)
{
    test_reset_and_state_contract();
    test_pushf_and_popf_images();
    test_iret_preserves_locked_md();
    test_interrupt_psw_image();
    test_brkem_enters_emulation_mode();
    return 0;
}
