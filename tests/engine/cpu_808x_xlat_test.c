/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "cpu_808x_test_harness.h"

#include <assert.h>
#include <stdint.h>

int
main(void)
{
    static const uint8_t program[] = {
        0xb8, 0x10, 0x00,       /* MOV AX,0010h. */
        0x8e, 0xd8,             /* MOV DS,AX. */
        0xbb, 0x00, 0x01,       /* MOV BX,0100h. */
        0xb8, 0x00, 0xd5,       /* MOV AX,D500h. */
        0x9e,                   /* SAHF: XLAT must preserve flags. */
        0xb0, 0x02,             /* MOV AL,2. */
        0xd7,                   /* XLAT DS:[BX+AL] -> 44h. */
        0x86, 0xd0,             /* XCHG AL,DL: retain first result. */
        0xb0, 0x02,             /* MOV AL,2. */
        0x2e, 0xd7,             /* XLAT CS:[BX+AL] -> 55h. */
        0xf4                    /* HLT. */
    };
    cpu_808x_test_machine_t machine;

    cpu_808x_test_machine_create(&machine, NULL, program, sizeof(program));
    cpu_808x_test_poke(&machine, 0x0202U, 0x44U);  /* DS:0102h. */
    cpu_808x_test_poke(&machine, 0xf0102U, 0x55U); /* CS:0102h. */
    assert(cpu_808x_test_run(&machine, 20U) == BM_STATUS_OK);

    assert(cpu_808x_test_inspect(&machine, "halted") == 1U);
    assert(cpu_808x_test_inspect(&machine, "ax") == 0xd555U);
    assert(cpu_808x_test_inspect(&machine, "dx") == 0x0044U);
    assert((cpu_808x_test_inspect(&machine, "flags") & 0x00d5U) == 0x00d5U);

    cpu_808x_test_machine_destroy(&machine);
    return 0;
}
