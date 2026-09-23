/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Draft interface. Register semantics require board/firmware evidence.
 */
#ifndef BLUMACH_COMPONENTS_OLIVETTI_IOC02_H
#define BLUMACH_COMPONENTS_OLIVETTI_IOC02_H
#include <blumach/components/bus.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct bm_ioc02 bm_ioc02_t;
typedef struct bm_ioc02_state {
    uint8_t select, data, control;
} bm_ioc02_state_t;
typedef void (*bm_ioc02_outputs_fn)(void *context, const bm_ioc02_state_t *state);
typedef struct bm_ioc02_config {
    bm_ioc02_outputs_fn outputs;
    void *output_context;
} bm_ioc02_config_t;
bm_status_t bm_ioc02_create(const bm_host_services_t *host,
                            const bm_ioc02_config_t *config, bm_ioc02_t **out_ioc);
void bm_ioc02_destroy(bm_ioc02_t *ioc);
void bm_ioc02_reset(bm_ioc02_t *ioc);
/* Inherited decode: 68h, 6Ah, 6Ch. Not every bit is understood. Implementers
 * must document selector-dependent outputs, diagnostic readback and reset
 * values before wiring them to board devices. No first-read override, PC/IP
 * check, ROM patch or permanent memory alias is authorized by this contract.
 * Query is observational; DEBUG I/O never changes selection or latch state. */
bm_status_t bm_ioc02_io(void *context, bm_bus_transaction_t *transaction);
bm_status_t bm_ioc02_state(const bm_ioc02_t *ioc, bm_ioc02_state_t *out_state);
#ifdef __cplusplus
}
#endif
#endif
