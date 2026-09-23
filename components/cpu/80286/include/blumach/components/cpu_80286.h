/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Partial Intel 80286 reset/fetch/NOP interpreter; timing and full ISA pending.
 */
#ifndef BLUMACH_COMPONENTS_CPU_80286_H
#define BLUMACH_COMPONENTS_CPU_80286_H

#include <blumach/components/bus.h>
#include <blumach/engine/engine.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BM_286_CONTRACT_VERSION 2U
#define BM_286_STATE_VERSION 2U

/* A20 gating belongs to the motherboard, not to segment translation. */
typedef enum bm_286_signal {
    BM_286_SIGNAL_INTR = 0,
    BM_286_SIGNAL_NMI,
    BM_286_SIGNAL_HOLD
} bm_286_signal_t;

typedef enum bm_286_timing_quality {
    BM_286_TIMING_UNKNOWN = 0,
    BM_286_TIMING_DOCUMENTED,
    BM_286_TIMING_PROVISIONAL
} bm_286_timing_quality_t;

typedef enum bm_286_boundary_kind {
    BM_286_BOUNDARY_INSTRUCTION = 0,
    BM_286_BOUNDARY_INTERRUPT,
    BM_286_BOUNDARY_EXCEPTION,
    BM_286_BOUNDARY_HALT,
    BM_286_BOUNDARY_HOLD,
    BM_286_BOUNDARY_SHUTDOWN,
    /* One completed, resumable repetition; IP retains the prefix address
     * until the repeated instruction has completed. */
    BM_286_BOUNDARY_REP_ITERATION
} bm_286_boundary_kind_t;

/* Includes hidden caches: a selector alone cannot describe protected-mode
 * state or the reset CS base. This is inspection/conformance data, not an
 * on-disk save-state format or a complete microarchitectural checkpoint. */
typedef struct bm_286_segment_state {
    uint16_t selector;
    uint32_t base;              /* 24 significant bits */
    uint16_t limit;
    uint8_t access;
    uint8_t valid;
} bm_286_segment_state_t;

typedef struct bm_286_table_state {
    uint32_t base;
    uint16_t limit;
} bm_286_table_state_t;

typedef struct bm_286_arch_state {
    uint32_t size;
    uint32_t version;
    uint16_t ax, cx, dx, bx, sp, bp, si, di;
    uint16_t ip, flags, msw;
    bm_286_segment_state_t es, cs, ss, ds, ldtr, tr;
    bm_286_table_state_t gdtr, idtr;
    uint8_t cpl;
    uint8_t halted;
    uint8_t shutdown;
    uint8_t interrupt_shadow; /* Draft inhibition latch; STI versus SS-load
                               * distinctions remain unimplemented. */
    uint8_t nmi_blocked;
    uint8_t nmi_pending;
    /* A trap sampled from TF before a completed instruction can remain due
     * even if the instruction changes FLAGS. It is not inferred from TF. */
    uint8_t trap_pending;
} bm_286_arch_state_t;

typedef struct bm_286_boundary {
    bm_286_boundary_kind_t kind;
    bm_286_timing_quality_t timing;
    /* DOCUMENTED: total elapsed native clocks, including waits.
     * UNKNOWN: known lower bound only (currently bus waits); never submit
     * that value to the native-clock scheduler as an elapsed duration. */
    uint64_t cpu_cycles;
    uint64_t bus_wait_cycles;   /* included in cpu_cycles, never add twice */
    uint32_t instruction_address;
    uint16_t instruction_ip;
    uint8_t vector;
    uint8_t has_vector;
} bm_286_boundary_t;

/* Two interrupt-acknowledge phases per accepted INTR. phase is 0 then 1;
 * vector is meaningful on phase 1; waits are extra CPU clocks. Device state
 * changes only at these real acknowledge calls, not when INTR is asserted. */
typedef bm_status_t (*bm_286_inta_fn)(void *context, unsigned int phase,
                                     uint8_t *vector, uint32_t *waits);
typedef void (*bm_286_pin_fn)(void *context, int asserted);
typedef void (*bm_286_trace_fn)(void *context, const bm_286_boundary_t *boundary);

typedef struct bm_286_config {
    uint32_t size;
    uint32_t version;
    /* Logical bus transfers use 24-bit memory / 16-bit I/O addresses.
     * CPU splits unaligned words; the board splits accesses to 8-bit targets.
     * CPU initializes wait_states to zero; access returns total extra native
     * CPU clocks. They are counted once in cpu_cycles and bus_wait_cycles.
     * The callback completes synchronously; IDLE is not a mid-instruction
     * retry. Handle HOLD before the next boundary, without any bus access.
     * Board resolves UNMAPPED/READ_ONLY into its documented hardware policy.
     * It receives DEBUG and LOCKED attributes without losing either. */
    bm_bus_access_fn access;
    void *access_context;
    bm_286_inta_fn interrupt_ack;
    void *interrupt_context;
    bm_286_pin_fn hold_ack;
    bm_286_pin_fn bus_lock;
    bm_286_pin_fn shutdown;
    void *pin_context;
    bm_286_trace_fn trace;
    void *trace_context;
} bm_286_config_t;

/* Produces bm_cpu_t with per-instance storage and normal reset/signal/inspect/
 * destroy operations. On failure, out_cpu is cleared and allocations released.
 * Creation leaves reset architectural state but performs no fetch/INTA.
 * CPU owns no RAM, chipset, scheduler or host file. ops.run follows the legacy
 * engine's one-instruction-per-tick diagnostic convention; it is not CPU
 * clocks or nanoseconds and must not schedule a PCS 286 machine. Use the
 * strict clocked callback after instruction timing is established.
 * Configuration is copied;
 * callback contexts and host services remain valid until CPU destruction. */
bm_status_t bm_286_create(const bm_host_services_t *host,
                          const bm_286_config_t *config, bm_cpu_t *out_cpu);
bm_status_t bm_286_get_arch_state(const bm_cpu_t *cpu,
                                  bm_286_arch_state_t *out_state);
/* Conformance-only state import at a stopped boundary. Validates size,
 * version, Boolean fields and architectural invariants atomically, flushes
 * prefetch and discards any private REP/decode continuation. trap_pending is
 * imported as supplied, never synthesized from the current TF bit. It starts
 * a new microarchitectural observation interval, not a cycle-exact restore. */
bm_status_t bm_286_set_arch_state(bm_cpu_t *cpu,
                                  const bm_286_arch_state_t *state);
bm_status_t bm_286_step(bm_cpu_t *cpu, bm_286_boundary_t *out_boundary);
/* Exact existing engine callback: start_ns is virtual boundary time; returned
 * cycles are native CPU clocks. Never turn unknown timing into zero/one clocks.
 * Provisional scheduling, if added later, requires a separately named policy.
 * Guest faults are emulated exceptions, not host BM_STATUS_UNSUPPORTED errors.
 * An implementation gap stops execution; reset is required before continuing.
 * Shutdown must notify the board; it must not exit the host process or reset
 * automatically. Intel documents NMI or RESET as shutdown exit sources: NMI
 * retains protected mode, RESET returns to real mode. */
bm_status_t bm_286_step_clocked(void *context, bm_tick_t start_ns,
                                uint64_t *cycles);

#ifdef __cplusplus
}
#endif
#endif
