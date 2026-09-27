/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Functional 32.768-kHz MC146818A register model with AT index/NMI transport.
 * An explicitly selected 128-byte extension does not identify PCS286 silicon.
 */
#ifndef BLUMACH_COMPONENTS_RTC_AT_H
#define BLUMACH_COMPONENTS_RTC_AT_H
#include <blumach/components/at_bus.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct bm_at_rtc bm_at_rtc_t;
#define BM_AT_RTC_CMOS_BYTES 128U
typedef bm_status_t (*bm_at_rtc_line_fn)(void *context, int level);
typedef enum bm_at_rtc_divider_policy {
    BM_AT_RTC_DIVIDER_QUALIFIED = 0,
    /* Classic AT approximation: retain every DV encoding, run only DV010.
     * Other values stop/clear divider phase and UIP until DV010 is written.
     * Does not emulate alternate oscillator frequencies or factory-test modes. */
    BM_AT_RTC_DIVIDER_CLASSIC_STOP
} bm_at_rtc_divider_policy_t;
typedef struct bm_at_rtc_config {
    uint16_t io_base;
    size_t cmos_size; /* 64 (MC146818A) or 128 (unqualified AT extension) */
    const uint8_t *initial_cmos; /* copied; cmos_size bytes or NULL/depleted */
    size_t initial_cmos_size;
    int battery_valid; /* initial VRT latch, not a simulated battery voltage */
    bm_at_rtc_line_fn irq; /* required; semantic asserted = 1 */
    bm_at_rtc_line_fn nmi_mask; /* required; bit7 = 1 blocks NMI */
    void *output_context;
    bm_at_rtc_divider_policy_t divider_policy;
} bm_at_rtc_config_t;
typedef struct bm_at_rtc_state {
    uint64_t cycles;
    uint32_t divider_phase;
    uint8_t index;
    int nmi_mask, irq, uip, updating;
    bm_status_t failure;
} bm_at_rtc_state_t;
/* No maps or callbacks during create. Initial outputs are IRQ0/mask1; owner
 * connects matching receivers or calls reset. Initial A/B are retained except
 * read-only UIP, pending C, B interrupt/SQW enables and D (config VRT). NULL
 * creates zero CMOS with SET and divider-reset asserted, VRT0: no invented date.
 * Host wall time/files/century/checksum/vendor BIOS policies are never consulted.
 * Default QUALIFIED supports DV010 (32.768 kHz), DV110/111 (reset); other DV
 * reject. Explicit CLASSIC_STOP accepts them with the approximation above.
 * DSE still rejects with UNSUPPORTED. SQWE is stored; SQW is unconnected.
 * The phase origin is a deterministic divider-release policy, not measured PCS
 * timing. Invalid calendar encodings and accesses during the actual update
 * window are undefined hardware cases, reported UNSUPPORTED rather than repaired.
 * Single owner. Outputs may inspect with DEBUG/state/export, never reenter a
 * mutator or destroy. First output failure is retained until reset; accepted
 * effects survive, caller read results stay unchanged on failure, no retry or
 * host-to-guest exception. IDLE from an output becomes INVALID_STATE. */
bm_status_t bm_at_rtc_create(const bm_host_services_t *host,
                             const bm_at_rtc_config_t *config,
                             bm_at_rtc_t **out_rtc);
void bm_at_rtc_destroy(bm_at_rtc_t *rtc);
/* Warm board reset preserves battery-backed bytes/calendar. Cold power and
 * depleted battery are explicit configuration, never host time at reset. */
bm_status_t bm_at_rtc_reset(bm_at_rtc_t *rtc);
bm_status_t bm_at_rtc_io(void *context, bm_bus_transaction_t *transaction);
/* Native oscillator edges at 32768 Hz. Divider release starts an update after
 * 16384 edges, preceded by UIP for 8 edges; completion follows 65 edges later.
 * Subsequent cycles repeat every 32768 edges. SET aborts update, not divider or
 * periodic events. Advance consumes a prefix through a failing event; inspect
 * cycles afterwards. Invalid calendar completion clears UIP, leaves calendar
 * and UF unchanged and returns UNSUPPORTED; software can SET/reprogram it.
 * Warm reset preserves divider phase/UIP, time and RAM; clears C and B enables,
 * resets index/mask (board policy), and resynchronizes both outputs. CPU-only
 * reset must not call it. Destroy after disconnecting recipients/maps. */
bm_status_t bm_at_rtc_advance(bm_at_rtc_t *rtc, uint64_t cycles);
bm_status_t bm_at_rtc_next_deadline(const bm_at_rtc_t *rtc, uint64_t *cycles);
bm_status_t bm_at_rtc_state(const bm_at_rtc_t *rtc, bm_at_rtc_state_t *state);
/* Pure raw-register snapshot, including current A/C/D; not a phase/save-state.
 * Size must match configured capacity. Guest read C clears all flags, read D
 * returns prior VRT then sets it (operating PS high); DEBUG does neither.
 * Index port is write-only for guests, inspectable with DEBUG. */
bm_status_t bm_at_rtc_export_cmos(const bm_at_rtc_t *rtc,
                                  uint8_t *bytes, size_t size);
#ifdef __cplusplus
}
#endif
#endif
