/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2016-2023 Miran Grca <mgrca8@gmail.com>
 * Copyright 2017-2023 Fred N. van Kempen <decwiz@yahoo.com>
 * Copyright 2026 rtzor, BluMach contributors
 * Derived from src/device/keyboard_at.c and the portable PCS86 key map.
 * IBM6183355 (1986) governs commands/key modes; no machine/BIOS hacks.
 */
#include "keyboard_at_private.h"
#include <string.h>

enum { REPEAT = 1, BREAK = 2, INVALID_KEY = 255 };
typedef struct key_code {
    uint8_t set1, set2, set3, extended;
} key_code_t;
static const key_code_t key_codes[256] = {
    [BM_KEY_ESCAPE]           = { 0x01, 0x76, 0x08, 0 },
    [BM_KEY_1]                = { 0x02, 0x16, 0x16, 0 },
    [BM_KEY_2]                = { 0x03, 0x1e, 0x1e, 0 },
    [BM_KEY_3]                = { 0x04, 0x26, 0x26, 0 },
    [BM_KEY_4]                = { 0x05, 0x25, 0x25, 0 },
    [BM_KEY_5]                = { 0x06, 0x2e, 0x2e, 0 },
    [BM_KEY_6]                = { 0x07, 0x36, 0x36, 0 },
    [BM_KEY_7]                = { 0x08, 0x3d, 0x3d, 0 },
    [BM_KEY_8]                = { 0x09, 0x3e, 0x3e, 0 },
    [BM_KEY_9]                = { 0x0a, 0x46, 0x46, 0 },
    [BM_KEY_0]                = { 0x0b, 0x45, 0x45, 0 },
    [BM_KEY_MINUS]            = { 0x0c, 0x4e, 0x4e, 0 },
    [BM_KEY_EQUAL]            = { 0x0d, 0x55, 0x55, 0 },
    [BM_KEY_BACKSPACE]        = { 0x0e, 0x66, 0x66, 0 },
    [BM_KEY_TAB]              = { 0x0f, 0x0d, 0x0d, 0 },
    [BM_KEY_Q]                = { 0x10, 0x15, 0x15, 0 },
    [BM_KEY_W]                = { 0x11, 0x1d, 0x1d, 0 },
    [BM_KEY_E]                = { 0x12, 0x24, 0x24, 0 },
    [BM_KEY_R]                = { 0x13, 0x2d, 0x2d, 0 },
    [BM_KEY_T]                = { 0x14, 0x2c, 0x2c, 0 },
    [BM_KEY_Y]                = { 0x15, 0x35, 0x35, 0 },
    [BM_KEY_U]                = { 0x16, 0x3c, 0x3c, 0 },
    [BM_KEY_I]                = { 0x17, 0x43, 0x43, 0 },
    [BM_KEY_O]                = { 0x18, 0x44, 0x44, 0 },
    [BM_KEY_P]                = { 0x19, 0x4d, 0x4d, 0 },
    [BM_KEY_LEFT_BRACKET]     = { 0x1a, 0x54, 0x54, 0 },
    [BM_KEY_RIGHT_BRACKET]    = { 0x1b, 0x5b, 0x5b, 0 },
    [BM_KEY_ENTER]            = { 0x1c, 0x5a, 0x5a, 0 },
    [BM_KEY_LEFT_CONTROL]     = { 0x1d, 0x14, 0x11, 0 },
    [BM_KEY_A]                = { 0x1e, 0x1c, 0x1c, 0 },
    [BM_KEY_S]                = { 0x1f, 0x1b, 0x1b, 0 },
    [BM_KEY_D]                = { 0x20, 0x23, 0x23, 0 },
    [BM_KEY_F]                = { 0x21, 0x2b, 0x2b, 0 },
    [BM_KEY_G]                = { 0x22, 0x34, 0x34, 0 },
    [BM_KEY_H]                = { 0x23, 0x33, 0x33, 0 },
    [BM_KEY_J]                = { 0x24, 0x3b, 0x3b, 0 },
    [BM_KEY_K]                = { 0x25, 0x42, 0x42, 0 },
    [BM_KEY_L]                = { 0x26, 0x4b, 0x4b, 0 },
    [BM_KEY_SEMICOLON]        = { 0x27, 0x4c, 0x4c, 0 },
    [BM_KEY_APOSTROPHE]       = { 0x28, 0x52, 0x52, 0 },
    [BM_KEY_GRAVE]            = { 0x29, 0x0e, 0x0e, 0 },
    [BM_KEY_LEFT_SHIFT]       = { 0x2a, 0x12, 0x12, 0 },
    [BM_KEY_BACKSLASH]        = { 0x2b, 0x5d, 0x5c, 0 },
    [BM_KEY_Z]                = { 0x2c, 0x1a, 0x1a, 0 },
    [BM_KEY_X]                = { 0x2d, 0x22, 0x22, 0 },
    [BM_KEY_C]                = { 0x2e, 0x21, 0x21, 0 },
    [BM_KEY_V]                = { 0x2f, 0x2a, 0x2a, 0 },
    [BM_KEY_B]                = { 0x30, 0x32, 0x32, 0 },
    [BM_KEY_N]                = { 0x31, 0x31, 0x31, 0 },
    [BM_KEY_M]                = { 0x32, 0x3a, 0x3a, 0 },
    [BM_KEY_COMMA]            = { 0x33, 0x41, 0x41, 0 },
    [BM_KEY_PERIOD]           = { 0x34, 0x49, 0x49, 0 },
    [BM_KEY_SLASH]            = { 0x35, 0x4a, 0x4a, 0 },
    [BM_KEY_RIGHT_SHIFT]      = { 0x36, 0x59, 0x59, 0 },
    [BM_KEY_LEFT_ALT]         = { 0x38, 0x11, 0x19, 0 },
    [BM_KEY_SPACE]            = { 0x39, 0x29, 0x29, 0 },
    [BM_KEY_CAPS_LOCK]        = { 0x3a, 0x58, 0x14, 0 },
    [BM_KEY_F1]               = { 0x3b, 0x05, 0x07, 0 },
    [BM_KEY_F2]               = { 0x3c, 0x06, 0x0f, 0 },
    [BM_KEY_F3]               = { 0x3d, 0x04, 0x17, 0 },
    [BM_KEY_F4]               = { 0x3e, 0x0c, 0x1f, 0 },
    [BM_KEY_F5]               = { 0x3f, 0x03, 0x27, 0 },
    [BM_KEY_F6]               = { 0x40, 0x0b, 0x2f, 0 },
    [BM_KEY_F7]               = { 0x41, 0x83, 0x37, 0 },
    [BM_KEY_F8]               = { 0x42, 0x0a, 0x3f, 0 },
    [BM_KEY_F9]               = { 0x43, 0x01, 0x47, 0 },
    [BM_KEY_F10]              = { 0x44, 0x09, 0x4f, 0 },
    [BM_KEY_SCROLL_LOCK]      = { 0x46, 0x7e, 0x5f, 0 },
    [BM_KEY_NON_US_BACKSLASH] = { 0x56, 0x61, 0x13, 0 },
    [BM_KEY_HOME]             = { 0x47, 0x6c, 0x6e, 1 },
    [BM_KEY_UP]               = { 0x48, 0x75, 0x63, 1 },
    [BM_KEY_PAGE_UP]          = { 0x49, 0x7d, 0x6f, 1 },
    [BM_KEY_LEFT]             = { 0x4b, 0x6b, 0x61, 1 },
    [BM_KEY_RIGHT]            = { 0x4d, 0x74, 0x6a, 1 },
    [BM_KEY_END]              = { 0x4f, 0x69, 0x65, 1 },
    [BM_KEY_DOWN]             = { 0x50, 0x72, 0x60, 1 },
    [BM_KEY_PAGE_DOWN]        = { 0x51, 0x7a, 0x6d, 1 },
    [BM_KEY_INSERT]           = { 0x52, 0x70, 0x67, 1 },
    [BM_KEY_DELETE]           = { 0x53, 0x71, 0x64, 1 },
    [BM_KEY_RIGHT_CONTROL]    = { 0x1d, 0x14, 0x58, 1 },
    [BM_KEY_RIGHT_ALT]        = { 0x38, 0x11, 0x39, 1 },
    [BM_KEY_PRINT_SCREEN]     = { 0x37, 0x7c, 0x57, 1 },
    [BM_KEY_PAUSE]            = { 0x45, 0x77, 0x62, 0 },
};

static const uint8_t default_codes[][2] = {
    { 0x07, 0 },
    { 0x08, 0 },
    { 0x0d, 1 },
    { 0x0e, 1 },
    { 0x0f, 0 },
    { 0x11, 2 },
    { 0x12, 2 },
    { 0x13, 1 },
    { 0x14, 2 },
    { 0x15, 1 },
    { 0x16, 1 },
    { 0x17, 0 },
    { 0x19, 2 },
    { 0x1a, 1 },
    { 0x1b, 1 },
    { 0x1c, 1 },
    { 0x1d, 1 },
    { 0x1e, 1 },
    { 0x1f, 0 },
    { 0x21, 1 },
    { 0x22, 1 },
    { 0x23, 1 },
    { 0x24, 1 },
    { 0x25, 1 },
    { 0x26, 1 },
    { 0x27, 0 },
    { 0x29, 1 },
    { 0x2a, 1 },
    { 0x2b, 1 },
    { 0x2c, 1 },
    { 0x2d, 1 },
    { 0x2e, 1 },
    { 0x2f, 0 },
    { 0x31, 1 },
    { 0x32, 1 },
    { 0x33, 1 },
    { 0x34, 1 },
    { 0x35, 1 },
    { 0x36, 1 },
    { 0x37, 0 },
    { 0x39, 0 },
    { 0x3a, 1 },
    { 0x3b, 1 },
    { 0x3c, 1 },
    { 0x3d, 1 },
    { 0x3e, 1 },
    { 0x3f, 0 },
    { 0x41, 1 },
    { 0x42, 1 },
    { 0x43, 1 },
    { 0x44, 1 },
    { 0x45, 1 },
    { 0x46, 1 },
    { 0x47, 0 },
    { 0x49, 1 },
    { 0x4a, 1 },
    { 0x4b, 1 },
    { 0x4c, 1 },
    { 0x4d, 1 },
    { 0x4e, 1 },
    { 0x4f, 0 },
    { 0x52, 1 },
    { 0x53, 1 },
    { 0x54, 1 },
    { 0x55, 1 },
    { 0x56, 0 },
    { 0x57, 0 },
    { 0x58, 0 },
    { 0x59, 2 },
    { 0x5a, 1 },
    { 0x5b, 1 },
    { 0x5c, 1 },
    { 0x5e, 0 },
    { 0x5f, 0 },
    { 0x60, 1 },
    { 0x61, 1 },
    { 0x62, 0 },
    { 0x63, 1 },
    { 0x64, 1 },
    { 0x65, 0 },
    { 0x66, 1 },
    { 0x67, 0 },
    { 0x69, 0 },
    { 0x6a, 1 },
    { 0x6b, 0 },
    { 0x6c, 0 },
    { 0x6d, 0 },
    { 0x6e, 0 },
    { 0x6f, 0 },
    { 0x70, 0 },
    { 0x71, 0 },
    { 0x72, 0 },
    { 0x73, 0 },
    { 0x74, 0 },
    { 0x75, 0 },
    { 0x76, 0 },
    { 0x77, 0 },
    { 0x79, 0 },
    { 0x7a, 0 },
    { 0x7c, 1 },
    { 0x7d, 0 },
    { 0x7e, 0 },
    { 0x84, 0 },
};

enum { NO_ACTION, RESET_ACK_SENT, BAT_SENT, ID_SENT };

static uint64_t
gcd(uint64_t a, uint64_t b)
{
    while (b) {
        uint64_t n = a % b;
        a          = b;
        b          = n;
    }
    return a;
}
static bm_status_t
native_time(bm_clock_rate_t rate, uint64_t us, uint64_t *out)
{
    uint64_t a = rate.cycles_per_second_numerator, b = us;
    uint64_t c = rate.cycles_per_second_denominator, d = 1000000, g;
    g = gcd(a, c);
    a /= g;
    c /= g;
    g = gcd(a, d);
    a /= g;
    d /= g;
    g = gcd(b, c);
    b /= g;
    c /= g;
    g = gcd(b, d);
    b /= g;
    d /= g;
    if (a > UINT64_MAX / b || c > UINT64_MAX / d)
        return BM_STATUS_CAPACITY_EXCEEDED;
    a *= b;
    c *= d;
    *out = a / c + (a % c != 0);
    return BM_STATUS_OK;
}
static int
scanning(const bm_at_keyboard_t *k)
{
    return k->s.enabled && k->s.phase == BM_AT_KEYBOARD_READY && !k->s.parameter && !k->identifying;
}
static int
output_pending(const bm_at_keyboard_t *k)
{
    return k->resend_pending || k->s.response_count || (scanning(k) && (k->s.scan_count || k->s.overrun));
}
static void
arm(bm_at_keyboard_t *k)
{
    if (!output_pending(k))
        k->s.transmit_remaining = 0;
    else if (!k->s.transmit_remaining)
        k->s.transmit_remaining = k->config.byte_cycles;
}
static void
stop_repeat(bm_at_keyboard_t *k)
{
    k->s.repeat_key       = 0;
    k->s.repeat_remaining = 0;
}
static void
clear_scans(bm_at_keyboard_t *k)
{
    k->s.scan_count = 0;
    k->s.overrun    = 0;
    stop_repeat(k);
}
static void
defaults(bm_at_keyboard_t *k)
{
    k->s.scan_set  = 2;
    k->s.typematic = 0x2b;
    k->s.parameter = 0;
    memset(k->modes, INVALID_KEY, sizeof(k->modes));
    for (unsigned i = 0; i < sizeof(default_codes) / sizeof(*default_codes); ++i)
        k->modes[default_codes[i][0]] = default_codes[i][1];
    if (k->config.iso_layout)
        k->modes[0x5c] = INVALID_KEY;
    else
        k->modes[0x13] = k->modes[0x53] = INVALID_KEY;
    clear_scans(k);
}
static void
initial(bm_at_keyboard_t *k)
{
    uint64_t cycles    = k->s.cycles;
    int      inhibited = k->s.inhibited;
    memset(&k->s, 0, sizeof(k->s));
    k->s.cycles    = cycles;
    k->s.inhibited = inhibited;
    memset(k->down, 0, sizeof(k->down));
    memset(k->scanned, 0, sizeof(k->scanned));
    defaults(k);
    k->s.phase           = BM_AT_KEYBOARD_POWER_ON;
    k->s.phase_remaining = k->config.power_on_cycles;
    k->identifying = k->resend_pending = 0;
}
bm_status_t
bm_at_keyboard_create(const bm_host_services_t *host, const bm_at_keyboard_config_t *c,
                      bm_at_keyboard_t **out)
{
    bm_at_keyboard_t *k;
    uint64_t          delays[4], periods[32];
    bm_status_t       r;
    if (out)
        *out = NULL;
    if (!out || !c || bm_host_services_validate(host) != BM_STATUS_OK || !c->send
        || !c->clock.cycles_per_second_numerator || !c->clock.cycles_per_second_denominator || !c->bat_cycles
        || !c->byte_cycles || !c->power_on_cycles || !c->reset_accept_cycles
        || (c->iso_layout != 0 && c->iso_layout != 1))
        return BM_STATUS_INVALID_ARGUMENT;
    for (unsigned i = 0; i < 4; ++i) {
        r = native_time(c->clock, (i + 1U) * 250000U, &delays[i]);
        if (r != BM_STATUS_OK)
            return r;
    }
    for (unsigned i = 0; i < 32; ++i) {
        r = native_time(c->clock, (8U + (i & 7U)) * (1U << (i >> 3)) * 4170U, &periods[i]);
        if (r != BM_STATUS_OK)
            return r;
    }
    k = host->allocate(host->context, sizeof(*k));
    if (!k)
        return BM_STATUS_OUT_OF_MEMORY;
    memset(k, 0, sizeof(*k));
    k->host   = *host;
    k->config = *c;
    memcpy(k->delays, delays, sizeof(delays));
    memcpy(k->periods, periods, sizeof(periods));
    initial(k);
    *out = k;
    return BM_STATUS_OK;
}
void
bm_at_keyboard_destroy(bm_at_keyboard_t *k)
{
    if (k && !k->busy && !k->clock_link)
        k->host.release(k->host.context, k);
}
bm_status_t
bm_at_keyboard_reset(bm_at_keyboard_t *k)
{
    if (!k)
        return BM_STATUS_INVALID_ARGUMENT;
    if (k->busy)
        return BM_STATUS_INVALID_STATE;
    initial(k);
    return BM_STATUS_OK;
}
static void
response(bm_at_keyboard_t *k, uint8_t byte, uint8_t action)
{
    k->replies[0]           = byte;
    k->actions[0]           = action;
    k->s.response_count     = 1;
    k->s.transmit_remaining = k->config.byte_cycles;
}
static uint8_t
set3(const bm_at_keyboard_t *k, unsigned key)
{
    return key == BM_KEY_BACKSLASH && k->config.iso_layout ? 0x53 : key_codes[key].set3;
}
static unsigned
key_mode(const bm_at_keyboard_t *k, unsigned key)
{
    if (k->s.scan_set == 3)
        return k->modes[set3(k, key)];
    return key == BM_KEY_PAUSE ? 0U : REPEAT | BREAK;
}
bm_status_t
bm_at_keyboard_command(bm_at_keyboard_t *k, uint8_t v)
{
    uint8_t parameter;
    if (!k)
        return BM_STATUS_INVALID_ARGUMENT;
    if (k->busy)
        return BM_STATUS_INVALID_STATE;
    if (k->s.failure != BM_STATUS_OK)
        return k->s.failure;
    if (k->s.phase == BM_AT_KEYBOARD_POWER_ON || k->s.phase == BM_AT_KEYBOARD_BAT)
        return BM_STATUS_IDLE;
    /* RESEND preserves the outstanding response/parameter; never cache FE. */
    if (v == 0xfe) {
        if (!k->s.last_valid)
            return BM_STATUS_UNSUPPORTED;
        k->resend_pending       = 1;
        k->resend_byte          = k->s.last_byte;
        k->s.transmit_remaining = k->config.byte_cycles;
        return BM_STATUS_OK;
    }
    parameter = k->s.parameter;
    if (parameter && v < 0xed) {
        int valid = parameter == 0xed ? v < 8
            : parameter == 0xf3       ? v < 128
            : parameter == 0xf0       ? v <= 3
                                      : k->modes[v] != INVALID_KEY;
        if (!valid) {
            response(k, 0xfe, NO_ACTION);
            return BM_STATUS_OK;
        }
        response(k, 0xfa, NO_ACTION);
        if (parameter == 0xed)
            k->s.leds = v;
        else if (parameter == 0xf3) {
            k->s.typematic = v;
            stop_repeat(k);
        } else if (parameter == 0xf0) {
            if (v)
                k->s.scan_set = v;
            else {
                k->replies[1]       = k->s.scan_set;
                k->actions[1]       = NO_ACTION;
                k->s.response_count = 2;
            }
        } else {
            k->modes[v] = parameter == 0xfb ? REPEAT : parameter == 0xfc ? BREAK : 0;
            return BM_STATUS_OK; /* Multiple key identifications until a command. */
        }
        k->s.parameter = 0;
        return BM_STATUS_OK;
    }
    /* A newly accepted command interrupts an unfinished option/reply sequence.
     * At this byte boundary no partially clocked byte is replayed. */
    k->s.parameter       = 0;
    k->s.response_count  = 0;
    k->resend_pending    = 0;
    k->identifying       = 0;
    k->s.phase           = BM_AT_KEYBOARD_READY;
    k->s.phase_remaining = 0;
    switch (v) {
        case 0xed:
        case 0xf0:
        case 0xf3:
        case 0xfb:
        case 0xfc:
        case 0xfd:
            k->s.parameter = v;
            if (v == 0xf0 || v >= 0xfb)
                clear_scans(k);
            response(k, 0xfa, NO_ACTION);
            break;
        case 0xee:
            response(k, 0xee, NO_ACTION);
            break;
        case 0xf2:
            response(k, 0xfa, NO_ACTION);
            k->replies[1]       = 0xab;
            k->replies[2]       = 0x83;
            k->actions[1]       = NO_ACTION;
            k->actions[2]       = ID_SENT;
            k->s.response_count = 3;
            k->identifying      = 1;
            break;
        case 0xf4:
            clear_scans(k);
            k->s.enabled = 1;
            response(k, 0xfa, NO_ACTION);
            break;
        case 0xf5:
        case 0xf6:
            defaults(k);
            k->s.enabled = v == 0xf6;
            response(k, 0xfa, NO_ACTION);
            break;
        case 0xf7:
        case 0xf8:
        case 0xf9:
        case 0xfa:
            clear_scans(k);
            for (unsigned i = 0; i < 256; ++i)
                if (k->modes[i] != INVALID_KEY)
                    k->modes[i] = v == 0xf7 ? REPEAT : v == 0xf8 ? BREAK : v == 0xf9 ? 0 : REPEAT | BREAK;
            response(k, 0xfa, NO_ACTION);
            break;
        case 0xff:
            k->s.enabled = 0;
            stop_repeat(k);
            k->s.phase = BM_AT_KEYBOARD_RESET_ACK;
            response(k, 0xfa, RESET_ACK_SENT);
            break;
        default:
            response(k, 0xfe, NO_ACTION);
            break;
    }
    return BM_STATUS_OK;
}
static void
code(uint8_t *bytes, unsigned *n, unsigned set, uint8_t scan, int extended, int pressed)
{
    if (extended)
        bytes[(*n)++] = 0xe0;
    if (!pressed && set != 1)
        bytes[(*n)++] = 0xf0;
    bytes[(*n)++] = (uint8_t) (scan | (!pressed && set == 1 ? 0x80U : 0U));
}
static bm_status_t
key_packet(const bm_at_keyboard_t *k, unsigned key, int pressed, uint8_t *bytes, unsigned *count)
{
    unsigned set = k->s.scan_set, n = 0;
    int      left = k->down[BM_KEY_LEFT_SHIFT], right = k->down[BM_KEY_RIGHT_SHIFT];
    int      shift = left || right, ctrl = k->down[BM_KEY_LEFT_CONTROL] || k->down[BM_KEY_RIGHT_CONTROL];
    int      alt        = k->down[BM_KEY_LEFT_ALT] || k->down[BM_KEY_RIGHT_ALT];
    const key_code_t *c = &key_codes[key];
    if (set == 3) {
        if (pressed || (key_mode(k, key) & BREAK))
            code(bytes, &n, set, set3(k, key), 0, pressed);
    } else if (key == BM_KEY_PAUSE) {
        if (pressed) {
            if (ctrl) {
                code(bytes, &n, set, set == 1 ? 0x46 : 0x7e, 1, 1);
                code(bytes, &n, set, set == 1 ? 0x46 : 0x7e, 1, 0);
            } else if (set == 1) {
                static const uint8_t pause[] = { 0xe1, 0x1d, 0x45, 0xe1, 0x9d, 0xc5 };
                memcpy(bytes, pause, sizeof(pause));
                n = sizeof(pause);
            } else
                return BM_STATUS_UNSUPPORTED; /* Printed 1986 sequence discrepancy. */
        }
    } else if (key == BM_KEY_PRINT_SCREEN) {
        if (alt)
            code(bytes, &n, set, set == 1 ? 0x54 : 0x84, 0, pressed);
        else {
            if (pressed && !shift && !ctrl)
                code(bytes, &n, set, set == 1 ? 0x2a : 0x12, 1, 1);
            code(bytes, &n, set, set == 1 ? c->set1 : c->set2, 1, pressed);
            if (!pressed && !shift && !ctrl)
                code(bytes, &n, set, set == 1 ? 0x2a : 0x12, 1, 0);
        }
    } else {
        int nav = c->extended && key != BM_KEY_RIGHT_CONTROL && key != BM_KEY_RIGHT_ALT;
        int num = (k->s.leds & BM_KEYBOARD_LED_NUM_LOCK) != 0;
        if (nav && pressed) {
            if (num && !shift)
                code(bytes, &n, set, set == 1 ? 0x2a : 0x12, 1, 1);
            if (!num && left)
                code(bytes, &n, set, set == 1 ? 0x2a : 0x12, 1, 0);
            if (!num && right)
                code(bytes, &n, set, set == 1 ? 0x36 : 0x59, 1, 0);
        }
        code(bytes, &n, set, set == 1 ? c->set1 : c->set2, c->extended, pressed);
        if (nav && !pressed) {
            if (num && !shift)
                code(bytes, &n, set, set == 1 ? 0x2a : 0x12, 1, 0);
            if (!num && left)
                code(bytes, &n, set, set == 1 ? 0x2a : 0x12, 1, 1);
            if (!num && right)
                code(bytes, &n, set, set == 1 ? 0x36 : 0x59, 1, 1);
        }
    }
    *count = n;
    return BM_STATUS_OK;
}
static void
queue_scan(bm_at_keyboard_t *k, const uint8_t *bytes, unsigned n)
{
    if (k->s.overrun) {
        stop_repeat(k);
        return;
    }
    if (n > 16U - k->s.scan_count) {
        k->s.overrun = 1;
        stop_repeat(k);
    } else {
        memcpy(k->scans + k->s.scan_count, bytes, n);
        k->s.scan_count += n;
    }
    arm(k);
}
bm_status_t
bm_at_keyboard_input(bm_at_keyboard_t *k, const bm_input_event_t *e)
{
    uint8_t     bytes[12];
    unsigned    count = 0, key;
    bm_status_t r;
    if (!k || !e)
        return BM_STATUS_INVALID_ARGUMENT;
    if (e->kind != BM_INPUT_KEY)
        return BM_STATUS_UNSUPPORTED;
    if ((e->pressed != 0 && e->pressed != 1) || (e->repeat != 0 && e->repeat != 1))
        return BM_STATUS_INVALID_ARGUMENT;
    key = (unsigned) e->key;
    if (key >= 256 || !key_codes[key].set1 || (key == BM_KEY_NON_US_BACKSLASH && !k->config.iso_layout))
        return BM_STATUS_UNSUPPORTED;
    if (k->busy)
        return BM_STATUS_INVALID_STATE;
    if (k->s.failure != BM_STATUS_OK)
        return k->s.failure;
    if (e->repeat || k->down[key] == e->pressed)
        return BM_STATUS_OK;
    if (scanning(k) && (e->pressed || k->scanned[key])) {
        r = key_packet(k, key, e->pressed, bytes, &count);
        if (r != BM_STATUS_OK)
            return r;
    }
    k->down[key] = (uint8_t) e->pressed;
    if (!e->pressed && k->s.repeat_key == key)
        stop_repeat(k);
    if (scanning(k) && (e->pressed || k->scanned[key])) {
        if (e->pressed) {
            stop_repeat(k);
            if (key_mode(k, key) & REPEAT) {
                k->s.repeat_key       = (uint16_t) key;
                k->s.repeat_remaining = k->delays[k->s.typematic >> 5];
            }
        }
        queue_scan(k, bytes, count);
        if (e->pressed)
            k->scanned[key] = !k->s.overrun;
    }
    if (!e->pressed)
        k->scanned[key] = 0;
    return BM_STATUS_OK;
}
bm_status_t
bm_at_keyboard_set_inhibit(bm_at_keyboard_t *k, int level)
{
    if (!k || (level != 0 && level != 1))
        return BM_STATUS_INVALID_ARGUMENT;
    if (k->busy && !k->sending)
        return BM_STATUS_INVALID_STATE;
    if (k->s.failure != BM_STATUS_OK)
        return k->s.failure;
    if (level != k->s.inhibited) {
        k->s.inhibited = level;
        if (k->s.phase == BM_AT_KEYBOARD_RESET_ACCEPT)
            k->s.phase_remaining = k->config.reset_accept_cycles;
        if (!level) {
            if (output_pending(k))
                k->s.transmit_remaining = k->config.byte_cycles;
            if (k->s.repeat_key)
                k->s.repeat_remaining = k->delays[k->s.typematic >> 5];
        }
    }
    return BM_STATUS_OK;
}
static bm_status_t
transmit(bm_at_keyboard_t *k)
{
    bm_status_t r;
    uint8_t     byte, action = NO_ACTION;
    int         resend = k->resend_pending, response_byte = !resend && k->s.response_count;
    if (resend)
        byte = k->resend_byte;
    else if (response_byte) {
        byte   = k->replies[0];
        action = k->actions[0];
    } else if (k->s.scan_count)
        byte = k->scans[0];
    else
        byte = k->s.scan_set == 1 ? 0xff : 0x00;
    k->sending = 1;
    r          = k->config.send(k->config.send_context, byte);
    k->sending = 0;
    if (r == BM_STATUS_IDLE || r == BM_STATUS_CAPACITY_EXCEEDED) {
        k->s.transmit_remaining = k->config.byte_cycles;
        return BM_STATUS_OK;
    }
    if (r != BM_STATUS_OK) {
        k->s.failure = r;
        return r;
    }
    if (byte != 0xfe) {
        k->s.last_byte  = byte;
        k->s.last_valid = 1;
    }
    if (resend)
        k->resend_pending = 0;
    else if (response_byte) {
        --k->s.response_count;
        memmove(k->replies, k->replies + 1, k->s.response_count);
        memmove(k->actions, k->actions + 1, k->s.response_count);
    } else if (k->s.scan_count) {
        --k->s.scan_count;
        memmove(k->scans, k->scans + 1, k->s.scan_count);
    } else
        k->s.overrun = 0;
    if (action == RESET_ACK_SENT) {
        k->s.phase           = BM_AT_KEYBOARD_RESET_ACCEPT;
        k->s.phase_remaining = k->config.reset_accept_cycles;
    } else if (action == BAT_SENT) {
        k->s.phase   = BM_AT_KEYBOARD_READY;
        k->s.enabled = 1;
    } else if (action == ID_SENT)
        k->identifying = 0;
    k->s.transmit_remaining = 0;
    arm(k);
    return BM_STATUS_OK;
}
static int
phase_running(const bm_at_keyboard_t *k)
{
    return k->s.phase_remaining && (k->s.phase != BM_AT_KEYBOARD_RESET_ACCEPT || !k->s.inhibited);
}
static int
repeat_running(const bm_at_keyboard_t *k)
{
    return !k->s.inhibited && scanning(k) && k->s.repeat_key && k->s.repeat_remaining;
}
static uint64_t
deadline(const bm_at_keyboard_t *k)
{
    uint64_t n = phase_running(k) ? k->s.phase_remaining : 0, v;
    v          = !k->s.inhibited && output_pending(k) ? k->s.transmit_remaining : 0;
    if (v && (!n || v < n))
        n = v;
    v = repeat_running(k) ? k->s.repeat_remaining : 0;
    if (v && (!n || v < n))
        n = v;
    return n;
}
void
bm_at_keyboard_elapse(bm_at_keyboard_t *k, uint64_t step, bm_keyboard_edge_t *edge)
{
    edge->phase = phase_running(k);
    edge->tx = !k->s.inhibited && output_pending(k);
    edge->repeat = repeat_running(k);
    edge->old_phase = k->s.phase;
    if (edge->phase) k->s.phase_remaining -= step;
    if (edge->tx) k->s.transmit_remaining -= step;
    if (edge->repeat) k->s.repeat_remaining -= step;
    k->s.cycles += step;
}
bm_status_t
bm_at_keyboard_settle(bm_at_keyboard_t *k, const bm_keyboard_edge_t *edge)
{
    bm_status_t r = BM_STATUS_OK;
    if (edge->phase && k->s.phase == edge->old_phase && !k->s.phase_remaining) {
        if (k->s.phase == BM_AT_KEYBOARD_BAT) {
            k->s.leds  = 0;
            k->s.phase = BM_AT_KEYBOARD_READY;
            response(k, 0xaa, BAT_SENT);
        } else {
            defaults(k);
            k->s.leds            = 7;
            k->s.phase           = BM_AT_KEYBOARD_BAT;
            k->s.phase_remaining = k->config.bat_cycles;
        }
    }
    if (edge->tx && !k->s.inhibited && output_pending(k) && !k->s.transmit_remaining)
        r = transmit(k);
    if (r == BM_STATUS_OK && edge->repeat && !k->s.repeat_remaining && k->s.repeat_key) {
        uint8_t  bytes[12];
        unsigned count = 0;
        if (!k->s.inhibited && scanning(k)) {
            r = key_packet(k, k->s.repeat_key, 1, bytes, &count);
            if (r == BM_STATUS_OK)
                queue_scan(k, bytes, count);
            else
                k->s.failure = r;
        }
        if (k->s.repeat_key)
            k->s.repeat_remaining = k->periods[k->s.typematic & 31U];
    }
    return r;
}
bm_status_t
bm_at_keyboard_advance(bm_at_keyboard_t *k, uint64_t cycles)
{
    bm_status_t r = BM_STATUS_OK;
    if (!k) return BM_STATUS_INVALID_ARGUMENT;
    if (k->busy) return BM_STATUS_INVALID_STATE;
    if (k->s.failure != BM_STATUS_OK) return k->s.failure;
    if (cycles > UINT64_MAX - k->s.cycles) return BM_STATUS_CAPACITY_EXCEEDED;
    k->busy = 1;
    while (cycles && r == BM_STATUS_OK) {
        uint64_t n = deadline(k), step = n && n < cycles ? n : cycles;
        bm_keyboard_edge_t edge;
        bm_at_keyboard_elapse(k, step, &edge); cycles -= step;
        r = bm_at_keyboard_settle(k, &edge);
    }
    k->busy = 0;
    return r;
}
bm_status_t
bm_at_keyboard_next_deadline(const bm_at_keyboard_t *k, uint64_t *cycles)
{
    if (!k || !cycles)
        return BM_STATUS_INVALID_ARGUMENT;
    if (k->s.failure != BM_STATUS_OK)
        return k->s.failure;
    *cycles = deadline(k);
    return *cycles ? BM_STATUS_OK : BM_STATUS_IDLE;
}
bm_status_t
bm_at_keyboard_state(const bm_at_keyboard_t *k, bm_at_keyboard_state_t *s)
{
    if (!k || !s)
        return BM_STATUS_INVALID_ARGUMENT;
    *s = k->s;
    return BM_STATUS_OK;
}
bm_status_t
bm_at_keyboard_leds(const bm_at_keyboard_t *k, bm_keyboard_led_state_t *s)
{
    if (!k || !s)
        return BM_STATUS_INVALID_ARGUMENT;
    s->indicators = k->s.leds;
    return BM_STATUS_OK;
}
