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
        0xb8, 0xef, 0xbe,       /* MOV AX,BEEFh. */
        0x50,                   /* PUSH AX. */
        0x2e, 0x8f, 0x06, 0x30, 0x00, /* POP CS:[0030h]. */
        0xb8, 0x34, 0x12,       /* MOV AX,1234h. */
        0x50,                   /* PUSH AX. */
        0x8f, 0xc4,             /* POP SP. */
        0xf4                    /* HLT. */
    };
    cpu_808x_test_machine_t machine;

    cpu_808x_test_machine_create(&machine, NULL, program, sizeof(program));
    assert(cpu_808x_test_run(&machine, 16U) == BM_STATUS_OK);

    assert(cpu_808x_test_inspect(&machine, "halted") == 1U);
    assert(cpu_808x_test_inspect(&machine, "sp") == 0x1234U);
    assert(cpu_808x_test_peek(&machine, 0xf0030U) == 0xefU);
    assert(cpu_808x_test_peek(&machine, 0xf0031U) == 0xbeU);

    cpu_808x_test_machine_destroy(&machine);
    return 0;
}
