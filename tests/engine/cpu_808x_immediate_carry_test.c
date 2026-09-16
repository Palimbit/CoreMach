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

#define FLAG_CF 0x0001U
#define FLAG_ZF 0x0040U

static uint64_t
inspect(bm_engine_t *engine, const char *name)
{
    uint64_t value = UINT64_MAX;
    assert(bm_engine_inspect_cpu(engine, 0U, name, &value) == BM_STATUS_OK);
    return value;
}

int
main(void)
{
    static const uint8_t program[] = {
        0xba, 0x00, 0x12,       /* MOV DX,1200h. */
        0xf9,                   /* STC. */
        0x80, 0xd6, 0x34,       /* ADC DH,34h -> 47h. */
        0x8b, 0xea,             /* MOV BP,DX: retain byte ADC result. */
        0xb8, 0x00, 0x00,       /* MOV AX,0. */
        0xf9,                   /* STC. */
        0x80, 0xd8, 0x00,       /* SBB AL,0 -> FFh. */
        0x8b, 0xf0,             /* MOV SI,AX: retain byte SBB result. */
        0xb8, 0xff, 0xff,       /* MOV AX,FFFFh. */
        0xf9,                   /* STC. */
        0x81, 0xd0, 0x00, 0x00, /* ADC AX,0 -> 0 with carry. */
        0x9c,                   /* PUSHF. */
        0x5b,                   /* POP BX. */
        0xb9, 0x00, 0x00,       /* MOV CX,0. */
        0xf9,                   /* STC. */
        0x83, 0xd9, 0x00,       /* SBB CX,+0 -> FFFFh. */
        0x9c,                   /* PUSHF. */
        0x5a,                   /* POP DX. */
        0xf4                    /* HLT. */
    };
    bm_host_services_t host = bm_null_host_services();
    bm_engine_config_t engine_config = { 1U, 1U };
    bm_linear_memory_config_t memory_config;
    bm_808x_config_t cpu_config;
    bm_engine_t *engine = NULL;
    bm_bus_t *bus = NULL;
    bm_linear_memory_t *memory = NULL;
    bm_cpu_t cpu;
    uint8_t *image = calloc(1U, 0x100000U);

    assert(image != NULL);
    memcpy(image + 0xf0000U, program, sizeof(program));
    image[0xffff0U] = 0xeaU;
    image[0xffff1U] = 0x00U;
    image[0xffff2U] = 0x00U;
    image[0xffff3U] = 0x00U;
    image[0xffff4U] = 0xf0U;
    assert(bm_bus_create(&host, 1U, &bus) == BM_STATUS_OK);
    memory_config = (bm_linear_memory_config_t) {
        BM_ADDRESS_MEMORY, 0U, 0x100000U, 0, image, 0x100000U
    };
    assert(bm_linear_memory_create(&host, bus, &memory_config, &memory) ==
           BM_STATUS_OK);
    assert(bm_engine_create(&host, &engine_config, &engine) == BM_STATUS_OK);
    cpu_config = (bm_808x_config_t) {
        BM_808X_NEC_V30, 10000000U, bus, NULL, NULL, NULL, NULL
    };
    assert(bm_808x_create(&host, &cpu_config, &cpu) == BM_STATUS_OK);
    assert(bm_engine_add_cpu(engine, &cpu, NULL) == BM_STATUS_OK);
    assert(bm_engine_reset(engine) == BM_STATUS_OK);
    assert(bm_engine_run_for(engine, 32U) == BM_STATUS_OK);

    assert(inspect(engine, "halted") == 1U);
    assert(inspect(engine, "bp") == 0x4700U);
    assert(inspect(engine, "si") == 0x00ffU);
    assert(inspect(engine, "ax") == 0U);
    assert((inspect(engine, "bx") & (FLAG_CF | FLAG_ZF)) ==
           (FLAG_CF | FLAG_ZF));
    assert(inspect(engine, "cx") == 0xffffU);
    assert((inspect(engine, "dx") & FLAG_CF) != 0U);
    assert((inspect(engine, "dx") & FLAG_ZF) == 0U);

    bm_engine_destroy(engine);
    bm_linear_memory_destroy(memory);
    bm_bus_destroy(bus);
    free(image);
    return 0;
}
