/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "cpu_808x_test_harness.h"

#include <assert.h>
#include <stdint.h>

int
main(void)
{
    static const uint8_t program[] = {
        0xb8, 0x00, 0xd5, /* MOV AX,D500h. */
        0x9e,             /* SAHF: establish flags that CBW must preserve. */
        0xb8, 0x7f, 0x12, /* MOV AX,127Fh. */
        0x98,             /* CBW -> AX=007Fh. */
        0x93,             /* XCHG AX,BX: retain positive result. */
        0xb8, 0x80, 0x12, /* MOV AX,1280h. */
        0x98,             /* CBW -> AX=FF80h. */
        0x91,             /* XCHG AX,CX: retain negative result. */
        0xf4              /* HLT. */
    };
    cpu_808x_test_machine_t machine;

    cpu_808x_test_machine_create(&machine, NULL, program, sizeof(program));
    assert(cpu_808x_test_run(&machine, 16U) == BM_STATUS_OK);
    assert(cpu_808x_test_inspect(&machine, "halted") == 1U);
    assert(cpu_808x_test_inspect(&machine, "bx") == 0x007fU);
    assert(cpu_808x_test_inspect(&machine, "cx") == 0xff80U);
    assert((cpu_808x_test_inspect(&machine, "flags") & 0x00d5U) == 0x00d5U);
    cpu_808x_test_machine_destroy(&machine);
    return 0;
}
