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
        0x75, 0x25,             /* JNE failure */
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
        0x81, 0x3e, 0x00, 0x05, 0x55, 0xaa, /* CMP [0500h],AA55h */
        0x75, 0x01,             /* JNE failure */
        0xf4,                   /* success HLT */
        0xf4                    /* failure HLT */
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
    image[0xffff0U] = 0xea;
    image[0xffff1U] = 0x00;
    image[0xffff2U] = 0x01;
    image[0xffff3U] = 0x00;
    image[0xffff4U] = 0xf0;
    memcpy(image + 0xf0100U, setup, sizeof(setup));
    memcpy(image + 0xf0120U, checksum, sizeof(checksum));
    memcpy(image + 0xf0130U, memory_check, sizeof(memory_check));
    image[0x0300U] = 1;
    image[0x0301U] = 2;
    image[0x0302U] = 3;
    image[0x0400U] = 0x30;
    image[0x0401U] = 0x01;

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
    assert(bm_engine_run_for(engine, 64) == BM_STATUS_OK);
    assert(inspect(engine, "halted") == 1);
    assert(inspect(engine, "ax") == 0xaa55U);
    assert(inspect(engine, "dx") == 0xf000U);
    assert(inspect(engine, "cx") == 0U);
    assert(inspect(engine, "di") == 0x0612U);
    assert(inspect(engine, "sp") == 0x0402U);
    assert(inspect(engine, "last_fetch") == 0xf017bU);
    assert((inspect(engine, "flags") & 0x0040U) != 0); /* ZF from group 81h CMP. */
    assert(peek(memory, 0x0610U) == 0x55U && peek(memory, 0x0611U) == 0xaaU);
    assert(peek(memory, 0x0612U) == 0x55U);

    bm_engine_destroy(engine);
    bm_linear_memory_destroy(memory);
    bm_bus_destroy(bus);
    free(image);
    return 0;
}
