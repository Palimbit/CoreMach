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

typedef struct io_capture {
    uint16_t ports[3];
    uint8_t values[3];
    size_t count;
} io_capture_t;

static bm_status_t
acknowledge_vector_20(void *context, uint8_t *vector)
{
    unsigned int *calls = context;
    ++*calls;
    *vector = 0x20U;
    return BM_STATUS_OK;
}

static bm_status_t
io_access(void *context, bm_bus_transaction_t *transaction)
{
    io_capture_t *capture = context;
    if ((transaction->operation != BM_BUS_WRITE) || (transaction->size != 1))
        return BM_STATUS_UNSUPPORTED;
    assert(capture->count < 3);
    capture->ports[capture->count] = (uint16_t) transaction->address;
    capture->values[capture->count++] = (uint8_t) transaction->value;
    return BM_STATUS_OK;
}

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
    /*
     * Original PCS 86 BIOS 1.09 CPU self-test from F000:0000 through
     * F000:0051. The byte at 0051 is replaced with HLT so this test has a
     * deterministic boundary before the next BIOS phase.
     */
    static const uint8_t post_slice[] = {
        0xfa, 0x32, 0xc0, 0xe6, 0xa0, 0xb0, 0x82, 0xe6, 0x65,
        0xba, 0x78, 0x03, 0xb0, 0x40, 0xee, 0xb8, 0xff, 0xff,
        0x05, 0x01, 0x00, 0x73, 0x33, 0x7b, 0x31, 0x75, 0x2f,
        0x78, 0x2d, 0x70, 0x2b, 0x05, 0x01, 0x80, 0x72, 0x26,
        0x7a, 0x24, 0x74, 0x22, 0x79, 0x20, 0x05, 0x01, 0x80,
        0x71, 0x1b, 0xb8, 0xaa, 0xaa, 0x8e, 0xd0, 0x8c, 0xd6,
        0x8b, 0xde, 0x8e, 0xdb, 0x8c, 0xdf, 0x8b, 0xcf, 0x8e,
        0xc1, 0x8c, 0xc5, 0x8b, 0xd5, 0x8b, 0xe2, 0x3b, 0xc4,
        0x74, 0x01, 0xf4, 0xf7, 0xd0, 0x0b, 0xc0, 0x79, 0xe1,
        0xf4
    };
    bm_host_services_t host = bm_null_host_services();
    bm_engine_config_t engine_config = { 1, 1 };
    bm_linear_memory_config_t memory_config;
    bm_808x_config_t cpu_config;
    bm_engine_t *engine = NULL;
    bm_bus_t *bus = NULL;
    bm_linear_memory_t *memory = NULL;
    bm_cpu_t cpu;
    io_capture_t io = { { 0 }, { 0 }, 0 };
    uint8_t *image = calloc(1, 0x100000U);

    assert(image != NULL);
    memcpy(image + 0xf0000U, post_slice, sizeof(post_slice));
    image[0xffff0U] = 0xea; /* JMP F000:0000 */
    image[0xffff1U] = 0x00;
    image[0xffff2U] = 0x00;
    image[0xffff3U] = 0x00;
    image[0xffff4U] = 0xf0;

    assert(bm_bus_create(&host, 2, &bus) == BM_STATUS_OK);
    memory_config = (bm_linear_memory_config_t) {
        BM_ADDRESS_MEMORY, 0, 0x100000U, 0, image, 0x100000U
    };
    assert(bm_linear_memory_create(&host, bus, &memory_config, &memory) == BM_STATUS_OK);
    assert(bm_bus_map(bus, BM_ADDRESS_IO, 0, 0xffffU, io_access, &io) == BM_STATUS_OK);
    assert(bm_engine_create(&host, &engine_config, &engine) == BM_STATUS_OK);
    cpu_config = (bm_808x_config_t) {
        BM_808X_NEC_V30, 10000000U, bus, NULL, NULL, NULL, NULL
    };
    assert(bm_808x_create(&host, &cpu_config, &cpu) == BM_STATUS_OK);
    assert(bm_engine_add_cpu(engine, &cpu, NULL) == BM_STATUS_OK);
    assert(bm_engine_reset(engine) == BM_STATUS_OK);
    assert(bm_engine_run_for(engine, 100) == BM_STATUS_OK);

    assert(inspect(engine, "halted") == 1);
    assert(inspect(engine, "last_fetch") == 0xf0051U);
    assert(inspect(engine, "ax") == 0xaaaaU);
    assert((inspect(engine, "flags") & 0x0080U) != 0); /* SF from OR AX,AX. */
    assert(io.count == 3);
    assert(io.ports[0] == 0x00a0U && io.values[0] == 0x00U);
    assert(io.ports[1] == 0x0065U && io.values[1] == 0x82U);
    assert(io.ports[2] == 0x0378U && io.values[2] == 0x40U);

    bm_engine_destroy(engine);
    bm_linear_memory_destroy(memory);
    bm_bus_destroy(bus);
    free(image);

    /* A maskable interrupt enters through the IVT without platform coupling. */
    image = calloc(1, 0x100000U);
    assert(image != NULL);
    image[0xffff0U] = 0xea;
    image[0xffff1U] = 0x00;
    image[0xffff2U] = 0x00;
    image[0xffff3U] = 0x00;
    image[0xffff4U] = 0xf0;
    image[0xf0000U] = 0xfb; /* STI */
    image[0xf0001U] = 0x90; /* Would be next without the pending IRQ. */
    image[0x0080U] = 0x00; /* Vector 20h -> 0000:0200. */
    image[0x0081U] = 0x02;
    image[0x0200U] = 0xf4; /* HLT in the interrupt handler. */
    {
        unsigned int acknowledge_calls = 0;
        assert(bm_bus_create(&host, 1, &bus) == BM_STATUS_OK);
        memory_config = (bm_linear_memory_config_t) {
            BM_ADDRESS_MEMORY, 0, 0x100000U, 0, image, 0x100000U
        };
        assert(bm_linear_memory_create(&host, bus, &memory_config, &memory) == BM_STATUS_OK);
        assert(bm_engine_create(&host, &engine_config, &engine) == BM_STATUS_OK);
        cpu_config = (bm_808x_config_t) {
            BM_808X_NEC_V30, 10000000U, bus, NULL, NULL,
            acknowledge_vector_20, &acknowledge_calls
        };
        assert(bm_808x_create(&host, &cpu_config, &cpu) == BM_STATUS_OK);
        assert(bm_engine_add_cpu(engine, &cpu, NULL) == BM_STATUS_OK);
        assert(bm_engine_reset(engine) == BM_STATUS_OK);
        assert(bm_engine_signal_cpu(engine, 0, 0, 1) == BM_STATUS_OK);
        assert(bm_engine_run_for(engine, 10) == BM_STATUS_OK);
        assert(acknowledge_calls == 1);
        assert(inspect(engine, "cs") == 0);
        assert(inspect(engine, "ip") == 0x0201U);
        assert(inspect(engine, "sp") == 0xfffaU);
        assert(inspect(engine, "last_fetch") == 0x0200U);
        assert(inspect(engine, "halted") == 1);
        bm_engine_destroy(engine);
        bm_linear_memory_destroy(memory);
        bm_bus_destroy(bus);
    }
    free(image);
    return 0;
}
