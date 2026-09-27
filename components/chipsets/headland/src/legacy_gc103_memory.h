/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Private migration model, not certification of PCS286 wiring/silicon.
 */
#ifndef BM_LEGACY_GC103_MEMORY_H
#define BM_LEGACY_GC103_MEMORY_H
#include "legacy_gc103_registers.h"
#include <blumach/components/headland_gc10x.h>

typedef struct bm_gc103_window {
    uint32_t base, size;
    int enabled;
    int ems_slot; /* -1: ordinary RAM; 0..63: EMS handler context. */
} bm_gc103_window_t;

typedef struct bm_gc103_memory {
    bm_gc103_registers_t registers;
    uint32_t ram_bytes;
    uint32_t physical_bank_bytes; /* 0: legacy; configured: 512KiB or 2MiB. */
    /* Incremental maps with documented separation of I/O/memory contexts.
     * 16KiB is the smallest mapping unit in this inherited variant. */
    uint8_t access[1024];
    bm_gc103_window_t window[93];
} bm_gc103_memory_t;

typedef enum bm_gc103_dram {
    BM_GC103_DRAM_256K = 0,
    BM_GC103_DRAM_1M
} bm_gc103_dram_t;

typedef struct bm_gc103_memory_config {
    bm_gc103_dram_t dram; /* Actual uniform installed density, not CR.D7. */
    unsigned installed_banks; /* 1..4 contiguous banks, no invented cards. */
    bm_gc103_straps_t pins;
} bm_gc103_memory_config_t;

bm_status_t bm_gc103_memory_initialize(bm_gc103_memory_t *memory, uint32_t ram_bytes);
/* Configured component profile, not identification of the PCS286 board.
 * RAMSW1/2 and SPLSW must FLOAT: tied-input decode precedence is unresolved.
 * RAM1M may float or be tied consistently with the installed density.
 * CR.D7 and D6:5 control selected geometry/linear extent; installed backing
 * remains fixed. No storage allocated. Failure preserves the previous object.
 * Ordinary linear decode below a selected 1MiB remains unsupported; EMS and
 * ROM can still be used/programmed. A selected/physical density mismatch
 * rejects RAM routes (including EMS/shadow) without guessing DRAM aliases.
 */
bm_status_t bm_gc103_memory_initialize_configured(bm_gc103_memory_t *memory,
                                                  const bm_gc103_memory_config_t *config);
/* Replacing a legacy model's registers with STRAP_READBACK alone remains
 * unsupported: configured storage geometry must be initialized explicitly. */
/* Commit registers and their mapping effects together; pure DEBUG reads.
 * Reinitialization resets this model only, never caller-owned backing bytes. */
bm_status_t bm_gc103_memory_io(bm_gc103_memory_t *memory, uint16_t port,
                              uint32_t width, bm_bus_operation_t operation,
                              int debug, uint16_t *value);
/* Pure byte route, conservative contiguous span <=16KiB. Valid initialized
 * objects only. UNKNOWN timing remains UNKNOWN. A20 input applies to CPU
 * alone (GC102 reference p13); ownership/grant is checked by the AT adapter.
 * RAM overflow becomes OPEN_BUS, never a pointer or guest exception. ROMCS
 * uses the classic PCS286 128KiB layout. External routes preserve the gated
 * physical address. Writes to ROM/shadow return writable=0 for board policy;
 * this query itself performs no writes, ignored writes or successful reads.
 */
bm_status_t bm_gc103_memory_resolve(const bm_gc103_memory_t *memory,
                                   bm_gc10x_requester_t requester, int cpu_a20,
                                   uint32_t address, bm_bus_operation_t operation,
                                   bm_gc10x_route_t *out_route);
#endif
