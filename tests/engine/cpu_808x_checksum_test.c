/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "cpu_808x_test_harness.h"

#include <assert.h>
#include <stdint.h>

int
main(void)
{
    static const uint8_t setup[] = {
        0xb8, 0x00, 0x00, /* MOV AX,0 */
        0x8e, 0xd8,       /* MOV DS,AX */
        0x8e, 0xd0,       /* MOV SS,AX */
        0xbc, 0x00, 0x04, /* MOV SP,0400h */
        0xbe, 0x00, 0x03, /* MOV SI,0300h */
        0xb9, 0x03, 0x00, /* MOV CX,3 */
        0x32, 0xc0,       /* XOR AL,AL */
        0xe9, 0x0b, 0x00  /* JMP 0120h */
    };
    static const uint8_t checksum[] = {
        0x02, 0x04, /* ADD AL,[SI] */
        0x46,       /* INC SI */
        0xe2, 0xfb, /* LOOP 0120h */
        0x0a, 0xc0, /* OR AL,AL */
        0xc3        /* RET */
    };
    static const uint8_t memory_check[] = {
        0xb8, 0x55, 0xaa,       /* MOV AX,AA55h */
        0xb9, 0xaa, 0x55,       /* MOV CX,55AAh */
        0x86, 0xe9,             /* XCHG CH,CL -> CX=AA55h */
        0x89, 0x06, 0x00, 0x05, /* MOV [0500h],AX */
        0x39, 0x0e, 0x00, 0x05, /* CMP [0500h],CX */
        0x75, 0x50,             /* JNE failure */
        0x24, 0x0f,             /* AND AL,0Fh */
        0x0c, 0x80,             /* OR AL,80h -> AX=AA85h */
        0xba, 0x23, 0xf1,       /* MOV DX,F123h */
        0x81, 0xe2, 0x00, 0xf0, /* AND DX,F000h -> DX=F000h */
        0xbf, 0x00, 0x06,       /* MOV DI,0600h */
        0xb9, 0x02, 0x00,       /* MOV CX,2 */
        0xfc,                   /* CLD */
        0xf3, 0xab,             /* REP STOSW */
        0xbf, 0x00, 0x06,       /* MOV DI,0600h */
        0xb9, 0x02, 0x00,       /* MOV CX,2 */
        0xf3, 0xaf,             /* REPE SCASW */
        0xbe, 0x00, 0x05,       /* MOV SI,0500h */
        0xbf, 0x10, 0x06,       /* MOV DI,0610h */
        0xb9, 0x02, 0x00,       /* MOV CX,2 */
        0xf3, 0xa4,             /* REP MOVSB */
        0xa1, 0x00, 0x05,       /* MOV AX,[0500h] */
        0xa0, 0x10, 0x06,       /* MOV AL,[0610h] */
        0x88, 0x06, 0x12, 0x06, /* MOV [0612h],AL */
        0x03, 0x06, 0x00, 0x05, /* ADD AX,[0500h] */
        0xf7, 0x06, 0x00, 0x05, 0xff, 0xff, /* TEST word [0500h],FFFFh */
        0x81, 0x3e, 0x00, 0x05, 0x55, 0xaa, /* CMP [0500h],AA55h */
        0x75, 0x0d,             /* JNE failure */
        0xf6, 0x26, 0x00, 0x05, /* MUL byte [0500h]: AAh * 55h = 3872h */
        0xba, 0x00, 0x00,       /* MOV DX,0 */
        0xbb, 0x55, 0x00,       /* MOV BX,0055h */
        0xf7, 0xf3,             /* DIV BX: 3872h / 55h = AAh */
        0xf4,                   /* success HLT */
        0xf4                    /* failure HLT */
    };
    static const uint8_t reset_vector[] = { 0xeaU, 0x00U, 0x01U, 0x00U, 0xf0U };
    static const uint8_t checksum_input[] = { 1U, 2U, 3U };
    static const uint8_t return_address[] = { 0x30U, 0x01U };
    cpu_808x_test_machine_t machine;

    cpu_808x_test_machine_create(&machine, NULL, NULL, 0U);
    cpu_808x_test_write(&machine, 0xffff0U, reset_vector,
                        sizeof(reset_vector));
    cpu_808x_test_write(&machine, 0xf0100U, setup, sizeof(setup));
    cpu_808x_test_write(&machine, 0xf0120U, checksum, sizeof(checksum));
    cpu_808x_test_write(&machine, 0xf0130U, memory_check,
                        sizeof(memory_check));
    cpu_808x_test_write(&machine, 0x0300U, checksum_input,
                        sizeof(checksum_input));
    cpu_808x_test_write(&machine, 0x0400U, return_address,
                        sizeof(return_address));
    assert(cpu_808x_test_run(&machine, 70U) == BM_STATUS_OK);
    assert(cpu_808x_test_inspect(&machine, "halted") == 1U);
    assert(cpu_808x_test_inspect(&machine, "ax") == 0x00aaU);
    assert(cpu_808x_test_inspect(&machine, "dx") == 0U);
    assert(cpu_808x_test_inspect(&machine, "cx") == 0U);
    assert(cpu_808x_test_inspect(&machine, "di") == 0x0612U);
    assert(cpu_808x_test_inspect(&machine, "sp") == 0x0402U);
    assert(cpu_808x_test_inspect(&machine, "last_fetch") == 0xf0191U);
    assert((cpu_808x_test_inspect(&machine, "flags") & 0x0040U) != 0U);
    assert((cpu_808x_test_inspect(&machine, "flags") & 0x0801U) == 0x0801U);
    assert(cpu_808x_test_peek(&machine, 0x0610U) == 0x55U &&
           cpu_808x_test_peek(&machine, 0x0611U) == 0xaaU);
    assert(cpu_808x_test_peek(&machine, 0x0612U) == 0x55U);

    cpu_808x_test_machine_destroy(&machine);
    return 0;
}
