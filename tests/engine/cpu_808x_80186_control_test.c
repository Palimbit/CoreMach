/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * NEC instruction semantics are taken from the NEC V20/V30 User's Manual,
 * October 1986, and the 16-Bit V Series Instruction User's Manual.  Historical
 * firmware and external hardware-vector corpora are not test inputs here.
 */
#include "cpu_808x_test_harness.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

enum {
    FLAG_CF = 0x0001,
    FLAG_PF = 0x0004,
    FLAG_ZF = 0x0040,
    FLAG_SF = 0x0080,
    FLAG_OF = 0x0800
};

static void
set_execution_state(cpu_808x_test_machine_t *machine,
                    bm_808x_arch_state_t *state)
{
    *state = cpu_808x_test_get_state(machine);
    state->cs = 0xf000U;
    state->ss = 0x2000U;
    state->ds = 0U;
    state->ip = 0U;
}

static void
write_word(cpu_808x_test_machine_t *machine, uint64_t address, uint16_t value)
{
    cpu_808x_test_poke(machine, address, (uint8_t) value);
    cpu_808x_test_poke(machine, address + 1U, (uint8_t) (value >> 8U));
}

static void
test_bound_signed_and_next_ip(void)
{
    static const uint8_t bound[] = { 0x62U, 0x06U, 0x00U, 0x01U };
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;
    bm_tick_t consumed = 0U;

    cpu_808x_test_machine_create(&machine, NULL, bound, sizeof(bound));
    write_word(&machine, 0x00100U, 0xfffbU); /* -5 */
    write_word(&machine, 0x00102U, 0x0003U);
    set_execution_state(&machine, &state);
    state.ax = 0xfffeU; /* -2 */
    state.flags = 0xa5d7U;
    state.sp = 0x0100U;
    cpu_808x_test_set_state(&machine, &state);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(consumed == 1U && state.ip == 4U && state.sp == 0x0100U);
    assert(state.flags == 0xa5d7U);
    cpu_808x_test_machine_destroy(&machine);

    cpu_808x_test_machine_create(&machine, NULL, bound, sizeof(bound));
    write_word(&machine, 0x00100U, 0xfffbU);
    write_word(&machine, 0x00102U, 0x0003U);
    write_word(&machine, 0x00014U, 0x1234U); /* BRK 5 PC */
    write_word(&machine, 0x00016U, 0x5678U); /* BRK 5 PS */
    set_execution_state(&machine, &state);
    state.ax = 4U;
    state.flags = 0x0f47U;
    state.sp = 0x0100U;
    cpu_808x_test_set_state(&machine, &state);
    consumed = 0U;
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(consumed == 1U && state.sp == 0x00faU);
    assert(state.cs == 0x5678U && state.ip == 0x1234U);
    assert(cpu_808x_test_peek(&machine, 0x200faU) == 0x04U);
    assert(cpu_808x_test_peek(&machine, 0x200fbU) == 0x00U);
    assert(cpu_808x_test_peek(&machine, 0x200fcU) == 0x00U);
    assert(cpu_808x_test_peek(&machine, 0x200fdU) == 0xf0U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_undefined_forms_are_rejected(void)
{
    static const uint8_t bound_register[] = { 0x62U, 0xc0U };
    static const uint8_t opcode_63[] = { 0x63U, 0xc0U };
    static const uint8_t shift_group_6[] = { 0xc0U, 0xf0U, 0x01U };
    const uint8_t *programs[] = { bound_register, opcode_63, shift_group_6 };
    const size_t sizes[] = {
        sizeof(bound_register), sizeof(opcode_63), sizeof(shift_group_6)
    };
    const uint16_t expected_ip[] = { 2U, 1U, 2U };
    size_t index;

    for (index = 0U; index < sizeof(programs) / sizeof(programs[0]); ++index) {
        cpu_808x_test_machine_t machine;
        bm_808x_arch_state_t state;
        bm_tick_t consumed = 99U;
        cpu_808x_test_machine_create(&machine, NULL, programs[index], sizes[index]);
        set_execution_state(&machine, &state);
        state.ax = 0x1234U;
        cpu_808x_test_set_state(&machine, &state);
        assert(cpu_808x_test_step(&machine, &consumed) ==
               BM_STATUS_UNSUPPORTED);
        state = cpu_808x_test_get_state(&machine);
        assert(consumed == 0U && state.ip == expected_ip[index]);
        assert(state.ax == 0x1234U);
        cpu_808x_test_machine_destroy(&machine);
    }
}

static void
test_immediate_shifts_use_full_count(void)
{
    static const uint8_t shift_32[] = { 0xc0U, 0xe0U, 0x20U };
    static const uint8_t shift_zero[] = { 0xc1U, 0xe0U, 0x00U };
    static const uint8_t shift_memory[] = {
        0xc1U, 0x2eU, 0x00U, 0x01U, 0x04U
    };
    static const uint8_t rotate_one[] = { 0xc0U, 0xc8U, 0x01U };
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;
    bm_tick_t consumed = 0U;

    cpu_808x_test_machine_create(&machine, NULL, shift_32, sizeof(shift_32));
    set_execution_state(&machine, &state);
    state.ax = 0xab81U;
    state.flags = FLAG_CF | FLAG_OF;
    cpu_808x_test_set_state(&machine, &state);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.ax == 0xab00U);
    assert((state.flags & (FLAG_CF | FLAG_PF | FLAG_ZF | FLAG_SF)) ==
           (FLAG_PF | FLAG_ZF));
    cpu_808x_test_machine_destroy(&machine);

    cpu_808x_test_machine_create(&machine, NULL, shift_zero,
                                  sizeof(shift_zero));
    set_execution_state(&machine, &state);
    state.ax = 0x1234U;
    state.flags = 0xa5d7U;
    cpu_808x_test_set_state(&machine, &state);
    consumed = 0U;
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.ax == 0x1234U && state.flags == 0xa5d7U);
    cpu_808x_test_machine_destroy(&machine);

    cpu_808x_test_machine_create(&machine, NULL, shift_memory,
                                  sizeof(shift_memory));
    write_word(&machine, 0x00100U, 0xf000U);
    set_execution_state(&machine, &state);
    cpu_808x_test_set_state(&machine, &state);
    consumed = 0U;
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    assert(cpu_808x_test_peek(&machine, 0x00100U) == 0x00U);
    assert(cpu_808x_test_peek(&machine, 0x00101U) == 0x0fU);
    cpu_808x_test_machine_destroy(&machine);

    cpu_808x_test_machine_create(&machine, NULL, rotate_one,
                                  sizeof(rotate_one));
    set_execution_state(&machine, &state);
    state.ax = 0x5503U;
    state.flags = 0U;
    cpu_808x_test_set_state(&machine, &state);
    consumed = 0U;
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.ax == 0x5581U);
    assert((state.flags & (FLAG_CF | FLAG_OF)) == (FLAG_CF | FLAG_OF));
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_all_documented_immediate_shift_groups(void)
{
    static const struct {
        uint8_t operation;
        uint8_t byte_result;
        uint16_t word_result;
        uint16_t byte_carry;
        uint16_t word_carry;
    } cases[] = {
        { 0U, 0xacU, 0x091cU, 0U, 0U },
        { 1U, 0xb2U, 0x7024U, FLAG_CF, 0U },
        { 2U, 0xaeU, 0x091eU, 0U, 0U },
        { 3U, 0x72U, 0xf024U, FLAG_CF, 0U },
        { 4U, 0xa8U, 0x0918U, 0U, 0U },
        { 5U, 0x12U, 0x1024U, FLAG_CF, 0U },
        { 7U, 0xf2U, 0xf024U, FLAG_CF, 0U }
    };
    size_t index;

    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        uint8_t byte_program[] = {
            0xc0U, (uint8_t) (0xc0U | (cases[index].operation << 3U)), 3U
        };
        uint8_t word_program[] = {
            0xc1U, (uint8_t) (0xc0U | (cases[index].operation << 3U)), 3U
        };
        cpu_808x_test_machine_t machine;
        bm_808x_arch_state_t state;
        bm_tick_t consumed = 0U;

        cpu_808x_test_machine_create(&machine, NULL, byte_program,
                                      sizeof(byte_program));
        set_execution_state(&machine, &state);
        state.ax = 0xab95U;
        state.flags = FLAG_CF;
        cpu_808x_test_set_state(&machine, &state);
        assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
        state = cpu_808x_test_get_state(&machine);
        assert(state.ax == (uint16_t) (0xab00U | cases[index].byte_result));
        assert((state.flags & FLAG_CF) == cases[index].byte_carry);
        cpu_808x_test_machine_destroy(&machine);

        cpu_808x_test_machine_create(&machine, NULL, word_program,
                                      sizeof(word_program));
        set_execution_state(&machine, &state);
        state.ax = 0x8123U;
        state.flags = FLAG_CF;
        cpu_808x_test_set_state(&machine, &state);
        consumed = 0U;
        assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
        state = cpu_808x_test_get_state(&machine);
        assert(state.ax == cases[index].word_result);
        assert((state.flags & FLAG_CF) == cases[index].word_carry);
        cpu_808x_test_machine_destroy(&machine);
    }
}

static void
test_prepare_and_dispose(void)
{
    static const uint8_t prepare[] = { 0xc8U, 0x06U, 0x00U, 0x03U };
    static const uint8_t prepare_level_33[] = {
        0xc8U, 0x00U, 0x00U, 0x21U
    };
    static const uint8_t dispose[] = { 0xc9U };
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;
    bm_tick_t consumed = 0U;

    cpu_808x_test_machine_create(&machine, NULL, prepare, sizeof(prepare));
    write_word(&machine, 0x201feU, 0x1111U);
    write_word(&machine, 0x201fcU, 0x2222U);
    set_execution_state(&machine, &state);
    state.sp = 0x0100U;
    state.bp = 0x0200U;
    state.flags = 0xa5d7U;
    cpu_808x_test_set_state(&machine, &state);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.bp == 0x00feU && state.sp == 0x00f2U);
    assert(state.flags == 0xa5d7U);
    assert(cpu_808x_test_peek(&machine, 0x200f8U) == 0xfeU);
    assert(cpu_808x_test_peek(&machine, 0x200faU) == 0x22U);
    assert(cpu_808x_test_peek(&machine, 0x200fcU) == 0x11U);
    assert(cpu_808x_test_peek(&machine, 0x200feU) == 0x00U);
    assert(cpu_808x_test_peek(&machine, 0x200ffU) == 0x02U);
    cpu_808x_test_machine_destroy(&machine);

    cpu_808x_test_machine_create(&machine, NULL, prepare_level_33,
                                  sizeof(prepare_level_33));
    set_execution_state(&machine, &state);
    state.sp = 0x0300U;
    state.bp = 0x0500U;
    cpu_808x_test_set_state(&machine, &state);
    consumed = 0U;
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.bp == 0x02feU && state.sp == 0x02bcU);
    cpu_808x_test_machine_destroy(&machine);

    cpu_808x_test_machine_create(&machine, NULL, dispose, sizeof(dispose));
    write_word(&machine, 0x20100U, 0x1234U);
    set_execution_state(&machine, &state);
    state.sp = 0x0080U;
    state.bp = 0x0100U;
    state.flags = 0xa5d7U;
    cpu_808x_test_set_state(&machine, &state);
    consumed = 0U;
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.sp == 0x0102U && state.bp == 0x1234U);
    assert(state.flags == 0xa5d7U);
    cpu_808x_test_machine_destroy(&machine);
}

int
main(void)
{
    test_bound_signed_and_next_ip();
    test_undefined_forms_are_rejected();
    test_immediate_shifts_use_full_count();
    test_all_documented_immediate_shift_groups();
    test_prepare_and_dispose();
    return 0;
}
