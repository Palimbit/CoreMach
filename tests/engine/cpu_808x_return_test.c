/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "cpu_808x_test_harness.h"

#include <assert.h>
#include <stdint.h>

int
main(void)
{
    static const uint8_t program[] = {
        0xbd, 0x01, 0x00,       /* MOV BP,1 */
        0xbb, 0x02, 0x00,       /* MOV BX,2 */
        0x01, 0xdd,             /* ADD BP,BX -> 3. */
        0xf9,                   /* STC */
        0xbe, 0xff, 0xff,       /* MOV SI,FFFFh */
        0xbf, 0x00, 0x00,       /* MOV DI,0 */
        0x11, 0xfe,             /* ADC SI,DI -> 0, carry remains set. */
        0xba, 0x00, 0x00,       /* MOV DX,0 */
        0x19, 0xfa,             /* SBB DX,DI -> FFFFh. */
        0xb8, 0x00, 0xd5,       /* MOV AX,D500h */
        0x9e,                   /* SAHF: set all five status flags. */
        0xb8, 0x00, 0x00,       /* MOV AX,0 */
        0x9f,                   /* LAHF: restore D7h (bit 1 is fixed). */
        0xb8, 0x00, 0x02,       /* MOV AX,0200h */
        0x8e, 0xd0,             /* MOV SS,AX */
        0xbc, 0x00, 0x01,       /* MOV SP,0100h */
        0xb8, 0x11, 0x11, 0x50, /* First discarded argument. */
        0xb8, 0x22, 0x22, 0x50, /* Second discarded argument. */
        0x0e,                   /* PUSH CS */
        0xb8, 0x36, 0x00, 0x50, /* PUSH return IP (0036h). */
        0xca, 0x04, 0x00,       /* RETF 4 */
        0x9f,                   /* LAHF */
        0xf4                    /* HLT */
    };
    cpu_808x_test_machine_t machine;

    cpu_808x_test_machine_create(&machine, NULL, program, sizeof(program));
    assert(cpu_808x_test_run(&machine, 27U) == BM_STATUS_OK);
    assert(cpu_808x_test_inspect(&machine, "halted") == 1U);
    assert(cpu_808x_test_inspect(&machine, "cs") == 0xf000U);
    assert(cpu_808x_test_inspect(&machine, "ip") == 0x0038U);
    assert(cpu_808x_test_inspect(&machine, "sp") == 0x0100U);
    assert(cpu_808x_test_inspect(&machine, "bp") == 3U);
    assert(cpu_808x_test_inspect(&machine, "si") == 0U);
    assert(cpu_808x_test_inspect(&machine, "dx") == 0xffffU);
    assert(cpu_808x_test_inspect(&machine, "ax") == 0xd736U);
    assert((cpu_808x_test_inspect(&machine, "flags") & 0x00d5U) == 0x00d5U);

    cpu_808x_test_machine_destroy(&machine);
    return 0;
}
