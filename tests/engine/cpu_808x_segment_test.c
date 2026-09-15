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
        0x26, 0xc7, 0x06, 0x00, 0x01, 0x11, 0x11, /* MOV ES:[0100h],1111h */
        0x36, 0xc7, 0x06, 0x00, 0x01, 0x22, 0x22, /* MOV SS:[0100h],2222h */
        0x3e, 0xc7, 0x06, 0x00, 0x01, 0x33, 0x33, /* MOV DS:[0100h],3333h */
        0x2e, 0xc7, 0x06, 0x00, 0x01, 0x44, 0x44, /* MOV CS:[0100h],4444h */
        0x26, 0x3e, 0xc7, 0x06, 0x02, 0x01, 0x55, 0x55, /* Last prefix wins. */
        0xf4                    /* HLT */
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
    assert(bm_engine_run_for(engine, 20) == BM_STATUS_OK);

    assert(inspect(engine, "halted") == 1U);
    assert(inspect(engine, "last_fetch") == 0xf0033U);
    assert(peek(memory, 0x01100U) == 0x11U && peek(memory, 0x01101U) == 0x11U);
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
