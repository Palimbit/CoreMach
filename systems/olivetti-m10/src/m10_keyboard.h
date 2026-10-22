/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_SYSTEMS_M10_KEYBOARD_H
#define BLUMACH_SYSTEMS_M10_KEYBOARD_H

#include <blumach/engine/input.h>
#include <blumach/engine/types.h>

#include <stdint.h>

/* M10 key matrix (service manual 6.8, Fig. 6-7, p. 6-10): nine rows
 * strobed low by 81C55 PA0-PA7 and PB0, eight returns KR0-KR7 with 33 kOhm
 * pull-ups read through a 40H244 at E0h-EFh. Every key has a diode, so
 * several pressed keys never create phantom contacts. A key's code is
 * row * 8 + return bit. */
#define M10_KEY_ROWS 9U
#define M10_KEY_CAPS_LOCK 70U

typedef struct m10_keyboard {
    uint8_t rows[M10_KEY_ROWS];     /* 1 = contact closed */
    int caps_lock;                  /* latched mechanically */
} m10_keyboard_t;

void m10_keyboard_reset(m10_keyboard_t *keyboard);

/* Applies a host key by physical position. Returns BM_STATUS_UNSUPPORTED
 * for keys without an M10 counterpart. */
bm_status_t m10_keyboard_input(m10_keyboard_t *keyboard,
                               const bm_input_event_t *event);

/* Return lines for the given row strobes (bit n of port A selects row n,
 * bit 0 of port B row 8; low selects). Pressed keys read 0. */
uint8_t m10_keyboard_scan(const m10_keyboard_t *keyboard, uint8_t port_a,
                          uint8_t port_b);

/* Matrix code for a host key, or -1. */
int m10_keyboard_code(bm_key_code_t key);

#endif
