/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Functional 8254-compatible timer; not a PCS286 discrete-chip identity claim.
 */
#ifndef BLUMACH_COMPONENTS_PIT8254_H
#define BLUMACH_COMPONENTS_PIT8254_H
#include <blumach/components/at_bus.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct bm_pit8254 bm_pit8254_t;
typedef void (*bm_pit8254_output_fn)(void *context, unsigned int channel, int level);
typedef struct bm_pit8254_config {
    uint16_t io_base;
    bm_pit8254_output_fn output;
    void *output_context;
} bm_pit8254_config_t;
/* Owns one allocation, copies config, borrows callback context. Construction
 * publishes no mapping/output. Owner connects ports and synchronizes clocks.
 * Reset is deterministic model policy: unprogrammed/NULL, OUT low, gates1/1/0;
 * real power-up values are undefined. Reset reports actual changed OUT levels.
 * Synchronous callbacks may inspect output/deadline/DEBUG only; mutation returns
 * INVALID_STATE, reset/destroy during callback are ignored. No recursive CPU.
 * No callback indicates a guest fault or requests automatic retry. */
bm_status_t bm_pit8254_create(const bm_host_services_t *host,
                              const bm_pit8254_config_t *config,
                              bm_pit8254_t **out_pit);
void bm_pit8254_destroy(bm_pit8254_t *pit);
void bm_pit8254_reset(bm_pit8254_t *pit);
/* Native byte I/O only; control reads UNMAPPED, reserved read-back D0 and
 * invalid BCD/count1 in modes2/3 UNSUPPORTED before the offending byte changes
 * state. Prior LSB effects remain. Mode must be programmed before count writes.
 * Reads use status-first/count latches without forcing pending CE loads or
 * inverting a pending LSB. DEBUG peeks without consuming status/count/phase.
 * DEBUG writes rejected. Wait count preserved, not a zero-wait timing claim.
 * Undefined pre-load count retains previous CE as deterministic model policy. */
bm_status_t bm_pit8254_io(void *context, bm_bus_transaction_t *transaction);
bm_status_t bm_pit8254_set_gate(bm_pit8254_t *pit, unsigned int channel, int level);
bm_status_t bm_pit8254_output(const bm_pit8254_t *pit, unsigned int channel, int *level);
/* Advance in complete input CLK pulses; gates represent sampled rising-edge
 * requests, not analog setup/hold. Sum since reset must fit uint64_t; overflow
 * rejects before effects. Preserves every OUT transition, even for large steps;
 * runtime scales with observable transitions. Quiet intervals are batched.
 * No host clock, IRQ, refresh, speaker or board scheduler is implicitly owned. */
bm_status_t bm_pit8254_advance(bm_pit8254_t *pit, uint64_t cycles);
/* Pure output deadline: OK => positive pulses until any OUT changes; IDLE =>
 * zero, no clock-driven OUT change. Owner still advances elapsed pulses before
 * I/O when idle: counters/NULL can change without an output edge. */
bm_status_t bm_pit8254_next_deadline(const bm_pit8254_t *pit, uint64_t *cycles);
#ifdef __cplusplus
}
#endif
#endif
