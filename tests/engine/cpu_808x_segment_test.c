/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "cpu_808x_test_harness.h"

#include <assert.h>
#include <stdint.h>

int
main(void)
{
    static const uint8_t segment_writes[] = {
        0xb8, 0x00, 0x01,       /* MOV AX,0100h */
        0x8e, 0xc0,             /* MOV ES,AX */
        0xb8, 0x00, 0x02,       /* MOV AX,0200h */
        0x8e, 0xd0,             /* MOV SS,AX */
        0xb8, 0x00, 0x03,       /* MOV AX,0300h */
        0x8e, 0xd8,             /* MOV DS,AX */
        0x26, 0xa3, 0x04, 0x01, /* MOV ES:[0104h],AX */
        0x26, 0xc7, 0x06, 0x00, 0x01, 0x11, 0x11, /* MOV ES:[0100h],1111h */
        0x36, 0xc7, 0x06, 0x00, 0x01, 0x22, 0x22, /* MOV SS:[0100h],2222h */
        0x3e, 0xc7, 0x06, 0x00, 0x01, 0x33, 0x33, /* MOV DS:[0100h],3333h */
        0x2e, 0xc7, 0x06, 0x00, 0x01, 0x44, 0x44, /* MOV CS:[0100h],4444h */
        0x26, 0x3e, 0xc7, 0x06, 0x02, 0x01, 0x55, 0x55, /* Last prefix wins. */
        0xbe, 0x00, 0x01,       /* MOV SI,0100h */
        0x2e, 0x8a, 0x14,       /* MOV DL,CS:[SI] */
        0x2e, 0xad,             /* LODSW CS:[SI] */
        0x2e, 0x8e, 0x1e, 0x00, 0x01, /* MOV DS,CS:[0100h] */
        0x2e, 0x8b, 0x1e, 0x00, 0x01, /* MOV BX,CS:[0100h] */
        0x26, 0x8c, 0x1e, 0x06, 0x01, /* MOV ES:[0106h],DS */
        0x26, 0xc6, 0x06, 0x08, 0x01, 0x5a, /* MOV byte ES:[0108h],5Ah */
        0x81, 0xc5, 0x00, 0x10, /* ADD BP,1000h */
        0x83, 0xc5, 0x01,       /* ADD BP,+1 */
        0xbe, 0x01, 0x80,       /* MOV SI,8001h */
        0xb1, 0x04,             /* MOV CL,4 */
        0xd3, 0xee,             /* SHR SI,CL */
        0x53,                   /* PUSH BX */
        0x06,                   /* PUSH ES */
        0x1e,                   /* PUSH DS */
        0xbb, 0x00, 0x00,       /* MOV BX,0 */
        0x8e, 0xc3,             /* MOV ES,BX */
        0x8e, 0xdb,             /* MOV DS,BX */
        0x1f,                   /* POP DS */
        0x07,                   /* POP ES */
        0x5b,                   /* POP BX */
        0x0e,                   /* PUSH CS */
        0x1f,                   /* POP DS */
        0x16,                   /* PUSH SS */
        0x17,                   /* POP SS */
        0x8e, 0xdb,             /* MOV DS,BX: restore 4444h. */
        0xf8,                   /* CLC */
        0xf9,                   /* STC */
        0xf5,                   /* CMC: carry is clear again. */
        0xfb,                   /* STI */
        0x9c,                   /* PUSHF */
        0xfa,                   /* CLI */
        0x9d,                   /* POPF: restore IF. */
        0xcd, 0x10,             /* INT 10h; handler increments DL. */
        0x25, 0xff, 0x0f,       /* AND AX,0FFFh */
        0x08, 0xc4,             /* OR AH,AL */
        0x83, 0xcd, 0x10,       /* OR BP,+10h */
        0x2e, 0xff, 0x16, 0x04, 0x02, /* CALL word CS:[0204h] */
        0xe8, 0x0a, 0x00,       /* CALL increment_bp */
        0x2e, 0xc4, 0x1e, 0x20, 0x02, /* LES BX,CS:[0220h] */
        0xd0, 0xef,             /* SHR BH,1 */
        0x3c, 0x45,             /* CMP AL,45h */
        0xf4,                   /* HLT */
        0x45,                   /* increment_bp: INC BP */
        0xfe, 0xc0,             /* INC AL */
        0x4a,                   /* DEC DX */
        0x8d, 0x7c, 0x02,       /* LEA DI,[SI+2] */
        0x2b, 0xf8,             /* SUB DI,AX */
        0xd1, 0xe7,             /* SHL DI,1 */
        0x81, 0xef, 0x7a, 0x87, /* SUB DI,877Ah */
        0x03, 0xf7,             /* ADD SI,DI */
        0x23, 0xff,             /* AND DI,DI */
        0x2a, 0xed,             /* SUB CH,CH */
        0xb1, 0x00,             /* MOV CL,0 */
        0x0f, 0x14, 0xc0,       /* SET1 AL,CL */
        0xb1, 0x04,             /* MOV CL,4 */
        0x83, 0xfa, 0x44,       /* CMP DX,+44h */
        0xe0, 0x00,             /* LOOPNE +0: ZF prevents the branch. */
        0xc3                    /* RET */
    };
    static const uint8_t interrupt_vector[] = { 0x00U, 0x02U, 0x00U, 0xf0U };
    static const uint8_t interrupt_handler[] = { 0xfeU, 0xc2U, 0xcfU };
    static const uint8_t call_pointer[] = { 0x10U, 0x02U };
    static const uint8_t call_target[] = { 0x45U, 0xc3U };
    static const uint8_t far_pointer[] = { 0x34U, 0x12U, 0x78U, 0x56U };
    cpu_808x_test_machine_t machine;

    cpu_808x_test_machine_create(&machine, NULL, segment_writes,
                                 sizeof(segment_writes));
    cpu_808x_test_write(&machine, 0x0040U, interrupt_vector,
                        sizeof(interrupt_vector));
    cpu_808x_test_write(&machine, 0xf0200U, interrupt_handler,
                        sizeof(interrupt_handler));
    cpu_808x_test_write(&machine, 0xf0204U, call_pointer,
                        sizeof(call_pointer));
    cpu_808x_test_write(&machine, 0xf0210U, call_target,
                        sizeof(call_target));
    cpu_808x_test_write(&machine, 0xf0220U, far_pointer,
                        sizeof(far_pointer));
    assert(cpu_808x_test_run(&machine, 81U) == BM_STATUS_OK);

    assert(cpu_808x_test_inspect(&machine, "halted") == 1U);
    assert(cpu_808x_test_inspect(&machine, "last_fetch") == 0xf0097U);
    assert(cpu_808x_test_inspect(&machine, "dx") == 0x0044U);
    assert(cpu_808x_test_inspect(&machine, "ax") == 0x4445U);
    assert(cpu_808x_test_inspect(&machine, "ds") == 0x4444U);
    assert(cpu_808x_test_inspect(&machine, "es") == 0x5678U);
    assert(cpu_808x_test_inspect(&machine, "bx") == 0x0934U);
    assert(cpu_808x_test_inspect(&machine, "bp") == 0x1013U);
    assert(cpu_808x_test_inspect(&machine, "si") == 0x0800U);
    assert(cpu_808x_test_inspect(&machine, "di") == 0U);
    assert(cpu_808x_test_inspect(&machine, "cx") == 0x0003U);
    assert((cpu_808x_test_inspect(&machine, "flags") & 0x0040U) != 0U);
    assert((cpu_808x_test_inspect(&machine, "flags") & 0x0200U) != 0U);
    assert(cpu_808x_test_peek(&machine, 0x01100U) == 0x11U &&
           cpu_808x_test_peek(&machine, 0x01101U) == 0x11U);
    assert(cpu_808x_test_peek(&machine, 0x01104U) == 0x00U &&
           cpu_808x_test_peek(&machine, 0x01105U) == 0x03U);
    assert(cpu_808x_test_peek(&machine, 0x01106U) == 0x44U &&
           cpu_808x_test_peek(&machine, 0x01107U) == 0x44U);
    assert(cpu_808x_test_peek(&machine, 0x01108U) == 0x5aU);
    assert(cpu_808x_test_peek(&machine, 0x02100U) == 0x22U &&
           cpu_808x_test_peek(&machine, 0x02101U) == 0x22U);
    assert(cpu_808x_test_peek(&machine, 0x03100U) == 0x33U &&
           cpu_808x_test_peek(&machine, 0x03101U) == 0x33U);
    assert(cpu_808x_test_peek(&machine, 0xf0100U) == 0x44U &&
           cpu_808x_test_peek(&machine, 0xf0101U) == 0x44U);
    assert(cpu_808x_test_peek(&machine, 0x03102U) == 0x55U &&
           cpu_808x_test_peek(&machine, 0x03103U) == 0x55U);

    cpu_808x_test_machine_destroy(&machine);
    return 0;
}
