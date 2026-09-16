/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "cpu_808x_test_harness.h"

#include <blumach/components/bus.h>

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

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
    if ((transaction->operation != BM_BUS_WRITE) || (transaction->size != 1U))
        return BM_STATUS_UNSUPPORTED;
    assert(capture->count < 3U);
    capture->ports[capture->count] = (uint16_t) transaction->address;
    capture->values[capture->count++] = (uint8_t) transaction->value;
    return BM_STATUS_OK;
}

static void
test_post_slice(void)
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
    cpu_808x_test_config_t config = { 0 };
    cpu_808x_test_machine_t machine;
    io_capture_t io = { { 0 }, { 0 }, 0U };

    config.bus_capacity = 2U;
    cpu_808x_test_machine_create(&machine, &config, post_slice,
                                 sizeof(post_slice));
    assert(bm_bus_map(machine.bus, BM_ADDRESS_IO, 0U, 0xffffU,
                      io_access, &io) == BM_STATUS_OK);
    assert(cpu_808x_test_run(&machine, 100U) == BM_STATUS_OK);

    assert(cpu_808x_test_inspect(&machine, "halted") == 1U);
    assert(cpu_808x_test_inspect(&machine, "last_fetch") == 0xf0051U);
    assert(cpu_808x_test_inspect(&machine, "ax") == 0xaaaaU);
    assert((cpu_808x_test_inspect(&machine, "flags") & 0x0080U) != 0U);
    assert(io.count == 3U);
    assert(io.ports[0] == 0x00a0U && io.values[0] == 0x00U);
    assert(io.ports[1] == 0x0065U && io.values[1] == 0x82U);
    assert(io.ports[2] == 0x0378U && io.values[2] == 0x40U);

    cpu_808x_test_machine_destroy(&machine);
}

static void
test_maskable_interrupt(void)
{
    static const uint8_t program[] = {
        0xfbU, /* STI. */
        0x90U  /* Would be next without the pending IRQ. */
    };
    static const uint8_t vector[] = {
        0x00U, 0x02U, 0x00U, 0x00U /* Vector 20h -> 0000:0200. */
    };
    cpu_808x_test_config_t config = { 0 };
    cpu_808x_test_machine_t machine;
    unsigned int acknowledge_calls = 0U;

    config.interrupt_ack = acknowledge_vector_20;
    config.interrupt_context = &acknowledge_calls;
    cpu_808x_test_machine_create(&machine, &config, program, sizeof(program));
    cpu_808x_test_write(&machine, 0x0080U, vector, sizeof(vector));
    cpu_808x_test_poke(&machine, 0x0200U, 0xf4U);
    assert(bm_engine_signal_cpu(machine.engine, 0U, 0U, 1) == BM_STATUS_OK);
    assert(cpu_808x_test_run(&machine, 10U) == BM_STATUS_OK);

    assert(acknowledge_calls == 1U);
    assert(cpu_808x_test_inspect(&machine, "cs") == 0U);
    assert(cpu_808x_test_inspect(&machine, "ip") == 0x0201U);
    assert(cpu_808x_test_inspect(&machine, "sp") == 0xfffaU);
    assert(cpu_808x_test_inspect(&machine, "last_fetch") == 0x0200U);
    assert(cpu_808x_test_inspect(&machine, "halted") == 1U);

    cpu_808x_test_machine_destroy(&machine);
}

int
main(void)
{
    test_post_slice();
    test_maskable_interrupt();
    return 0;
}
