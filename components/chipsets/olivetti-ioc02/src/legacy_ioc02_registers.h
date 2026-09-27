/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Private migration boundary, not a qualified PCS286 IOC02 profile or ABI.
 */
#ifndef BM_LEGACY_IOC02_REGISTERS_H
#define BM_LEGACY_IOC02_REGISTERS_H
#include <blumach/components/bus.h>

typedef struct bm_ioc02_legacy_registers {
    uint8_t select, data, control;
} bm_ioc02_legacy_registers_t;

enum bm_ioc02_legacy_latch {
    BM_IOC02_LEGACY_SELECT = 1U,
    BM_IOC02_LEGACY_DATA = 2U,
    BM_IOC02_LEGACY_CONTROL = 4U
};
typedef struct bm_ioc02_legacy_effect {
    uint8_t written; /* Latch enabled by this write, including same-value writes. */
    uint8_t changed; /* Subset of written whose stored value actually changed. */
} bm_ioc02_legacy_effect_t;

/* Caller-owned, isolated state; no allocation, callbacks, CPU or memory map.
 * Reapply classic initialization (04/04/FF). This is inherited model policy,
 * not documented hardware reset. No first-read flag exists: EVERY data read,
 * including the first after initialization, returns data XOR 20h.
 */
bm_status_t bm_ioc02_legacy_initialize(bm_ioc02_legacy_registers_t *registers);

/* Exact byte-only decode at 68/6A/6C; other ports return UNMAPPED. At 6A,
 * writes latch only if select & 1F is nonzero. There is ONE data latch, not
 * an invented bank per selector. Readback inversion/gating are inherited
 * classic PCS386SX-derived policies, not established PCS286 electrical facts.
 * DEBUG reads are pure; DEBUG writes return READ_ONLY. Invalid arguments,
 * unowned ports and rejected writes preserve all state/value/effect outputs.
 * Reads publish zero effects. Writes preserve the supplied value.
 * Effects expose raw latch activity ONLY. They do not identify pins, control
 * A20/remapping, publish memory maps or imply any known timing. The future
 * board adapter owns width splitting, timing and qualified output wiring.
 * Output objects must be separate from register storage and each other.
 */
bm_status_t bm_ioc02_legacy_access(bm_ioc02_legacy_registers_t *registers,
                                  uint16_t port, uint32_t width,
                                  bm_bus_operation_t operation, int debug,
                                  uint16_t *value,
                                  bm_ioc02_legacy_effect_t *effect);
#endif
