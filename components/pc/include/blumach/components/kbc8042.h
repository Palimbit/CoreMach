/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Bounded behavioural AT controller; no MCU firmware or PCS286 identity claimed.
 */
#ifndef BLUMACH_COMPONENTS_KBC8042_H
#define BLUMACH_COMPONENTS_KBC8042_H
#include <blumach/components/at_bus.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct bm_kbc8042 bm_kbc8042_t;
typedef bm_status_t (*bm_at_keyboard_byte_fn)(void *context, uint8_t byte);
typedef bm_status_t (*bm_kbc8042_line_fn)(void *context, int asserted);
typedef enum bm_kbc8042_command_profile {
    BM_KBC8042_COMMANDS_AT = 0,
    /* Explicit classic compatibility: 80h reads the vendor P2 latch, 84h
     * writes it without driving lines; CFh consumes without a response.
     * Not an authentic Olivetti MCU model. */
    BM_KBC8042_COMMANDS_OLIVETTI_PCS286
} bm_kbc8042_command_profile_t;
typedef struct bm_kbc8042_config {
    uint16_t data_port, command_port;
    bm_kbc8042_line_fn irq;
    bm_kbc8042_line_fn a20;
    bm_kbc8042_line_fn cpu_reset;   /* asserted semantic, not raw active-low pin */
    void *output_context;
    bm_at_keyboard_byte_fn keyboard_command;
    /* Optional second-channel sink. A null sink models an electrically
     * present controller channel with no auxiliary device attached: D4/data
     * is consumed and no response is fabricated. */
    bm_at_keyboard_byte_fn auxiliary_command;
    void *auxiliary_context;
    bm_kbc8042_line_fn keyboard_inhibit;
    void *keyboard_context;
    /* Explicit behavioural timing profile, with native clock and sourced or
     * provisional delays. Never tie BAT to a BIOS instruction address. */
    bm_clock_rate_t clock;
    uint64_t input_cycles, self_test_cycles, output_cycles, pulse_cycles;
    uint8_t input_port; /* explicit board straps, bit7=1 (unlocked) required */
    uint8_t initial_output_port; /* bits7:6=11 (idle serial), bit0=1 required */
    bm_kbc8042_command_profile_t command_profile;
} bm_kbc8042_config_t;
typedef struct bm_kbc8042_state {
    uint64_t cycles, input_remaining, output_remaining, pulse_remaining;
    uint8_t status, command_byte, output_port, output_byte, parameter;
    uint8_t internal_ram[31]; /* Controller RAM cells 21h..3fh; 20h is command_byte. */
    int irq, a20, cpu_reset, inhibited, break_pending, auxiliary_enabled;
    bm_status_t failure;
    uint8_t olivetti_p2; /* Vendor readback latch, distinct from driven outputs. */
} bm_kbc8042_state_t;
/* Copies host/config, borrows contexts, no callbacks/maps on create. Required
 * fallible IRQ/A20/reset/inhibit outputs; optional keyboard command endpoint.
 * Initial CCB10h, empty buffers, IRQ0, inhibit1, explicit P2. Connect matching
 * recipients or reset to publish. No default timing: configured native delays
 * are functional policy, not PCS286 firmware measurements. CPU-only reset must
 * not reset the KBC. Reset clears work/failures, keeps lifetime native cycles.
 * Supported commands:20..3F/60..7F,A4,AA,AD/AE,C0,D0/D1,F0..FF. A4 returns
 * F1 (no password installed); password loading/enforcement is not modeled. The first
 * read/write RAM pair (20/60) addresses the command byte; all eight command-byte
 * bits are retained, while reserved bits7/1 have no modeled external effect.
 * The remaining 31 cells are independent retained bytes. The explicit
 * OLIVETTI_PCS286 profile adds A7/A8 to control the auxiliary byte channel;
 * D4 directs exactly the following data byte to
 * its optional sink. With no sink, that byte is consumed without a response,
 * modeling an absent auxiliary device rather than a successful one. Incoming
 * auxiliary bytes and IRQ12 are outside this boundary. AB/AC/E0 and other
 * vendor/PS2 commands reject; the same profile also adds 80/84/CF.
 * 80 returns a delayed raw P2 latch; 84 accepts any following byte without
 * driving IRQ/A20/reset/serial lines. Latch starts at initial_output_port;
 * D1 also records its accepted byte there, while D0 and pulses use actual
 * outputs. No classic polling P1 counter/serial-bit side effects are modeled.
 * CF has no reply. All follow normal IBF delay and replace unfinished parameters;
 * pending output, IRQ, translation and pulse state are not canceled.
 * AA tests native model state, never an authentic MCU ROM.
 * D1 bit7 must be1; low serial DATA drive is unsupported. Clock output bit6
 * can inhibit the byte link; AE/enabling CCB releases it. Bits5:4 reflect buffer
 * status, not writable latches; bits3:2 are unconnected latches. No serial pin
 * waveforms, parity/timeouts or locked-keyboard switch. PC mode bypasses byte
 * translation in the AT profile. The explicit PCS286 profile follows the
 * classic Olivetti policy: XLAT remains active when PC mode is also set. This
 * is functional compatibility, not qualification of the Mitsubishi mask ROM.
 * No guessed diagnostic reply or automatic ACK/RESEND.
 * First callback error retained until reset; partial effects survive without
 * replay. Line callback IDLE becomes INVALID_STATE. The keyboard and optional
 * auxiliary byte sinks may return IDLE before acceptance: this retains IBF
 * and retries after input_cycles.
 * Callbacks may inspect/DEBUG, never reenter a mutator, destroy or send inline.
 * Board latches CPU reset at an architectural boundary; callback cannot run CPU.
 * Full IBF and not-yet-published response collisions explicitly refuse without
 * mutation. A full OBF does not block an independent host-to-keyboard IBF byte;
 * the retained output remains readable and inhibits the keyboard-to-host reply
 * until drained. Empty OBF reads return the retained output latch, matching the classic
 * AT functional policy; no new byte, IRQ or pending-reply consumption.
 * DEBUG reads remain pure, even after a failure.
 * A keyboard-bound host byte is admitted while disabled, then enables the
 * interface at input consumption before calling keyboard_command (classic AT
 * policy). Controller parameters do not enable it. Receive remains inhibited
 * until then; no ACK is synthesized. A failed enable retains IBF and stops.
 */
bm_status_t bm_kbc8042_create(const bm_host_services_t *host,
                              const bm_kbc8042_config_t *config,
                              bm_kbc8042_t **out_kbc);
void bm_kbc8042_destroy(bm_kbc8042_t *kbc);
bm_status_t bm_kbc8042_reset(bm_kbc8042_t *kbc);
bm_status_t bm_kbc8042_io(void *context, bm_bus_transaction_t *transaction);
/* Bytes originate in a separate AT keyboard protocol device; the controller
 * owns command/status/translation and OBF/IBF, not host keys or keyboard LEDs.
 * Overflow is explicit and leaves the queue unchanged. A full interface
 * inhibits the keyboard link; do not lose/reorder bytes or fake an ACK. */
bm_status_t bm_kbc8042_receive_keyboard(bm_kbc8042_t *kbc, uint8_t byte);
bm_status_t bm_kbc8042_advance(bm_kbc8042_t *kbc, uint64_t cycles);
bm_status_t bm_kbc8042_next_deadline(const bm_kbc8042_t *kbc, uint64_t *cycles);
bm_status_t bm_kbc8042_state(const bm_kbc8042_t *kbc, bm_kbc8042_state_t *state);
bm_status_t bm_kbc8042_inspect(const bm_kbc8042_t *kbc,
                               uint8_t *status, uint8_t *command_byte);
#ifdef __cplusplus
}
#endif
#endif
