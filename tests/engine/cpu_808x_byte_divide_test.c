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
    memcpy(image + 0xf0100U, divide_error_handler,
           sizeof(divide_error_handler));
    image[0x0000U] = 0x00U; /* INT 0 -> F000:0100. */
    image[0x0001U] = 0x01U;
    image[0x0002U] = 0x00U;
    image[0x0003U] = 0xf0U;
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
    assert(bm_engine_run_for(engine, 24U) == BM_STATUS_OK);

    assert(inspect(engine, "halted") == 1U);
    assert(inspect(engine, "dx") == 0x0012U);
    assert(inspect(engine, "bx") == 0x0badU);
    assert(inspect(engine, "cs") == 0xf000U);
    assert(inspect(engine, "ip") == 0x0104U);

    bm_engine_destroy(engine);
    bm_linear_memory_destroy(memory);
    bm_bus_destroy(bus);
    free(image);
    return 0;
}
