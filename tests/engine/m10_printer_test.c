/* SPDX-License-Identifier: GPL-2.0-or-later */
/* M10 printer port without firmware. A synthetic program reads BUSY and
 * SLCT on port C into RAM at F000h and strobes "Hi" out of port A; a
 * repeated write with STROBE still active sends nothing. */
#include <blumach/platforms/null_host.h>
#include <blumach/runtime/runtime.h>
#include <blumach/systems/olivetti_m10.h>

#include <assert.h>
#include <stdint.h>
#include <string.h>

#define MS UINT64_C(1000000)

static uint8_t firmware[BM_M10_FIRMWARE_SIZE];
static uint8_t ram[BM_M10_RAM_STATE_SIZE];

static const uint8_t reset_vector[] = { 0xc3U, 0x40U, 0x00U };   /* JMP 0040h */
static const uint8_t program[] = {                               /* at 0040h */
    0x3eU, 0x03U, 0xd3U, 0xb0U,   /* 81C55: PA and PB output             */
    0xdbU, 0xb3U,                 /* IN B3h      port C                  */
    0x32U, 0x00U, 0xf0U,          /* STA F000h                           */
    0x3eU, 0x48U, 0xd3U, 0xb1U,   /* PA 'H'                              */
    0x3eU, 0x02U, 0xd3U, 0xe0U,   /* STROBE active                       */
    0x3eU, 0x00U, 0xd3U, 0xe0U,   /* STROBE inactive                     */
    0x3eU, 0x69U, 0xd3U, 0xb1U,   /* PA 'i'                              */
    0x3eU, 0x02U, 0xd3U, 0xe0U,   /* STROBE active                       */
    0xd3U, 0xe0U,                 /* again: no new byte                  */
    0x3eU, 0x00U, 0xd3U, 0xe0U,   /* STROBE inactive                     */
    0x76U                         /* HLT                                 */
};

typedef struct printer {
    unsigned int bytes;
    uint8_t data[4];
    bm_tick_t time[4];
    uint32_t lines;
} printer_t;

static void
write_byte(void *context, uint32_t port, uint8_t data, bm_tick_t time)
{
    printer_t *printer = context;
    assert(port == 0U);
    assert(printer->bytes < 4U);
    printer->data[printer->bytes] = data;
    printer->time[printer->bytes++] = time;
}

static uint32_t
status(void *context, uint32_t port)
{
    const printer_t *printer = context;
    assert(port == 0U);
    return printer->lines;
}

/* Port C as the program read it. */
static uint8_t
run(printer_t *printer, int attached, int with_status)
{
    const bm_host_parallel_services_t services = {
        write_byte, with_status ? status : NULL, printer
    };
    const bm_host_capability_t capability = {
        BM_HOST_CAPABILITY_PARALLEL, BM_HOST_PARALLEL_VERSION,
        sizeof(services), &services
    };
    bm_host_services_t host = bm_null_host_services();
    bm_m10_config_t config;
    bm_machine_config_t machine;
    bm_session_t *session = NULL;

    if (attached) {
        host.capabilities = &capability;
        host.capability_count = 1U;
    }
    memset(&config, 0, sizeof(config));
    config.firmware = (bm_blob_view_t) { "synthetic-m10-printer", firmware, sizeof(firmware), NULL };
    config.ram_kib = 32U;
    config.rtc_initial_time = (bm_upd1990ac_time_t) { 0, 0, 0, 1, 0, 1 };
    machine = bm_m10_machine_config(&config);
    assert(bm_session_create(&host, &session) == BM_STATUS_OK);
    assert(bm_session_configure(session, &machine) == BM_STATUS_OK);
    assert(bm_session_start(session) == BM_STATUS_OK);
    assert(bm_session_run_for(session, 1U * MS) == BM_STATUS_OK);
    assert(bm_session_save_persistent_state(session, "ram", ram, sizeof(ram)) ==
           BM_STATUS_OK);
    bm_session_destroy(session);
    return ram[0x7000U];
}

int
main(void)
{
    printer_t printer;

    memcpy(firmware, reset_vector, sizeof(reset_vector));
    memcpy(firmware + 0x40U, program, sizeof(program));

    /* No printer: BUSY and SLCT read high, as before. */
    memset(&printer, 0, sizeof(printer));
    assert((run(&printer, 0, 0) & 0x06U) == 0x06U);
    assert(printer.bytes == 0U);

    /* A printer without status: never busy, on line. Two bytes, one per
     * STROBE edge, in order. */
    memset(&printer, 0, sizeof(printer));
    assert((run(&printer, 1, 0) & 0x06U) == 0x02U);
    assert((printer.bytes == 2U) && (printer.data[0] == 'H') && (printer.data[1] == 'i'));
    assert(printer.time[1] > printer.time[0]);

    /* Its own lines: busy and off line, then busy and on line. */
    memset(&printer, 0, sizeof(printer));
    printer.lines = BM_PARALLEL_STATUS_BUSY;
    assert((run(&printer, 1, 1) & 0x06U) == 0x04U);
    printer.bytes = 0U;
    printer.lines = BM_PARALLEL_STATUS_BUSY | BM_PARALLEL_STATUS_SELECTED;
    assert((run(&printer, 1, 1) & 0x06U) == 0x06U);
    return 0;
}
