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

typedef struct interrupt_trace_sink {
    uint8_t vector;
    size_t count;
} interrupt_trace_sink_t;

static void
capture_io_trace(void *context, const bm_pcs86_io_trace_t *trace)
{
    io_trace_sink_t *sink = context;
    assert(sink->count < (sizeof(sink->entries) / sizeof(sink->entries[0])));
    sink->entries[sink->count++] = *trace;
}

static void
capture_interrupt_trace(void *context, uint8_t vector)
{
    interrupt_trace_sink_t *sink = context;
    sink->vector = vector;
    ++sink->count;
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
run_until_cpu_value(bm_session_t *session, const char *name, uint64_t expected)
{
    uint64_t elapsed;

    for (elapsed = 0U; elapsed < UINT64_C(10000000); elapsed += 100U) {
        if (inspect_cpu(session, name) == expected)
            return;
        assert(bm_session_run_for(session, 100U) == BM_STATUS_OK);
    }
    assert(0 && "PCS 86 did not reach the expected CPU state");
}

static void
run_until_halted(bm_session_t *session)
{
    run_until_cpu_value(session, "halted", 1U);
}

static void
test_unclaimed_io(const bm_host_services_t *host,
                  bm_pcs86_config_t *config,
                  uint8_t *even,
                  uint8_t *odd,
                  uint16_t port)
{
    static const uint8_t reset_jump[] = { 0xea, 0x00, 0x01, 0x00, 0xf0 };
    uint8_t program[] = {
        0xba, (uint8_t) port, (uint8_t) (port >> 8U),
        0xb0, 0xff,
        0xee, /* Unclaimed write is acknowledged and discarded. */
        0xec, /* Unclaimed read sees the open bus. */
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
    run_until_halted(session);
    assert(inspect_cpu(session, "dx") == port);
    assert((inspect_cpu(session, "ax") & 0xffU) == 0xffU);
    assert(inspect_cpu(session, "halted") == 1U);
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
    run_until_halted(session);
    assert(inspect_cpu(session, "halted") == 1U);
    assert(inspect_cpu(session, "bx") == 0xffffU);
    assert((inspect_cpu(session, "cx") & 0xffU) == 0xffU);
    assert(bm_session_stop(session) == BM_STATUS_OK);
    bm_session_destroy(session);
}

static void
test_passive_register_directions(const bm_host_services_t *host,
                                 bm_pcs86_config_t *config,
                                 uint8_t *even,
                                 uint8_t *odd)
{
    static const uint8_t reset_jump[] = { 0xea, 0x00, 0x01, 0x00, 0xf0 };
    static const uint8_t program[] = {
        0xba, 0x00, 0x01,       /* DX=100h, read-only jumpers. */
        0xb0, 0x00, 0xee, 0xec, /* Write ignored; read remains FFh. */
        0x88, 0xc3,             /* BL=jumpers. */
        0xe4, 0x70, 0x88, 0xc7, /* BH=read from write-only board latch. */
        0xba, 0x02, 0x01, 0xec, 0x88, 0xc1, /* CL=video-select read. */
        0xba, 0xe8, 0x46, 0xec, 0x88, 0xc5, /* CH=video-setup read. */
        0xe4, 0x43, 0x88, 0xc2, /* DL=PIT write-only control read. */
        0xe4, 0x09, 0x88, 0xc6, /* DH=DMA write-only request read. */
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
    run_until_halted(session);
    assert(inspect_cpu(session, "halted") == 1U);
    assert(inspect_cpu(session, "bx") == 0xffffU);
    assert(inspect_cpu(session, "cx") == 0xffffU);
    assert(inspect_cpu(session, "dx") == 0xffffU);
    bm_session_destroy(session);
}

static void
test_keyboard_scan_stream(const bm_host_services_t *host)
{
    uint8_t even[BM_PCS86_FIRMWARE_HALF_SIZE] = { 0 };
    uint8_t odd[BM_PCS86_FIRMWARE_HALF_SIZE] = { 0 };
    static const uint8_t reset_jump[] = { 0xea, 0x00, 0x01, 0x00, 0xf0 };
    static const uint8_t program[] = {
        0xba, 0x60, 0x00, /* MOV DX,0060h. */
        0xec, 0xec, 0xec, 0xec, 0xec, 0xec, 0xec, 0xec,
        /* Read seven queued scan bytes and the retained data latch. */
        0xf4
    };
    static const bm_input_event_t events[] = {
        { .kind = BM_INPUT_KEY, .key = BM_KEY_LEFT_SHIFT, .pressed = 1 },
        { .kind = BM_INPUT_KEY, .key = BM_KEY_P, .pressed = 1 },
        { .kind = BM_INPUT_KEY, .key = BM_KEY_P },
        { .kind = BM_INPUT_KEY, .key = BM_KEY_LEFT_SHIFT },
        { .kind = BM_INPUT_KEY, .key = BM_KEY_RIGHT_CONTROL, .pressed = 1 },
        { .kind = BM_INPUT_KEY, .key = BM_KEY_NON_US_BACKSLASH, .pressed = 1 }
    };
    static const uint8_t expected[] = {
        0x2aU, 0x19U, 0x99U, 0xaaU, 0xe0U, 0x1dU, 0x56U, 0x56U
    };
    const bm_input_event_t unsupported = {
        .kind = BM_INPUT_KEY, .key = BM_KEY_PRINT_SCREEN, .pressed = 1
    };
    bm_pcs86_config_t config;
    bm_machine_config_t machine;
    bm_session_t *session = NULL;
    io_trace_sink_t io_trace = { 0 };
    size_t index;

    for (index = 0U; index < sizeof(reset_jump); ++index)
        put_combined_byte(even, odd, 0xfff0U + index, reset_jump[index]);
    for (index = 0U; index < sizeof(program); ++index)
        put_combined_byte(even, odd, 0x0100U + index, program[index]);
    config = (bm_pcs86_config_t) {
        .firmware_even = { "synthetic-even", even, sizeof(even), NULL },
        .firmware_odd = { "synthetic-odd", odd, sizeof(odd), NULL },
        .io_trace = capture_io_trace,
        .io_trace_context = &io_trace
    };
    machine = bm_pcs86_machine_config(&config);
    assert(bm_session_create(host, &session) == BM_STATUS_OK);
    assert(bm_session_configure(session, &machine) == BM_STATUS_OK);
    assert(bm_session_start(session) == BM_STATUS_OK);
    assert(bm_session_send_input(session, &unsupported) ==
           BM_STATUS_UNSUPPORTED);
    for (index = 0U; index < sizeof(events) / sizeof(events[0]); ++index)
        assert(bm_session_send_input(session, &events[index]) == BM_STATUS_OK);
    assert(inspect_machine(session, "keyboard_queue_depth") ==
           sizeof(expected) / sizeof(expected[0]) - 1U);
    run_until_halted(session);
    assert(inspect_cpu(session, "halted") == 1U);
    assert(io_trace.count == sizeof(expected) / sizeof(expected[0]));
    for (index = 0U; index < sizeof(expected) / sizeof(expected[0]); ++index) {
        assert(io_trace.entries[index].operation == BM_BUS_READ);
        assert(io_trace.entries[index].port == 0x0060U);
        assert(io_trace.entries[index].value == expected[index]);
    }
    assert(inspect_machine(session, "keyboard_queue_depth") == 0U);
    assert(bm_session_stop(session) == BM_STATUS_OK);
    bm_session_destroy(session);
}

static void
test_interrupt_trace(const bm_host_services_t *host)
{
    uint8_t even[BM_PCS86_FIRMWARE_HALF_SIZE] = { 0 };
    uint8_t odd[BM_PCS86_FIRMWARE_HALF_SIZE] = { 0 };
    static const uint8_t reset_jump[] = { 0xea, 0x00, 0x01, 0x00, 0xf0 };
    static const uint8_t program[] = {
        0xb0, 0xfd,       /* MOV AL,FDh: unmask IRQ1 only. */
        0xe6, 0x21,       /* OUT 21h,AL. */
        0xfb,             /* STI. */
        0x90,             /* Complete the STI interrupt shadow. */
        0xf4              /* HLT if no interrupt is accepted. */
    };
    const bm_input_event_t key = {
        .kind = BM_INPUT_KEY, .key = BM_KEY_A, .pressed = 1
    };
    interrupt_trace_sink_t interrupt_trace = { 0 };
    bm_pcs86_config_t config;
    bm_machine_config_t machine;
    bm_session_t *session = NULL;
    size_t index;

    for (index = 0U; index < sizeof(reset_jump); ++index)
        put_combined_byte(even, odd, 0xfff0U + index, reset_jump[index]);
    for (index = 0U; index < sizeof(program); ++index)
        put_combined_byte(even, odd, 0x0100U + index, program[index]);
    config = (bm_pcs86_config_t) {
        .firmware_even = { "synthetic-even", even, sizeof(even), NULL },
        .firmware_odd = { "synthetic-odd", odd, sizeof(odd), NULL },
        .interrupt_trace = capture_interrupt_trace,
        .interrupt_trace_context = &interrupt_trace
    };
    machine = bm_pcs86_machine_config(&config);
    assert(bm_session_create(host, &session) == BM_STATUS_OK);
    assert(bm_session_configure(session, &machine) == BM_STATUS_OK);
    assert(bm_session_start(session) == BM_STATUS_OK);
    assert(bm_session_send_input(session, &key) == BM_STATUS_OK);
    for (index = 0U; (index < 100000U) && (interrupt_trace.count == 0U);
         ++index)
        assert(bm_session_run_for(session, 100U) == BM_STATUS_OK);
    assert(interrupt_trace.count == 1U);
    assert(interrupt_trace.vector == 0x09U);
    assert(bm_session_stop(session) == BM_STATUS_OK);
    bm_session_destroy(session);
}

static void
test_keyboard_led_protocol(const bm_host_services_t *host)
{
    uint8_t even[BM_PCS86_FIRMWARE_HALF_SIZE] = { 0 };
    uint8_t odd[BM_PCS86_FIRMWARE_HALF_SIZE] = { 0 };
    static const uint8_t reset_jump[] = { 0xea, 0x00, 0x01, 0x00, 0xf0 };
    static const uint8_t program[] = {
        0xb0, 0xed, 0xe6, 0x67, /* Request keyboard LED update. */
        0xe4, 0x67, 0x88, 0xc3, /* Read ACK into BL. */
        0xb0, 0x87, 0xe6, 0x67, /* Set all three defined indicators. */
        0xe4, 0x67, 0x88, 0xc7, /* Read parameter ACK into BH. */
        0xf4
    };
    bm_pcs86_config_t config;
    bm_machine_config_t machine;
    bm_session_t *session = NULL;
    bm_keyboard_led_state_t leds = { UINT8_MAX };
    size_t index;

    for (index = 0U; index < sizeof(reset_jump); ++index)
        put_combined_byte(even, odd, 0xfff0U + index, reset_jump[index]);
    for (index = 0U; index < sizeof(program); ++index)
        put_combined_byte(even, odd, 0x0100U + index, program[index]);
    config = (bm_pcs86_config_t) {
        .firmware_even = { "synthetic-even", even, sizeof(even), NULL },
        .firmware_odd = { "synthetic-odd", odd, sizeof(odd), NULL }
    };
    machine = bm_pcs86_machine_config(&config);
    assert(bm_session_create(host, &session) == BM_STATUS_OK);
    assert(bm_session_configure(session, &machine) == BM_STATUS_OK);
    assert(bm_session_start(session) == BM_STATUS_OK);
    assert(bm_session_keyboard_leds(session, &leds) == BM_STATUS_OK);
    assert(leds.indicators == 0U);
    run_until_halted(session);
    assert(inspect_cpu(session, "halted") == 1U);
    assert(inspect_cpu(session, "bx") == 0xfafaU);
    assert(bm_session_keyboard_leds(session, &leds) == BM_STATUS_OK);
    assert(leds.indicators == (BM_KEYBOARD_LED_SCROLL_LOCK |
                               BM_KEYBOARD_LED_NUM_LOCK |
                               BM_KEYBOARD_LED_CAPS_LOCK));
    assert(bm_session_reset(session) == BM_STATUS_OK);
    assert(bm_session_keyboard_leds(session, &leds) == BM_STATUS_OK);
    assert(leds.indicators == 0U);
    assert(bm_session_stop(session) == BM_STATUS_OK);
    bm_session_destroy(session);
}

static void
test_absent_at_cmos(const bm_host_services_t *host, bm_pcs86_config_t *config,
                    uint8_t *even, uint8_t *odd)
{
    static const uint8_t jump[] = { 0xea, 0x00, 0x01, 0x00, 0xf0 };
    static const uint8_t program[] = {
        0xb0, 0x40, 0xe6, 0x70, /* Existing independent board latch. */
        0xe4, 0x71, 0x88, 0xc3, /* Absent CMOS data reads FFh. */
        0xb0, 0x00, 0xe6, 0x71, /* Writes must not create storage. */
        0xe4, 0x71, 0x88, 0xc7,
        0xf4
    };
    bm_session_t *session = NULL;
    bm_machine_config_t machine;
    size_t index;
    memset(even, 0, BM_PCS86_FIRMWARE_HALF_SIZE);
    memset(odd, 0, BM_PCS86_FIRMWARE_HALF_SIZE);
    for (index = 0; index < sizeof(jump); ++index)
        put_combined_byte(even, odd, 0xfff0U + index, jump[index]);
    for (index = 0; index < sizeof(program); ++index)
        put_combined_byte(even, odd, 0x100U + index, program[index]);
    machine = bm_pcs86_machine_config(config);
    assert(bm_session_create(host, &session) == BM_STATUS_OK);
    assert(bm_session_configure(session, &machine) == BM_STATUS_OK);
    assert(bm_session_start(session) == BM_STATUS_OK);
    run_until_halted(session);
    assert(inspect_cpu(session, "halted") == 1U);
    assert(inspect_cpu(session, "bx") == 0xffffU);
    bm_session_destroy(session);
}

static void
test_ps2_mouse_stream(const bm_host_services_t *host)
{
    uint8_t even[BM_PCS86_FIRMWARE_HALF_SIZE] = { 0 };
    uint8_t odd[BM_PCS86_FIRMWARE_HALF_SIZE] = { 0 };
    static const uint8_t reset_jump[] = { 0xea, 0x00, 0x01, 0x00, 0xf0 };
    static const uint8_t program[] = {
        0xb0, 0xf4, 0xe6, 0x68, /* Enable stream reporting. */
        0xe4, 0x68,             /* Consume ACK. */
        0xba, 0x6a, 0x00,       /* Wait for auxiliary output. */
        0xec, 0xa8, 0x04, 0x74, 0xfb,
        0xe4, 0x68, 0x88, 0xc3, /* BL=packet header. */
        0xe4, 0x68, 0x88, 0xc1, /* CL=X. */
        0xe4, 0x68, 0x88, 0xc2, /* DL=Y. */
        0xb0, 0xe8, 0xe6, 0x68, 0xe4, 0x68, /* Set resolution. */
        0xb0, 0x03, 0xe6, 0x68, 0xe4, 0x68,
        0xb0, 0xf3, 0xe6, 0x68, 0xe4, 0x68, /* Set 80 Hz rate. */
        0xb0, 0x50, 0xe6, 0x68, 0xe4, 0x68,
        0xb0, 0xe9, 0xe6, 0x68, 0xe4, 0x68, /* Status ACK. */
        0xe4, 0x68, 0x88, 0xc4, /* AH=enabled/stream/buttons. */
        0xe4, 0x68, 0x88, 0xc5, /* CH=resolution. */
        0xe4, 0x68, 0x88, 0xc6, /* DH=sample rate. */
        0xb0, 0xe1, 0xe6, 0x68, /* Undefined command must request resend. */
        0xe4, 0x68, 0x88, 0xc7, /* BH=FEh, not a fabricated ACK. */
        0xf4
    };
    const bm_input_event_t pointer = {
        .kind = BM_INPUT_RELATIVE_POINTER,
        .delta_x = 12,
        .delta_y = -5,
        .buttons = BM_POINTER_BUTTON_LEFT
    };
    bm_pcs86_config_t config;
    bm_machine_config_t machine;
    bm_session_t *session = NULL;
    size_t index;

    for (index = 0U; index < sizeof(reset_jump); ++index)
        put_combined_byte(even, odd, 0xfff0U + index, reset_jump[index]);
    for (index = 0U; index < sizeof(program); ++index)
        put_combined_byte(even, odd, 0x0100U + index, program[index]);
    config = (bm_pcs86_config_t) {
        .firmware_even = { "synthetic-even", even, sizeof(even), NULL },
        .firmware_odd = { "synthetic-odd", odd, sizeof(odd), NULL }
    };
    machine = bm_pcs86_machine_config(&config);
    assert(bm_session_create(host, &session) == BM_STATUS_OK);
    assert(bm_session_configure(session, &machine) == BM_STATUS_OK);
    assert(bm_session_start(session) == BM_STATUS_OK);
    assert(bm_session_send_input(session, &pointer) == BM_STATUS_OK);
    /* Wait until firmware is polling the auxiliary-output bit. The first
     * pointer event preceded stream enable and is intentionally discarded. */
    run_until_cpu_value(session, "ip", 0x0109U);
    assert(inspect_cpu(session, "halted") == 0U);
    assert(bm_session_send_input(session, &pointer) == BM_STATUS_OK);
    run_until_halted(session);
    assert(inspect_cpu(session, "halted") == 1U);
    assert(inspect_cpu(session, "ax") == 0x24feU);
    assert(inspect_cpu(session, "bx") == 0xfe29U);
    assert(inspect_cpu(session, "cx") == 0x030cU);
    assert(inspect_cpu(session, "dx") == 0x50fbU);
    assert(bm_session_stop(session) == BM_STATUS_OK);
    bm_session_destroy(session);
}

static void
test_absent_coprocessor_and_extended_dma_latches(
    const bm_host_services_t *host, bm_pcs86_config_t *config,
    uint8_t *even, uint8_t *odd)
{
    static const uint8_t jump[] = { 0xea, 0x00, 0x01, 0x00, 0xf0 };
    static const uint8_t program[] = {
        0x9b,                         /* No coprocessor is busy on this board. */
        0xb0, 0x05, 0xe6, 0x87,       /* Real XT DMA channel-0 page. */
        0xb0, 0x12, 0xe6, 0x88,       /* Wider PCS 86 board latches. */
        0xb0, 0x44, 0xe6, 0x89,
        0xb0, 0x87, 0xe6, 0x96,
        0xe4, 0x88, 0x88, 0xc3,       /* BL=12h. */
        0xe4, 0x89, 0x88, 0xc7,       /* BH=44h. */
        0xe4, 0x96, 0x88, 0xc1,       /* CL=87h. */
        0xe4, 0x87, 0x88, 0xc5,       /* CH=05h; channel latch unchanged. */
        0xf4
    };
    bm_session_t *session = NULL;
    bm_machine_config_t machine;
    size_t index;
    memset(even, 0, BM_PCS86_FIRMWARE_HALF_SIZE);
    memset(odd, 0, BM_PCS86_FIRMWARE_HALF_SIZE);
    for (index = 0; index < sizeof(jump); ++index)
        put_combined_byte(even, odd, 0xfff0U + index, jump[index]);
    for (index = 0; index < sizeof(program); ++index)
        put_combined_byte(even, odd, 0x100U + index, program[index]);
    machine = bm_pcs86_machine_config(config);
    assert(bm_session_create(host, &session) == BM_STATUS_OK);
    assert(bm_session_configure(session, &machine) == BM_STATUS_OK);
    assert(bm_session_start(session) == BM_STATUS_OK);
    run_until_halted(session);
    assert(inspect_cpu(session, "halted") == 1U);
    assert(inspect_cpu(session, "bx") == 0x4412U);
    assert(inspect_cpu(session, "cx") == 0x0587U);
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
    run_until_halted(session);
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
    test_absent_at_cmos(&host, &config, even, odd);
    test_absent_coprocessor_and_extended_dma_latches(&host, &config, even, odd);
    test_unclaimed_io(&host, &config, even, odd, 0x00afU);
    test_unclaimed_io(&host, &config, even, odd, 0x4404U);
    test_unclaimed_io(&host, &config, even, odd, 0x3400U);
    test_unclaimed_io(&host, &config, even, odd, 0x0072U);
    test_absent_xta_slots(&host, &config, even, odd);
    test_passive_register_directions(&host, &config, even, odd);
    test_unclaimed_io(&host, &config, even, odd, 0x02f1U);
    test_unclaimed_io(&host, &config, even, odd, 0x06f2U);
    test_keyboard_scan_stream(&host);
    test_interrupt_trace(&host);
    test_keyboard_led_protocol(&host);
    test_ps2_mouse_stream(&host);
    return 0;
}
