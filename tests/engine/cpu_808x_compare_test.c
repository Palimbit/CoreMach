/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "cpu_808x_test_harness.h"

#include <assert.h>
#include <stdint.h>

int
main(void)
{
    static const uint8_t program[] = {
        0xb3, 0xaa,             /* MOV BL,AAh. */
        0xb0, 0xaa,             /* MOV AL,AAh. */
        0x3a, 0xd8,             /* CMP BL,AL. */
        0x75, 0x20,             /* JNE failure. */
        0xb0, 0xab,             /* MOV AL,ABh. */
        0x3a, 0xd8,             /* CMP BL,AL: carry and sign. */
        0x73, 0x1a,             /* JNC failure: CMP must set carry. */
        0x79, 0x18,             /* JNS failure: CMP must set sign. */
        0xf6, 0xd3,             /* NOT BL. */
        0x80, 0xe3, 0x0f,       /* AND BL,0Fh. */
        0xf6, 0xc3, 0x01,       /* TEST BL,01h. */
        0xa8, 0x01,             /* TEST AL,01h. */
        0x74, 0x0c,             /* JZ failure. */
        0x80, 0xcc, 0x20,       /* OR AH,20h. */
        0x22, 0xe3,             /* AND AH,BL. */
        0xa9, 0x00, 0xff,       /* TEST AX,FF00h: zero, unchanged. */
        0x75, 0x02,             /* JNZ failure. */
        0xf4,                   /* HLT success. */
        0x90,
        0xf4                    /* HLT failure. */
    };
    cpu_808x_test_machine_t machine;

    cpu_808x_test_machine_create(&machine, NULL, program, sizeof(program));
    assert(cpu_808x_test_run(&machine, 32U) == BM_STATUS_OK);
    assert(cpu_808x_test_inspect(&machine, "halted") == 1U);
    assert(cpu_808x_test_inspect(&machine, "last_fetch") == 0xf0026U);
    assert(cpu_808x_test_inspect(&machine, "bx") == 0x0005U);
    assert(cpu_808x_test_inspect(&machine, "ax") == 0x00abU);
    /* TEST clears CF; the final AND result is zero. */
    assert((cpu_808x_test_inspect(&machine, "flags") & 0x0001U) == 0U);
    assert((cpu_808x_test_inspect(&machine, "flags") & 0x0040U) != 0U);

    cpu_808x_test_machine_destroy(&machine);
    return 0;
}
