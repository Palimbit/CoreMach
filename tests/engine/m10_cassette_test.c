/* SPDX-License-Identifier: GPL-2.0-or-later */
/* M10 cassette interface without firmware. A synthetic program starts the
 * recorder motor (E0h bit 3), toggles SOD twice and counts SID edges into
 * RAM at F000h while a 1 kHz tape plays for 100 ms. */
#include <blumach/platforms/null_host.h>
#include <blumach/runtime/runtime.h>
#include <blumach/systems/olivetti_m10.h>

#include <assert.h>
#include <stdint.h>
#include <string.h>

#define MS UINT64_C(1000000)
#define RATE 48000U

static uint8_t firmware[BM_M10_FIRMWARE_SIZE];
static uint8_t ram[BM_M10_RAM_STATE_SIZE];
static int16_t tape[RATE / 5U]; /* 200 ms */

static const uint8_t reset_vector[] = { 0xc3U, 0x40U, 0x00U };   /* JMP 0040h */
static const uint8_t program[] = {                               /* at 0040h */
    0x31U, 0xf0U, 0xffU,          /* LXI SP,FFF0h                        */
    0x11U, 0x00U, 0x00U,          /* LXI D,0     edge count              */
    0x3eU, 0x08U, 0xd3U, 0xe0U,   /* REMOTE on                           */
    0x3eU, 0xc0U, 0x30U,          /* SIM: SOD 1 (MIC low)                */
    0x3eU, 0x40U, 0x30U,          /* SIM: SOD 0 (MIC high), 11 clocks on */
    0x06U, 0x00U,                 /* MVI B,0     last SID                */
    0x20U,                        /* 0052 RIM                            */
    0xe6U, 0x80U,                 /* ANI 80h                             */
    0xb8U,                        /* CMP B                               */
    0xcaU, 0x52U, 0x00U,          /* JZ 0052h                            */
    0x47U,                        /* MOV B,A                             */
    0x13U,                        /* INX D                               */
    0xebU,                        /* XCHG                                */
    0x22U, 0x00U, 0xf0U,          /* SHLD F000h                          */
    0xebU,                        /* XCHG                                */
    0xc3U, 0x52U, 0x00U           /* JMP 0052h                           */
};

typedef struct host_log {
    unsigned int motor_changes;
    int motor;
    bm_tick_t motor_time;
    unsigned int records;
    int levels[4];
    bm_tick_t times[4];
} host_log_t;

static void
motor(void *context, uint32_t port, int on, bm_tick_t time)
{
    host_log_t *log = context;
    assert(port == 0U);
    ++log->motor_changes;
    log->motor = on;
    log->motor_time = time;
}

static void
record(void *context, uint32_t port, int level, bm_tick_t time)
{
    host_log_t *log = context;
    assert(port == 0U);
    if (log->records < 4U) {
        log->levels[log->records] = level;
        log->times[log->records] = time;
    }
    ++log->records;
}

static unsigned int
edges(bm_session_t *session)
{
    assert(bm_session_save_persistent_state(session, "ram", ram, sizeof(ram)) ==
           BM_STATUS_OK);
    return (unsigned int) ram[0x7000U] | ((unsigned int) ram[0x7001U] << 8U);
}

int
main(void)
{
    static host_log_t log;
    const bm_host_cassette_services_t cassette = { motor, record, &log };
    const bm_host_capability_t capability = {
        BM_HOST_CAPABILITY_CASSETTE, BM_HOST_CASSETTE_VERSION,
        sizeof(cassette), &cassette
    };
    bm_host_services_t host = bm_null_host_services();
    bm_m10_config_t config;
    bm_machine_config_t machine;
    bm_session_t *session = NULL;
    bm_cassette_tape_t medium;
    size_t position = 0U;
    uint64_t value = 0U;
    unsigned int index;
    unsigned int count;

    /* 1 kHz: 24 samples high, 24 low, for 100 ms; then silence. */
    for (index = 0U; index < RATE / 10U; ++index)
        tape[index] = ((index / 24U) % 2U) == 0U ? 8000 : -8000;
    memcpy(firmware, reset_vector, sizeof(reset_vector));
    memcpy(firmware + 0x40U, program, sizeof(program));
    host.capabilities = &capability;
    host.capability_count = 1U;
    memset(&config, 0, sizeof(config));
    config.firmware = (bm_blob_view_t) { "synthetic-m10-cassette", firmware, sizeof(firmware), NULL };
    config.ram_kib = 32U;
    config.rtc_initial_time = (bm_upd1990ac_time_t) { 0, 0, 0, 1, 0, 1 };
    machine = bm_m10_machine_config(&config);
    assert(bm_session_create(&host, &session) == BM_STATUS_OK);
    assert(bm_session_configure(session, &machine) == BM_STATUS_OK);
    assert(bm_session_start(session) == BM_STATUS_OK);

    medium.samples = tape;
    medium.sample_count = sizeof(tape) / sizeof(tape[0]);
    medium.sample_rate = RATE;
    medium.position = 0U;
    assert(bm_session_cassette_load(session, 0U, &medium) == BM_STATUS_OK);
    medium.sample_rate = 0U;
    assert(bm_session_cassette_load(session, 0U, &medium) == BM_STATUS_INVALID_ARGUMENT);
    medium.sample_rate = RATE;
    assert(bm_session_cassette_load(session, 1U, &medium) == BM_STATUS_INVALID_ARGUMENT);

    /* The motor starts and SOD reaches MIC inverted, 11 clocks apart. */
    assert(bm_session_run_for(session, 50U * MS) == BM_STATUS_OK);
    assert((log.motor_changes == 1U) && (log.motor == 1));
    assert(bm_session_inspect_machine(session, "cassette_motor", &value) == BM_STATUS_OK);
    assert(value == 1U);
    assert((log.records == 2U) && (log.levels[0] == 0) && (log.levels[1] == 1));
    assert((log.times[1] - log.times[0] == (bm_tick_t) ((UINT64_C(11) * 1000000000U) / 2457600U)) ||
           (log.times[1] - log.times[0] == (bm_tick_t) ((UINT64_C(11) * 1000000000U) / 2457600U) + 1U));
    assert(log.times[0] > log.motor_time);

    /* Two SID edges per millisecond, and the tape where the time says. */
    count = edges(session);
    assert((count >= 98U) && (count <= 102U));
    assert(bm_session_cassette_position(session, 0U, &position) == BM_STATUS_OK);
    assert((position >= 50U * RATE / 1000U - 48U) && (position <= 50U * RATE / 1000U + 48U));

    /* After 100 ms the tape is silent: SID holds and the count stops. */
    assert(bm_session_run_for(session, 100U * MS) == BM_STATUS_OK);
    count = edges(session);
    assert((count >= 198U) && (count <= 201U));
    assert(bm_session_run_for(session, 20U * MS) == BM_STATUS_OK);
    assert(edges(session) == count);

    /* Ejecting stops it for good; the end of the tape reads its length. */
    assert(bm_session_run_for(session, 100U * MS) == BM_STATUS_OK);
    assert(bm_session_cassette_position(session, 0U, &position) == BM_STATUS_OK);
    assert(position == medium.sample_count);
    assert(bm_session_cassette_load(session, 0U, NULL) == BM_STATUS_OK);
    assert(bm_session_cassette_position(session, 0U, &position) == BM_STATUS_OK);
    assert(position == 0U);

    /* A reset stops the motor. */
    assert(bm_session_reset(session) == BM_STATUS_OK);
    assert((log.motor_changes >= 2U) && (log.motor == 0));
    bm_session_destroy(session);
    return 0;
}
