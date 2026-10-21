/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_COMPONENTS_RTC_UPD1990AC_H
#define BLUMACH_COMPONENTS_RTC_UPD1990AC_H

#include <stdint.h>
#include <blumach/engine/host.h>

#ifdef __cplusplus
extern "C" {
#endif

/* NEC uPD1990AC serial I/O calendar and clock, one chip per instance.
 * Primary source: NEC 1983 "Integrated Circuits for Consumer Use",
 * uPD1990AC, book pages 792-799 (bitsavers PDF pages 797-804). The owner
 * drives the pins and advances time in periods of the 32.768 kHz crystal;
 * the chip never reads the host clock.
 *
 * Not modelled: TEST MODE counting (command 111 is latched and reported
 * but time keeps counting normally) and the analogue timing limits. */

#define BM_UPD1990AC_OSC_HZ 32768U

/* Calendar values in binary. The chip keeps no year. weekday is 0-6; the
 * data book does not name day 0. */
typedef struct bm_upd1990ac_time {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;       /* 24-hour form */
    uint8_t day;        /* 1-31 */
    uint8_t weekday;    /* 0-6 */
    uint8_t month;      /* 1-12 */
} bm_upd1990ac_time_t;

typedef enum bm_upd1990ac_command {
    BM_UPD1990AC_REGISTER_HOLD = 0,
    BM_UPD1990AC_REGISTER_SHIFT = 1,
    BM_UPD1990AC_TIME_SET = 2,
    BM_UPD1990AC_TIME_READ = 3,
    BM_UPD1990AC_TP_64HZ = 4,
    BM_UPD1990AC_TP_256HZ = 5,
    BM_UPD1990AC_TP_2048HZ = 6,
    BM_UPD1990AC_TEST_MODE = 7
} bm_upd1990ac_command_t;

/* Called whenever the TP square wave changes level (1 = released high). */
typedef void (*bm_upd1990ac_tp_fn)(void *context, int level);

typedef struct bm_upd1990ac_config {
    bm_upd1990ac_time_t initial_time;
    bm_upd1990ac_tp_fn tp_changed;
    void *tp_context;
} bm_upd1990ac_config_t;

typedef struct bm_upd1990ac bm_upd1990ac_t;

bm_status_t bm_upd1990ac_create(const bm_host_services_t *host,
                                const bm_upd1990ac_config_t *config,
                                bm_upd1990ac_t **out_chip);
void bm_upd1990ac_destroy(bm_upd1990ac_t *chip);

/* Input pins. C0-C2 are bits 0-2 of command; STB latches them on its
 * rising edge and CLK shifts the 40-bit register on its rising edge, both
 * only while CS is high (book page 795). */
void bm_upd1990ac_set_command_pins(bm_upd1990ac_t *chip, uint8_t c2_c1_c0);
void bm_upd1990ac_set_data_in(bm_upd1990ac_t *chip, int level);
void bm_upd1990ac_set_clk(bm_upd1990ac_t *chip, int level);
void bm_upd1990ac_set_stb(bm_upd1990ac_t *chip, int level);
void bm_upd1990ac_set_cs(bm_upd1990ac_t *chip, int level);
void bm_upd1990ac_set_output_enable(bm_upd1990ac_t *chip, int level);

/* DATA OUT and TP are open drain. Returns 1 when the pin is released
 * (high through the board pull-up, including high impedance) and 0 when
 * the chip pulls it low. */
int bm_upd1990ac_data_out(const bm_upd1990ac_t *chip);
int bm_upd1990ac_tp(const bm_upd1990ac_t *chip);

/* Advance by whole 32.768 kHz oscillator periods. */
void bm_upd1990ac_advance(bm_upd1990ac_t *chip, uint64_t oscillator_cycles);

void bm_upd1990ac_get_time(const bm_upd1990ac_t *chip,
                           bm_upd1990ac_time_t *out_time);
bm_upd1990ac_command_t bm_upd1990ac_mode(const bm_upd1990ac_t *chip);
/* TP frequency in Hz selected by group-1 commands (64, 256 or 2048). */
unsigned int bm_upd1990ac_tp_hz(const bm_upd1990ac_t *chip);

#ifdef __cplusplus
}
#endif

#endif
