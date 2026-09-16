/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "cpu_808x_test_harness.h"

#include <assert.h>
#include <stdint.h>

int
main(void)
{
    static const uint8_t program[] = {
        0xb8, 0x00, 0x02,       /* MOV AX,0200h. */
        0x8e, 0xd0,             /* MOV SS,AX. */
        0xbc, 0x00, 0x01,       /* MOV SP,0100h. */
        0x2e, 0xff, 0x1e, 0x30, 0x00, /* CALL FAR CS:[0030h]. */
        0xbb, 0x34, 0x12,       /* MOV BX,1234h after return. */
        0x2e, 0xff, 0x2e, 0x34, 0x00 /* JMP FAR CS:[0034h]. */
    };
    static const uint8_t far_pointers[] = {
        0x10U, 0x00U, 0x00U, 0xe0U, /* E000:0010. */
        0x20U, 0x00U, 0x00U, 0xd0U  /* D000:0020. */
    };
    static const uint8_t called_program[] = {
        0xb8U, 0xefU, 0xbeU, /* MOV AX,BEEFh. */
        0xcbU                /* RETF. */
    };
    static const uint8_t jumped_program[] = {
        0xbaU, 0x78U, 0x56U, /* MOV DX,5678h. */
        0xbcU, 0x80U, 0x00U, /* MOV SP,0080h. */
        0x9aU, 0x30U, 0x00U, 0x00U, 0xc0U, /* CALL FAR C000:0030. */
        0xf4U                /* HLT after return. */
    };
    static const uint8_t nested_program[] = {
        0xbeU, 0xbcU, 0x9aU, /* MOV SI,9ABCh. */
        0xcbU                /* RETF. */
    };
    cpu_808x_test_machine_t machine;

    cpu_808x_test_machine_create(&machine, NULL, program, sizeof(program));
    cpu_808x_test_write(&machine, 0xf0030U, far_pointers,
                        sizeof(far_pointers));
    cpu_808x_test_write(&machine, 0xe0010U, called_program,
                        sizeof(called_program));
    cpu_808x_test_write(&machine, 0xd0020U, jumped_program,
                        sizeof(jumped_program));
    cpu_808x_test_write(&machine, 0xc0030U, nested_program,
                        sizeof(nested_program));
    assert(cpu_808x_test_run(&machine, 16U) == BM_STATUS_OK);

    assert(cpu_808x_test_inspect(&machine, "halted") == 1U);
    assert(cpu_808x_test_inspect(&machine, "cs") == 0xd000U);
    assert(cpu_808x_test_inspect(&machine, "ip") == 0x002cU);
    assert(cpu_808x_test_inspect(&machine, "sp") == 0x0080U);
    assert(cpu_808x_test_inspect(&machine, "ax") == 0xbeefU);
    assert(cpu_808x_test_inspect(&machine, "bx") == 0x1234U);
    assert(cpu_808x_test_inspect(&machine, "dx") == 0x5678U);
    assert(cpu_808x_test_inspect(&machine, "si") == 0x9abcU);
    assert(cpu_808x_test_peek(&machine, 0x020fcU) == 0x0dU);
    assert(cpu_808x_test_peek(&machine, 0x020fdU) == 0x00U);
    assert(cpu_808x_test_peek(&machine, 0x020feU) == 0x00U);
    assert(cpu_808x_test_peek(&machine, 0x020ffU) == 0xf0U);
    assert(cpu_808x_test_peek(&machine, 0x0207cU) == 0x2bU);
    assert(cpu_808x_test_peek(&machine, 0x0207dU) == 0x00U);
    assert(cpu_808x_test_peek(&machine, 0x0207eU) == 0x00U);
    assert(cpu_808x_test_peek(&machine, 0x0207fU) == 0xd0U);

    cpu_808x_test_machine_destroy(&machine);
    return 0;
}
