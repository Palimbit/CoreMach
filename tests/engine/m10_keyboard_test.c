/* SPDX-License-Identifier: GPL-2.0-or-later */
/* M10 keyboard contract without firmware: a synthetic 8085 program strobes
 * the nine matrix rows through the 81C55 (service manual 6.8, Fig. 6-7),
 * reads the returns at E8h and the language jumpers on port C (6.16.3),
 * and stores what it sees in RAM, which the test reads back as the "ram"
 * persistent state. */
#include <blumach/platforms/null_host.h>
#include <blumach/runtime/runtime.h>
#include <blumach/systems/olivetti_m10.h>

#include <assert.h>
#include <stdint.h>
#include <string.h>

/* RAM 8000h-8007h rows 0-7, 8008h row 8, 8009h all rows, 800Ah port C. */
static const uint8_t scan_program[] = {
    0x3eU, 0x03U, 0xd3U, 0xb8U,         /* PA, PB out; PC in (ALT1) */
    0x21U, 0x00U, 0x80U,                /* 0004 LXI H,8000h */
    0x06U, 0xfeU,                       /* MVI B,FEh */
    0x0eU, 0x08U,                       /* MVI C,8 */
    0x78U, 0xd3U, 0xb9U,                /* 000B MOV A,B; OUT B9h */
    0x3eU, 0x01U, 0xd3U, 0xbaU,         /* PB0 high: row 8 off */
    0xdbU, 0xe8U, 0x77U, 0x23U,         /* IN E8h; MOV M,A; INX H */
    0x78U, 0x07U, 0x47U,                /* next row */
    0x0dU, 0xc2U, 0x0bU, 0x00U,         /* DCR C; JNZ 000Bh */
    0x3eU, 0xffU, 0xd3U, 0xb9U,         /* rows 0-7 off */
    0xafU, 0xd3U, 0xbaU,                /* row 8 on */
    0xdbU, 0xe8U, 0x77U, 0x23U,
    0xafU, 0xd3U, 0xb9U, 0xd3U, 0xbaU,  /* every row on */
    0xdbU, 0xe8U, 0x77U, 0x23U,
    0xdbU, 0xbbU, 0x77U,                /* IN BBh; MOV M,A */
    0xc3U, 0x04U, 0x00U                 /* JMP 0004h */
};

typedef struct fixture {
    uint8_t firmware[BM_M10_FIRMWARE_SIZE];
    uint8_t ram[BM_M10_RAM_STATE_SIZE];
    bm_m10_config_t config;
    bm_session_t *session;
} fixture_t;

static fixture_t fixture;

static void
open_machine(bm_m10_language_t language)
{
    bm_host_services_t host = bm_null_host_services();
    bm_machine_config_t machine;

    memset(fixture.firmware, 0xff, sizeof(fixture.firmware));
    memcpy(fixture.firmware, scan_program, sizeof(scan_program));
    memset(&fixture.config, 0, sizeof(fixture.config));
    fixture.config.firmware = (bm_blob_view_t) {
        "synthetic-m10-keyboard", fixture.firmware, sizeof(fixture.firmware), NULL
    };
    fixture.config.ram_kib = 32U;
    fixture.config.language = language;
    fixture.config.rtc_initial_time = (bm_upd1990ac_time_t) { 0, 0, 0, 1, 0, 1 };
    machine = bm_m10_machine_config(&fixture.config);
    assert(bm_session_create(&host, &fixture.session) == BM_STATUS_OK);
    assert(bm_session_configure(fixture.session, &machine) == BM_STATUS_OK);
    assert(bm_session_start(fixture.session) == BM_STATUS_OK);
}

static void
close_machine(void)
{
    bm_session_destroy(fixture.session);
    fixture.session = NULL;
}

/* Run long enough for several complete scans, then read RAM back. */
static const uint8_t *
scan(void)
{
    assert(bm_session_run_for(fixture.session, UINT64_C(5000000)) == BM_STATUS_OK);
    assert(bm_session_save_persistent_state(fixture.session, "ram", fixture.ram,
                                            sizeof(fixture.ram)) == BM_STATUS_OK);
    return fixture.ram;
}

static bm_status_t
key(bm_key_code_t code, int pressed)
{
    const bm_input_event_t event = {
        .kind = BM_INPUT_KEY, .key = code, .pressed = pressed
    };
    return bm_session_send_input(fixture.session, &event);
}

static uint64_t
inspect(const char *name)
{
    uint64_t value = UINT64_MAX;
    assert(bm_session_inspect_machine(fixture.session, name, &value) == BM_STATUS_OK);
    return value;
}

/* Matrix code row * 8 + bit reads 0 only on its own row (Fig. 6-7). */
static void
expect_only(int code)
{
    const uint8_t *ram = scan();
    unsigned int row;
    for (row = 0U; row < 9U; ++row) {
        const uint8_t expected = (code >= 0 && (unsigned int) code / 8U == row) ?
            (uint8_t) ~(1U << ((unsigned int) code % 8U)) : 0xffU;
        assert(ram[row] == expected);
    }
}

static void
test_matrix(void)
{
    static const struct { bm_key_code_t key; int code; } positions[] = {
        { BM_KEY_Q, 0 }, { BM_KEY_GRAVE, 2 }, { BM_KEY_1, 3 },
        { BM_KEY_Y, 8 }, { BM_KEY_0, 13 }, { BM_KEY_EQUAL, 15 },
        { BM_KEY_RIGHT_BRACKET, 16 }, { BM_KEY_LEFT_BRACKET, 17 },
        { BM_KEY_APOSTROPHE, 19 }, { BM_KEY_M, 23 }, { BM_KEY_A, 24 },
        { BM_KEY_NON_US_BACKSLASH, 25 }, { BM_KEY_Z, 26 },
        { BM_KEY_L, 39 }, { BM_KEY_P, 46 }, { BM_KEY_SPACE, 47 },
        { BM_KEY_LEFT, 48 }, { BM_KEY_BACKSPACE, 52 }, { BM_KEY_ENTER, 54 },
        { BM_KEY_F9, 55 }, { BM_KEY_F1, 56 }, { BM_KEY_F8, 63 },
        { BM_KEY_F10, 64 }, { BM_KEY_RIGHT_SHIFT, 66 },
        { BM_KEY_LEFT_CONTROL, 67 }, { BM_KEY_LEFT_ALT, 68 },
        { BM_KEY_INSERT, 69 }, { BM_KEY_PAUSE, 71 }
    };
    const uint8_t *ram;
    size_t index;

    open_machine(BM_M10_LANGUAGE_ITALIAN);
    expect_only(-1);
    assert(fixture.ram[9] == 0xffU);
    for (index = 0U; index < sizeof(positions) / sizeof(positions[0]); ++index) {
        assert(key(positions[index].key, 1) == BM_STATUS_OK);
        expect_only(positions[index].code);
        assert(key(positions[index].key, 0) == BM_STATUS_OK);
        expect_only(-1);
    }

    /* Two keys on different rows: each row sees its own key; strobing all
     * rows together ORs them, with no phantom key (one diode per key). */
    assert(key(BM_KEY_Q, 1) == BM_STATUS_OK);
    assert(key(BM_KEY_P, 1) == BM_STATUS_OK);
    assert(key(BM_KEY_LEFT_SHIFT, 1) == BM_STATUS_OK);
    ram = scan();
    assert(ram[0] == 0xfeU && ram[5] == 0xbfU && ram[8] == 0xfbU);
    assert(ram[1] == 0xffU && ram[9] == (uint8_t) (0xfeU & 0xbfU & 0xfbU));
    assert(key(BM_KEY_Q, 0) == BM_STATUS_OK);
    assert(key(BM_KEY_P, 0) == BM_STATUS_OK);
    assert(key(BM_KEY_LEFT_SHIFT, 0) == BM_STATUS_OK);
    expect_only(-1);

    /* Keys without an M10 counterpart are refused and change nothing. */
    assert(key(BM_KEY_ESCAPE, 1) == BM_STATUS_UNSUPPORTED);
    expect_only(-1);
    close_machine();
}

/* CAPS LOCK latches mechanically: one host press closes it until the next
 * press, and the keyboard LED reports it. */
static void
test_caps_lock(void)
{
    bm_keyboard_led_state_t leds = { 0xffU };

    open_machine(BM_M10_LANGUAGE_ENGLISH);
    assert(key(BM_KEY_CAPS_LOCK, 1) == BM_STATUS_OK);
    assert(key(BM_KEY_CAPS_LOCK, 0) == BM_STATUS_OK);
    expect_only(70);
    assert(inspect("caps_lock") == 1U);
    assert(bm_session_keyboard_leds(fixture.session, &leds) == BM_STATUS_OK);
    assert(leds.indicators == BM_KEYBOARD_LED_CAPS_LOCK);
    assert(bm_session_reset(fixture.session) == BM_STATUS_OK);
    expect_only(70);
    assert(key(BM_KEY_CAPS_LOCK, 1) == BM_STATUS_OK);
    assert(key(BM_KEY_CAPS_LOCK, 0) == BM_STATUS_OK);
    expect_only(-1);
    assert(bm_session_keyboard_leds(fixture.session, &leds) == BM_STATUS_OK);
    assert(leds.indicators == 0U);
    close_machine();
}

/* Language jumpers read on PC5:PC4: 0 English, 1 German, 2 Italian,
 * 3 French (Table 6-7). Lines without a modelled source float high. */
static void
test_language_jumpers(void)
{
    unsigned int language;

    for (language = 0U; language < 4U; ++language) {
        const uint8_t *ram;
        open_machine((bm_m10_language_t) language);
        ram = scan();
        assert((ram[10] & 0x30U) == (uint8_t) (language << 4U));
        assert((ram[10] & 0xceU) == 0xceU);
        assert(inspect("language") == language);
        close_machine();
    }
}

int
main(void)
{
    test_matrix();
    test_caps_lock();
    test_language_jumpers();
    return 0;
}
