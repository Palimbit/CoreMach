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

static uint8_t
peek(const bm_linear_memory_t *memory, uint16_t address)
{
    uint8_t value = 0;
    assert(bm_linear_memory_peek(memory, address, &value) == BM_STATUS_OK);
    return value;
}

int
main(void)
{
    static const uint8_t program[] = {
        0xb0, 0x81, 0xd0, 0xc0, 0xa2, 0x00, 0x02, /* ROL AL,1 -> 03 */
        0xb0, 0x01, 0xd0, 0xc8, 0xa2, 0x01, 0x02, /* ROR AL,1 -> 80 */
        0xf8, 0xb0, 0x80, 0xd0, 0xd0, 0xa2, 0x02, 0x02, /* RCL -> 00, CF=1 */
        0xf9, 0xb0, 0x00, 0xd0, 0xd8, 0xa2, 0x03, 0x02, /* RCR -> 80 */
        0xb0, 0x81, 0xd0, 0xe0, 0xa2, 0x04, 0x02, /* SHL -> 02 */
        0xb0, 0x81, 0xd0, 0xe8, 0xa2, 0x05, 0x02, /* SHR -> 40 */
        0xb0, 0x81, 0xd0, 0xf0, 0xa2, 0x06, 0x02, /* SAL alias -> 02 */
        0xb0, 0x81, 0xd0, 0xf8, 0xa2, 0x07, 0x02, /* SAR -> C0 */
        0xb1, 0x02, 0xb0, 0x81, 0xd2, 0xc0, 0xa2, 0x08, 0x02, /* ROL AL,CL -> 06 */
        0xb8, 0x01, 0x80, 0xd1, 0xc8, 0xa3, 0x0a, 0x02, /* ROR AX,1 -> C000 */
        0xb1, 0x04, 0xb8, 0x00, 0xf0, 0xd3, 0xf8, 0xa3, 0x0c, 0x02, /* SAR AX,CL -> FF00 */
        0xb8, 0x55, 0xaa, 0xbb, 0x0f, 0x0f, 0x85, 0xd8, 0x84, 0xd8, /* TEST word/byte. */
        0xb8, 0x00, 0x10, 0xb9, 0x01, 0x10, 0xba, 0x02, 0x10, 0xbb, 0x03, 0x10,
        0xbc, 0x04, 0x10, 0xbd, 0x05, 0x10, 0xbe, 0x06, 0x10, 0xbf, 0x07, 0x10,
        0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, /* XCHG AX,CX/DX/BX/SP/BP/SI/DI. */
        0xf4
    };
    bm_host_services_t host = bm_null_host_services();
    bm_engine_config_t engine_config = { 1, 1 };
    bm_engine_t *engine = NULL;
    bm_bus_t *bus = NULL;
    bm_linear_memory_t *memory = NULL;
    bm_cpu_t cpu;
    uint8_t *image = calloc(1, 0x100000U);
    size_t index;

    assert(image != NULL);
    image[0xffff0U] = 0xea;
    image[0xffff1U] = 0x00;
    image[0xffff2U] = 0x00;
    image[0xffff3U] = 0x00;
    image[0xffff4U] = 0xf0;
    for (index = 0; index < sizeof(program); ++index)
        image[0xf0000U + index] = program[index];

    assert(bm_bus_create(&host, 1, &bus) == BM_STATUS_OK);
    {
        bm_linear_memory_config_t config = {
            BM_ADDRESS_MEMORY, 0, 0x100000U, 0, image, 0x100000U
        };
        assert(bm_linear_memory_create(&host, bus, &config, &memory) == BM_STATUS_OK);
    }
    assert(bm_engine_create(&host, &engine_config, &engine) == BM_STATUS_OK);
    {
        bm_808x_config_t config = {
            BM_808X_NEC_V30, 10000000U, bus, NULL, NULL, NULL, NULL
        };
        assert(bm_808x_create(&host, &config, &cpu) == BM_STATUS_OK);
    }
    assert(bm_engine_add_cpu(engine, &cpu, NULL) == BM_STATUS_OK);
    assert(bm_engine_reset(engine) == BM_STATUS_OK);
    assert(bm_engine_run_for(engine, 100U) == BM_STATUS_OK);
    assert(inspect(engine, "halted") == 1U);
    assert(peek(memory, 0x0200U) == 0x03U);
    assert(peek(memory, 0x0201U) == 0x80U);
    assert(peek(memory, 0x0202U) == 0x00U);
    assert(peek(memory, 0x0203U) == 0x80U);
    assert(peek(memory, 0x0204U) == 0x02U);
    assert(peek(memory, 0x0205U) == 0x40U);
    assert(peek(memory, 0x0206U) == 0x02U);
    assert(peek(memory, 0x0207U) == 0xc0U);
    assert(peek(memory, 0x0208U) == 0x06U);
    assert(peek(memory, 0x020aU) == 0x00U && peek(memory, 0x020bU) == 0xc0U);
    assert(peek(memory, 0x020cU) == 0x00U && peek(memory, 0x020dU) == 0xffU);
    assert((inspect(engine, "flags") & 0x0045U) == 0x0004U); /* CF/ZF clear, PF set. */
    assert(inspect(engine, "ax") == 0x1007U);
    assert(inspect(engine, "cx") == 0x1000U);
    assert(inspect(engine, "dx") == 0x1001U);
    assert(inspect(engine, "bx") == 0x1002U);
    assert(inspect(engine, "sp") == 0x1003U);
    assert(inspect(engine, "bp") == 0x1004U);
    assert(inspect(engine, "si") == 0x1005U);
    assert(inspect(engine, "di") == 0x1006U);

    bm_engine_destroy(engine);
    bm_linear_memory_destroy(memory);
    bm_bus_destroy(bus);
    free(image);
    return 0;
}
