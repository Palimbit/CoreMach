/* SPDX-License-Identifier: GPL-2.0-or-later */
/* M10 board contracts without firmware, using synthetic 8085 programs:
 * uPD1990AC TP on RST 7.5 (service manual 6.15), battery-backed RAM and
 * clock persistence, the buzzer gate PB5 AND (PB2 OR TIMER OUT) (6.12) and
 * the P.C.S. power-off on PB4 (6.20.2). */
#include <blumach/platforms/null_host.h>
#include <blumach/runtime/runtime.h>
#include <blumach/systems/olivetti_m10.h>

#include <assert.h>
#include <stdint.h>
#include <string.h>

#define MS UINT64_C(1000000)

static uint8_t firmware[BM_M10_FIRMWARE_SIZE];
static uint8_t ram[BM_M10_RAM_STATE_SIZE];
static uint8_t rtc[BM_M10_RTC_STATE_SIZE];

typedef struct wall {
    int64_t seconds;
} wall_t;

static int64_t
wall_clock(void *context)
{
    return ((wall_t *) context)->seconds;
}

static void
load_program(const uint8_t *program, size_t size, uint16_t address)
{
    memcpy(firmware + address, program, size);
}

static bm_session_t *
open_machine(bm_m10_config_t *config)
{
    bm_host_services_t host = bm_null_host_services();
    bm_machine_config_t machine;
    bm_session_t *session = NULL;

    config->firmware = (bm_blob_view_t) {
        "synthetic-m10-board", firmware, sizeof(firmware), NULL
    };
    if (config->ram_kib == 0U)
        config->ram_kib = 32U;
    if (config->rtc_initial_time.month == 0U)
        config->rtc_initial_time = (bm_upd1990ac_time_t) { 0, 0, 10, 21, 4, 10 };
    machine = bm_m10_machine_config(config);
    assert(bm_session_create(&host, &session) == BM_STATUS_OK);
    assert(bm_session_configure(session, &machine) == BM_STATUS_OK);
    assert(bm_session_start(session) == BM_STATUS_OK);
    return session;
}

static uint64_t
inspect(bm_session_t *session, const char *name)
{
    uint64_t value = UINT64_MAX;
    assert(bm_session_inspect_machine(session, name, &value) == BM_STATUS_OK);
    return value;
}

static void
save(bm_session_t *session)
{
    assert(bm_session_save_persistent_state(session, "ram", ram, sizeof(ram)) ==
           BM_STATUS_OK);
    assert(bm_session_save_persistent_state(session, "rtc", rtc, sizeof(rtc)) ==
           BM_STATUS_OK);
}

/* TP programmed to 256 Hz through the serial command pins and STB, then
 * counted by an RST 7.5 handler into 8000h while the CPU halts. */
static void
test_tp_interrupt(void)
{
    static const uint8_t vector[] = {
        0xe5U, 0x2aU, 0x00U, 0x80U, 0x23U, 0x22U, 0x00U, 0x80U, /* count */
        0xe1U, 0xfbU, 0xc9U                                      /* EI; RET */
    };
    static const uint8_t main_program[] = {
        0x31U, 0x00U, 0x00U,            /* LXI SP,0000h */
        0x3eU, 0x03U, 0xd3U, 0xb8U,     /* PA, PB out */
        0x3eU, 0x05U, 0xd3U, 0xb9U,     /* C2-C0 = 101: TP 256 Hz */
        0x3eU, 0x04U, 0xd3U, 0xe0U,     /* STB high */
        0xafU, 0xd3U, 0xe0U,            /* STB low */
        0x3eU, 0x1bU, 0x30U,            /* SIM: reset R7.5, unmask 7.5 */
        0xfbU, 0x76U,                   /* EI; HLT */
        0xc3U, 0x16U, 0x01U             /* JMP back to HLT */
    };
    static const uint8_t reset_jump[] = { 0xc3U, 0x00U, 0x01U };
    bm_m10_config_t config;
    bm_session_t *session;
    unsigned int count;

    memset(firmware, 0xff, sizeof(firmware));
    load_program(reset_jump, sizeof(reset_jump), 0x0000U);
    load_program(vector, sizeof(vector), 0x003cU);
    load_program(main_program, sizeof(main_program), 0x0100U);
    memset(&config, 0, sizeof(config));
    session = open_machine(&config);
    assert(bm_session_run_for(session, 1000U * MS) == BM_STATUS_OK);
    save(session);
    count = ram[0] | (unsigned int) ram[1] << 8U;
    assert(count >= 254U && count <= 257U);
    assert(inspect(session, "rtc_tp_hz") == 256U);
    assert(inspect(session, "rtc_second") == 1U);
    bm_session_destroy(session);
}

/* RAM survives through the "ram" state; only the installed top part is
 * loaded and saved. */
static void
test_ram_persistence(void)
{
    static const uint8_t program[] = {
        0x3aU, 0x00U, 0xe0U,            /* LDA E000h */
        0x3cU, 0x32U, 0x00U, 0xe0U,     /* INR A; STA E000h */
        0x3aU, 0x00U, 0x90U,            /* LDA 9000h */
        0x32U, 0x01U, 0xe0U,            /* STA E001h */
        0x76U                           /* HLT */
    };
    bm_m10_config_t config;
    bm_session_t *session;
    size_t index;

    memset(firmware, 0xff, sizeof(firmware));
    load_program(program, sizeof(program), 0U);
    memset(ram, 0x55, sizeof(ram));
    ram[0x6000] = 0x41U;
    memset(&config, 0, sizeof(config));
    config.ram_kib = 8U;
    config.ram_state = ram;
    config.ram_state_size = sizeof(ram);
    session = open_machine(&config);
    assert(bm_session_run_for(session, 10U * MS) == BM_STATUS_OK);
    save(session);
    assert(ram[0x6000] == 0x42U);
    assert(ram[0x6001] == 0xffU);           /* 9000h not installed */
    for (index = 0U; index < 0x6000U; ++index)
        assert(ram[index] == 0U);
    assert(ram[0x6002] == 0x55U && ram[0x7fffU] == 0x55U);
    bm_session_destroy(session);

    /* The saved image starts the next session: E000h counts on. */
    memset(&config, 0, sizeof(config));
    config.ram_kib = 8U;
    config.ram_state = ram;
    config.ram_state_size = sizeof(ram);
    session = open_machine(&config);
    assert(bm_session_run_for(session, 10U * MS) == BM_STATUS_OK);
    save(session);
    assert(ram[0x6000] == 0x43U);
    bm_session_destroy(session);
}

/* The clock continues from its saved state, advanced by the wall-clock
 * time spent switched off; without a saved state it starts at the
 * configured time. */
static void
test_clock_persistence(void)
{
    static const uint8_t program[] = { 0x76U };
    uint8_t saved_rtc[BM_M10_RTC_STATE_SIZE];
    wall_t wall = { 1000000 };
    bm_m10_config_t config;
    bm_session_t *session;

    memset(firmware, 0xff, sizeof(firmware));
    load_program(program, sizeof(program), 0U);
    memset(&config, 0, sizeof(config));
    config.wall_clock = wall_clock;
    config.wall_clock_context = &wall;
    session = open_machine(&config);
    assert(inspect(session, "rtc_hour") == 10U && inspect(session, "rtc_day") == 21U);
    assert(bm_session_run_for(session, 2500U * MS) == BM_STATUS_OK);
    assert(inspect(session, "rtc_second") == 2U);
    save(session);
    memcpy(saved_rtc, rtc, sizeof(saved_rtc));
    bm_session_destroy(session);

    /* One hour and one minute later. */
    wall.seconds += 3660;
    memset(&config, 0, sizeof(config));
    config.rtc_initial_time = (bm_upd1990ac_time_t) { 0, 0, 0, 1, 0, 1 };
    config.rtc_state = saved_rtc;
    config.rtc_state_size = sizeof(saved_rtc);
    config.wall_clock = wall_clock;
    config.wall_clock_context = &wall;
    session = open_machine(&config);
    assert(inspect(session, "rtc_month") == 10U && inspect(session, "rtc_day") == 21U);
    assert(inspect(session, "rtc_hour") == 11U && inspect(session, "rtc_minute") == 1U);
    assert(inspect(session, "rtc_second") == 2U);
    /* The divider phase is kept: half a second later it ticks. */
    assert(bm_session_run_for(session, 500U * MS) == BM_STATUS_OK);
    assert(inspect(session, "rtc_second") == 3U);
    bm_session_destroy(session);

    /* Without a wall clock the saved time is used as it was. */
    memset(&config, 0, sizeof(config));
    config.rtc_state = saved_rtc;
    config.rtc_state_size = sizeof(saved_rtc);
    session = open_machine(&config);
    assert(inspect(session, "rtc_hour") == 10U && inspect(session, "rtc_second") == 2U);
    bm_session_destroy(session);

    /* A blank state (battery depleted) starts at the configured time. */
    memset(saved_rtc, 0, sizeof(saved_rtc));
    memset(&config, 0, sizeof(config));
    config.rtc_initial_time = (bm_upd1990ac_time_t) { 30, 45, 8, 2, 1, 1 };
    config.rtc_state = saved_rtc;
    config.rtc_state_size = sizeof(saved_rtc);
    session = open_machine(&config);
    assert(inspect(session, "rtc_hour") == 8U && inspect(session, "rtc_minute") == 45U &&
           inspect(session, "rtc_second") == 30U && inspect(session, "rtc_month") == 1U);
    bm_session_destroy(session);
}

/* Timer mode: a continuous square wave of 1024 TIMER IN cycles gated by
 * PB5 high and PB2 low gives 2457600 / 1024 = 2400 Hz. */
static void
test_buzzer_timer(void)
{
    static const uint8_t program[] = {
        0x3eU, 0x03U, 0xd3U, 0xb8U,     /* PA, PB out */
        0x3eU, 0x00U, 0xd3U, 0xbcU,     /* count length 0400h ... */
        0x3eU, 0x44U, 0xd3U, 0xbdU,     /* ... continuous square wave */
        0x3eU, 0xc3U, 0xd3U, 0xb8U,     /* START */
        0x3eU, 0x20U, 0xd3U, 0xbaU,     /* PB5 high, PB2 low */
        0x76U
    };
    bm_m10_config_t config;
    bm_session_t *session;

    memset(firmware, 0xff, sizeof(firmware));
    load_program(program, sizeof(program), 0U);
    memset(&config, 0, sizeof(config));
    session = open_machine(&config);
    assert(inspect(session, "buzzer_tone_hz") == 0U);
    assert(bm_session_run_for(session, 5U * MS) == BM_STATUS_OK);
    assert(inspect(session, "buzzer_tone_hz") == 2400U);
    bm_session_destroy(session);
}

/* PB5 mode with PB2 high: the buzzer follows each PB5 change. Then P.C.S.
 * on PB4 switches the machine off until the power switch (reset). */
static void
test_buzzer_software_and_power_off(void)
{
    static const uint8_t program[] = {
        0x3eU, 0x03U, 0xd3U, 0xb8U,     /* PA, PB out */
        0x06U, 0x0aU,                   /* MVI B,10 */
        0x3eU, 0x24U, 0xd3U, 0xbaU,     /* 0006 PB2 and PB5 high */
        0x3eU, 0x04U, 0xd3U, 0xbaU,     /* PB5 low */
        0x05U, 0xc2U, 0x06U, 0x00U,     /* DCR B; JNZ 0006h */
        0x3eU, 0x10U, 0xd3U, 0xbaU,     /* P.C.S. */
        0x76U
    };
    bm_m10_config_t config;
    bm_session_t *session;

    memset(firmware, 0xff, sizeof(firmware));
    load_program(program, sizeof(program), 0U);
    memset(&config, 0, sizeof(config));
    session = open_machine(&config);
    assert(bm_session_run_for(session, 5U * MS) == BM_STATUS_OK);
    assert(inspect(session, "buzzer_edges") == 20U);
    assert(inspect(session, "buzzer_tone_hz") == 0U);
    assert(inspect(session, "powered_off") == 1U);
    assert(bm_session_run_for(session, 5U * MS) == BM_STATUS_OK);
    assert(inspect(session, "buzzer_edges") == 20U);
    assert(bm_session_reset(session) == BM_STATUS_OK);
    assert(inspect(session, "powered_off") == 0U);
    assert(bm_session_run_for(session, 5U * MS) == BM_STATUS_OK);
    assert(inspect(session, "buzzer_edges") == 40U);
    assert(inspect(session, "powered_off") == 1U);
    bm_session_destroy(session);
}

/* The 240 x 64 panel shows columns 0-239 of the five chips per half; the
 * render stays inside an exactly sized framebuffer. */
static void
test_render_bounds(void)
{
    static const uint8_t program[] = { 0x76U };
    static uint32_t pixels[240U * 64U + 16U];
    bm_video_framebuffer_t framebuffer;
    bm_video_geometry_t geometry;
    bm_m10_config_t config;
    bm_session_t *session;
    size_t index;

    memset(firmware, 0xff, sizeof(firmware));
    load_program(program, sizeof(program), 0U);
    memset(&config, 0, sizeof(config));
    session = open_machine(&config);
    assert(bm_session_video_geometry(session, &geometry) == BM_STATUS_OK);
    assert(geometry.width == 240U && geometry.height == 64U);
    for (index = 0U; index < sizeof(pixels) / sizeof(pixels[0]); ++index)
        pixels[index] = 0x12345678U;
    framebuffer = (bm_video_framebuffer_t) { pixels, 240U * 64U, 240U, geometry };
    assert(bm_session_render_video(session, &framebuffer) == BM_STATUS_OK);
    for (index = 0U; index < 240U * 64U; ++index)
        assert(pixels[index] == pixels[0] && pixels[index] != 0x12345678U);
    for (; index < sizeof(pixels) / sizeof(pixels[0]); ++index)
        assert(pixels[index] == 0x12345678U);
    framebuffer.pixel_capacity = 240U * 63U;
    assert(bm_session_render_video(session, &framebuffer) == BM_STATUS_INVALID_ARGUMENT);
    bm_session_destroy(session);
}

int
main(void)
{
    test_render_bounds();
    test_tp_interrupt();
    test_ram_persistence();
    test_clock_persistence();
    test_buzzer_timer();
    test_buzzer_software_and_power_off();
    return 0;
}
