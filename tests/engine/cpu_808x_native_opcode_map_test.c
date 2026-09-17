/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Executable classification of the NEC V30 native instruction map. The map
 * and reserved encodings come from Appendix C of the NEC 16-Bit V Series
 * Instruction User's Manual. No firmware or external vector corpus is used.
 */
#include "cpu_808x_test_harness.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static bm_status_t
poll_ready(void *context, int *ready)
{
    (void) context;
    *ready = 1;
    return BM_STATUS_OK;
}

static bm_status_t
run_program(const uint8_t *program, size_t size, uint16_t cx)
{
    cpu_808x_test_config_t config = { .poll = poll_ready };
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;
    bm_tick_t consumed = 99U;
    bm_status_t status;

    cpu_808x_test_machine_create(&machine, &config, program, size);
    state = cpu_808x_test_get_state(&machine);
    state.cs = 0xf000U;
    state.ds = 0x1000U;
    state.es = 0x2000U;
    state.ss = 0x3000U;
    state.ip = 0U;
    state.sp = 0x0100U;
    state.bp = 0x0200U;
    state.cx = cx;
    cpu_808x_test_set_state(&machine, &state);
    status = cpu_808x_test_step(&machine, &consumed);
    if ((status == BM_STATUS_OK) || (status == BM_STATUS_IDLE))
        assert(consumed == 1U);
    else
        assert(consumed == 0U);
    cpu_808x_test_machine_destroy(&machine);
    return status;
}

static int
accepted_status(bm_status_t status)
{
    return (status == BM_STATUS_OK) || (status == BM_STATUS_IDLE) ||
           (status == BM_STATUS_UNMAPPED);
}

static int
undefined_primary_opcode(unsigned int opcode)
{
    return (opcode == 0x63U) || (opcode == 0xd6U) || (opcode == 0xf1U);
}

static void
test_all_primary_opcodes_are_classified(void)
{
    unsigned int opcode;

    for (opcode = 0U; opcode <= 0xffU; ++opcode) {
        uint8_t program[16] = { 0U };
        bm_status_t status;

        program[0] = (uint8_t) opcode;
        if (opcode == 0x0fU) { /* A documented Group 3 register form. */
            program[1] = 0x10U;
            program[2] = 0xc0U;
        } else if (opcode == 0x62U) { /* CHKIND requires memory. */
            program[1] = 0x06U;
            program[2] = 0x00U;
            program[3] = 0x01U;
        } else if ((opcode == 0x64U) || (opcode == 0x65U)) {
            program[1] = 0xa6U; /* REPNC/REPC with CMPBKB and CW=0. */
        } else if ((opcode == 0x66U) || (opcode == 0x67U)) {
            program[1] = 0xc0U; /* FPO2 register form. */
        } else if ((opcode == 0xf0U) || (opcode == 0xf2U) ||
                   (opcode == 0xf3U)) {
            program[1] = 0x90U;
        }
        status = run_program(program, sizeof(program), 0U);
        if (undefined_primary_opcode(opcode))
            assert(status == BM_STATUS_UNSUPPORTED);
        else
            assert(accepted_status(status));
    }
}

static int
valid_nec_extension(unsigned int extension)
{
    return ((extension >= 0x10U) && (extension <= 0x1fU)) ||
           (extension == 0x20U) || (extension == 0x22U) ||
           (extension == 0x26U) || (extension == 0x28U) ||
           (extension == 0x2aU) || (extension == 0x31U) ||
           (extension == 0x33U) || (extension == 0x39U) ||
           (extension == 0x3bU) || (extension == 0xffU);
}

static void
test_complete_nec_group3_map(void)
{
    unsigned int extension;

    for (extension = 0U; extension <= 0xffU; ++extension) {
        uint8_t program[8] = { 0x0fU, (uint8_t) extension, 0xc0U, 0U };
        uint16_t cx = 0U;
        bm_status_t status;

        if ((extension == 0x20U) || (extension == 0x22U) ||
            (extension == 0x26U))
            cx = 2U;
        status = run_program(program, sizeof(program), cx);
        if (valid_nec_extension(extension))
            assert(accepted_status(status));
        else
            assert(status == BM_STATUS_UNSUPPORTED);
    }
}

static void
test_modrm_group_classification(void)
{
    unsigned int operation;

    for (operation = 0U; operation < 8U; ++operation) {
        uint8_t immediate8[] = {
            0x80U, (uint8_t) (0xc0U | (operation << 3U)), 1U
        };
        uint8_t immediate16[] = {
            0x81U, (uint8_t) (0xc0U | (operation << 3U)), 1U, 0U
        };
        uint8_t sign_extended[] = {
            0x83U, (uint8_t) (0xc0U | (operation << 3U)), 1U
        };
        uint8_t shift_immediate8[] = {
            0xc0U, (uint8_t) (0xc0U | (operation << 3U)), 1U
        };
        uint8_t shift_immediate16[] = {
            0xc1U, (uint8_t) (0xc0U | (operation << 3U)), 1U
        };
        uint8_t shift_one8[] = {
            0xd0U, (uint8_t) (0xc0U | (operation << 3U))
        };
        uint8_t shift_cl16[] = {
            0xd3U, (uint8_t) (0xc0U | (operation << 3U))
        };
        uint8_t group1_byte[] = {
            0xf6U, (uint8_t) (0xc0U | (operation << 3U)), 1U
        };
        uint8_t group1_word[] = {
            0xf7U, (uint8_t) (0xc0U | (operation << 3U)), 1U, 0U
        };
        uint8_t group2_byte[] = {
            0xfeU, (uint8_t) (0xc0U | (operation << 3U))
        };
        uint8_t group2_word[] = {
            0xffU, (uint8_t) ((operation << 3U) | 0x06U), 0U, 1U
        };

        assert(accepted_status(run_program(immediate8, sizeof(immediate8), 0U)));
        assert(accepted_status(run_program(immediate16, sizeof(immediate16), 0U)));
        assert(accepted_status(run_program(sign_extended,
                                           sizeof(sign_extended), 0U)));
        assert(run_program(shift_immediate8, sizeof(shift_immediate8), 0U) ==
               (operation == 6U ? BM_STATUS_UNSUPPORTED : BM_STATUS_OK));
        assert(run_program(shift_immediate16, sizeof(shift_immediate16), 0U) ==
               (operation == 6U ? BM_STATUS_UNSUPPORTED : BM_STATUS_OK));
        /* The register-count /6 alias is hardware-observed in the fixed V20
         * vector corpus even though Appendix C leaves the slot undefined. */
        assert(run_program(shift_one8, sizeof(shift_one8), 0U) == BM_STATUS_OK);
        assert(run_program(shift_cl16, sizeof(shift_cl16), 0U) == BM_STATUS_OK);
        assert(accepted_status(run_program(group1_byte,
                                           sizeof(group1_byte), 0U)));
        assert(accepted_status(run_program(group1_word,
                                           sizeof(group1_word), 0U)));
        assert(run_program(group2_byte, sizeof(group2_byte), 0U) ==
               (operation <= 1U ? BM_STATUS_OK : BM_STATUS_UNSUPPORTED));
        assert(run_program(group2_word, sizeof(group2_word), 0U) ==
               (operation <= 6U ? BM_STATUS_OK : BM_STATUS_UNSUPPORTED));
    }
}

static void
test_reserved_modrm_fields_and_memory_only_forms(void)
{
    unsigned int field;

    for (field = 0U; field < 8U; ++field) {
        uint8_t pop_rm[] = { 0x8fU, (uint8_t) (0xc0U | (field << 3U)) };
        uint8_t mov_imm8[] = {
            0xc6U, (uint8_t) (0xc0U | (field << 3U)), 0U
        };
        uint8_t mov_imm16[] = {
            0xc7U, (uint8_t) (0xc0U | (field << 3U)), 0U, 0U
        };
        uint8_t mov_from_segment[] = {
            0x8cU, (uint8_t) (0xc0U | (field << 3U))
        };
        uint8_t mov_to_segment[] = {
            0x8eU, (uint8_t) (0xc0U | (field << 3U))
        };
        bm_status_t expected_pop_mov = field == 0U ?
                                       BM_STATUS_OK : BM_STATUS_UNSUPPORTED;
        bm_status_t expected_from_segment = field < 4U ?
                                            BM_STATUS_OK :
                                            BM_STATUS_UNSUPPORTED;
        bm_status_t expected_to_segment =
            ((field == 0U) || (field == 2U) || (field == 3U)) ?
            BM_STATUS_OK : BM_STATUS_UNSUPPORTED;

        assert(run_program(pop_rm, sizeof(pop_rm), 0U) == expected_pop_mov);
        assert(run_program(mov_imm8, sizeof(mov_imm8), 0U) == expected_pop_mov);
        assert(run_program(mov_imm16, sizeof(mov_imm16), 0U) == expected_pop_mov);
        assert(run_program(mov_from_segment, sizeof(mov_from_segment), 0U) ==
               expected_from_segment);
        assert(run_program(mov_to_segment, sizeof(mov_to_segment), 0U) ==
               expected_to_segment);
    }

    {
        static const uint8_t bound_register[] = { 0x62U, 0xc0U };
        static const uint8_t lea_register[] = { 0x8dU, 0xc0U };
        static const uint8_t les_register[] = { 0xc4U, 0xc0U };
        static const uint8_t lds_register[] = { 0xc5U, 0xc0U };

        assert(run_program(bound_register, sizeof(bound_register), 0U) ==
               BM_STATUS_UNSUPPORTED);
        assert(run_program(lea_register, sizeof(lea_register), 0U) ==
               BM_STATUS_UNSUPPORTED);
        assert(run_program(les_register, sizeof(les_register), 0U) ==
               BM_STATUS_UNSUPPORTED);
        assert(run_program(lds_register, sizeof(lds_register), 0U) ==
               BM_STATUS_UNSUPPORTED);
    }
}

int
main(void)
{
    test_all_primary_opcodes_are_classified();
    test_complete_nec_group3_map();
    test_modrm_group_classification();
    test_reserved_modrm_fields_and_memory_only_forms();
    return 0;
}
