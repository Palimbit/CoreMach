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
        0xb8, 0x10, 0x00,       /* MOV AX,0010h. */
        0x8e, 0xc0,             /* MOV ES,AX. */
        0xb8, 0x00, 0x00,       /* MOV AX,0. */
        0xf9,                   /* STC. */
        0x26, 0x13, 0x06, 0x00, 0x01, /* ADC AX,ES:[0100h] -> 0. */
        0x9c,                   /* PUSHF. */
        0x5b,                   /* POP BX. */
        0xba, 0x00, 0x00,       /* MOV DX,0. */
        0xf9,                   /* STC. */
        0x26, 0x1b, 0x16, 0x02, 0x01, /* SBB DX,ES:[0102h] -> FFFFh. */
        0xf4                    /* HLT. */
    };
    static const uint8_t operands[] = { 0xffU, 0xffU, 0x00U, 0x00U };
    cpu_808x_test_machine_t machine;

    cpu_808x_test_machine_create(&machine, NULL, program, sizeof(program));
    cpu_808x_test_write(&machine, 0x0200U, operands, sizeof(operands));
    assert(cpu_808x_test_run(&machine, 20U) == BM_STATUS_OK);

    assert(cpu_808x_test_inspect(&machine, "halted") == 1U);
    assert(cpu_808x_test_inspect(&machine, "ax") == 0U);
    assert((cpu_808x_test_inspect(&machine, "bx") & (FLAG_CF | FLAG_ZF)) ==
           (FLAG_CF | FLAG_ZF));
    assert(cpu_808x_test_inspect(&machine, "dx") == 0xffffU);
    assert((cpu_808x_test_inspect(&machine, "flags") & FLAG_CF) != 0U);
    assert((cpu_808x_test_inspect(&machine, "flags") & FLAG_ZF) == 0U);

    cpu_808x_test_machine_destroy(&machine);
    return 0;
}
