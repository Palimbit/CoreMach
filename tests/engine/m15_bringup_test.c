/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/platforms/null_host.h>
#include <blumach/runtime/runtime.h>
#include <blumach/systems/olivetti_m15.h>

#include "failure_injection_host.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

typedef struct m15_trace_sink {
    uint16_t display_switches;
    uint16_t memory_switches;
    uint16_t keyboard_status;
    unsigned int seen;
} m15_trace_sink_t;

static void
capture_trace(void *context, const bm_808x_trace_t *trace)
{
    m15_trace_sink_t *sink = context;

    if (trace->cs != 0xf000U)
        return;
    if (trace->ip == 0x0106U) {
        sink->display_switches = trace->ax & 0xffU;
        sink->seen |= 1U;
    } else if (trace->ip == 0x010cU) {
        sink->memory_switches = trace->ax & 0xffU;
        sink->seen |= 2U;
    } else if (trace->ip == 0x0112U) {
        sink->keyboard_status = trace->ax & 0xffU;
        sink->seen |= 4U;
    }
}

static uint64_t
inspect_cpu(bm_session_t *session, const char *name)
{
    uint64_t value = UINT64_MAX;
    assert(bm_session_inspect_cpu(session, 0U, name, &value) == BM_STATUS_OK);
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
test_partial_initialization_cleanup(const bm_m15_config_t *config)
{
    size_t startup_allocations;
    size_t failure;
    failure_injection_host_t tracker;
    bm_host_services_t host;
    bm_machine_config_t machine = bm_m15_machine_config(config);
    bm_session_t *session = NULL;
    size_t before_start;

    failure_injection_host_initialize(&tracker);
    host = failure_injection_host_services(&tracker);
    assert(bm_session_create(&host, &session) == BM_STATUS_OK);
    assert(bm_session_configure(session, &machine) == BM_STATUS_OK);
    before_start = tracker.allocation_calls;
    assert(bm_session_start(session) == BM_STATUS_OK);
    startup_allocations = tracker.allocation_calls - before_start;
    bm_session_destroy(session);
    assert(tracker.outstanding_allocations == 0U);
    assert(startup_allocations != 0U);

    for (failure = 0U; failure < startup_allocations; ++failure) {
        failure_injection_host_initialize(&tracker);
        host = failure_injection_host_services(&tracker);
        session = NULL;
        assert(bm_session_create(&host, &session) == BM_STATUS_OK);
        assert(bm_session_configure(session, &machine) == BM_STATUS_OK);
        failure_injection_host_fail_after(&tracker, failure);
        assert(bm_session_start(session) == BM_STATUS_OUT_OF_MEMORY);
        bm_session_destroy(session);
        assert(tracker.outstanding_allocations == 0U);
    }
}

int
main(void)
{
    static const uint8_t reset_jump[] = { 0xea, 0x00, 0x01, 0x00, 0xf0 };
    static const uint8_t program[] = {
        0xb0, 0x80, 0xe6, 0x61, /* Select startup DIP switches. */
        0xe4, 0x60,             /* Read 20h display + memory Y bit. */
        0xb0, 0x04, 0xe6, 0x61, /* Select low memory-switch nibble. */
        0xe4, 0x62,
        0xb0, 0x05, 0xe6, 0x60, /* Internal keyboard identification. */
        0xe4, 0x64,             /* Status: output buffer full. */
        0xe4, 0x60,             /* Response: 82h, consumed on read. */
        0xf4
    };
    uint8_t firmware[BM_M15_FIRMWARE_SIZE];
    bm_host_services_t host = bm_null_host_services();
    bm_m15_config_t config = { 0 };
    bm_machine_config_t machine;
    bm_session_t *session = NULL;
    m15_trace_sink_t trace = { 0 };
    size_t index;
    uint64_t elapsed;

    memset(firmware, 0xff, sizeof(firmware));
    memcpy(firmware + 0xfff0U, reset_jump, sizeof(reset_jump));
    memcpy(firmware + 0x0100U, program, sizeof(program));
    config.firmware = (bm_blob_view_t) {
        "synthetic-m15-test", firmware, sizeof(firmware), NULL
    };
    config.ram_kib = 256U;
    config.startup_display_switches = 0x20U;
    config.trace = capture_trace;
    config.trace_context = &trace;
    for (index = 0U; index < 2U; ++index) {
        config.floppy[index] = (bm_floppy_drive_config_t) {
            .installed = 1, .geometry = { 80U, 2U, 9U, 512U }
        };
    }
    machine = bm_m15_machine_config(&config);
    assert(strcmp(machine.definition->id, "olivetti-m15") == 0);
    assert(bm_session_create(&host, &session) == BM_STATUS_OK);
    assert(bm_session_configure(session, &machine) == BM_STATUS_OK);
    assert(bm_session_start(session) == BM_STATUS_OK);
    assert(inspect_cpu(session, "cs") == 0xffffU);
    assert(inspect_cpu(session, "ip") == 0U);
    for (elapsed = 0U; elapsed < UINT64_C(10000000); elapsed += 100U) {
        if (inspect_cpu(session, "halted") != 0U)
            break;
        assert(bm_session_run_for(session, 100U) == BM_STATUS_OK);
    }
    assert(inspect_cpu(session, "halted") == 1U);
    assert(inspect_cpu(session, "ax") == 0x0082U);
    assert(trace.seen == 7U);
    assert(trace.display_switches == 0x24U);
    assert(trace.memory_switches == 0x07U);
    assert(trace.keyboard_status == 1U);
    assert(inspect_machine(session, "port_b") == 0x04U);
    assert(inspect_machine(session, "keyboard_response_pending") == 0U);
    assert(inspect_machine(session, "ram_kib") == 256U);
    assert(inspect_machine(session, "rtc_seconds") == 0U);
    assert(bm_session_run_for(session, UINT64_C(1000000000)) == BM_STATUS_OK);
    assert(inspect_machine(session, "rtc_seconds") == 1U);
    assert(bm_session_reset(session) == BM_STATUS_OK);
    assert(inspect_cpu(session, "cs") == 0xffffU);
    assert(inspect_cpu(session, "ip") == 0U);
    assert(inspect_machine(session, "port_b") == 0U);
    assert(inspect_machine(session, "rtc_seconds") == 1U);
    assert(bm_session_stop(session) == BM_STATUS_OK);
    bm_session_destroy(session);

    config.ram_kib = 512U;
    test_partial_initialization_cleanup(&config);
    memset(&trace, 0, sizeof(trace));
    session = NULL;
    assert(bm_session_create(&host, &session) == BM_STATUS_OK);
    assert(bm_session_configure(session, &machine) == BM_STATUS_OK);
    assert(bm_session_start(session) == BM_STATUS_OK);
    for (elapsed = 0U; elapsed < UINT64_C(10000000); elapsed += 100U) {
        if (inspect_cpu(session, "halted") != 0U)
            break;
        assert(bm_session_run_for(session, 100U) == BM_STATUS_OK);
    }
    assert(inspect_cpu(session, "halted") == 1U);
    assert(trace.seen == 7U);
    assert(trace.memory_switches == 0x0fU);
    bm_session_destroy(session);
    config.ram_kib = 640U;
    session = NULL;
    assert(bm_session_create(&host, &session) == BM_STATUS_OK);
    assert(bm_session_configure(session, &machine) == BM_STATUS_INVALID_ARGUMENT);
    bm_session_destroy(session);
    config.ram_kib = 256U;
    config.rtc_initial_state = firmware;
    config.rtc_initial_state_size = 1U;
    session = NULL;
    assert(bm_session_create(&host, &session) == BM_STATUS_OK);
    assert(bm_session_configure(session, &machine) == BM_STATUS_INVALID_ARGUMENT);
    bm_session_destroy(session);
    return 0;
}
