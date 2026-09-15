/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/components/bus.h>
#include <blumach/components/cpu_808x.h>
#include <blumach/components/linear_memory.h>
#include <blumach/engine/engine.h>
#include <blumach/platforms/null_host.h>

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

static uint64_t
inspect(bm_engine_t *engine, const char *name)
{
    uint64_t value = UINT64_MAX;
    assert(bm_engine_inspect_cpu(engine, 0, name, &value) == BM_STATUS_OK);
    return value;
}

int
main(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_engine_config_t engine_config = { 1, 1 };
    bm_engine_t *engine = NULL;
    bm_bus_t *bus = NULL;
    bm_linear_memory_t *memory = NULL;
    bm_cpu_t cpu;
    uint8_t *image = calloc(1, 0x100000U);

    assert(image != NULL);
    image[0xffff0U] = 0xea; /* JMP F000:0000 */
    image[0xffff1U] = 0x00;
    image[0xffff2U] = 0x00;
    image[0xffff3U] = 0x00;
    image[0xffff4U] = 0xf0;
    image[0xf0000U] = 0xb3; /* MOV BL,AAh */
    image[0xf0001U] = 0xaa;
    image[0xf0002U] = 0xb0; /* MOV AL,AAh */
    image[0xf0003U] = 0xaa;
    image[0xf0004U] = 0x3a; /* CMP BL,AL */
    image[0xf0005U] = 0xd8;
    image[0xf0006U] = 0x75; /* JNE failure */
    image[0xf0007U] = 0x06;
    image[0xf0008U] = 0xb0; /* MOV AL,ABh */
    image[0xf0009U] = 0xab;
    image[0xf000aU] = 0x3a; /* CMP BL,AL: carry and sign. */
    image[0xf000bU] = 0xd8;
    image[0xf000cU] = 0x73; /* JNC failure: CMP must set carry. */
    image[0xf000dU] = 0x0e;
    image[0xf000eU] = 0x79; /* JNS failure: CMP must set sign. */
    image[0xf000fU] = 0x0c;
    image[0xf0010U] = 0xf6; /* NOT BL */
    image[0xf0011U] = 0xd3;
    image[0xf0012U] = 0x80; /* AND BL,0Fh */
    image[0xf0013U] = 0xe3;
    image[0xf0014U] = 0x0f;
    image[0xf0015U] = 0xf6; /* TEST BL,01h */
    image[0xf0016U] = 0xc3;
    image[0xf0017U] = 0x01;
    image[0xf0018U] = 0xa8; /* TEST AL,01h */
    image[0xf0019U] = 0x01;
    image[0xf001aU] = 0x74; /* JZ failure */
    image[0xf001bU] = 0x04;
    image[0xf001cU] = 0x22; /* AND AH,BL */
    image[0xf001dU] = 0xe3;
    image[0xf001eU] = 0xf4; /* HLT success */
    image[0xf001fU] = 0x90;
    image[0xf0020U] = 0xf4; /* HLT failure */

    assert(bm_bus_create(&host, 1, &bus) == BM_STATUS_OK);
    {
        bm_linear_memory_config_t memory_config = {
            BM_ADDRESS_MEMORY, 0, 0x100000U, 0, image, 0x100000U
        };
        assert(bm_linear_memory_create(&host, bus, &memory_config, &memory) == BM_STATUS_OK);
    }
    assert(bm_engine_create(&host, &engine_config, &engine) == BM_STATUS_OK);
    {
        bm_808x_config_t cpu_config = {
            BM_808X_NEC_V30, 10000000U, bus, NULL, NULL, NULL, NULL
        };
        assert(bm_808x_create(&host, &cpu_config, &cpu) == BM_STATUS_OK);
    }
    assert(bm_engine_add_cpu(engine, &cpu, NULL) == BM_STATUS_OK);
    assert(bm_engine_reset(engine) == BM_STATUS_OK);
    assert(bm_engine_run_for(engine, 32) == BM_STATUS_OK);
    assert(inspect(engine, "halted") == 1);
    assert(inspect(engine, "last_fetch") == 0xf001eU);
    assert(inspect(engine, "bx") == 0x0005U);
    assert(inspect(engine, "ax") == 0x00abU);
    assert((inspect(engine, "flags") & 0x0001U) == 0); /* TEST clears CF. */
    assert((inspect(engine, "flags") & 0x0040U) != 0); /* Final AND result is zero. */

    bm_engine_destroy(engine);
    bm_linear_memory_destroy(memory);
    bm_bus_destroy(bus);
    free(image);
    return 0;
}
