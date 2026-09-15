/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/components/bus.h>
#include <blumach/components/cpu_808x.h>
#include <blumach/components/linear_memory.h>
#include <blumach/engine/engine.h>
#include <blumach/platforms/null_host.h>

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static uint64_t
inspect(bm_engine_t *engine, const char *name)
{
    uint64_t value = UINT64_MAX;
    assert(bm_engine_inspect_cpu(engine, 0, name, &value) == BM_STATUS_OK);
    return value;
}

static uint8_t
peek(const bm_linear_memory_t *memory, uint64_t address)
{
    uint8_t value = 0;
    assert(bm_linear_memory_peek(memory, address, &value) == BM_STATUS_OK);
    return value;
}

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
    bm_host_services_t host = bm_null_host_services();
    bm_engine_config_t engine_config = { 1, 1 };
    bm_linear_memory_config_t memory_config;
    bm_808x_config_t cpu_config;
    bm_engine_t *engine = NULL;
    bm_bus_t *bus = NULL;
    bm_linear_memory_t *memory = NULL;
    bm_cpu_t cpu;
    uint8_t *image = calloc(1, 0x100000U);

    assert(image != NULL);
    memcpy(image + 0xf0000U, segment_writes, sizeof(segment_writes));
    image[0xffff0U] = 0xea;
    image[0xffff1U] = 0x00;
    image[0xffff2U] = 0x00;
    image[0xffff3U] = 0x00;
    image[0xffff4U] = 0xf0;
    image[0x0040U] = 0x00; /* INT 10h -> F000:0200. */
    image[0x0041U] = 0x02;
    image[0x0042U] = 0x00;
    image[0x0043U] = 0xf0;
    image[0xf0200U] = 0xfe; /* INC DL */
    image[0xf0201U] = 0xc2;
    image[0xf0202U] = 0xcf; /* IRET */
    image[0xf0204U] = 0x10; /* Indirect-call target F000:0210. */
    image[0xf0205U] = 0x02;
    image[0xf0210U] = 0x45; /* INC BP */
    image[0xf0211U] = 0xc3; /* RET */
    image[0xf0220U] = 0x34; /* Far pointer 5678:1234. */
    image[0xf0221U] = 0x12;
    image[0xf0222U] = 0x78;
    image[0xf0223U] = 0x56;

    assert(bm_bus_create(&host, 1, &bus) == BM_STATUS_OK);
    memory_config = (bm_linear_memory_config_t) {
        BM_ADDRESS_MEMORY, 0, 0x100000U, 0, image, 0x100000U
    };
    assert(bm_linear_memory_create(&host, bus, &memory_config, &memory) == BM_STATUS_OK);
    assert(bm_engine_create(&host, &engine_config, &engine) == BM_STATUS_OK);
    cpu_config = (bm_808x_config_t) {
        BM_808X_NEC_V30, 10000000U, bus, NULL, NULL, NULL, NULL
    };
    assert(bm_808x_create(&host, &cpu_config, &cpu) == BM_STATUS_OK);
    assert(bm_engine_add_cpu(engine, &cpu, NULL) == BM_STATUS_OK);
    assert(bm_engine_reset(engine) == BM_STATUS_OK);
    assert(bm_engine_run_for(engine, 81) == BM_STATUS_OK);

    assert(inspect(engine, "halted") == 1U);
    assert(inspect(engine, "last_fetch") == 0xf0097U);
    assert(inspect(engine, "dx") == 0x0044U);
    assert(inspect(engine, "ax") == 0x4445U);
    assert(inspect(engine, "ds") == 0x4444U);
    assert(inspect(engine, "es") == 0x5678U);
    assert(inspect(engine, "bx") == 0x0934U);
    assert(inspect(engine, "bp") == 0x1013U);
    assert(inspect(engine, "si") == 0x0800U);
    assert(inspect(engine, "di") == 0U);
    assert(inspect(engine, "cx") == 0x0003U);
    assert((inspect(engine, "flags") & 0x0040U) != 0U);
    assert((inspect(engine, "flags") & 0x0200U) != 0U);
    assert(peek(memory, 0x01100U) == 0x11U && peek(memory, 0x01101U) == 0x11U);
    assert(peek(memory, 0x01104U) == 0x00U && peek(memory, 0x01105U) == 0x03U);
    assert(peek(memory, 0x01106U) == 0x44U && peek(memory, 0x01107U) == 0x44U);
    assert(peek(memory, 0x01108U) == 0x5aU);
    assert(peek(memory, 0x02100U) == 0x22U && peek(memory, 0x02101U) == 0x22U);
    assert(peek(memory, 0x03100U) == 0x33U && peek(memory, 0x03101U) == 0x33U);
    assert(peek(memory, 0xf0100U) == 0x44U && peek(memory, 0xf0101U) == 0x44U);
    assert(peek(memory, 0x03102U) == 0x55U && peek(memory, 0x03103U) == 0x55U);

    bm_engine_destroy(engine);
    bm_linear_memory_destroy(memory);
    bm_bus_destroy(bus);
    free(image);
    return 0;
}
