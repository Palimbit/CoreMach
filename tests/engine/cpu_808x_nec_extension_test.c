/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Native extension encodings and semantics follow the NEC 16-Bit V Series
 * Instruction User's Manual. All operands are synthetic; no firmware or
 * guest-media image is used by this test.
 */
#include "cpu_808x_test_harness.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

enum {
    FLAG_CF = 0x0001,
    FLAG_ZF = 0x0040,
    FLAG_OF = 0x0800
};

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
    state.flags = 0xf002U;
    return state;
}

static void
test_bit_operation_matrix(void)
{
    unsigned int extension;

    for (extension = 0x10U; extension <= 0x1fU; ++extension) {
        uint8_t program[] = { 0x0fU, (uint8_t) extension, 0xc0U, 0U };
        unsigned int width = (extension & 1U) != 0U ? 16U : 8U;
        unsigned int operation = (extension >> 1U) & 3U;
        unsigned int bit = width == 8U ? 3U : 8U;
        uint16_t selected = (uint16_t) (1U << bit);
        uint16_t other = width == 8U ? 0x0040U : 0x4000U;
        uint16_t initial = operation == 2U ? other : (uint16_t) (other | selected);
        uint16_t expected = initial;
        uint16_t initial_flags = 0xf8d7U;
        uint16_t expected_flags = initial_flags;
        cpu_808x_test_machine_t machine;
        bm_808x_arch_state_t state;
        size_t size = (extension & 8U) != 0U ? 4U : 3U;

        program[3] = (uint8_t) (width == 8U ? 0xfbU : 0xf8U);
        cpu_808x_test_machine_create(&machine, NULL, program, size);
        state = execution_state(&machine);
        state.ax = initial;
        state.cx = (uint16_t) bit;
        state.flags = initial_flags;
        cpu_808x_test_set_state(&machine, &state);
        {
            bm_tick_t consumed = 0U;
            assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
            assert(consumed == 1U);
        }
        state = cpu_808x_test_get_state(&machine);
        if (operation == 0U) {
            expected_flags &= (uint16_t) ~(FLAG_CF | FLAG_OF | FLAG_ZF);
        } else if (operation == 1U) {
            expected &= (uint16_t) ~selected;
        } else if (operation == 2U) {
            expected |= selected;
        } else {
            expected ^= selected;
        }
        assert(state.ax == expected);
        assert(state.flags == expected_flags);
        assert(state.ip == size);
        cpu_808x_test_machine_destroy(&machine);
    }
}

static void
test_immediate_bit_memory_layout_and_reserved_field(void)
{
    static const uint8_t set_memory[] = {
        0x0fU, 0x1cU, 0x06U, 0x34U, 0x12U, 0xfbU
    };
    static const uint8_t invalid_modrm[] = { 0x0fU, 0x10U, 0xc8U };
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;
    bm_tick_t consumed = 0U;

    cpu_808x_test_machine_create(&machine, NULL, set_memory,
                                 sizeof(set_memory));
    state = execution_state(&machine);
    cpu_808x_test_poke(&machine, 0x11234U, 0x40U);
    cpu_808x_test_set_state(&machine, &state);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    assert(cpu_808x_test_peek(&machine, 0x11234U) == 0x48U);
    assert(cpu_808x_test_get_state(&machine).ip == sizeof(set_memory));
    cpu_808x_test_machine_destroy(&machine);

    cpu_808x_test_machine_create(&machine, NULL, invalid_modrm,
                                 sizeof(invalid_modrm));
    state = execution_state(&machine);
    state.ax = 0x1234U;
    cpu_808x_test_set_state(&machine, &state);
    consumed = 99U;
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_UNSUPPORTED);
    state = cpu_808x_test_get_state(&machine);
    assert(consumed == 0U && state.ax == 0x1234U && state.ip == 3U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_bcd_strings(void)
{
    static const uint8_t add[] = { 0x0fU, 0x20U };
    static const uint8_t subtract[] = { 0x0fU, 0x22U };
    static const uint8_t compare[] = { 0x0fU, 0x26U };
    static const uint8_t add_override[] = { 0x2eU, 0x0fU, 0x20U };
    static const uint8_t left_8765[] = { 0x65U, 0x87U };
    static const uint8_t right_1234[] = { 0x34U, 0x12U };
    static const uint8_t nines[] = { 0x99U, 0x99U };
    static const uint8_t one[] = { 0x01U, 0x00U };
    static const uint8_t odd_nines[] = { 0x99U, 0x09U };
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;
    bm_tick_t consumed;

    cpu_808x_test_machine_create(&machine, NULL, add, sizeof(add));
    state = execution_state(&machine);
    state.cx = 4U;
    state.si = state.di = 0x0100U;
    state.flags = 0xf897U;
    cpu_808x_test_write(&machine, 0x10100U, right_1234, sizeof(right_1234));
    cpu_808x_test_write(&machine, 0x20100U, left_8765, sizeof(left_8765));
    cpu_808x_test_set_state(&machine, &state);
    consumed = 0U;
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    assert(cpu_808x_test_peek(&machine, 0x20100U) == 0x99U);
    assert(cpu_808x_test_peek(&machine, 0x20101U) == 0x99U);
    state = cpu_808x_test_get_state(&machine);
    assert((state.flags & (FLAG_CF | FLAG_ZF)) == 0U);
    assert((state.flags & (uint16_t) ~(FLAG_CF | FLAG_ZF)) ==
           (0xf897U & (uint16_t) ~(FLAG_CF | FLAG_ZF)));
    cpu_808x_test_machine_destroy(&machine);

    cpu_808x_test_machine_create(&machine, NULL, add, sizeof(add));
    state = execution_state(&machine);
    state.cx = 4U;
    state.si = state.di = 0x0100U;
    cpu_808x_test_write(&machine, 0x10100U, one, sizeof(one));
    cpu_808x_test_write(&machine, 0x20100U, nines, sizeof(nines));
    cpu_808x_test_set_state(&machine, &state);
    consumed = 0U;
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(cpu_808x_test_peek(&machine, 0x20100U) == 0x00U);
    assert(cpu_808x_test_peek(&machine, 0x20101U) == 0x00U);
    assert((state.flags & (FLAG_CF | FLAG_ZF)) == (FLAG_CF | FLAG_ZF));
    cpu_808x_test_machine_destroy(&machine);

    cpu_808x_test_machine_create(&machine, NULL, add, sizeof(add));
    state = execution_state(&machine);
    state.cx = 3U;
    state.si = state.di = 0x0100U;
    cpu_808x_test_write(&machine, 0x10100U, one, sizeof(one));
    cpu_808x_test_write(&machine, 0x20100U, odd_nines, sizeof(odd_nines));
    cpu_808x_test_set_state(&machine, &state);
    consumed = 0U;
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(cpu_808x_test_peek(&machine, 0x20100U) == 0x00U);
    assert(cpu_808x_test_peek(&machine, 0x20101U) == 0x10U);
    assert((state.flags & FLAG_CF) == 0U);
    cpu_808x_test_machine_destroy(&machine);

    cpu_808x_test_machine_create(&machine, NULL, subtract, sizeof(subtract));
    state = execution_state(&machine);
    state.cx = 4U;
    state.si = state.di = 0x0100U;
    cpu_808x_test_write(&machine, 0x10100U, one, sizeof(one));
    cpu_808x_test_set_state(&machine, &state);
    consumed = 0U;
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(cpu_808x_test_peek(&machine, 0x20100U) == 0x99U);
    assert(cpu_808x_test_peek(&machine, 0x20101U) == 0x99U);
    assert((state.flags & FLAG_CF) != 0U && (state.flags & FLAG_ZF) == 0U);
    cpu_808x_test_machine_destroy(&machine);

    cpu_808x_test_machine_create(&machine, NULL, compare, sizeof(compare));
    state = execution_state(&machine);
    state.cx = 4U;
    state.si = state.di = 0x0100U;
    cpu_808x_test_write(&machine, 0x10100U, nines, sizeof(nines));
    cpu_808x_test_write(&machine, 0x20100U, nines, sizeof(nines));
    cpu_808x_test_set_state(&machine, &state);
    consumed = 0U;
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(cpu_808x_test_peek(&machine, 0x20100U) == 0x99U);
    assert(cpu_808x_test_peek(&machine, 0x20101U) == 0x99U);
    assert((state.flags & (FLAG_CF | FLAG_ZF)) == FLAG_ZF);
    cpu_808x_test_machine_destroy(&machine);

    cpu_808x_test_machine_create(&machine, NULL, add_override,
                                 sizeof(add_override));
    state = execution_state(&machine);
    state.cx = 2U;
    state.si = state.di = 0x0100U;
    cpu_808x_test_poke(&machine, 0xf0100U, 0x01U);
    cpu_808x_test_poke(&machine, 0x20100U, 0x01U);
    cpu_808x_test_set_state(&machine, &state);
    consumed = 0U;
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    assert(cpu_808x_test_peek(&machine, 0x20100U) == 0x02U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_bcd_invalid_lengths(void)
{
    static const uint8_t add[] = { 0x0fU, 0x20U };
    const uint16_t lengths[] = { 0U, 255U };
    size_t index;

    for (index = 0U; index < sizeof(lengths) / sizeof(lengths[0]); ++index) {
        cpu_808x_test_machine_t machine;
        bm_808x_arch_state_t state;
        bm_tick_t consumed = 99U;

        cpu_808x_test_machine_create(&machine, NULL, add, sizeof(add));
        state = execution_state(&machine);
        state.cx = lengths[index];
        cpu_808x_test_set_state(&machine, &state);
        assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_UNSUPPORTED);
        assert(consumed == 0U);
        cpu_808x_test_machine_destroy(&machine);
    }
}

static void
test_nibble_rotates(void)
{
    static const uint8_t rol4_bl[] = { 0x0fU, 0x28U, 0xc3U };
    static const uint8_t ror4_memory[] = {
        0x0fU, 0x2aU, 0x06U, 0x00U, 0x01U
    };
    static const uint8_t invalid[] = { 0x0fU, 0x28U, 0xcbU };
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;
    bm_tick_t consumed = 0U;

    cpu_808x_test_machine_create(&machine, NULL, rol4_bl, sizeof(rol4_bl));
    state = execution_state(&machine);
    state.ax = 0x5a04U;
    state.bx = 0x0083U;
    state.flags = 0xf8d7U;
    cpu_808x_test_set_state(&machine, &state);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.ax == 0x5a08U && state.bx == 0x0034U);
    assert(state.flags == 0xf8d7U);
    cpu_808x_test_machine_destroy(&machine);

    cpu_808x_test_machine_create(&machine, NULL, ror4_memory,
                                 sizeof(ror4_memory));
    state = execution_state(&machine);
    state.ax = 0x5a04U;
    state.flags = 0xf8d7U;
    cpu_808x_test_poke(&machine, 0x10100U, 0x83U);
    cpu_808x_test_set_state(&machine, &state);
    consumed = 0U;
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.ax == 0x5a03U);
    assert(state.flags == 0xf8d7U);
    assert(cpu_808x_test_peek(&machine, 0x10100U) == 0x48U);
    cpu_808x_test_machine_destroy(&machine);

    cpu_808x_test_machine_create(&machine, NULL, invalid, sizeof(invalid));
    state = execution_state(&machine);
    state.bx = 0x0083U;
    cpu_808x_test_set_state(&machine, &state);
    consumed = 99U;
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_UNSUPPORTED);
    assert(cpu_808x_test_get_state(&machine).bx == 0x0083U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_bit_fields(void)
{
    static const uint8_t ext_register[] = { 0x0fU, 0x33U, 0xd1U };
    static const uint8_t ext_immediate[] = { 0x0fU, 0x3bU, 0xc1U, 0x0fU };
    static const uint8_t ins_override[] = {
        0x3eU, 0x0fU, 0x39U, 0xc2U, 0x07U
    };
    static const uint8_t ins_register[] = { 0x0fU, 0x31U, 0xcaU };
    static const uint8_t source[] = { 0xa5U, 0x5aU };
    static const uint8_t wide_source[] = { 0x00U, 0x80U, 0x34U, 0x12U };
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;
    bm_tick_t consumed = 0U;

    cpu_808x_test_machine_create(&machine, NULL, ext_register,
                                 sizeof(ext_register));
    state = execution_state(&machine);
    state.cx = 6U;
    state.dx = 3U;
    state.si = 0x0100U;
    cpu_808x_test_write(&machine, 0x10100U, source, sizeof(source));
    cpu_808x_test_set_state(&machine, &state);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.ax == 0x000aU && state.cx == 10U && state.si == 0x0100U);
    cpu_808x_test_machine_destroy(&machine);

    cpu_808x_test_machine_create(&machine, NULL, ext_immediate,
                                 sizeof(ext_immediate));
    state = execution_state(&machine);
    state.cx = 15U;
    state.si = 0x0100U;
    cpu_808x_test_write(&machine, 0x10100U, wide_source, sizeof(wide_source));
    cpu_808x_test_set_state(&machine, &state);
    consumed = 0U;
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.ax == 0x2469U && state.cx == 15U && state.si == 0x0102U);
    cpu_808x_test_machine_destroy(&machine);

    cpu_808x_test_machine_create(&machine, NULL, ins_override,
                                 sizeof(ins_override));
    state = execution_state(&machine);
    state.ax = 0x00abU;
    state.dx = 4U;
    state.di = 0x0100U;
    cpu_808x_test_poke(&machine, 0x10100U, 0xf0U);
    cpu_808x_test_poke(&machine, 0x10101U, 0x0fU);
    cpu_808x_test_poke(&machine, 0x20100U, 0x55U);
    cpu_808x_test_set_state(&machine, &state);
    consumed = 0U;
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(cpu_808x_test_peek(&machine, 0x10100U) == 0xb0U);
    assert(cpu_808x_test_peek(&machine, 0x10101U) == 0x0aU);
    assert(cpu_808x_test_peek(&machine, 0x20100U) == 0x55U);
    assert(state.dx == 12U && state.di == 0x0100U);
    cpu_808x_test_machine_destroy(&machine);

    cpu_808x_test_machine_create(&machine, NULL, ins_register,
                                 sizeof(ins_register));
    state = execution_state(&machine);
    state.ax = 0x005aU;
    state.cx = 7U;
    state.dx = 12U;
    state.di = 0x0100U;
    cpu_808x_test_set_state(&machine, &state);
    consumed = 0U;
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(cpu_808x_test_peek(&machine, 0x20101U) == 0xa0U);
    assert(cpu_808x_test_peek(&machine, 0x20102U) == 0x05U);
    assert(state.dx == 4U && state.di == 0x0102U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_invalid_bit_fields_and_extension(void)
{
    static const uint8_t invalid_programs[][4] = {
        { 0x0fU, 0x33U, 0x06U, 0x00U }, /* Memory ModR/M is not encoded. */
        { 0x0fU, 0x3bU, 0xc9U, 0x03U }, /* Immediate form reserves reg. */
        { 0x0fU, 0x3bU, 0xc1U, 0x13U }, /* imm4 high bits are undefined. */
        { 0x0fU, 0x21U, 0x00U, 0x00U }  /* Blank extension-map entry. */
    };
    size_t index;

    for (index = 0U; index < sizeof(invalid_programs) /
                                  sizeof(invalid_programs[0]); ++index) {
        cpu_808x_test_machine_t machine;
        bm_808x_arch_state_t state;
        bm_tick_t consumed = 99U;

        cpu_808x_test_machine_create(&machine, NULL, invalid_programs[index],
                                     sizeof(invalid_programs[index]));
        state = execution_state(&machine);
        state.ax = 0x1234U;
        state.cx = 1U;
        cpu_808x_test_set_state(&machine, &state);
        assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_UNSUPPORTED);
        state = cpu_808x_test_get_state(&machine);
        assert(consumed == 0U && state.ax == 0x1234U);
        cpu_808x_test_machine_destroy(&machine);
    }

    {
        static const uint8_t ext_register[] = { 0x0fU, 0x33U, 0xd1U };
        const uint16_t operands[][2] = {
            { 0x10U, 3U }, /* Invalid bit offset. */
            { 1U, 0x10U }  /* Invalid length code. */
        };
        size_t operand_index;

        for (operand_index = 0U; operand_index < sizeof(operands) /
                                               sizeof(operands[0]);
             ++operand_index) {
            cpu_808x_test_machine_t machine;
            bm_808x_arch_state_t state;
            bm_tick_t consumed = 99U;

            cpu_808x_test_machine_create(&machine, NULL, ext_register,
                                         sizeof(ext_register));
            state = execution_state(&machine);
            state.cx = operands[operand_index][0];
            state.dx = operands[operand_index][1];
            cpu_808x_test_set_state(&machine, &state);
            assert(cpu_808x_test_step(&machine, &consumed) ==
                   BM_STATUS_UNSUPPORTED);
            state = cpu_808x_test_get_state(&machine);
            assert(state.ax == 0U && state.si == 0U);
            assert(state.cx == operands[operand_index][0]);
            assert(state.dx == operands[operand_index][1]);
            cpu_808x_test_machine_destroy(&machine);
        }
    }
}

int
main(void)
{
    test_bit_operation_matrix();
    test_immediate_bit_memory_layout_and_reserved_field();
    test_bcd_strings();
    test_bcd_invalid_lengths();
    test_nibble_rotates();
    test_bit_fields();
    test_invalid_bit_fields_and_extension();
    return 0;
}
