/* SPDX-License-Identifier: GPL-2.0-or-later */
/* M10 fallbacks without firmware: a synthetic program reads and writes the
 * unmodelled UART, touches memory between the ROM and 8 KiB of RAM and
 * sends an undefined instruction to an HD44102. Every host gets the same
 * fallbacks; a tolerant host also receives one report per case, with the
 * PC and opcode of the instruction. */
#include <blumach/platforms/null_host.h>
#include <blumach/runtime/runtime.h>
#include <blumach/systems/olivetti_m10.h>

#include <assert.h>
#include <stdint.h>
#include <string.h>

#define MS UINT64_C(1000000)
#define MAX_REPORTS 16U

static uint8_t firmware[BM_M10_FIRMWARE_SIZE];

/* At 0000h. The ROM ends at 7FFFh and 8 KiB of RAM starts at E000h, so
 * 9000h has no RAM. */
static const uint8_t program[] = {
    0xdbU, 0xc8U,                 /* 0000 IN C8h      UART: open bus     */
    0xd3U, 0xc8U,                 /* 0002 OUT C8h     UART: ignored      */
    0xdbU, 0xc8U,                 /* 0004 IN C8h      same port again    */
    0x3aU, 0x00U, 0x90U,          /* 0006 LDA 9000h   no RAM: FFh        */
    0x32U, 0x00U, 0x90U,          /* 0009 STA 9000h   no RAM: ignored    */
    0x3eU, 0x03U,                 /* 000C MVI A,03h                      */
    0xd3U, 0xb0U,                 /* 000E OUT B0h     81C55: PA, PB out  */
    0x3eU, 0x01U,                 /* 0010 MVI A,01h                      */
    0xd3U, 0xb1U,                 /* 0012 OUT B1h     PA0: select CS20   */
    0x3eU, 0x3cU,                 /* 0014 MVI A,3Ch                      */
    0xd3U, 0xf0U,                 /* 0016 OUT F0h     undefined HD44102  */
    0xd3U, 0xf0U,                 /* 0018 OUT F0h     the same again     */
    0x76U                         /* 001A HLT                            */
};

typedef struct reports {
    unsigned int count;
    bm_tolerance_event_t events[MAX_REPORTS];
} reports_t;

static void
record(void *context, const bm_tolerance_event_t *event)
{
    reports_t *reports = context;
    assert(reports->count < MAX_REPORTS);
    reports->events[reports->count++] = *event;
}

static void
run(int tolerant, reports_t *reports)
{
    const bm_host_tolerance_services_t services = { record, reports };
    const bm_host_capability_t capability = {
        BM_HOST_CAPABILITY_TOLERANCE, BM_HOST_TOLERANCE_VERSION,
        sizeof(services), &services
    };
    bm_host_services_t host = bm_null_host_services();
    bm_m10_config_t config;
    bm_machine_config_t machine;
    bm_session_t *session = NULL;
    uint64_t halted = 0U;

    if (tolerant) {
        host.capabilities = &capability;
        host.capability_count = 1U;
    }
    memset(&config, 0, sizeof(config));
    memset(reports, 0, sizeof(*reports));
    config.firmware = (bm_blob_view_t) {
        "synthetic-m10-tolerance", firmware, sizeof(firmware), NULL
    };
    config.ram_kib = 8U;
    config.rtc_initial_time = (bm_upd1990ac_time_t) { 0, 0, 0, 1, 0, 1 };
    machine = bm_m10_machine_config(&config);
    assert(bm_session_create(&host, &session) == BM_STATUS_OK);
    assert(bm_session_configure(session, &machine) == BM_STATUS_OK);
    assert(bm_session_start(session) == BM_STATUS_OK);
    assert(bm_session_run_for(session, 2U * MS) == BM_STATUS_OK);
    assert(bm_session_inspect_cpu(session, 0U, "halted", &halted) == BM_STATUS_OK);
    assert(halted == 1U);
    bm_session_destroy(session);
}

static const bm_tolerance_event_t *
find(const reports_t *reports, bm_tolerance_event_kind_t kind)
{
    unsigned int index;
    const bm_tolerance_event_t *found = NULL;

    for (index = 0U; index < reports->count; ++index) {
        if (reports->events[index].kind == kind) {
            assert(found == NULL); /* one report per case */
            found = &reports->events[index];
        }
    }
    assert(found != NULL);
    assert(found->size == sizeof(bm_tolerance_event_t));
    assert((found->flags & BM_TOLERANCE_FLAG_HAS_PC) != 0U);
    assert((found->flags & BM_TOLERANCE_FLAG_ASSUMED_BEHAVIOUR) == 0U);
    assert((found->device != NULL) && (found->note != NULL));
    return found;
}

int
main(void)
{
    reports_t reports;
    const bm_tolerance_event_t *event;

    memset(firmware, 0x76U, sizeof(firmware));
    memcpy(firmware, program, sizeof(program));

    /* A strict host runs the same program to HLT without any report. */
    run(0, &reports);
    assert(reports.count == 0U);

    run(1, &reports);
    assert(reports.count == 5U);
    event = find(&reports, BM_TOLERANCE_UNMODELLED_IO_READ);
    assert((event->pc == 0x0000U) && (event->opcode == 0xdbU));
    assert((event->address == 0xc8U) && (event->value == 0xffU));
    event = find(&reports, BM_TOLERANCE_UNMODELLED_IO_WRITE);
    assert((event->pc == 0x0002U) && (event->opcode == 0xd3U));
    assert(event->address == 0xc8U);
    event = find(&reports, BM_TOLERANCE_UNMODELLED_MEMORY_READ);
    assert((event->pc == 0x0006U) && (event->address == 0x9000U));
    assert(event->value == 0xffU);
    event = find(&reports, BM_TOLERANCE_UNMODELLED_MEMORY_WRITE);
    assert((event->pc == 0x0009U) && (event->address == 0x9000U));
    event = find(&reports, BM_TOLERANCE_UNKNOWN_DEVICE_COMMAND);
    assert((event->pc == 0x0016U) && (event->address == 0xf0U));
    assert(event->value == 0x3cU);
    assert(strcmp(event->device, "HD44102") == 0);
    return 0;
}
