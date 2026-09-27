/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Private migration boundary, NOT the PCS286 chip implementation or public ABI.
 * Classic registers plus opt-in documented CR readback; see pcs286-headland.md.
 */
#ifndef BM_LEGACY_GC103_REGISTERS_H
#define BM_LEGACY_GC103_REGISTERS_H
#include <blumach/components/bus.h>

typedef enum bm_gc103_pin_level {
    BM_GC103_PIN_FLOATING = 0,
    BM_GC103_PIN_LOW,
    BM_GC103_PIN_HIGH
} bm_gc103_pin_level_t;

typedef struct bm_gc103_straps {
    bm_gc103_pin_level_t ram1m;  /* pin36: LOW selects 1M, HIGH selects 256K. */
    bm_gc103_pin_level_t ramsw1; /* pin43: inverse input ORs into CR.D5. */
    bm_gc103_pin_level_t ramsw2; /* pin41: inverse input ORs into CR.D6. */
    bm_gc103_pin_level_t splsw;  /* pin19: inverse input ORs into CR.D2. */
} bm_gc103_straps_t;

typedef enum bm_gc103_control_profile {
    BM_GC103_CONTROL_LEGACY = 0,
    BM_GC103_CONTROL_STRAP_READBACK
} bm_gc103_control_profile_t;

typedef struct bm_gc103_registers {
    uint16_t ems[64];
    uint8_t mar;
    uint8_t cr0; /* Legacy map control, or configured CR readback (not decode). */
    uint8_t ram_straps;
    bm_gc103_control_profile_t control_profile;
    bm_gc103_straps_t pins;
    uint8_t cr_written; /* Unmodified software latch for STRAP_READBACK. */
} bm_gc103_registers_t;

typedef enum bm_gc103_mapping_change {
    BM_GC103_MAPPING_UNCHANGED = 0,
    BM_GC103_MAPPING_EMS_SLOT,
    BM_GC103_MAPPING_ALL
} bm_gc103_mapping_change_t;

typedef struct bm_gc103_register_effect {
    bm_gc103_mapping_change_t mapping;
    uint8_t slot;
} bm_gc103_register_effect_t;

/* Reproduce the CLASSIC PCS286-selected GC103 register initialization.
 * Accept 1/2/3/4 MiB only. This does not identify the real PCS286 silicon.
 * No allocations, borrowed pointers, registration, timing or RAM accesses.
 * Failed calls preserve every output and the prior register state.
 */
bm_status_t bm_gc103_registers_initialize(bm_gc103_registers_t *registers,
                                         uint32_t ram_bytes);

/* Standalone configured CR register profile, Headland GC103 07-89 (01),
 * pp5-7. Pins are copied explicitly; never inferred from installed RAM.
 * CR software latch starts at zero. MR/MAR initialization and all non-CR
 * accesses retain the inherited model policy, not new silicon qualification.
 * No live pin-change API or physical warm-reset retention is claimed.
 * This initializer qualifies CR readback only. A configured memory instance
 * must separately supply installed geometry and reject unresolved pin modes.
 * Failure (including invalid pin levels) preserves the previous object.
 */
bm_status_t bm_gc103_registers_initialize_strapped(bm_gc103_registers_t *registers,
                                                  const bm_gc103_straps_t *pins);

/* Native byte/word register accesses; board width splitting is separate.
 * Port 1ECh is the EMS data port, 1EEh MAR, 1EFh fixed CR0. 1EDh has no
 * selector on the inherited GC103 variant. Word accesses at other owned
 * ports return FFFF/ignore writes as the classic word handler does.
 * DEBUG reads do not advance MAR; DEBUG writes return READ_ONLY.
 * An accepted write reports which mapping the future owner must rebuild;
 * this helper does NOT publish or claim to update a board memory map.
 */
bm_status_t bm_gc103_registers_access(bm_gc103_registers_t *registers,
                                     uint16_t port, uint32_t width,
                                     bm_bus_operation_t operation, int debug,
                                     uint16_t *value,
                                     bm_gc103_register_effect_t *effect);
#endif
