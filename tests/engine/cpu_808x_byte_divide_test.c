/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "cpu_808x_test_harness.h"

#include <assert.h>
#include <stdint.h>

int
main(void)
{
    static const uint8_t program[] = {
        0xb8, 0x20, 0x01,       /* MOV AX,0120h (288). */
        0xb1, 0x10,             /* MOV CL,16. */
        0xf6, 0xf1,             /* DIV CL -> AL=12h, AH=0. */
        0x8b, 0xd0,             /* MOV DX,AX: retain valid result. */
        0xb8, 0x00, 0x01,       /* MOV AX,0100h. */
        0xb1, 0x01,             /* MOV CL,1. */
        0xf6, 0xf1,             /* DIV CL: quotient overflow -> INT 0. */
        0xf4                    /* Must not be reached. */
    };
    static const uint8_t divide_error_handler[] = {
        0xbb, 0xad, 0x0b,       /* MOV BX,0BADh. */
        0xf4                    /* HLT. */
    };
    static const uint8_t divide_error_vector[] = {
        0x00U, 0x01U, 0x00U, 0xf0U /* INT 0 -> F000:0100. */
    };
    cpu_808x_test_machine_t machine;

    cpu_808x_test_machine_create(&machine, NULL, program, sizeof(program));
    cpu_808x_test_write(&machine, 0xf0100U, divide_error_handler,
                        sizeof(divide_error_handler));
    cpu_808x_test_write(&machine, 0U, divide_error_vector,
                        sizeof(divide_error_vector));
    assert(cpu_808x_test_run(&machine, 24U) == BM_STATUS_OK);

    assert(cpu_808x_test_inspect(&machine, "halted") == 1U);
    assert(cpu_808x_test_inspect(&machine, "dx") == 0x0012U);
    assert(cpu_808x_test_inspect(&machine, "bx") == 0x0badU);
    assert(cpu_808x_test_inspect(&machine, "cs") == 0xf000U);
    assert(cpu_808x_test_inspect(&machine, "ip") == 0x0104U);

    cpu_808x_test_machine_destroy(&machine);
    return 0;
}
