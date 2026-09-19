/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/platforms/null_host.h>
#include <blumach/runtime/runtime.h>
#include <blumach/systems/olivetti_pcs86.h>

#include "failure_injection_host.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

typedef struct trace_sink {
    bm_808x_trace_t entries[64];
    size_t count;
} trace_sink_t;

typedef struct io_trace_sink {
    bm_pcs86_io_trace_t entries[32];
    size_t count;
} io_trace_sink_t;

static void
capture_trace(void *context, const bm_808x_trace_t *trace)
{
    trace_sink_t *sink = context;
    assert(sink->count < (sizeof(sink->entries) / sizeof(sink->entries[0])));
    sink->entries[sink->count++] = *trace;
}

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
inspect(bm_session_t *session, const char *name)
{
    uint64_t value = UINT64_MAX;
    assert(bm_session_inspect_cpu(session, 0, name, &value) == BM_STATUS_OK);
    return value;
}

static void
test_partial_initialization_cleanup(const bm_pcs86_config_t *config)
{
    size_t failure;

    /* Exercise every allocation performed while starting this machine. */
    for (failure = 0; failure < 16; ++failure) {
        failure_injection_host_t tracker;
        bm_host_services_t host;
        bm_machine_config_t machine = bm_pcs86_machine_config(config);
        bm_session_t *session = NULL;

        failure_injection_host_initialize(&tracker);
        host = failure_injection_host_services(&tracker);
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
    bm_host_services_t host = bm_null_host_services();
    uint8_t even[BM_PCS86_FIRMWARE_HALF_SIZE] = { 0 };
    uint8_t odd[BM_PCS86_FIRMWARE_HALF_SIZE] = { 0 };
    static const uint8_t reset_jump[] = { 0xea, 0x00, 0x01, 0x00, 0xf0 };
    static const uint8_t program[] = {
        0xb8, 0x00, 0x00, /* MOV AX,0 */
        0x8e, 0xd8,       /* MOV DS,AX */
        0xb0, 0x5a,       /* MOV AL,5Ah */
        0xa2, 0x00, 0x02, /* MOV [0200h],AL */
        0xb8, 0x00, 0xc0, /* MOV AX,C000h */
        0x8e, 0xd8,       /* MOV DS,AX */
        0x8b, 0x06, 0x00, 0x00, /* MOV AX,[0000h] -> open bus FFFFh. */
        0x89, 0xc1,       /* MOV CX,AX */
        0xbb, 0x00, 0x00, /* MOV BX,0 */
        0x8e, 0xdb,       /* MOV DS,BX */
        0xfa,             /* CLI */
        0xb0, 0x11, 0xe6, 0x20, /* Initialize the single 8259A. */
        0xb0, 0x08, 0xe6, 0x21,
        0xb0, 0x00, 0xe6, 0x21,
        0xb0, 0x01, 0xe6, 0x21,
        0xb0, 0xfe, 0xe6, 0x21,
        0xb0, 0x34, 0xe6, 0x43, /* Program PIT channel 0, mode 2. */
        0xb0, 0x04, 0xe6, 0x40,
        0xb0, 0x00, 0xe6, 0x40,
        0xb0, 0x91, 0xe6, 0x65, /* Exercise PCS 86 board control. */
        0xb0, 0x16,             /* Begin the observed video-selection sequence. */
        0xba, 0xe8, 0x46,
        0xee,
        0xb0, 0x01,
        0xba, 0x02, 0x01,
        0xee,
        0xb0, 0x40, 0xe6, 0x70, /* Preserve the opaque memory-control write. */
        0xe4, 0x65,             /* IN AL,65h -> 91h. */
        0xb0, 0x80,             /* Select EMS window 0, page 0. */
        0xba, 0x00, 0x84,
        0xee,
        0xba, 0x00, 0x01,       /* MOV DX,100h. */
        0xec,                   /* IN AL,DX -> jumper bank. */
        0xe4, 0x63,             /* IN AL,63h -> fixed 08h. */
        0xf4              /* HLT */
    };
    trace_sink_t trace = { 0 };
    io_trace_sink_t io_trace = { 0 };
    bm_pcs86_config_t config;
    bm_machine_config_t machine;
    const bm_machine_definition_t *definition;
    bm_session_t *session = NULL;
    const bm_pcs86_firmware_identity_t *identities;
    size_t identity_count = 0;
    size_t index;
    bm_video_geometry_t geometry;
    uint32_t pixels[9];
    bm_video_framebuffer_t framebuffer = {
        pixels, sizeof(pixels) / sizeof(pixels[0]), 9U,
        { 0, 0, BM_PIXEL_XRGB8888, 0U, 0U }
    };
    static const uint8_t rtc_state[BM_PCS86_RTC_STATE_SIZE] = {
        0x00U, 0x00U, 0x45U, 0x34U, 0x12U, 0x05U, 0x18U, 0x09U
    };
    const bm_input_event_t key_down = {
        .kind = BM_INPUT_KEY, .key = BM_KEY_A, .pressed = 1
    };
    const bm_input_event_t key_up = {
        .kind = BM_INPUT_KEY, .key = BM_KEY_A
    };

    for (index = 0; index < sizeof(reset_jump); ++index)
        put_combined_byte(even, odd, 0xfff0U + index, reset_jump[index]);
    for (index = 0; index < sizeof(program); ++index)
        put_combined_byte(even, odd, 0x0100U + index, program[index]);

    config = (bm_pcs86_config_t) {
        .firmware_even = { "synthetic-even", even, sizeof(even), NULL },
        .firmware_odd = { "synthetic-odd", odd, sizeof(odd), NULL },
        .trace = capture_trace,
        .trace_context = &trace,
        .io_trace = capture_io_trace,
        .io_trace_context = &io_trace,
        .ems_kib = BM_PCS86_EMS_1920_KIB,
        .rtc_initial_state = rtc_state,
        .rtc_initial_state_size = sizeof(rtc_state)
    };
    config.floppy[0] = (bm_floppy_drive_config_t) {
        .installed = 1,
        .geometry = { 80U, 2U, 9U, 512U }
    };
    machine = bm_pcs86_machine_config(&config);
    definition = bm_pcs86_machine_definition();
    assert(definition != NULL);
    assert(strcmp(definition->id, "olivetti-pcs86") == 0);
    assert(strcmp(definition->configuration.type, BM_PCS86_CONFIG_TYPE) == 0);
    assert(definition->configuration.version == BM_PCS86_CONFIG_VERSION);
    assert(definition->configuration.size == sizeof(config));
    assert(machine.definition == definition);
    assert(machine.configuration.data == &config);

    test_partial_initialization_cleanup(&config);

    identities = bm_pcs86_expected_firmware(&identity_count);
    assert(identities != NULL && identity_count == 2);
    assert(identities[0].size == BM_PCS86_FIRMWARE_HALF_SIZE);
    assert(strlen(identities[0].sha256) == 64);

    assert(bm_session_create(&host, &session) == BM_STATUS_OK);
    assert(bm_session_configure(session, &machine) == BM_STATUS_OK);
    assert(bm_session_start(session) == BM_STATUS_OK);
    {
        uint64_t value = UINT64_MAX;
        assert(bm_session_inspect_machine(session, "rtc_seconds", &value) ==
               BM_STATUS_OK && value == 0x45U);
        assert(bm_session_inspect_machine(session, "rtc_minutes", &value) ==
               BM_STATUS_OK && value == 0x34U);
        assert(bm_session_inspect_machine(session, "rtc_hours", &value) ==
               BM_STATUS_OK && value == 0x12U);
        assert(bm_session_inspect_machine(session, "rtc_day_of_week", &value) ==
               BM_STATUS_OK && value == 0x05U);
        assert(bm_session_inspect_machine(session, "rtc_day_of_month", &value) ==
               BM_STATUS_OK && value == 0x18U);
        assert(bm_session_inspect_machine(session, "rtc_month", &value) ==
               BM_STATUS_OK && value == 0x09U);
    }
    assert(inspect(session, "cs") == 0xffff);
    assert(inspect(session, "ip") == 0);
    assert(inspect(session, "frequency_hz") == 10000000U);
    assert(bm_session_video_geometry(session, &geometry) == BM_STATUS_OK);
    assert(geometry.width == 9U && geometry.height == 1U);
    memset(pixels, 0xff, sizeof(pixels));
    assert(bm_session_render_video(session, &framebuffer) == BM_STATUS_OK);
    for (index = 0; index < sizeof(pixels) / sizeof(pixels[0]); ++index)
        assert(pixels[index] == 0U);
    assert(bm_session_send_input(session, &key_down) == BM_STATUS_OK);
    assert(bm_session_send_input(session, &key_up) == BM_STATUS_OK);
    {
        uint64_t value = UINT64_MAX;
        assert(bm_session_inspect_machine(session, "keyboard_queue_depth", &value) ==
               BM_STATUS_OK);
        assert(value == 2U);
    }

    assert(bm_session_run_for(session, 52) == BM_STATUS_OK);
    assert(inspect(session, "cs") == 0xf000);
    assert(inspect(session, "ip") == 0x015e);
    assert(inspect(session, "ax") == 0xff08U);
    assert(inspect(session, "ds") == 0);
    assert(inspect(session, "halted") == 1);
    assert(inspect(session, "dx") == 0x0100);
    assert(inspect(session, "cx") == 0xffffU);
    {
        uint64_t value = UINT64_MAX;
        assert(bm_session_inspect_machine(session, "xta_enabled", &value) ==
               BM_STATUS_OK);
        assert(value == 1U);
    }
    assert(trace.count == 46);
    assert(trace.entries[7].physical_address == 0xf010fU);
    assert(trace.entries[7].opcode == 0x8bU);
    assert(trace.entries[0].physical_address == 0xffff0U);
    assert(trace.entries[0].opcode == 0xea);
    assert(trace.entries[1].physical_address == 0xf0100U);
    assert(io_trace.count == 16);
    assert(io_trace.entries[0].operation == BM_BUS_WRITE);
    assert(io_trace.entries[0].port == 0x20U && io_trace.entries[0].value == 0x11U);
    assert(io_trace.entries[7].port == 0x40U && io_trace.entries[7].value == 0x00U);
    assert(io_trace.entries[8].operation == BM_BUS_WRITE);
    assert(io_trace.entries[8].port == 0x65U && io_trace.entries[8].value == 0x91U);
    assert(io_trace.entries[9].operation == BM_BUS_WRITE);
    assert(io_trace.entries[9].port == 0x46e8U && io_trace.entries[9].value == 0x16U);
    assert(io_trace.entries[10].operation == BM_BUS_WRITE);
    assert(io_trace.entries[10].port == 0x102U && io_trace.entries[10].value == 0x01U);
    assert(io_trace.entries[11].operation == BM_BUS_WRITE);
    assert(io_trace.entries[11].port == 0x70U && io_trace.entries[11].value == 0x40U);
    assert(io_trace.entries[12].operation == BM_BUS_READ);
    assert(io_trace.entries[12].port == 0x65U && io_trace.entries[12].value == 0x91U);
    assert(io_trace.entries[13].operation == BM_BUS_WRITE);
    assert(io_trace.entries[13].port == 0x8400U && io_trace.entries[13].value == 0x80U);
    assert(io_trace.entries[14].operation == BM_BUS_READ);
    assert(io_trace.entries[14].port == 0x100U && io_trace.entries[14].value == 0xfdU);
    assert(io_trace.entries[15].operation == BM_BUS_READ);
    assert(io_trace.entries[15].port == 0x63U && io_trace.entries[15].value == 0x08U);

    trace.count = 0;
    io_trace.count = 0;
    assert(bm_session_reset(session) == BM_STATUS_OK);
    assert(inspect(session, "cs") == 0xffffU);
    assert(inspect(session, "ip") == 0U);
    {
        uint64_t value = UINT64_MAX;
        assert(bm_session_inspect_machine(session, "pit0_count", &value) == BM_STATUS_OK);
        assert(value == 0U);
        assert(bm_session_inspect_machine(session, "keyboard_queue_depth", &value) ==
               BM_STATUS_OK);
        assert(value == 0U);
        assert(bm_session_inspect_machine(session, "fdc_dor", &value) ==
               BM_STATUS_OK);
        assert(value == 0U);
        assert(bm_session_inspect_machine(session, "fdc_msr", &value) ==
               BM_STATUS_OK);
        assert(value == 0x80U);
        assert(bm_session_inspect_machine(session, "floppy0_cylinder", &value) ==
               BM_STATUS_OK);
        assert(value == 0U);
        assert(bm_session_inspect_machine(session, "ems_kib", &value) ==
               BM_STATUS_OK);
        assert(value == BM_PCS86_EMS_1920_KIB);
        assert(bm_session_inspect_machine(session, "ems_selector0", &value) ==
               BM_STATUS_OK);
        assert(value == 0U);
        assert(bm_session_inspect_machine(session, "xta_enabled", &value) ==
               BM_STATUS_OK);
        assert(value == 0U);
        assert(bm_session_inspect_machine(session, "unknown", &value) ==
               BM_STATUS_INVALID_ARGUMENT);
    }
    assert(bm_session_run_for(session, 52) == BM_STATUS_OK);
    assert(inspect(session, "halted") == 1U);
    assert(trace.count == 46U);
    assert(io_trace.count == 16U);

    assert(bm_session_stop(session) == BM_STATUS_OK);
    bm_session_destroy(session);

    config.rtc_initial_state_size = sizeof(rtc_state) - 1U;
    machine = bm_pcs86_machine_config(&config);
    assert(bm_session_create(&host, &session) == BM_STATUS_OK);
    assert(bm_session_configure(session, &machine) == BM_STATUS_INVALID_ARGUMENT);
    bm_session_destroy(session);
    config.rtc_initial_state_size = sizeof(rtc_state);

    config.firmware_even.sha256 = "invalid";
    machine = bm_pcs86_machine_config(&config);
    assert(bm_session_create(&host, &session) == BM_STATUS_OK);
    assert(bm_session_configure(session, &machine) == BM_STATUS_INVALID_ARGUMENT);
    bm_session_destroy(session);

    memset(even, 0, sizeof(even));
    memset(odd, 0, sizeof(odd));
    even[sizeof(even) - 8U] = 0xd6U; /* Explicit unsupported-opcode sentinel at FFFF0h. */
    config.firmware_even.sha256 = NULL;
    trace.count = 0;
    machine = bm_pcs86_machine_config(&config);
    assert(bm_session_create(&host, &session) == BM_STATUS_OK);
    assert(bm_session_configure(session, &machine) == BM_STATUS_OK);
    assert(bm_session_start(session) == BM_STATUS_OK);
    assert(bm_session_run_for(session, 1) == BM_STATUS_UNSUPPORTED);
    assert(inspect(session, "last_fetch") == 0xffff0U);
    assert(inspect(session, "last_opcode") == 0xd6U);
    assert(bm_session_stop(session) == BM_STATUS_OK);
    bm_session_destroy(session);
    return 0;
}
