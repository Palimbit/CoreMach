/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_COMPONENTS_MSM81C55_H
#define BLUMACH_COMPONENTS_MSM81C55_H

#include <stdint.h>
#include <blumach/engine/host.h>

#ifdef __cplusplus
extern "C" {
#endif

/* OKI MSM81C55 I/O ports and 14-bit timer, one chip per instance.
 * Primary source: OKI 1986 Microprocessor Data Book, MSM81C55RS/GS, book
 * pages 102-114. Register addresses are AD2-AD0 of an I/O cycle; the owner
 * decodes chip enable and IO/M. Time is counted in TIMER IN cycles supplied
 * by the owner; the chip never reads a host clock.
 *
 * Not modelled: the 256-byte RAM, the strobed ALT3/ALT4 port C modes and
 * their interrupt and buffer-full bits, and the count-by-twos internals of
 * the counter (reads return the remaining whole TIMER IN cycles). */

enum {
    BM_MSM81C55_PORT_A = 0,
    BM_MSM81C55_PORT_B = 1,
    BM_MSM81C55_PORT_C = 2
};

/* Register addresses (AD2-AD0). */
enum {
    BM_MSM81C55_COMMAND_STATUS = 0,
    BM_MSM81C55_REGISTER_A = 1,
    BM_MSM81C55_REGISTER_B = 2,
    BM_MSM81C55_REGISTER_C = 3,
    BM_MSM81C55_TIMER_LOW = 4,
    BM_MSM81C55_TIMER_HIGH = 5
};

/* Timer output modes, bits 15-14 of the count length register. */
typedef enum bm_msm81c55_timer_mode {
    BM_MSM81C55_SINGLE_SQUARE = 0,
    BM_MSM81C55_CONTINUOUS_SQUARE = 1,
    BM_MSM81C55_SINGLE_PULSE = 2,
    BM_MSM81C55_CONTINUOUS_PULSE = 3
} bm_msm81c55_timer_mode_t;

#define BM_MSM81C55_STATUS_TIMER 0x40U

/* Pin levels seen on input lines of a port; bits in output mode are
 * ignored. Port C uses bits 0-5; bits 6-7 are returned unchanged and let
 * the owner choose what the bus carries there. */
typedef uint8_t (*bm_msm81c55_input_fn)(void *context, unsigned int port);
/* Called after the latch or the direction of a port changes. */
typedef void (*bm_msm81c55_port_fn)(void *context, unsigned int port);

typedef struct bm_msm81c55_config {
    bm_msm81c55_input_fn read_pins;
    bm_msm81c55_port_fn port_changed;
    void *context;
} bm_msm81c55_config_t;

typedef struct bm_msm81c55 bm_msm81c55_t;

bm_status_t bm_msm81c55_create(const bm_host_services_t *host,
                               const bm_msm81c55_config_t *config,
                               bm_msm81c55_t **out_chip);
void bm_msm81c55_destroy(bm_msm81c55_t *chip);

/* RESET: command register cleared (all ports input), output latches
 * cleared, timer stopped and its flag cleared (page 110). The count is not
 * reinitialised (page 113, note 2). */
void bm_msm81c55_reset(bm_msm81c55_t *chip, uint64_t now);

uint8_t bm_msm81c55_read(bm_msm81c55_t *chip, uint8_t address, uint64_t now);
void bm_msm81c55_write(bm_msm81c55_t *chip, uint8_t address, uint8_t value,
                       uint64_t now);

/* Levels on a port's pins: output latch on output bits, input_levels on the
 * others (the board's pull-ups or drivers). */
uint8_t bm_msm81c55_port_levels(const bm_msm81c55_t *chip, unsigned int port,
                                uint8_t input_levels);
uint8_t bm_msm81c55_command(const bm_msm81c55_t *chip);

/* TIMER OUT level at now. A timer that is not running leaves it high
 * (assumption: the data book shows both waveforms starting high). */
int bm_msm81c55_timer_out(bm_msm81c55_t *chip, uint64_t now);
/* Count length of a running continuous mode, 0 otherwise. */
uint16_t bm_msm81c55_timer_period(bm_msm81c55_t *chip, uint64_t now,
                                  bm_msm81c55_timer_mode_t *out_mode);

#ifdef __cplusplus
}
#endif

#endif
