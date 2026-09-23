/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Draft 8254-compatible timer register contract, not a discrete-chip claim.
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
bm_status_t bm_pit8254_create(const bm_host_services_t *host,
                              const bm_pit8254_config_t *config,
                              bm_pit8254_t **out_pit);
void bm_pit8254_destroy(bm_pit8254_t *pit);
void bm_pit8254_reset(bm_pit8254_t *pit);
bm_status_t bm_pit8254_io(void *context, bm_bus_transaction_t *transaction);
bm_status_t bm_pit8254_set_gate(bm_pit8254_t *pit, unsigned int channel, int level);
bm_status_t bm_pit8254_output(const bm_pit8254_t *pit, unsigned int channel, int *level);
/* Advance in input-clock edges; adapters synchronize before I/O. Status/count
 * read-back, null-count and latch semantics must be added when reusing the
 * existing 8253 implementation; renaming that model is not sufficient. */
bm_status_t bm_pit8254_advance(bm_pit8254_t *pit, uint64_t cycles);
/* OK => strictly positive native cycles; IDLE => zero, no pending transition. */
bm_status_t bm_pit8254_next_deadline(const bm_pit8254_t *pit, uint64_t *cycles);
#ifdef __cplusplus
}
#endif
#endif
