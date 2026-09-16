/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "cpu_808x_test_harness.h"

#include <assert.h>
#include <stdint.h>

#define FLAG_CF 0x0001U
#define FLAG_ZF 0x0040U

int
main(void)
{
    static const uint8_t program[] = {
        0xba, 0x00, 0x12,       /* MOV DX,1200h. */
        0xf9,                   /* STC. */
        0x80, 0xd6, 0x34,       /* ADC DH,34h -> 47h. */
        0x8b, 0xea,             /* MOV BP,DX: retain byte ADC result. */
        0xb8, 0x00, 0x00,       /* MOV AX,0. */
        0xf9,                   /* STC. */
        0x80, 0xd8, 0x00,       /* SBB AL,0 -> FFh. */
        0x8b, 0xf0,             /* MOV SI,AX: retain byte SBB result. */
        0xb8, 0xff, 0xff,       /* MOV AX,FFFFh. */
        0xf9,                   /* STC. */
        0x81, 0xd0, 0x00, 0x00, /* ADC AX,0 -> 0 with carry. */
        0x9c,                   /* PUSHF. */
        0x5b,                   /* POP BX. */
        0xb9, 0x00, 0x00,       /* MOV CX,0. */
        0xf9,                   /* STC. */
        0x83, 0xd9, 0x00,       /* SBB CX,+0 -> FFFFh. */
        0x9c,                   /* PUSHF. */
        0x5a,                   /* POP DX. */
        0xf4                    /* HLT. */
    };
    cpu_808x_test_machine_t machine;

    cpu_808x_test_machine_create(&machine, NULL, program, sizeof(program));
    assert(cpu_808x_test_run(&machine, 32U) == BM_STATUS_OK);

    assert(cpu_808x_test_inspect(&machine, "halted") == 1U);
    assert(cpu_808x_test_inspect(&machine, "bp") == 0x4700U);
    assert(cpu_808x_test_inspect(&machine, "si") == 0x00ffU);
    assert(cpu_808x_test_inspect(&machine, "ax") == 0U);
    assert((cpu_808x_test_inspect(&machine, "bx") & (FLAG_CF | FLAG_ZF)) ==
           (FLAG_CF | FLAG_ZF));
    assert(cpu_808x_test_inspect(&machine, "cx") == 0xffffU);
    assert((cpu_808x_test_inspect(&machine, "dx") & FLAG_CF) != 0U);
    assert((cpu_808x_test_inspect(&machine, "dx") & FLAG_ZF) == 0U);

    cpu_808x_test_machine_destroy(&machine);
    return 0;
}
