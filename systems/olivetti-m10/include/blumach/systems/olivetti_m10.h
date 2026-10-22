/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_SYSTEMS_OLIVETTI_M10_H
#define BLUMACH_SYSTEMS_OLIVETTI_M10_H

#include <blumach/components/rtc_upd1990ac.h>
#include <blumach/runtime/runtime.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BM_M10_FIRMWARE_SIZE 32768U
#define BM_M10_CONFIG_TYPE "blumach.system.olivetti-m10.config"
#define BM_M10_CONFIG_VERSION 1U

/* Persistent states, saved with bm_session_save_persistent_state():
 * "ram" is the battery-backed RAM image of 8000h-FFFFh (bytes below the
 * installed size are ignored on load and saved as zero); "rtc" is the
 * uPD1990AC with the wall-clock second at which it was saved. */
#define BM_M10_RAM_STATE_SIZE 32768U
#define BM_M10_RTC_STATE_SIZE 40U

/* Language jumpers J1-J4 as read on 81C55 PC5:PC4 (service manual 6.16.3,
 * Table 6-7). */
typedef enum bm_m10_language {
    BM_M10_LANGUAGE_ENGLISH = 0,
    BM_M10_LANGUAGE_GERMAN = 1,
    BM_M10_LANGUAGE_ITALIAN = 2,
    BM_M10_LANGUAGE_FRENCH = 3
} bm_m10_language_t;

/* Frontend-owned wall clock in seconds since 1970-01-01 UTC. The machine
 * only uses differences between two readings: the clock keeps running
 * while the machine is switched off, as the battery-backed chip does. */
typedef int64_t (*bm_m10_wall_clock_fn)(void *context);

/* Experimental Olivetti M10 (1983): OKI MSM80C85A at 2.4576 MHz, 32 KiB
 * ROM, 8-32 KiB battery-backed RAM, ten HD44102CH LCD drivers, uPD1990AC
 * clock, key matrix, language jumpers and buzzer. Cassette, RS-232C UART,
 * printer, bar-code reader and modem are not modelled. Host audio output is
 * not available yet: the buzzer is observable through inspection. */
typedef struct bm_m10_config {
    bm_blob_view_t firmware;
    uint32_t ram_kib;                   /* 8, 16, 24 or 32 */
    bm_m10_language_t language;
    /* Optional caller-owned persistent states (sizes above). Without a
     * valid RTC state the chip starts at rtc_initial_time. */
    const uint8_t *ram_state;
    size_t ram_state_size;
    const uint8_t *rtc_state;
    size_t rtc_state_size;
    bm_upd1990ac_time_t rtc_initial_time;
    bm_m10_wall_clock_fn wall_clock;    /* optional */
    void *wall_clock_context;
} bm_m10_config_t;

const bm_machine_definition_t *bm_m10_machine_definition(void);
bm_machine_config_t bm_m10_machine_config(const bm_m10_config_t *configuration);

#ifdef __cplusplus
}
#endif

#endif
