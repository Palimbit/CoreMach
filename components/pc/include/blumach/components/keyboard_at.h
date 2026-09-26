/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Functional IBM enhanced keyboard byte protocol, independent of the KBC.
 */
#ifndef BLUMACH_COMPONENTS_KEYBOARD_AT_H
#define BLUMACH_COMPONENTS_KEYBOARD_AT_H
#include <blumach/components/kbc8042.h>
#include <blumach/engine/input.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct bm_at_keyboard bm_at_keyboard_t;
typedef struct bm_at_keyboard_config {
    bm_at_keyboard_byte_fn send;
    void                  *send_context;
    bm_clock_rate_t        clock;
    uint64_t               bat_cycles, byte_cycles;
    uint64_t               power_on_cycles, reset_accept_cycles;
    int                    iso_layout; /* 0:101-key ANSI; 1:102-key ISO. No PCS286 identity claim. */
} bm_at_keyboard_config_t;
typedef enum bm_at_keyboard_phase {
    BM_AT_KEYBOARD_POWER_ON,
    BM_AT_KEYBOARD_BAT,
    BM_AT_KEYBOARD_RESET_ACK,
    BM_AT_KEYBOARD_RESET_ACCEPT,
    BM_AT_KEYBOARD_READY
} bm_at_keyboard_phase_t;
typedef struct bm_at_keyboard_state {
    uint64_t               cycles, phase_remaining, transmit_remaining, repeat_remaining;
    bm_at_keyboard_phase_t phase;
    bm_status_t            failure;
    uint16_t               repeat_key;
    uint8_t                scan_set, parameter, typematic, leds, last_byte;
    unsigned               scan_count, response_count;
    int                    enabled, inhibited, overrun, last_valid;
} bm_at_keyboard_state_t;
/* Explicit IBM6183355 enhanced profile: three scan sets, ID AB83, key-type
 * commands, LEDs, typematic, 16-byte scan FIFO and independent replies. Not the
 * 84-key protocol or proof of Olivetti keyboard identity. Host key identifiers
 * are the existing named bm_key_code values; unmapped keys reject.
 * Native durations mandatory; power-on then BAT, no constructor callbacks.
 * Reset FF sends ACK first, waits an uninterrupted uninhibited acceptance
 * interval, then BAT. Native BAT validates model state, not an MCU ROM.
 * All periods use integer ceil of the IBM nominal microsecond formula to native
 * clocks; not measured matrix/serial timing. Host repeat events are ignored.
 * IDLE/CAPACITY_EXCEEDED from send mean no byte accepted and preserve it;
 * other failures stick until reset. Never translate a host error to guest FE.
 * Callbacks cannot reenter mutators/destroy, except set_inhibit may feed back
 * the controller's line DURING send, without recursive execution. This allows
 * direct KBC wiring. Input while scanning is suspended updates physical levels
 * without scans; resumption does not invent makes for already-held keys.
 * Normal set2 Pause is unsupported pending reconciliation of the 1986 printed
 * sequence with the inherited sequence; Ctrl+Pause and set1/set3 are supported.
 */
bm_status_t bm_at_keyboard_create(const bm_host_services_t *host, const bm_at_keyboard_config_t *config,
                                  bm_at_keyboard_t **out_keyboard);
void        bm_at_keyboard_destroy(bm_at_keyboard_t *keyboard);
bm_status_t bm_at_keyboard_reset(bm_at_keyboard_t *keyboard);
bm_status_t bm_at_keyboard_command(bm_at_keyboard_t *keyboard, uint8_t command);
bm_status_t bm_at_keyboard_input(bm_at_keyboard_t *keyboard, const bm_input_event_t *event);
bm_status_t bm_at_keyboard_leds(const bm_at_keyboard_t *keyboard, bm_keyboard_led_state_t *state);
bm_status_t bm_at_keyboard_set_inhibit(bm_at_keyboard_t *keyboard, int level);
bm_status_t bm_at_keyboard_advance(bm_at_keyboard_t *keyboard, uint64_t cycles);
bm_status_t bm_at_keyboard_next_deadline(const bm_at_keyboard_t *keyboard, uint64_t *cycles);
bm_status_t bm_at_keyboard_state(const bm_at_keyboard_t *keyboard, bm_at_keyboard_state_t *state);
/* ACK, RESEND, BAT, scan enable, make/break, set selection, typematic and
 * guest LED commands belong here. Controller translation remains in 8042.
 * A rejected delivery retains the byte; callbacks cannot recurse into advance.
 * No host layout, host repeats, file I/O or host lock-state dependency. */
#ifdef __cplusplus
}
#endif
#endif
