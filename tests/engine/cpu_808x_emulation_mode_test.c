/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * NEC V30 8080-emulation contracts derived from the NEC V20/V30 User's
 * Manual and the NEC 16-Bit V Series Instruction User's Manual. No firmware
 * or external vector corpus is used by this suite.
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
    FLAG_IF = 0x0200,
    FLAG_MD = 0x8000
};

typedef struct interrupt_source {
    uint8_t vector;
    unsigned int calls;
} interrupt_source_t;

static bm_status_t
acknowledge_interrupt(void *context, uint8_t *vector)
{
    interrupt_source_t *source = context;
    ++source->calls;
    *vector = source->vector;
    return BM_STATUS_OK;
}

static uint16_t
peek_word(const cpu_808x_test_machine_t *machine, uint64_t address)
{
    return (uint16_t) (cpu_808x_test_peek(machine, address) |
                       ((uint16_t) cpu_808x_test_peek(machine, address + 1U) <<
                        8U));
}

static void
poke_word(cpu_808x_test_machine_t *machine, uint64_t address, uint16_t value)
{
    cpu_808x_test_poke(machine, address, (uint8_t) value);
    cpu_808x_test_poke(machine, address + 1U, (uint8_t) (value >> 8U));
}

static bm_808x_arch_state_t
emulation_state(cpu_808x_test_machine_t *machine)
{
    bm_808x_arch_state_t state = cpu_808x_test_get_state(machine);
    state.cs = 0xf000U;
    state.ds = 0x2000U;
    state.ss = 0x3000U;
    state.ip = 0U;
    state.sp = 0x0100U;
    state.bp = 0x0200U;
    state.flags = 0x7002U;
    state.md_write_enabled = 1U;
    return state;
}

static bm_status_t
step(cpu_808x_test_machine_t *machine)
{
    bm_tick_t consumed = 99U;
    bm_status_t status = cpu_808x_test_step(machine, &consumed);
    assert((status != BM_STATUS_OK && status != BM_STATUS_IDLE) ||
           consumed == 1U);
    return status;
}

static void
test_register_memory_stack_and_alu_mapping(void)
{
    static const uint8_t program[] = {
        0x01U, 0x34U, 0x12U, /* LXI B,1234h -> CW. */
        0x11U, 0x78U, 0x56U, /* LXI D,5678h -> DW. */
        0x21U, 0x00U, 0x01U, /* LXI H,0100h -> BW. */
        0x36U, 0x7fU,       /* MVI M,7fh through DS0:HL. */
        0x7eU,              /* MOV A,M -> AL. */
        0xc6U, 0x01U,       /* ADI 1. */
        0xd6U, 0x81U,       /* SUI 81h. */
        0xe6U, 0x0fU,       /* ANI 0fh. */
        0xc5U,              /* PUSH B through DS0:BP. */
        0xd1U               /* POP D. */
    };
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;
    unsigned int index;

    cpu_808x_test_machine_create(&machine, NULL, program, sizeof(program));
    state = emulation_state(&machine);
    state.ax = 0xaa00U;
    state.bx = 0xbb00U;
    state.cx = 0xcc00U;
    state.dx = 0xdd00U;
    cpu_808x_test_set_state(&machine, &state);
    for (index = 0U; index < 10U; ++index)
        assert(step(&machine) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.cx == 0x1234U && state.dx == 0x1234U);
    assert(state.bx == 0x0100U && state.ax == 0xaa0fU);
    assert(state.bp == 0x0200U && state.sp == 0x0100U);
    assert(cpu_808x_test_peek(&machine, 0x20100U) == 0x7fU);
    assert(peek_word(&machine, 0x201feU) == 0x1234U);
    assert((state.flags & (FLAG_CF | FLAG_AF | FLAG_PF | FLAG_ZF | FLAG_SF)) ==
           (FLAG_AF | FLAG_PF));
    assert((state.flags & FLAG_MD) == 0U && state.md_write_enabled == 1U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_calln_iret_and_retem(void)
{
    static const uint8_t calln[] = { 0xedU, 0xedU, 0x20U, 0x00U };
    static const uint8_t retem[] = { 0xedU, 0xfdU };
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;

    cpu_808x_test_machine_create(&machine, NULL, calln, sizeof(calln));
    poke_word(&machine, 0x20U * 4U, 0x0010U);
    poke_word(&machine, 0x20U * 4U + 2U, 0xf100U);
    cpu_808x_test_poke(&machine, 0xf1010U, 0xcfU); /* Native RETI. */
    state = emulation_state(&machine);
    state.flags |= FLAG_IF;
    cpu_808x_test_set_state(&machine, &state);
    assert(step(&machine) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.cs == 0xf100U && state.ip == 0x0010U);
    assert((state.flags & FLAG_MD) != 0U && (state.flags & FLAG_IF) == 0U);
    assert(state.md_write_enabled == 1U && state.sp == 0x00faU);
    assert(peek_word(&machine, 0x300faU) == 3U);
    assert(peek_word(&machine, 0x300fcU) == 0xf000U);
    assert((peek_word(&machine, 0x300feU) & (FLAG_MD | FLAG_IF)) == FLAG_IF);
    assert(step(&machine) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.cs == 0xf000U && state.ip == 3U);
    assert((state.flags & (FLAG_MD | FLAG_IF)) == FLAG_IF);
    assert(state.md_write_enabled == 1U && state.sp == 0x0100U);
    cpu_808x_test_machine_destroy(&machine);

    cpu_808x_test_machine_create(&machine, NULL, retem, sizeof(retem));
    state = emulation_state(&machine);
    poke_word(&machine, 0x30100U, 0x4567U);
    poke_word(&machine, 0x30102U, 0x1234U);
    poke_word(&machine, 0x30104U, 0xf047U);
    cpu_808x_test_set_state(&machine, &state);
    assert(step(&machine) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.cs == 0x1234U && state.ip == 0x4567U);
    assert(state.sp == 0x0106U && state.flags == 0xf047U);
    assert(state.md_write_enabled == 0U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_subtraction_auxiliary_carry_uses_8080_sense(void)
{
    static const uint8_t program[] = {
        0x3eU, 0x00U, /* MVI A,0. */
        0xd6U, 0x01U  /* SUI 1: borrow, but no two's-complement half carry. */
    };
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;

    cpu_808x_test_machine_create(&machine, NULL, program, sizeof(program));
    state = emulation_state(&machine);
    cpu_808x_test_set_state(&machine, &state);
    assert(step(&machine) == BM_STATUS_OK);
    assert(step(&machine) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert((state.ax & 0x00ffU) == 0x00ffU);
    assert((state.flags & FLAG_CF) != 0U);
    assert((state.flags & FLAG_AF) == 0U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_native_interrupt_round_trip_and_disabled_halt_wake(void)
{
    static const uint8_t program[] = { 0x00U, 0x76U, 0x00U };
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;
    interrupt_source_t source = { 0x21U, 0U };
    cpu_808x_test_config_t config = {
        .interrupt_ack = acknowledge_interrupt,
        .interrupt_context = &source
    };

    cpu_808x_test_machine_create(&machine, &config, program, sizeof(program));
    poke_word(&machine, 0x21U * 4U, 0x0010U);
    poke_word(&machine, 0x21U * 4U + 2U, 0xf100U);
    cpu_808x_test_poke(&machine, 0xf1010U, 0xcfU);
    state = emulation_state(&machine);
    state.flags |= FLAG_IF;
    cpu_808x_test_set_state(&machine, &state);
    assert(machine.cpu.ops.signal(machine.cpu.context, BM_808X_SIGNAL_INT, 1) ==
           BM_STATUS_OK);
    assert(step(&machine) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(source.calls == 1U && state.cs == 0xf100U && state.ip == 0x0010U);
    assert((state.flags & FLAG_MD) != 0U && state.md_write_enabled == 1U);
    assert(machine.cpu.ops.signal(machine.cpu.context, BM_808X_SIGNAL_INT, 0) ==
           BM_STATUS_OK);
    assert(step(&machine) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.cs == 0xf000U && state.ip == 0U);
    assert((state.flags & (FLAG_MD | FLAG_IF)) == FLAG_IF);

    state.flags &= (uint16_t) ~FLAG_IF;
    state.ip = 1U;
    cpu_808x_test_set_state(&machine, &state);
    assert(machine.cpu.ops.signal(machine.cpu.context, BM_808X_SIGNAL_INT, 1) ==
           BM_STATUS_OK);
    assert(step(&machine) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.ip == 2U && state.halted == 0U && source.calls == 1U);
    assert(step(&machine) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.ip == 3U && state.halted == 0U && source.calls == 1U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_nmi_enters_native_mode_and_iret_restores_emulation(void)
{
    static const uint8_t program[] = { 0x00U };
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;

    cpu_808x_test_machine_create(&machine, NULL, program, sizeof(program));
    poke_word(&machine, 2U * 4U, 0x0010U);
    poke_word(&machine, 2U * 4U + 2U, 0xf100U);
    cpu_808x_test_poke(&machine, 0xf1010U, 0xcfU); /* Native RETI. */
    state = emulation_state(&machine);
    cpu_808x_test_set_state(&machine, &state);
    assert(machine.cpu.ops.signal(machine.cpu.context, BM_808X_SIGNAL_NMI, 1) ==
           BM_STATUS_OK);
    assert(step(&machine) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.cs == 0xf100U && state.ip == 0x0010U);
    assert((state.flags & FLAG_MD) != 0U && state.md_write_enabled == 1U);
    assert(peek_word(&machine, 0x300faU) == 0U);
    assert(peek_word(&machine, 0x300fcU) == 0xf000U);
    assert((peek_word(&machine, 0x300feU) & FLAG_MD) == 0U);
    assert(machine.cpu.ops.signal(machine.cpu.context, BM_808X_SIGNAL_NMI, 0) ==
           BM_STATUS_OK);
    assert(step(&machine) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.cs == 0xf000U && state.ip == 0U);
    assert((state.flags & FLAG_MD) == 0U && state.md_write_enabled == 1U);
    cpu_808x_test_machine_destroy(&machine);
}

static int
is_undefined_opcode(unsigned int opcode)
{
    switch (opcode) {
        case 0x08U:
        case 0x10U:
        case 0x18U:
        case 0x20U:
        case 0x28U:
        case 0x30U:
        case 0x38U:
        case 0xcbU:
        case 0xd9U:
        case 0xddU:
        case 0xedU:
        case 0xfdU:
            return 1;
        default:
            return 0;
    }
}

static void
test_complete_documented_opcode_map(void)
{
    unsigned int opcode;

    for (opcode = 0U; opcode <= 0xffU; ++opcode) {
        uint8_t program[] = { (uint8_t) opcode, 0U, 0U, 0U };
        cpu_808x_test_machine_t machine;
        bm_808x_arch_state_t state;
        bm_status_t status;

        cpu_808x_test_machine_create(&machine, NULL, program,
                                     sizeof(program));
        state = emulation_state(&machine);
        cpu_808x_test_set_state(&machine, &state);
        status = step(&machine);
        if (is_undefined_opcode(opcode))
            assert(status == BM_STATUS_UNSUPPORTED);
        else
            assert((status == BM_STATUS_OK) || (status == BM_STATUS_IDLE) ||
                   (status == BM_STATUS_UNMAPPED));
        cpu_808x_test_machine_destroy(&machine);
    }
}

static void
test_emulation_ei_delays_native_interrupt(void)
{
    static const uint8_t program[] = { 0xfbU, 0x00U, 0x00U };
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;
    interrupt_source_t source = { 0x22U, 0U };
    cpu_808x_test_config_t config = {
        .interrupt_ack = acknowledge_interrupt,
        .interrupt_context = &source
    };

    cpu_808x_test_machine_create(&machine, &config, program, sizeof(program));
    poke_word(&machine, 0x22U * 4U, 0x4567U);
    poke_word(&machine, 0x22U * 4U + 2U, 0x1234U);
    state = emulation_state(&machine);
    cpu_808x_test_set_state(&machine, &state);
    assert(machine.cpu.ops.signal(machine.cpu.context, BM_808X_SIGNAL_INT, 1) ==
           BM_STATUS_OK);
    assert(step(&machine) == BM_STATUS_OK); /* EI. */
    state = cpu_808x_test_get_state(&machine);
    assert(state.ip == 1U && state.interrupt_inhibit == 1U && source.calls == 0U);
    assert(step(&machine) == BM_STATUS_OK); /* Required following instruction. */
    state = cpu_808x_test_get_state(&machine);
    assert(state.ip == 2U && state.interrupt_inhibit == 0U && source.calls == 0U);
    assert(step(&machine) == BM_STATUS_OK); /* Native interrupt entry. */
    state = cpu_808x_test_get_state(&machine);
    assert(source.calls == 1U && state.cs == 0x1234U && state.ip == 0x4567U);
    assert((state.flags & FLAG_MD) != 0U && state.md_write_enabled == 1U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_nested_brkem_and_undefined_group_are_rejected(void)
{
    static const uint8_t nested[] = { 0x0fU, 0xffU, 0x12U };
    static const uint8_t undefined_group[] = { 0xedU, 0x00U };
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;

    cpu_808x_test_machine_create(&machine, NULL, nested, sizeof(nested));
    state = cpu_808x_test_get_state(&machine);
    state.cs = 0xf000U;
    state.ss = 0x3000U;
    state.sp = 0x0100U;
    state.md_write_enabled = 1U;
    cpu_808x_test_set_state(&machine, &state);
    assert(step(&machine) == BM_STATUS_UNSUPPORTED);
    state = cpu_808x_test_get_state(&machine);
    assert(state.ip == 3U && state.sp == 0x0100U);
    cpu_808x_test_machine_destroy(&machine);

    cpu_808x_test_machine_create(&machine, NULL, undefined_group,
                                 sizeof(undefined_group));
    state = emulation_state(&machine);
    cpu_808x_test_set_state(&machine, &state);
    assert(step(&machine) == BM_STATUS_UNSUPPORTED);
    state = cpu_808x_test_get_state(&machine);
    assert(state.ip == 2U && (state.flags & FLAG_MD) == 0U);
    cpu_808x_test_machine_destroy(&machine);
}

int
main(void)
{
    test_register_memory_stack_and_alu_mapping();
    test_calln_iret_and_retem();
    test_subtraction_auxiliary_carry_uses_8080_sense();
    test_native_interrupt_round_trip_and_disabled_halt_wake();
    test_nmi_enters_native_mode_and_iret_restores_emulation();
    test_complete_documented_opcode_map();
    test_emulation_ei_delays_native_interrupt();
    test_nested_brkem_and_undefined_group_are_rejected();
    return 0;
}
