/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "cpu_808x_test_harness.h"

#include <assert.h>
#include <stdint.h>

#define FLAG_CF 0x0001U
#define FLAG_ZF 0x0040U
#define FLAG_OF 0x0800U

int
main(void)
{
    static const uint8_t program[] = {
        0xb8, 0x01, 0x00,       /* MOV AX,1. */
        0xf7, 0xd8,             /* NEG AX -> FFFFh. */
        0x93,                   /* XCHG AX,BX. */
        0xb8, 0x00, 0x80,       /* MOV AX,8000h. */
        0xf7, 0xd8,             /* NEG AX -> 8000h, overflow. */
        0x9c,                   /* PUSHF. */
        0x5a,                   /* POP DX. */
        0xf6, 0x1e, 0x00, 0x01, /* NEG byte [0100h]: 1 -> FFh. */
        0xb8, 0x00, 0x00,       /* MOV AX,0. */
        0xf6, 0xd8,             /* NEG AL -> 0. */
        0x9c,                   /* PUSHF. */
        0x59,                   /* POP CX. */
        0xf4                    /* HLT. */
    };
    cpu_808x_test_machine_t machine;

    cpu_808x_test_machine_create(&machine, NULL, program, sizeof(program));
    cpu_808x_test_poke(&machine, 0x0100U, 1U);
    assert(cpu_808x_test_run(&machine, 24U) == BM_STATUS_OK);

    assert(cpu_808x_test_inspect(&machine, "halted") == 1U);
    assert(cpu_808x_test_inspect(&machine, "ax") == 0U);
    assert(cpu_808x_test_inspect(&machine, "bx") == 0xffffU);
    assert((cpu_808x_test_inspect(&machine, "dx") & (FLAG_CF | FLAG_OF)) ==
           (FLAG_CF | FLAG_OF));
    assert((cpu_808x_test_inspect(&machine, "dx") & FLAG_ZF) == 0U);
    assert((cpu_808x_test_inspect(&machine, "cx") & FLAG_ZF) != 0U);
    assert((cpu_808x_test_inspect(&machine, "cx") & (FLAG_CF | FLAG_OF)) == 0U);
    assert(cpu_808x_test_peek(&machine, 0x0100U) == 0xffU);

    cpu_808x_test_machine_destroy(&machine);
    return 0;
}
