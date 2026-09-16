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

static uint8_t
peek(const bm_linear_memory_t *memory, uint64_t address)
{
    uint8_t value = 0U;
    assert(bm_linear_memory_peek(memory, address, &value) == BM_STATUS_OK);
    return value;
}

int
main(void)
{
    static const uint8_t program[] = {
        0xb8, 0x00, 0x02,       /* MOV AX,0200h. */
        0x8e, 0xd0,             /* MOV SS,AX. */
        0xbc, 0x00, 0x01,       /* MOV SP,0100h. */
        0x2e, 0xff, 0x1e, 0x30, 0x00, /* CALL FAR CS:[0030h]. */
        0xbb, 0x34, 0x12,       /* MOV BX,1234h after return. */
        0x2e, 0xff, 0x2e, 0x34, 0x00 /* JMP FAR CS:[0034h]. */
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
    image[0xf0030U] = 0x10U; /* Target E000:0010. */
    image[0xf0031U] = 0x00U;
    image[0xf0032U] = 0x00U;
    image[0xf0033U] = 0xe0U;
    image[0xf0034U] = 0x20U; /* Jump target D000:0020. */
    image[0xf0035U] = 0x00U;
    image[0xf0036U] = 0x00U;
    image[0xf0037U] = 0xd0U;
    image[0xe0010U] = 0xb8U; /* MOV AX,BEEFh. */
    image[0xe0011U] = 0xefU;
    image[0xe0012U] = 0xbeU;
    image[0xe0013U] = 0xcbU; /* RETF. */
    image[0xd0020U] = 0xbaU; /* MOV DX,5678h. */
    image[0xd0021U] = 0x78U;
    image[0xd0022U] = 0x56U;
    image[0xd0023U] = 0xbcU; /* MOV SP,0080h. */
    image[0xd0024U] = 0x80U;
    image[0xd0025U] = 0x00U;
    image[0xd0026U] = 0x9aU; /* CALL FAR C000:0030. */
    image[0xd0027U] = 0x30U;
    image[0xd0028U] = 0x00U;
    image[0xd0029U] = 0x00U;
    image[0xd002aU] = 0xc0U;
    image[0xd002bU] = 0xf4U; /* HLT after return. */
    image[0xc0030U] = 0xbeU; /* MOV SI,9ABCh. */
    image[0xc0031U] = 0xbcU;
    image[0xc0032U] = 0x9aU;
    image[0xc0033U] = 0xcbU; /* RETF. */
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
    assert(bm_engine_run_for(engine, 16U) == BM_STATUS_OK);

    assert(inspect(engine, "halted") == 1U);
    assert(inspect(engine, "cs") == 0xd000U);
    assert(inspect(engine, "ip") == 0x002cU);
    assert(inspect(engine, "sp") == 0x0080U);
    assert(inspect(engine, "ax") == 0xbeefU);
    assert(inspect(engine, "bx") == 0x1234U);
    assert(inspect(engine, "dx") == 0x5678U);
    assert(inspect(engine, "si") == 0x9abcU);
    assert(peek(memory, 0x020fcU) == 0x0dU); /* Return IP 000Dh. */
    assert(peek(memory, 0x020fdU) == 0x00U);
    assert(peek(memory, 0x020feU) == 0x00U); /* Return CS F000h. */
    assert(peek(memory, 0x020ffU) == 0xf0U);
    assert(peek(memory, 0x0207cU) == 0x2bU); /* Return IP 002Bh. */
    assert(peek(memory, 0x0207dU) == 0x00U);
    assert(peek(memory, 0x0207eU) == 0x00U); /* Return CS D000h. */
    assert(peek(memory, 0x0207fU) == 0xd0U);

    bm_engine_destroy(engine);
    bm_linear_memory_destroy(memory);
    bm_bus_destroy(bus);
    free(image);
    return 0;
}
