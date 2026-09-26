/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Authored IBM6183355 protocol cases; no ROM or captured keyboard traffic. */
#include <blumach/components/keyboard_at.h>
#include <blumach/components/at_pic.h>
#include <blumach/platforms/null_host.h>
#include "failure_injection_host.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct fixture {
    bm_at_keyboard_t *k;
    bm_kbc8042_t     *controller;
    bm_at_pic_t      *pic;
    unsigned          count, calls;
    uint8_t           bytes[4096];
    uint64_t          times[4096];
    bm_status_t       result;
    int               after, inspect, feedback, irq;
} fixture_t;
static bm_at_keyboard_state_t
state(fixture_t *f)
{
    bm_at_keyboard_state_t s;
    assert(bm_at_keyboard_state(f->k, &s) == BM_STATUS_OK);
    return s;
}
static bm_input_event_t
event(bm_key_code_t key, int pressed)
{
    bm_input_event_t e = { 0 };
    e.kind             = BM_INPUT_KEY;
    e.key              = key;
    e.pressed          = pressed;
    return e;
}
static bm_status_t
send_byte(void *p, uint8_t v)
{
    fixture_t *f = p;
    ++f->calls;
    if (f->inspect) {
        bm_input_event_t e = event(BM_KEY_B, 1);
        assert(bm_at_keyboard_advance(f->k, 1) == BM_STATUS_INVALID_STATE);
        assert(bm_at_keyboard_command(f->k, 0xee) == BM_STATUS_INVALID_STATE);
        assert(bm_at_keyboard_reset(f->k) == BM_STATUS_INVALID_STATE);
        assert(bm_at_keyboard_input(f->k, &e) == BM_STATUS_INVALID_STATE);
        bm_at_keyboard_destroy(f->k);
    }
    if (f->feedback)
        assert(bm_at_keyboard_set_inhibit(f->k, 1) == BM_STATUS_OK);
    if (f->result && !f->after)
        return f->result;
    if (f->controller)
        return bm_kbc8042_receive_keyboard(f->controller, v);
    assert(f->count < sizeof(f->bytes));
    f->bytes[f->count]   = v;
    f->times[f->count++] = state(f).cycles;
    return f->result;
}
static bm_at_keyboard_config_t
config(fixture_t *f)
{
    bm_at_keyboard_config_t c             = { 0 };
    c.send                                = send_byte;
    c.send_context                        = f;
    c.clock.cycles_per_second_numerator   = 1000000;
    c.clock.cycles_per_second_denominator = 1;
    c.power_on_cycles                     = 150000;
    c.bat_cycles                          = 300000;
    c.reset_accept_cycles                 = 500;
    c.byte_cycles                         = 100;
    c.iso_layout                          = 1;
    return c;
}
static void
start_rate(fixture_t *f, uint64_t num, uint64_t den)
{
    bm_host_services_t      h = bm_null_host_services();
    bm_at_keyboard_config_t c;
    memset(f, 0, sizeof(*f));
    c                                     = config(f);
    c.clock.cycles_per_second_numerator   = num;
    c.clock.cycles_per_second_denominator = den;
    assert(bm_at_keyboard_create(&h, &c, &f->k) == BM_STATUS_OK && !f->calls);
}
static void
start(fixture_t *f)
{
    start_rate(f, 1000000, 1);
}
static void
advance(fixture_t *f, uint64_t n)
{
    assert(bm_at_keyboard_advance(f->k, n) == BM_STATUS_OK);
}
static void
expected(fixture_t *f, const uint8_t *v, unsigned n)
{
    assert(f->count == n);
    assert(!n || !memcmp(f->bytes, v, n));
    f->count = 0;
}
#define EXPECT(f, ...)                        \
    do {                                      \
        const uint8_t v_[] = { __VA_ARGS__ }; \
        expected(f, v_, sizeof(v_));          \
    } while (0)
static void
boot(fixture_t *f)
{
    advance(f, 450100);
    EXPECT(f, 0xaa);
}
static void
done(fixture_t *f)
{
    bm_at_keyboard_destroy(f->k);
}
static void
command(fixture_t *f, uint8_t v)
{
    assert(bm_at_keyboard_command(f->k, v) == BM_STATUS_OK);
    advance(f, 100);
}
static void
ack(fixture_t *f, uint8_t v)
{
    command(f, v);
    EXPECT(f, 0xfa);
}
static void
key(fixture_t *f, bm_key_code_t v, int down)
{
    bm_input_event_t e = event(v, down);
    assert(bm_at_keyboard_input(f->k, &e) == BM_STATUS_OK);
}
static void
tap(fixture_t *f, bm_key_code_t v)
{
    key(f, v, 1);
    key(f, v, 0);
    advance(f, 2000);
}
static void
set(fixture_t *f, unsigned v)
{
    ack(f, 0xf0);
    ack(f, (uint8_t) v);
}
static void
leds(fixture_t *f, unsigned v)
{
    ack(f, 0xed);
    ack(f, (uint8_t) v);
}
static void
lifecycle(void)
{
    fixture_t               f;
    uint64_t                d = 7;
    bm_keyboard_led_state_t ls;
    start(&f);
    assert(bm_at_keyboard_command(f.k, 0xfe) == BM_STATUS_IDLE);
    assert(bm_at_keyboard_next_deadline(f.k, &d) == BM_STATUS_OK && d == 150000);
    advance(&f, 149999);
    assert(!f.calls && state(&f).leds == 0);
    advance(&f, 1);
    assert(state(&f).phase == BM_AT_KEYBOARD_BAT && state(&f).leds == 7);
    assert(bm_at_keyboard_command(f.k, 0xff) == BM_STATUS_IDLE);
    assert(bm_at_keyboard_set_inhibit(f.k, 1) == BM_STATUS_OK);
    advance(&f, 300000);
    assert(!state(&f).leds && !state(&f).enabled && !f.calls);
    assert(bm_at_keyboard_next_deadline(f.k, &d) == BM_STATUS_IDLE && !d);
    assert(bm_at_keyboard_set_inhibit(f.k, 0) == BM_STATUS_OK);
    advance(&f, 99);
    assert(!f.calls);
    advance(&f, 1);
    EXPECT(&f, 0xaa);
    assert(state(&f).enabled && state(&f).scan_set == 2 && state(&f).typematic == 0x2b);
    leds(&f, 5);
    assert(bm_at_keyboard_leds(f.k, &ls) == BM_STATUS_OK && ls.indicators == 5);
    f.feedback = 1;
    ack(&f, 0xff);
    assert(state(&f).phase == BM_AT_KEYBOARD_RESET_ACCEPT && !state(&f).enabled);
    advance(&f, 1000000);
    assert(state(&f).phase_remaining == 500);
    f.feedback = 0;
    assert(bm_at_keyboard_set_inhibit(f.k, 0) == BM_STATUS_OK);
    advance(&f, 499);
    assert(state(&f).leds == 5);
    assert(bm_at_keyboard_set_inhibit(f.k, 1) == BM_STATUS_OK);
    assert(bm_at_keyboard_set_inhibit(f.k, 0) == BM_STATUS_OK);
    advance(&f, 499);
    assert(state(&f).phase_remaining == 1);
    advance(&f, 1);
    assert(state(&f).phase == BM_AT_KEYBOARD_BAT && state(&f).leds == 7);
    advance(&f, 300099);
    assert(!f.count && !state(&f).enabled);
    advance(&f, 1);
    EXPECT(&f, 0xaa);
    assert(state(&f).enabled);
    done(&f);
}
static void
commands(void)
{
    fixture_t f;
    start(&f);
    boot(&f);
    command(&f, 0xee);
    EXPECT(&f, 0xee);
    command(&f, 0xef);
    EXPECT(&f, 0xfe);
    command(&f, 0xfe);
    EXPECT(&f, 0xee); /* FE does not overwrite resend history. */
    command(&f, 0xf1);
    EXPECT(&f, 0xfe);
    command(&f, 0xf2);
    EXPECT(&f, 0xfa);
    advance(&f, 200);
    EXPECT(&f, 0xab, 0x83);
    for (unsigned i = 0; i < 8; ++i) {
        leds(&f, i);
        assert(state(&f).leds == i);
    }
    ack(&f, 0xed);
    command(&f, 8);
    EXPECT(&f, 0xfe);
    assert(state(&f).parameter == 0xed && state(&f).leds == 7);
    command(&f, 0xee);
    EXPECT(&f, 0xee);
    assert(!state(&f).parameter && state(&f).leds == 7);
    for (unsigned i = 1; i <= 3; ++i) {
        set(&f, i);
        ack(&f, 0xf0);
        command(&f, 0);
        EXPECT(&f, 0xfa);
        advance(&f, 100);
        {
            uint8_t v = (uint8_t) i;
            expected(&f, &v, 1);
        }
        ack(&f, 0xf0);
        command(&f, 4);
        EXPECT(&f, 0xfe);
        ack(&f, (uint8_t) i);
    }
    ack(&f, 0xf3);
    command(&f, 0x80);
    EXPECT(&f, 0xfe);
    ack(&f, 0x7f);
    ack(&f, 0xf5);
    assert(!state(&f).enabled && state(&f).scan_set == 2);
    assert(state(&f).typematic == 0x2b && state(&f).leds == 7);
    tap(&f, BM_KEY_A);
    assert(!f.count);
    ack(&f, 0xf6);
    assert(state(&f).enabled && state(&f).leds == 7);
    tap(&f, BM_KEY_A);
    EXPECT(&f, 0x1c, 0xf0, 0x1c);
    key(&f, BM_KEY_B, 1);
    ack(&f, 0xf4);
    assert(!state(&f).scan_count && !state(&f).repeat_key);
    done(&f);
}
static void
key_types(void)
{
    fixture_t f;
    start(&f);
    boot(&f);
    set(&f, 3);
    tap(&f, BM_KEY_A);
    EXPECT(&f, 0x1c); /* Default letters repeat, no break. */
    tap(&f, BM_KEY_LEFT_SHIFT);
    EXPECT(&f, 0x12, 0xf0, 0x12);
    tap(&f, BM_KEY_RIGHT_ALT);
    EXPECT(&f, 0x39);
    for (unsigned v = 0xf7; v <= 0xfa; ++v) {
        ack(&f, (uint8_t) v);
        key(&f, BM_KEY_A, 1);
        assert(!!state(&f).repeat_key == (v == 0xf7 || v == 0xfa));
        key(&f, BM_KEY_A, 0);
        advance(&f, 300);
        if (v == 0xf8 || v == 0xfa) {
            EXPECT(&f, 0x1c, 0xf0, 0x1c);
        } else {
            EXPECT(&f, 0x1c);
        }
    }
    for (unsigned v = 0xfb; v <= 0xfd; ++v) {
        ack(&f, (uint8_t) v);
        command(&f, 0);
        EXPECT(&f, 0xfe);
        ack(&f, 0x1c);
        ack(&f, 0x32);
        assert(state(&f).parameter == v);
        command(&f, 0xee);
        EXPECT(&f, 0xee);
        key(&f, BM_KEY_A, 1);
        assert(!!state(&f).repeat_key == (v == 0xfb));
        key(&f, BM_KEY_A, 0);
        tap(&f, BM_KEY_B);
        if (v == 0xfc) {
            EXPECT(&f, 0x1c, 0xf0, 0x1c, 0x32, 0xf0, 0x32);
        } else {
            EXPECT(&f, 0x1c, 0x32);
        }
    }
    tap(&f, BM_KEY_BACKSLASH);
    EXPECT(&f, 0x53, 0xf0, 0x53); /* FA affected other keys. */
    ack(&f, 0xfb);
    command(&f, 0x5c);
    EXPECT(&f, 0xfe);
    ack(&f, 0x53);
    ack(&f, 0x13);
    command(&f, 0xee);
    EXPECT(&f, 0xee);
    ack(&f, 0xf6);
    set(&f, 3);
    tap(&f, BM_KEY_BACKSLASH);
    EXPECT(&f, 0x53);
    done(&f);
}
static void
special_keys(void)
{
    fixture_t        f;
    bm_input_event_t e = event(BM_KEY_PAUSE, 1);
    start(&f);
    boot(&f);
    assert(bm_at_keyboard_input(f.k, &e) == BM_STATUS_UNSUPPORTED && !state(&f).scan_count);
    tap(&f, BM_KEY_PRINT_SCREEN);
    EXPECT(&f, 0xe0, 0x12, 0xe0, 0x7c, 0xe0, 0xf0, 0x7c, 0xe0, 0xf0, 0x12);
    key(&f, BM_KEY_LEFT_CONTROL, 1);
    advance(&f, 100);
    EXPECT(&f, 0x14);
    tap(&f, BM_KEY_PAUSE);
    EXPECT(&f, 0xe0, 0x7e, 0xe0, 0xf0, 0x7e);
    tap(&f, BM_KEY_PRINT_SCREEN);
    EXPECT(&f, 0xe0, 0x7c, 0xe0, 0xf0, 0x7c);
    key(&f, BM_KEY_LEFT_CONTROL, 0);
    advance(&f, 200);
    EXPECT(&f, 0xf0, 0x14);
    key(&f, BM_KEY_LEFT_ALT, 1);
    advance(&f, 100);
    EXPECT(&f, 0x11);
    tap(&f, BM_KEY_PRINT_SCREEN);
    EXPECT(&f, 0x84, 0xf0, 0x84);
    key(&f, BM_KEY_LEFT_ALT, 0);
    advance(&f, 200);
    EXPECT(&f, 0xf0, 0x11);
    leds(&f, 2);
    tap(&f, BM_KEY_UP);
    EXPECT(&f, 0xe0, 0x12, 0xe0, 0x75, 0xe0, 0xf0, 0x75, 0xe0, 0xf0, 0x12);
    leds(&f, 0);
    key(&f, BM_KEY_LEFT_SHIFT, 1);
    key(&f, BM_KEY_RIGHT_SHIFT, 1);
    advance(&f, 200);
    EXPECT(&f, 0x12, 0x59);
    tap(&f, BM_KEY_UP);
    EXPECT(&f, 0xe0, 0xf0, 0x12, 0xe0, 0xf0, 0x59, 0xe0, 0x75, 0xe0, 0xf0, 0x75, 0xe0, 0x12, 0xe0, 0x59);
    key(&f, BM_KEY_LEFT_SHIFT, 0);
    key(&f, BM_KEY_RIGHT_SHIFT, 0);
    advance(&f, 400);
    EXPECT(&f, 0xf0, 0x12, 0xf0, 0x59);
    set(&f, 1);
    tap(&f, BM_KEY_PAUSE);
    EXPECT(&f, 0xe1, 0x1d, 0x45, 0xe1, 0x9d, 0xc5);
    tap(&f, BM_KEY_PRINT_SCREEN);
    EXPECT(&f, 0xe0, 0x2a, 0xe0, 0x37, 0xe0, 0xb7, 0xe0, 0xaa);
    leds(&f, 2);
    tap(&f, BM_KEY_UP);
    EXPECT(&f, 0xe0, 0x2a, 0xe0, 0x48, 0xe0, 0xc8, 0xe0, 0xaa);
    set(&f, 3);
    tap(&f, BM_KEY_PAUSE);
    EXPECT(&f, 0x62);
    done(&f);
}
static void
fifo(void)
{
    fixture_t f;
    start(&f);
    boot(&f);
    assert(bm_at_keyboard_set_inhibit(f.k, 1) == BM_STATUS_OK);
    for (unsigned i = 0; i < 5; ++i) {
        key(&f, BM_KEY_A, 1);
        key(&f, BM_KEY_A, 0);
    }
    key(&f, BM_KEY_UP, 1);
    assert(state(&f).scan_count == 15 && state(&f).overrun);
    key(&f, BM_KEY_B, 1);
    assert(state(&f).scan_count == 15);
    assert(bm_at_keyboard_command(f.k, 0xee) == BM_STATUS_OK);
    assert(bm_at_keyboard_set_inhibit(f.k, 0) == BM_STATUS_OK);
    advance(&f, 1700);
    assert(f.count == 17 && f.bytes[0] == 0xee && f.bytes[16] == 0);
    for (unsigned i = 1; i < 16; i += 3)
        assert(f.bytes[i] == 0x1c && f.bytes[i + 1] == 0xf0 && f.bytes[i + 2] == 0x1c);
    f.count = 0;
    assert(!state(&f).overrun);
    key(&f, BM_KEY_C, 1);
    advance(&f, 100);
    EXPECT(&f, 0x21);
    ack(&f, 0xf4);
    assert(!state(&f).repeat_key);
    key(&f, BM_KEY_D, 1);
    assert(bm_at_keyboard_set_inhibit(f.k, 1) == BM_STATUS_OK);
    advance(&f, 10000000);
    assert(state(&f).scan_count == 1); /* No typematic accumulation. */
    assert(bm_at_keyboard_set_inhibit(f.k, 0) == BM_STATUS_OK);
    advance(&f, 100);
    EXPECT(&f, 0x23);
    assert(state(&f).repeat_remaining == 499900);
    done(&f);
}
static void
suspended_input(void)
{
    fixture_t f;
    start(&f);
    boot(&f);
    ack(&f, 0xf5);
    key(&f, BM_KEY_A, 1);
    ack(&f, 0xf4);
    key(&f, BM_KEY_A, 0);
    advance(&f, 1000);
    assert(!f.count); /* No orphan break. */
    ack(&f, 0xed);
    key(&f, BM_KEY_B, 1);
    ack(&f, 0);
    key(&f, BM_KEY_B, 0);
    advance(&f, 1000);
    assert(!f.count);
    tap(&f, BM_KEY_A);
    EXPECT(&f, 0x1c, 0xf0, 0x1c);
    /* RESEND during an option keeps the option; a new command cancels it. */
    ack(&f, 0xed);
    command(&f, 0xfe);
    EXPECT(&f, 0xfa);
    ack(&f, 2);
    assert(state(&f).leds == 2 && !state(&f).parameter);
    command(&f, 0xf2);
    EXPECT(&f, 0xfa);
    command(&f, 0xee);
    EXPECT(&f, 0xee);
    advance(&f, 1000);
    assert(!f.count);
    done(&f);
}
static void
mappings(void)
{
    /* Independent printed scan-table expectations, grouped by named API keys.
     * FA gives all set3 keys make/break, tested separately from default types. */
    static const uint8_t letters[][3] = {
        { 0x1e, 0x1c, 0x1c },
        { 0x30, 0x32, 0x32 },
        { 0x2e, 0x21, 0x21 },
        { 0x20, 0x23, 0x23 },
        { 0x12, 0x24, 0x24 },
        { 0x21, 0x2b, 0x2b },
        { 0x22, 0x34, 0x34 },
        { 0x23, 0x33, 0x33 },
        { 0x17, 0x43, 0x43 },
        { 0x24, 0x3b, 0x3b },
        { 0x25, 0x42, 0x42 },
        { 0x26, 0x4b, 0x4b },
        { 0x32, 0x3a, 0x3a },
        { 0x31, 0x31, 0x31 },
        { 0x18, 0x44, 0x44 },
        { 0x19, 0x4d, 0x4d },
        { 0x10, 0x15, 0x15 },
        { 0x13, 0x2d, 0x2d },
        { 0x1f, 0x1b, 0x1b },
        { 0x14, 0x2c, 0x2c },
        { 0x16, 0x3c, 0x3c },
        { 0x2f, 0x2a, 0x2a },
        { 0x11, 0x1d, 0x1d },
        { 0x2d, 0x22, 0x22 },
        { 0x15, 0x35, 0x35 },
        { 0x2c, 0x1a, 0x1a }
    };
    static const uint8_t digits[]    = { 0x16, 0x1e, 0x26, 0x25, 0x2e, 0x36, 0x3d, 0x3e, 0x46, 0x45 };
    static const uint8_t functions[] = { 0x05, 0x06, 0x04, 0x0c, 0x03, 0x0b, 0x83, 0x0a, 0x01, 0x09 };
    static const struct {
        bm_key_code_t key;
        uint8_t       s1, s2, s3, extended;
    } other[] = {
        { BM_KEY_ENTER,            0x1c, 0x5a, 0x5a, 0 },
        { BM_KEY_ESCAPE,           0x01, 0x76, 0x08, 0 },
        { BM_KEY_BACKSPACE,        0x0e, 0x66, 0x66, 0 },
        { BM_KEY_TAB,              0x0f, 0x0d, 0x0d, 0 },
        { BM_KEY_SPACE,            0x39, 0x29, 0x29, 0 },
        { BM_KEY_MINUS,            0x0c, 0x4e, 0x4e, 0 },
        { BM_KEY_EQUAL,            0x0d, 0x55, 0x55, 0 },
        { BM_KEY_LEFT_BRACKET,     0x1a, 0x54, 0x54, 0 },
        { BM_KEY_RIGHT_BRACKET,    0x1b, 0x5b, 0x5b, 0 },
        { BM_KEY_BACKSLASH,        0x2b, 0x5d, 0x53, 0 },
        { BM_KEY_SEMICOLON,        0x27, 0x4c, 0x4c, 0 },
        { BM_KEY_APOSTROPHE,       0x28, 0x52, 0x52, 0 },
        { BM_KEY_GRAVE,            0x29, 0x0e, 0x0e, 0 },
        { BM_KEY_COMMA,            0x33, 0x41, 0x41, 0 },
        { BM_KEY_PERIOD,           0x34, 0x49, 0x49, 0 },
        { BM_KEY_SLASH,            0x35, 0x4a, 0x4a, 0 },
        { BM_KEY_CAPS_LOCK,        0x3a, 0x58, 0x14, 0 },
        { BM_KEY_SCROLL_LOCK,      0x46, 0x7e, 0x5f, 0 },
        { BM_KEY_INSERT,           0x52, 0x70, 0x67, 1 },
        { BM_KEY_HOME,             0x47, 0x6c, 0x6e, 1 },
        { BM_KEY_PAGE_UP,          0x49, 0x7d, 0x6f, 1 },
        { BM_KEY_DELETE,           0x53, 0x71, 0x64, 1 },
        { BM_KEY_END,              0x4f, 0x69, 0x65, 1 },
        { BM_KEY_PAGE_DOWN,        0x51, 0x7a, 0x6d, 1 },
        { BM_KEY_RIGHT,            0x4d, 0x74, 0x6a, 1 },
        { BM_KEY_LEFT,             0x4b, 0x6b, 0x61, 1 },
        { BM_KEY_DOWN,             0x50, 0x72, 0x60, 1 },
        { BM_KEY_UP,               0x48, 0x75, 0x63, 1 },
        { BM_KEY_NON_US_BACKSLASH, 0x56, 0x61, 0x13, 0 },
        { BM_KEY_LEFT_CONTROL,     0x1d, 0x14, 0x11, 0 },
        { BM_KEY_LEFT_SHIFT,       0x2a, 0x12, 0x12, 0 },
        { BM_KEY_LEFT_ALT,         0x38, 0x11, 0x19, 0 },
        { BM_KEY_RIGHT_CONTROL,    0x1d, 0x14, 0x58, 1 },
        { BM_KEY_RIGHT_SHIFT,      0x36, 0x59, 0x59, 0 },
        { BM_KEY_RIGHT_ALT,        0x38, 0x11, 0x39, 1 }
    };
    for (unsigned s = 1; s <= 3; ++s) {
        fixture_t f;
        start(&f);
        boot(&f);
        set(&f, s);
        ack(&f, 0xfa);
        for (unsigned i = 0; i < 81; ++i) {
            unsigned      scan, ext = 0, n = 0;
            uint8_t       bytes[6];
            bm_key_code_t k;
            if (i < 26) {
                k    = (bm_key_code_t) (BM_KEY_A + i);
                scan = letters[i][s - 1];
            } else if (i < 36) {
                k    = (bm_key_code_t) (BM_KEY_1 + i - 26);
                scan = s == 1 ? i - 24 : digits[i - 26];
            } else if (i < 46) {
                k    = (bm_key_code_t) (BM_KEY_F1 + i - 36);
                scan = s == 1 ? i + 0x17 : s == 2 ? functions[i - 36] : 7 + 8 * (i - 36);
            } else {
                unsigned j = i - 46;
                k          = other[j].key;
                scan       = s == 1 ? other[j].s1 : s == 2 ? other[j].s2 : other[j].s3;
                ext        = s != 3 && other[j].extended;
            }
            if (ext)
                bytes[n++] = 0xe0;
            bytes[n++] = (uint8_t) scan;
            if (ext)
                bytes[n++] = 0xe0;
            if (s != 1)
                bytes[n++] = 0xf0;
            bytes[n++] = (uint8_t) (scan | (s == 1 ? 0x80 : 0));
            tap(&f, k);
            expected(&f, bytes, n);
        }
        done(&f);
    }
    {
        fixture_t               f;
        bm_at_keyboard_config_t c;
        bm_host_services_t      h = bm_null_host_services();
        bm_input_event_t        e = event(BM_KEY_NON_US_BACKSLASH, 1);
        memset(&f, 0, sizeof(f));
        c            = config(&f);
        c.iso_layout = 0;
        assert(bm_at_keyboard_create(&h, &c, &f.k) == BM_STATUS_OK);
        boot(&f);
        set(&f, 3);
        tap(&f, BM_KEY_BACKSLASH);
        EXPECT(&f, 0x5c);
        assert(bm_at_keyboard_input(f.k, &e) == BM_STATUS_UNSUPPORTED);
        ack(&f, 0xfb);
        command(&f, 0x13);
        EXPECT(&f, 0xfe);
        command(&f, 0x53);
        EXPECT(&f, 0xfe);
        ack(&f, 0x5c);
        done(&f);
    }
}
static void
typematic(void)
{
    for (unsigned rate = 0; rate < 128; ++rate) {
        fixture_t f;
        /* Nonintegral native clock, independent integer oracle. */
        const uint64_t num = 3579545, den = 3, divisor = 3000000;
        uint64_t       delay_us  = 250000U * (1U + rate / 32U);
        uint64_t       period_us = (8U + rate % 8U) * (1U << ((rate / 8U) % 4U)) * 4170U;
        uint64_t       delay     = (delay_us * num + divisor - 1) / divisor;
        uint64_t       period    = (period_us * num + divisor - 1) / divisor;
        start_rate(&f, num, den);
        boot(&f);
        ack(&f, 0xf3);
        ack(&f, (uint8_t) rate);
        key(&f, BM_KEY_A, 1);
        assert(state(&f).repeat_remaining == delay);
        advance(&f, delay - 1);
        EXPECT(&f, 0x1c);
        advance(&f, 1);
        assert(!f.count && state(&f).repeat_remaining == period);
        advance(&f, 100);
        EXPECT(&f, 0x1c);
        advance(&f, period - 100);
        advance(&f, 100);
        EXPECT(&f, 0x1c);
        key(&f, BM_KEY_B, 1);
        assert(state(&f).repeat_remaining == delay);
        key(&f, BM_KEY_B, 0);
        assert(!state(&f).repeat_key); /* A stays down: no fallback. */
        advance(&f, 2 * delay);
        EXPECT(&f, 0x32, 0xf0, 0x32);
        done(&f);
    }
}
static void
failures(void)
{
    const bm_status_t failures[] = { BM_STATUS_INVALID_STATE, BM_STATUS_DEVICE_ERROR, BM_STATUS_UNSUPPORTED };
    for (unsigned i = 0; i < sizeof(failures) / sizeof(*failures); ++i)
        for (int after = 0; after < 2; ++after) {
            fixture_t f;
            uint64_t  n = 123;
            start(&f);
            boot(&f);
            f.result  = failures[i];
            f.after   = after;
            f.inspect = 1;
            key(&f, BM_KEY_A, 1);
            {
                uint64_t before = state(&f).cycles;
                assert(bm_at_keyboard_advance(f.k, 1000) == failures[i]);
                assert(state(&f).cycles == before + 100 && state(&f).scan_count == 1);
            }
            assert(f.count == (unsigned) after && state(&f).failure == failures[i]);
            assert(bm_at_keyboard_next_deadline(f.k, &n) == failures[i] && n == 123);
            assert(bm_at_keyboard_command(f.k, 0xff) == failures[i]);
            {
                unsigned calls = f.calls;
                assert(bm_at_keyboard_advance(f.k, 100) == failures[i] && f.calls == calls);
            }
            f.result = BM_STATUS_OK;
            f.count  = 0;
            assert(bm_at_keyboard_reset(f.k) == BM_STATUS_OK);
            boot(&f);
            done(&f);
        }
    for (unsigned i = 0; i < 2; ++i) {
        fixture_t f;
        start(&f);
        boot(&f);
        f.result = i ? BM_STATUS_CAPACITY_EXCEEDED : BM_STATUS_IDLE;
        key(&f, BM_KEY_A, 1);
        advance(&f, 500);
        assert(!f.count && state(&f).scan_count == 1 && state(&f).failure == BM_STATUS_OK);
        f.result  = BM_STATUS_OK;
        f.inspect = 1;
        advance(&f, 100);
        EXPECT(&f, 0x1c);
        done(&f);
    }
    /* Each transfer of both a command packet and a multi-byte scan packet. */
    for (unsigned packet = 0; packet < 2; ++packet)
        for (unsigned pos = 0; pos < (packet ? 10U : 3U); ++pos)
            for (int after = 0; after < 2; ++after) {
                fixture_t f;
                unsigned  pending;
                uint64_t  before;
                start(&f);
                boot(&f);
                if (packet) {
                    key(&f, BM_KEY_PRINT_SCREEN, 1);
                    key(&f, BM_KEY_PRINT_SCREEN, 0);
                } else
                    assert(bm_at_keyboard_command(f.k, 0xf2) == BM_STATUS_OK);
                advance(&f, 100U * pos);
                assert(f.count == pos);
                pending  = packet ? state(&f).scan_count : state(&f).response_count;
                before   = state(&f).cycles;
                f.result = BM_STATUS_DEVICE_ERROR;
                f.after  = after;
                assert(bm_at_keyboard_advance(f.k, 10000) == BM_STATUS_DEVICE_ERROR);
                assert(state(&f).cycles == before + 100 && f.count == pos + (unsigned) after);
                assert((packet ? state(&f).scan_count : state(&f).response_count) == pending);
                assert(bm_at_keyboard_advance(f.k, 1) == BM_STATUS_DEVICE_ERROR);
                done(&f);
            }
}
static void
validation(void)
{
    fixture_t                f, g;
    bm_at_keyboard_config_t  c;
    bm_host_services_t       h;
    failure_injection_host_t a;
    bm_input_event_t         e;
    memset(&f, 0, sizeof(f));
    failure_injection_host_initialize(&a);
    h = failure_injection_host_services(&a);
    c = config(&f);
    failure_injection_host_fail_after(&a, 0);
    assert(bm_at_keyboard_create(&h, &c, &f.k) == BM_STATUS_OUT_OF_MEMORY && !f.k
           && !a.outstanding_allocations);
    h = bm_null_host_services();
    for (unsigned i = 0; i < 8; ++i) {
        c = config(&f);
        switch (i) {
            case 0:
                c.send = NULL;
                break;
            case 1:
                c.power_on_cycles = 0;
                break;
            case 2:
                c.reset_accept_cycles = 0;
                break;
            case 3:
                c.bat_cycles = 0;
                break;
            case 4:
                c.byte_cycles = 0;
                break;
            case 5:
                c.iso_layout = 2;
                break;
            case 6:
                c.clock.cycles_per_second_numerator = 0;
                break;
            default:
                c.clock.cycles_per_second_denominator = 0;
                break;
        }
        assert(bm_at_keyboard_create(&h, &c, &f.k) == BM_STATUS_INVALID_ARGUMENT && !f.k);
    }
    start(&f);
    boot(&f);
    start(&g);
    boot(&g);
    e = event(BM_KEY_A, 2);
    assert(bm_at_keyboard_input(f.k, &e) == BM_STATUS_INVALID_ARGUMENT);
    e = event((bm_key_code_t) 255, 1);
    assert(bm_at_keyboard_input(f.k, &e) == BM_STATUS_UNSUPPORTED);
    e        = event(BM_KEY_A, 1);
    e.repeat = 1;
    assert(bm_at_keyboard_input(f.k, &e) == BM_STATUS_OK && !state(&f).scan_count);
    key(&f, BM_KEY_A, 1);
    key(&f, BM_KEY_A, 1);
    key(&g, BM_KEY_A, 1);
    advance(&f, 1234567);
    for (unsigned i = 0; i < 1234; ++i)
        advance(&g, 1000);
    advance(&g, 567);
    assert(f.count == g.count && !memcmp(f.bytes, g.bytes, f.count));
    assert(!memcmp(f.times, g.times, f.count * sizeof(*f.times)));
    {
        bm_at_keyboard_state_t x = state(&f), y = state(&g);
        assert(!memcmp(&x, &y, sizeof(x)));
    }
    f.count = 0;
    leds(&f, 4);
    assert(!state(&g).leds);
    done(&f);
    done(&g);
    start(&f);
    boot(&f);
    advance(&f, UINT64_MAX - state(&f).cycles);
    assert(bm_at_keyboard_advance(f.k, 1) == BM_STATUS_CAPACITY_EXCEEDED && state(&f).cycles == UINT64_MAX);
    done(&f);
}

static bm_bus_transaction_t
transaction(unsigned port, bm_bus_operation_t op, unsigned v)
{
    bm_bus_transaction_t t = { 0 };
    t.space                = BM_ADDRESS_IO;
    t.address              = port;
    t.operation            = op;
    t.size                 = 1;
    t.value                = v;
    return t;
}
static bm_status_t
line(void *p, int level)
{
    (void) p;
    (void) level;
    return BM_STATUS_OK;
}
static bm_status_t
inhibit(void *p, int level)
{
    return bm_at_keyboard_set_inhibit(((fixture_t *) p)->k, level);
}
static bm_status_t
controller_command(void *p, uint8_t v)
{
    return bm_at_keyboard_command(((fixture_t *) p)->k, v);
}
static bm_status_t
irq(void *p, int level)
{
    fixture_t *f = p;
    f->irq       = level;
    return bm_at_pic_set_irq(f->pic, 1, level);
}
static void
pic_write(fixture_t *f, unsigned port, unsigned value)
{
    bm_bus_transaction_t t = transaction(port, BM_BUS_WRITE, value);
    assert(bm_at_pic_io(f->pic, &t) == BM_STATUS_OK);
}
static void
board_advance(fixture_t *f, unsigned n)
{
    /* Test-only native scheduler; production clock attachment remains pending. */
    while (n--) {
        assert(bm_kbc8042_advance(f->controller, 1) == BM_STATUS_OK);
        advance(f, 1);
    }
}
static void
write_controller(fixture_t *f, unsigned port, unsigned v)
{
    bm_bus_transaction_t t = transaction(port, BM_BUS_WRITE, v);
    assert(bm_kbc8042_io(f->controller, &t) == BM_STATUS_OK);
    board_advance(f, 2);
}
static unsigned
read_controller(fixture_t *f)
{
    uint8_t              vector = 0;
    bm_at_pic_state_t    ps;
    bm_bus_transaction_t t = transaction(0x60, BM_BUS_READ, 0);
    assert(f->irq && bm_at_pic_state(f->pic, &ps) == BM_STATUS_OK && ps.intr);
    assert(bm_at_pic_acknowledge(f->pic, 0, &vector) == BM_STATUS_OK);
    assert(bm_at_pic_acknowledge(f->pic, 1, &vector) == BM_STATUS_OK && vector == 0x31);
    assert(bm_kbc8042_io(f->controller, &t) == BM_STATUS_OK && !f->irq);
    pic_write(f, 0x20, 0x20);
    return (unsigned) t.value;
}
static void
integration(void)
{
    fixture_t           f;
    bm_host_services_t  h  = bm_null_host_services();
    bm_kbc8042_config_t c  = { 0 };
    bm_at_pic_config_t  pc = { 0 };
    start(&f);
    pc.master_base  = 0x20;
    pc.slave_base   = 0xa0;
    pc.cascade_line = 2;
    assert(bm_at_pic_create(&h, &pc, &f.pic) == BM_STATUS_OK);
    pic_write(&f, 0x20, 0x11);
    pic_write(&f, 0xa0, 0x11);
    pic_write(&f, 0x21, 0x30);
    pic_write(&f, 0xa1, 0x70);
    pic_write(&f, 0x21, 4);
    pic_write(&f, 0xa1, 2);
    pic_write(&f, 0x21, 1);
    pic_write(&f, 0xa1, 1);
    pic_write(&f, 0x21, 0xfd);
    pic_write(&f, 0xa1, 0xff);
    c.data_port           = 0x60;
    c.command_port        = 0x64;
    c.irq                 = irq;
    c.a20                 = line;
    c.cpu_reset           = line;
    c.output_context      = &f;
    c.keyboard_command    = controller_command;
    c.keyboard_inhibit    = inhibit;
    c.keyboard_context    = &f;
    c.clock               = config(&f).clock;
    c.input_cycles        = 2;
    c.self_test_cycles    = 10;
    c.output_cycles       = 3;
    c.pulse_cycles        = 6;
    c.input_port          = 0xa0;
    c.initial_output_port = 0xc1;
    assert(bm_kbc8042_create(&h, &c, &f.controller) == BM_STATUS_OK);
    assert(bm_kbc8042_reset(f.controller) == BM_STATUS_OK && state(&f).inhibited);
    write_controller(&f, 0x64, 0x60);
    write_controller(&f, 0x60, 0x41);
    board_advance(&f, 450200);
    assert(read_controller(&f) == 0xaa);
    key(&f, BM_KEY_A, 1);
    key(&f, BM_KEY_A, 0);
    board_advance(&f, 200);
    assert(read_controller(&f) == 0x1e);
    board_advance(&f, 400);
    assert(read_controller(&f) == 0x9e);
    write_controller(&f, 0x60, 0xf2);
    board_advance(&f, 200);
    assert(read_controller(&f) == 0xfa);
    board_advance(&f, 200);
    assert(read_controller(&f) == 0xab);
    board_advance(&f, 200);
    assert(read_controller(&f) == 0x41); /* KBC translates ID83. */
    write_controller(&f, 0x60, 0xff);
    board_advance(&f, 1000);
    assert(state(&f).phase == BM_AT_KEYBOARD_RESET_ACCEPT && state(&f).phase_remaining == 500);
    assert(read_controller(&f) == 0xfa);
    board_advance(&f, 499);
    assert(state(&f).phase == BM_AT_KEYBOARD_RESET_ACCEPT);
    board_advance(&f, 1);
    assert(state(&f).phase == BM_AT_KEYBOARD_BAT);
    board_advance(&f, 300200);
    assert(read_controller(&f) == 0xaa);
    bm_kbc8042_destroy(f.controller);
    bm_at_pic_destroy(f.pic);
    done(&f);
}
int
main(void)
{
    lifecycle();
    commands();
    key_types();
    special_keys();
    fifo();
    suspended_input();
    mappings();
    typematic();
    failures();
    validation();
    integration();
    puts("AT enhanced keyboard: native protocol, three scan sets, 128 typematic settings, "
         "backpressure/failures and real KBC/PIC pass");
    return 0;
}
