/* SPDX-License-Identifier: GPL-2.0-or-later */
/* M10 serial port and buzzer without firmware. A synthetic program clocks
 * the IM6402 from the 81C55 timer (count 16: 2457600 / 16 / 16 = 9600 bit/s),
 * sends "AB" as 8N1, receives characters on RST 6.5 into RAM at F000h and
 * puts the buzzer in timer mode. */
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
static const uint8_t rst65_vector[] = {                          /* at 0034h */
    0xdbU, 0xc0U,                 /* IN C0h      read the character      */
    0x77U,                        /* MOV M,A                             */
    0x23U,                        /* INX H                               */
    0xfbU,                        /* EI                                  */
    0xc9U                         /* RET                                 */
};
static const uint8_t program[] = {                               /* at 0040h */
    0x31U, 0xf0U, 0xffU,          /* LXI SP,FFF0h                        */
    0x21U, 0x00U, 0xf0U,          /* LXI H,F000h                         */
    0x3eU, 0x00U, 0xd3U, 0xb2U,   /* PB latch 00h                        */
    0x3eU, 0x10U, 0xd3U, 0xb4U,   /* timer count low: 16                 */
    0x3eU, 0x40U, 0xd3U, 0xb5U,   /* count high 0, continuous square     */
    0x3eU, 0xc2U, 0xd3U, 0xb0U,   /* START timer, PB output              */
    0x3eU, 0x1cU, 0xd3U, 0xd0U,   /* UART: 8 bits, no parity, 1 stop     */
    0x3eU, 0x41U, 0xd3U, 0xc0U,   /* send 'A'                            */
    0x3eU, 0x42U, 0xd3U, 0xc0U,   /* send 'B' (follows end to end)       */
    0x3eU, 0x24U, 0xd3U, 0xb2U,   /* PB5 and PB2 high: buzzer level 1    */
    0x3eU, 0x04U, 0xd3U, 0xb2U,   /* PB5 low: level 0, 17 clocks later   */
    0x3eU, 0x20U, 0xd3U, 0xb2U,   /* PB5 high, PB2 low: buzzer on timer  */
    0x3eU, 0x0dU, 0x30U,          /* SIM: unmask RST 6.5 only            */
    0xfbU,                        /* EI                                  */
    0xc3U, 0x72U, 0x00U           /* 0072 JMP 0072h                      */
};

typedef struct host_log {
    unsigned int sent;
    uint8_t data[8];
    bm_tick_t time[8];
    unsigned int audio;
    bm_audio_change_t changes[8];
    bm_audio_change_t last_audio;
} host_log_t;

static void
transmit(void *context, uint32_t port, uint8_t data, bm_tick_t time)
{
    host_log_t *log = context;
    assert(port == 0U);
    assert(log->sent < 8U);
    log->data[log->sent] = data;
    log->time[log->sent++] = time;
}

static void
audio(void *context, const bm_audio_change_t *change)
{
    host_log_t *log = context;
    assert(change->size == sizeof(bm_audio_change_t));
    if (log->audio < 8U)
        log->changes[log->audio] = *change;
    ++log->audio;
    log->last_audio = *change;
}

int
main(void)
{
    static host_log_t log;
    const bm_host_serial_services_t serial = { transmit, &log };
    const bm_host_audio_services_t sound = { audio, &log };
    const bm_host_capability_t capabilities[] = {
        { BM_HOST_CAPABILITY_SERIAL, BM_HOST_SERIAL_VERSION, sizeof(serial), &serial },
        { BM_HOST_CAPABILITY_AUDIO_OUTPUT, BM_HOST_AUDIO_VERSION, sizeof(sound), &sound }
    };
    bm_host_services_t host = bm_null_host_services();
    bm_m10_config_t config;
    bm_machine_config_t machine;
    bm_session_t *session = NULL;
    size_t accepted = 0U;
    const uint8_t incoming[] = { 'h', 'i', '\r' };
    /* One 8N1 character: 160 register clocks of 16 CPU clocks. */
    const bm_tick_t frame_ns = (bm_tick_t) ((UINT64_C(2560) * 1000000000U) / 2457600U);

    memcpy(firmware, reset_vector, sizeof(reset_vector));
    memcpy(firmware + 0x34U, rst65_vector, sizeof(rst65_vector));
    memcpy(firmware + 0x40U, program, sizeof(program));
    host.capabilities = capabilities;
    host.capability_count = 2U;
    memset(&config, 0, sizeof(config));
    config.firmware = (bm_blob_view_t) { "synthetic-m10-serial", firmware, sizeof(firmware), NULL };
    config.ram_kib = 32U;
    config.rtc_initial_time = (bm_upd1990ac_time_t) { 0, 0, 0, 1, 0, 1 };
    machine = bm_m10_machine_config(&config);
    assert(bm_session_create(&host, &session) == BM_STATUS_OK);
    assert(bm_session_configure(session, &machine) == BM_STATUS_OK);
    assert(bm_session_start(session) == BM_STATUS_OK);

    /* Transmission: 'A' then 'B' one frame later. */
    assert(bm_session_run_for(session, 5U * MS) == BM_STATUS_OK);
    assert(log.sent == 2U);
    assert((log.data[0] == 'A') && (log.data[1] == 'B'));
    assert(log.time[1] > log.time[0]);
    assert((log.time[1] - log.time[0] >= frame_ns - 2U) &&
           (log.time[1] - log.time[0] <= frame_ns + 2U));

    /* Buzzer: high while port B floats after reset, low once it drives
     * 00h, 1 and 0 again, then the timer's square wave, 2457600 / 16 Hz.
     * Changes carry the CPU time of their OUT: the two level writes are
     * 17 clocks (MVI + OUT) apart. */
    assert(log.audio == 5U);
    assert((log.changes[0].level == 1) && (log.changes[1].level == 0));
    assert((log.changes[2].level == 1) && (log.changes[2].tone_denominator == 0U));
    assert((log.changes[3].level == 0) && (log.changes[3].tone_denominator == 0U));
    assert(log.changes[3].time - log.changes[2].time ==
           (bm_tick_t) ((UINT64_C(17) * 1000000000U) / 2457600U) ||
           log.changes[3].time - log.changes[2].time ==
           (bm_tick_t) ((UINT64_C(17) * 1000000000U) / 2457600U) + 1U);
    assert(log.last_audio.tone_numerator == 2457600U);
    assert(log.last_audio.tone_denominator == 16U);

    /* Reception: three characters on RST 6.5, one frame each. */
    assert(bm_session_serial_receive(session, 1U, incoming, 1U, &accepted) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_session_serial_receive(session, 0U, incoming, sizeof(incoming),
                                     &accepted) == BM_STATUS_OK);
    assert(accepted == sizeof(incoming));
    /* Two frames, plus up to one TIMER OUT period of alignment and the
     * interrupt handler: two characters, not yet the third. */
    assert(bm_session_run_for(session, 2U * frame_ns + frame_ns / 4U) == BM_STATUS_OK);
    assert(bm_session_save_persistent_state(session, "ram", ram, sizeof(ram)) ==
           BM_STATUS_OK);
    assert((ram[0x7000U] == 'h') && (ram[0x7001U] == 'i') && (ram[0x7002U] == 0U));
    assert(bm_session_run_for(session, 5U * MS) == BM_STATUS_OK);
    assert(bm_session_save_persistent_state(session, "ram", ram, sizeof(ram)) ==
           BM_STATUS_OK);
    assert(memcmp(ram + 0x7000U, incoming, sizeof(incoming)) == 0);
    assert(log.sent == 2U);

    bm_session_destroy(session);
    return 0;
}
