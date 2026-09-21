/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/platforms/null_host.h>
#include <blumach/runtime/runtime.h>
#include <blumach/systems/olivetti_m15.h>

#include <assert.h>
#include <stdint.h>
#include <string.h>

static uint64_t
machine_value(bm_session_t *session, const char *name)
{
    uint64_t value = UINT64_MAX;
    assert(bm_session_inspect_machine(session, name, &value) == BM_STATUS_OK);
    return value;
}

static uint64_t
cpu_value(bm_session_t *session, const char *name)
{
    uint64_t value = UINT64_MAX;
    assert(bm_session_inspect_cpu(session, 0U, name, &value) == BM_STATUS_OK);
    return value;
}

int
main(void)
{
    static const uint8_t reset_jump[] = { 0xeaU, 0x00U, 0x01U, 0x00U, 0xf0U };
    static const uint8_t program[] = {
        0xb0U, 0x40U, 0xe6U, 0x61U, /* Clock enable -> AA self-test. */
        0xe4U, 0x64U, 0xa8U, 0x01U, 0x74U, 0xfaU,
        0xe4U, 0x60U,             /* Consume AA. */
        0xb0U, 0xc0U, 0xe6U, 0x61U, /* Acknowledge XT latch. */
        0xb0U, 0x40U, 0xe6U, 0x61U, /* Re-enable ordinary data path. */
        0xe4U, 0x64U, 0xa8U, 0x01U, 0x74U, 0xfaU,
        0xe4U, 0x60U, 0x88U, 0xc3U, /* Key make -> BL. */
        0xb0U, 0xc0U, 0xe6U, 0x61U,
        0xb0U, 0x40U, 0xe6U, 0x61U,
        0xe4U, 0x64U, 0xa8U, 0x01U, 0x74U, 0xfaU,
        0xe4U, 0x60U, 0xf4U      /* Key break -> AL, halt. */
    };
    uint8_t firmware[BM_M15_FIRMWARE_SIZE];
    bm_m15_config_t config = { 0 };
    bm_host_services_t host = bm_null_host_services();
    bm_machine_config_t machine;
    bm_session_t *session = NULL;
    bm_input_event_t event = { .kind = BM_INPUT_KEY, .key = BM_KEY_ENTER };
    unsigned int step;
    unsigned int drive;

    memset(firmware, 0xff, sizeof(firmware));
    memcpy(firmware + 0xfff0U, reset_jump, sizeof(reset_jump));
    memcpy(firmware + 0x0100U, program, sizeof(program));
    config.firmware = (bm_blob_view_t) {
        "synthetic-m15-keyboard", firmware, sizeof(firmware), NULL
    };
    config.ram_kib = 512U;
    config.startup_display_switches = 0x20U;
    for (drive = 0U; drive < 2U; ++drive) {
        config.floppy[drive] = (bm_floppy_drive_config_t) {
            .installed = 1, .geometry = { 80U, 2U, 9U, 512U }
        };
    }
    machine = bm_m15_machine_config(&config);
    assert(bm_session_create(&host, &session) == BM_STATUS_OK);
    assert(bm_session_configure(session, &machine) == BM_STATUS_OK);
    assert(bm_session_start(session) == BM_STATUS_OK);
    assert(machine_value(session, "keyboard_timer_armed") == 0U);
    assert(bm_session_run_for(session, UINT64_C(2000000)) == BM_STATUS_OK);
    assert(machine_value(session, "port_b") == 0x40U);
    assert(machine_value(session, "keyboard_queue_depth") == 0U);
    assert(machine_value(session, "keyboard_latch_full") == 0U);
    assert(machine_value(session, "keyboard_timer_armed") == 0U);

    event.key = BM_KEY_RIGHT_CONTROL; /* No undocumented right modifier. */
    assert(bm_session_send_input(session, &event) == BM_STATUS_UNSUPPORTED);
    event.key = BM_KEY_ENTER;
    event.pressed = 1;
    assert(bm_session_send_input(session, &event) == BM_STATUS_OK);
    event.pressed = 0;
    assert(bm_session_send_input(session, &event) == BM_STATUS_OK);
    assert(machine_value(session, "keyboard_queue_depth") == 2U);
    assert(machine_value(session, "keyboard_timer_armed") == 1U);
    for (step = 0U; step < 20U && cpu_value(session, "halted") == 0U; ++step)
        assert(bm_session_run_for(session, UINT64_C(1000000)) == BM_STATUS_OK);
    assert(cpu_value(session, "halted") == 1U);
    assert((cpu_value(session, "bx") & 0xffU) == 0x1cU);
    assert((cpu_value(session, "ax") & 0xffU) == 0x9cU);
    assert(machine_value(session, "keyboard_latch") == 0x9cU);
    assert(machine_value(session, "keyboard_latch_full") == 1U);
    assert(machine_value(session, "keyboard_timer_armed") == 0U);
    assert((machine_value(session, "pic_irq_requests") & 2U) != 0U);
    assert(bm_session_run_for(session, UINT64_C(60000000)) == BM_STATUS_OK);
    assert(machine_value(session, "keyboard_latch_full") == 1U);

    assert(bm_session_reset(session) == BM_STATUS_OK);
    assert(machine_value(session, "keyboard_latch_full") == 0U);
    assert(machine_value(session, "keyboard_queue_depth") == 0U);
    event.pressed = 1;
    for (step = 0U; step < 15U; ++step)
        assert(bm_session_send_input(session, &event) == BM_STATUS_OK);
    assert(bm_session_send_input(session, &event) ==
           BM_STATUS_CAPACITY_EXCEEDED);
    assert(machine_value(session, "keyboard_queue_depth") == 15U);
    bm_session_destroy(session);
    return 0;
}
