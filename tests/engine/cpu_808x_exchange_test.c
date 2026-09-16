/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "cpu_808x_test_harness.h"

#include <assert.h>
#include <stdint.h>

int
main(void)
{
    static const uint8_t program[] = {
        0xb8, 0x10, 0x00,       /* MOV AX,0010h. */
        0x8e, 0xc0,             /* MOV ES,AX. */
        0xb8, 0x00, 0xd5,       /* MOV AX,D500h. */
        0x9e,                   /* SAHF: flags must survive both exchanges. */
        0xb8, 0x12, 0x00,       /* MOV AX,0012h. */
        0xbb, 0xb2, 0xa1,       /* MOV BX,A1B2h. */
        0xb9, 0xd4, 0xc3,       /* MOV CX,C3D4h. */
        0x26, 0x86, 0x06, 0x00, 0x01, /* XCHG ES:[0100h],AL. */
        0x87, 0xd9,             /* XCHG CX,BX. */
        0xf4                    /* HLT. */
    };
    cpu_808x_test_machine_t machine;

    cpu_808x_test_machine_create(&machine, NULL, program, sizeof(program));
    cpu_808x_test_poke(&machine, 0x0200U, 0x34U); /* ES:0100h. */
    cpu_808x_test_poke(&machine, 0x0100U, 0x56U); /* DS:0100h untouched. */
    assert(cpu_808x_test_run(&machine, 16U) == BM_STATUS_OK);

    assert(cpu_808x_test_inspect(&machine, "halted") == 1U);
    assert(cpu_808x_test_inspect(&machine, "ax") == 0x0034U);
    assert(cpu_808x_test_inspect(&machine, "bx") == 0xc3d4U);
    assert(cpu_808x_test_inspect(&machine, "cx") == 0xa1b2U);
    assert((cpu_808x_test_inspect(&machine, "flags") & 0x00d5U) == 0x00d5U);
    assert(cpu_808x_test_peek(&machine, 0x0200U) == 0x12U);
    assert(cpu_808x_test_peek(&machine, 0x0100U) == 0x56U);

    cpu_808x_test_machine_destroy(&machine);
    return 0;
}
