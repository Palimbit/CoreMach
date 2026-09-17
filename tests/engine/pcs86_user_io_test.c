/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/platforms/null_host.h>
#include <blumach/runtime/runtime.h>
#include <blumach/systems/olivetti_pcs86.h>

#include <assert.h>
#include <stdint.h>
#include <string.h>

typedef struct io_trace_sink {
    bm_pcs86_io_trace_t entries[32];
    size_t count;
} io_trace_sink_t;

static void
capture_io_trace(void *context, const bm_pcs86_io_trace_t *trace)
{
    io_trace_sink_t *sink = context;
    assert(sink->count < (sizeof(sink->entries) / sizeof(sink->entries[0])));
    sink->entries[sink->count++] = *trace;
}

static void
put_combined_byte(uint8_t *even, uint8_t *odd, size_t offset, uint8_t value)
{
    if ((offset & 1U) == 0)
        even[offset / 2U] = value;
    else
        odd[offset / 2U] = value;
}

static uint64_t
inspect_cpu(bm_session_t *session, const char *name)
{
    uint64_t value = UINT64_MAX;
    assert(bm_session_inspect_cpu(session, 0, name, &value) == BM_STATUS_OK);
    return value;
}

static uint64_t
inspect_machine(bm_session_t *session, const char *name)
{
    uint64_t value = UINT64_MAX;
    assert(bm_session_inspect_machine(session, name, &value) == BM_STATUS_OK);
    return value;
}

static void
test_unmapped_output(const bm_host_services_t *host,
                     bm_pcs86_config_t *config,
                     uint8_t *even,
                     uint8_t *odd,
                     uint16_t port)
{
    static const uint8_t reset_jump[] = { 0xea, 0x00, 0x01, 0x00, 0xf0 };
    uint8_t program[] = {
        0xba, (uint8_t) port, (uint8_t) (port >> 8U),
        0xb0, 0xff,
        0xee
    };
    bm_machine_config_t machine;
    bm_session_t *session = NULL;
    size_t index;

    memset(even, 0, BM_PCS86_FIRMWARE_HALF_SIZE);
    memset(odd, 0, BM_PCS86_FIRMWARE_HALF_SIZE);
    for (index = 0; index < sizeof(reset_jump); ++index)
        put_combined_byte(even, odd, 0xfff0U + index, reset_jump[index]);
    for (index = 0; index < sizeof(program); ++index)
        put_combined_byte(even, odd, 0x0100U + index, program[index]);

    machine = bm_pcs86_machine_config(config);
    assert(bm_session_create(host, &session) == BM_STATUS_OK);
    assert(bm_session_configure(session, &machine) == BM_STATUS_OK);
    assert(bm_session_start(session) == BM_STATUS_OK);
    assert(bm_session_run_for(session, 4U) == BM_STATUS_UNMAPPED);
    assert(inspect_cpu(session, "dx") == port);
    assert(bm_session_stop(session) == BM_STATUS_OK);
    bm_session_destroy(session);
}

static void
test_absent_xta_slots(const bm_host_services_t *host,
                      bm_pcs86_config_t *config,
                      uint8_t *even,
                      uint8_t *odd)
{
    static const uint8_t reset_jump[] = { 0xea, 0x00, 0x01, 0x00, 0xf0 };
    static const uint8_t program[] = {
        0xba, 0x25, 0x03, 0xb0, 0x0c, 0xee, 0xec, 0x88, 0xc3,
        0xba, 0x29, 0x03, 0xb0, 0x0c, 0xee, 0xec, 0x88, 0xc7,
        0xba, 0x2d, 0x03, 0xb0, 0x0c, 0xee, 0xec, 0x88, 0xc1,
        0xf4
    };
    bm_machine_config_t machine;
    bm_session_t *session = NULL;
    size_t index;

    memset(even, 0, BM_PCS86_FIRMWARE_HALF_SIZE);
    memset(odd, 0, BM_PCS86_FIRMWARE_HALF_SIZE);
    for (index = 0; index < sizeof(reset_jump); ++index)
        put_combined_byte(even, odd, 0xfff0U + index, reset_jump[index]);
    for (index = 0; index < sizeof(program); ++index)
        put_combined_byte(even, odd, 0x0100U + index, program[index]);

    machine = bm_pcs86_machine_config(config);
    assert(bm_session_create(host, &session) == BM_STATUS_OK);
    assert(bm_session_configure(session, &machine) == BM_STATUS_OK);
    assert(bm_session_start(session) == BM_STATUS_OK);
    assert(bm_session_run_for(session, 32U) == BM_STATUS_OK);
    assert(inspect_cpu(session, "halted") == 1U);
    assert(inspect_cpu(session, "bx") == 0xffffU);
    assert((inspect_cpu(session, "cx") & 0xffU) == 0xffU);
    assert(bm_session_stop(session) == BM_STATUS_OK);
    bm_session_destroy(session);
}

int
main(void)
{
    bm_host_services_t host = bm_null_host_services();
    uint8_t even[BM_PCS86_FIRMWARE_HALF_SIZE] = { 0 };
    uint8_t odd[BM_PCS86_FIRMWARE_HALF_SIZE] = { 0 };
    static const uint8_t reset_jump[] = { 0xea, 0x00, 0x01, 0x00, 0xf0 };
    static const uint8_t program[] = {
        0xb0, 0x93, 0xe6, 0x65,       /* Enable LPT1 and COM1. */
        0xba, 0x78, 0x03,             /* DX=378h, SPP data register. */
        0xb0, 0xa5, 0xee, 0xec,       /* Write/read LPT data. */
        0x88, 0xc7,                   /* Save result in BH. */
        0xba, 0xfb, 0x03,             /* DX=3FBh, UART line control. */
        0xb0, 0x03, 0xee,
        0x42,                         /* DX=3FCh, UART modem control. */
        0xb0, 0x10, 0xee,             /* Enable internal loopback. */
        0xba, 0xf8, 0x03,             /* DX=3F8h, UART data. */
        0xb0, 0x5a, 0xee, 0xec,       /* Loop back one byte. */
        0x88, 0xc3,                   /* Save result in BL. */
        0xba, 0xf2, 0x02,             /* AT shared-IRQ rearm range is absent. */
        0xb0, 0xff, 0xee,
        0x42, 0xee, 0x42, 0xee,
        0x42, 0xee, 0x42, 0xee,
        0x42, 0xee,                   /* Writes 2F2h through 2F7h. */
        0xba, 0xf2, 0x02, 0xec,       /* An absent-device read returns FFh. */
        0xb0, 0xf2, 0xe6, 0x67,       /* Identify keyboard channel. */
        0xe4, 0x6a, 0x88, 0xc1,       /* Status in CL. */
        0xe4, 0x67, 0x88, 0xc5,       /* ACK in CH. */
        0xe4, 0x67, 0x88, 0xc2,       /* Manufacturer in DL. */
        0xe4, 0x67, 0x88, 0xc6,       /* Device ID in DH. */
        0xf4
    };
    bm_pcs86_config_t config;
    bm_machine_config_t machine;
    bm_session_t *session = NULL;
    io_trace_sink_t io_trace = { 0 };
    size_t index;

    for (index = 0; index < sizeof(reset_jump); ++index)
        put_combined_byte(even, odd, 0xfff0U + index, reset_jump[index]);
    for (index = 0; index < sizeof(program); ++index)
        put_combined_byte(even, odd, 0x0100U + index, program[index]);

    config = (bm_pcs86_config_t) {
        .firmware_even = { "synthetic-even", even, sizeof(even), NULL },
        .firmware_odd = { "synthetic-odd", odd, sizeof(odd), NULL },
        .io_trace = capture_io_trace,
        .io_trace_context = &io_trace
    };
    machine = bm_pcs86_machine_config(&config);

    assert(bm_session_create(&host, &session) == BM_STATUS_OK);
    assert(bm_session_configure(session, &machine) == BM_STATUS_OK);
    assert(bm_session_start(session) == BM_STATUS_OK);
    assert(bm_session_run_for(session, 64U) == BM_STATUS_OK);
    assert(inspect_cpu(session, "halted") == 1U);
    assert(inspect_cpu(session, "bx") == 0xa55aU);
    assert(inspect_cpu(session, "cx") == 0xfa20U);
    assert(inspect_cpu(session, "dx") == 0x83abU);
    assert(inspect_machine(session, "lpt_data") == 0xa5U);
    assert((inspect_machine(session, "uart_line_status") & 0x01U) == 0U);
    assert(io_trace.count == 19U);
    for (index = 0; index < 6U; ++index) {
        assert(io_trace.entries[7U + index].operation == BM_BUS_WRITE);
        assert(io_trace.entries[7U + index].port == (uint16_t) (0x02f2U + index));
        assert(io_trace.entries[7U + index].value == 0xffU);
    }
    assert(io_trace.entries[13].operation == BM_BUS_READ);
    assert(io_trace.entries[13].port == 0x02f2U);
    assert(io_trace.entries[13].value == 0xffU);
    assert(bm_session_stop(session) == BM_STATUS_OK);
    bm_session_destroy(session);

    config.io_trace = NULL;
    config.io_trace_context = NULL;
    test_absent_xta_slots(&host, &config, even, odd);
    test_unmapped_output(&host, &config, even, odd, 0x02f1U);
    test_unmapped_output(&host, &config, even, odd, 0x06f2U);
    return 0;
}
