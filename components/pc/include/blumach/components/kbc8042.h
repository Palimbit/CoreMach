/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Draft behavioural AT controller/keyboard boundary. No MCU firmware claimed.
 */
#ifndef BLUMACH_COMPONENTS_KBC8042_H
#define BLUMACH_COMPONENTS_KBC8042_H
#include <blumach/components/at_bus.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct bm_kbc8042 bm_kbc8042_t;
typedef bm_status_t (*bm_at_keyboard_byte_fn)(void *context, uint8_t byte);
typedef struct bm_kbc8042_config {
    uint16_t data_port, command_port;
    bm_at_line_fn irq;
    bm_at_line_fn a20;
    bm_at_line_fn cpu_reset;   /* asserted semantic, not raw active-low pin */
    void *output_context;
    bm_at_keyboard_byte_fn keyboard_command;
    bm_at_line_fn keyboard_inhibit;
    void *keyboard_context;
    /* Explicit behavioural timing profile, with native clock and sourced or
     * provisional delays. Never tie BAT to a BIOS instruction address. */
    bm_clock_rate_t clock;
    uint64_t input_cycles, self_test_cycles, output_cycles;
} bm_kbc8042_config_t;
bm_status_t bm_kbc8042_create(const bm_host_services_t *host,
                              const bm_kbc8042_config_t *config,
                              bm_kbc8042_t **out_kbc);
void bm_kbc8042_destroy(bm_kbc8042_t *kbc);
void bm_kbc8042_reset(bm_kbc8042_t *kbc);
bm_status_t bm_kbc8042_io(void *context, bm_bus_transaction_t *transaction);
/* Bytes originate in a separate AT keyboard protocol device; the controller
 * owns command/status/translation and OBF/IBF, not host keys or keyboard LEDs.
 * Overflow is explicit and leaves the queue unchanged. A full interface
 * inhibits the keyboard link; do not lose/reorder bytes or fake an ACK. */
bm_status_t bm_kbc8042_receive_keyboard(bm_kbc8042_t *kbc, uint8_t byte);
bm_status_t bm_kbc8042_advance(bm_kbc8042_t *kbc, uint64_t cycles);
bm_status_t bm_kbc8042_next_deadline(const bm_kbc8042_t *kbc, uint64_t *cycles);
bm_status_t bm_kbc8042_inspect(const bm_kbc8042_t *kbc,
                               uint8_t *status, uint8_t *command_byte);
#ifdef __cplusplus
}
#endif
#endif
