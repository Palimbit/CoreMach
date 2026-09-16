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
    assert(bm_engine_inspect_cpu(engine, 0U, name, &value) == BM_STATUS_OK);
    return value;
}

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
    assert(bm_engine_run_for(engine, 27U) == BM_STATUS_OK);
    assert(inspect(engine, "halted") == 1U);
    assert(inspect(engine, "cs") == 0xf000U);
    assert(inspect(engine, "ip") == 0x0038U);
    assert(inspect(engine, "sp") == 0x0100U);
    assert(inspect(engine, "bp") == 3U);
    assert(inspect(engine, "si") == 0U);
    assert(inspect(engine, "dx") == 0xffffU);
    assert(inspect(engine, "ax") == 0xd736U);
    assert((inspect(engine, "flags") & 0x00d5U) == 0x00d5U);

    bm_engine_destroy(engine);
    bm_linear_memory_destroy(memory);
    bm_bus_destroy(bus);
    free(image);
    return 0;
}
