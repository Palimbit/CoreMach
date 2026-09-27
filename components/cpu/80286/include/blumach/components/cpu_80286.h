/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Intel 80286 functional real/protected interpreter with an explicit supported
 * instruction profile. Exact timing, remaining ISA/80287 and board integration
 * are pending; see doc/architecture/pcs286-protected-public.md.
 */
#ifndef BLUMACH_COMPONENTS_CPU_80286_H
#define BLUMACH_COMPONENTS_CPU_80286_H

#include <blumach/components/bus.h>
#include <blumach/engine/engine.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BM_286_CONTRACT_VERSION 2U
#define BM_286_STATE_VERSION 3U

/* Inhibition for the next instruction boundary, not a clock count.
 * INTR_ONLY is set by STI.
 * SS_LOAD additionally inhibits NMI and single-step delivery. Faults are not
 * suppressed. HOLD/idle/error paths do not consume either shadow. */
typedef enum bm_286_interrupt_shadow {
    BM_286_SHADOW_NONE = 0,
    BM_286_SHADOW_INTR_ONLY = 1,
    BM_286_SHADOW_SS_LOAD = 2
} bm_286_interrupt_shadow_t;

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
    /* One completed, resumable repetition with more work pending. IP retains
     * the first prefix address; final or zero-count REP reports INSTRUCTION. */
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
    uint8_t interrupt_shadow; /* bm_286_interrupt_shadow_t; state v3 */
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
    /* INSTRUCTION + has_vector identifies a completed software interrupt;
     * EXCEPTION identifies sampled traps or delivered synchronous faults
     * including protected access/privilege/task faults and double faults;
     * external interrupts use INTERRUPT. SHUTDOWN has no delivered vector. */
    uint8_t has_vector;
} bm_286_boundary_t;

/* Two interrupt-acknowledge phases per accepted INTR. Requires bus_lock.
 * B-2/later logical exclusion spans both phases through the first stack word;
 * the remainder of a real-mode frame and IVT reads are not in that window.
 * Protected ordinary entry retains its documented descriptor/first-word
 * exclusion; task INTA conservatively excludes the complete switch boundary.
 * These are logical functional policies, not measured pin timing.
 * Phase is 0 then 1;
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
     * It receives DEBUG and LOCKED attributes without losing either.
     * Signal changes may be reported during access. Reset, destroy, state
     * import and nested execution must be deferred until the boundary returns;
     * in particular an I/O-triggered board reset must not re-enter the CPU. */
    bm_bus_access_fn access;
    void *access_context;
    bm_286_inta_fn interrupt_ack;
    void *interrupt_context;
    bm_286_pin_fn hold_ack;
    /* Required for accepted INTR, memory XCHG / supported LOCK memory forms.
     * NULL rejects INTR before acknowledging the interrupt controller, and those
     * forms before data access. Assert/deassert delimit all operand fragments;
     * adapter must establish exclusion synchronously (no failure return).
     * Signal changes are allowed; reset/import/destroy/execution are deferred.
     * Instruction LOCK releases after commit or a latched host-error stop.
     * Real/ordinary protected INTR releases after its first complete stack word;
     * task INTR retains exclusion for the switch boundary. Protected descriptor
     * accessed/busy updates also require this adapter and use its exclusion.
     * odd fragments stay together as a logical-word policy, not timed pins.
     * Operand transfers carry LOCKED, instruction fetches never do. */
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
 * engine's one-boundary-per-tick diagnostic convention; it is not CPU
 * clocks or nanoseconds and must not schedule a PCS 286 machine. Use the
 * strict clocked callback after instruction timing is established.
 * The current extension interface explicitly models an unpopulated 80287,
 * with BUSY/ERROR/PEREQ inactive. WAIT completes unless MP+TS requests #7.
 * ESC with EM or TS delivers #7; otherwise it consumes its complete effective-
 * address encoding and completes without an operand transfer. It therefore
 * cannot manufacture a store result. There is no populated 80287, arithmetic
 * or extension pin API yet.
 * SMSW/LMSW/CLTS control MSW. LMSW may set PE; subsequent functional step/run
 * boundaries use checked cached accesses, descriptors, IDT delivery/return,
 * ordinary privilege transfers and task switches under the C/D/E/F contracts.
 * Retained real caches remain until guest reload. Unsupported encodings and
 * prefix combinations still stop explicitly; no full CPU/OS/board claim.
 * run counts successful boundaries (including REP elements and delivered
 * events), not instructions or clocks. On idle/error, consumed excludes that
 * boundary and retains prior successes. A zero budget is a no-op even after
 * a latched stop; it does not recover execution. Only reset clears the stop.
 * Configuration is copied;
 * callback contexts and host services remain valid until CPU destruction. */
bm_status_t bm_286_create(const bm_host_services_t *host,
                          const bm_286_config_t *config, bm_cpu_t *out_cpu);
bm_status_t bm_286_get_arch_state(const bm_cpu_t *cpu,
                                  bm_286_arch_state_t *out_state);
/* Conformance-only state import at an idle API boundary (never during callbacks
 * or after a latched host stop). Validates size,
 * version, Boolean fields, shadow enum and architectural invariants atomically, flushes
 * prefetch and discards any private REP/decode continuation. trap_pending is
 * imported as supplied, never synthesized from the current TF bit. It starts
 * a new microarchitectural observation interval, not a cycle-exact restore. */
bm_status_t bm_286_set_arch_state(bm_cpu_t *cpu,
                                  const bm_286_arch_state_t *state);
/* Real-mode INTR (two INTA phases), NMI and sampled #1 are accepted before
 * fetch. An out-of-limit IVT vector attempts #8 with first-byte restart IP;
 * if #8 is outside the IVT too, enter guest shutdown (OK on entry, then IDLE).
 * Real-mode NMI can recover when its vector/frame is usable; failed IVT
 * recovery leaves NMI blocked until reset. Host endpoint errors do not
 * generate guest faults. Protected exceptions, #DF/shutdown and eligible NMI
 * recovery use the reviewed descriptor/task delivery contracts. This is
 * functional state/signalling, not bus-cycle timing.
 * Interrupts are accepted before
 * fetch, respecting SS/STI inhibition. Real-mode CLI/STI/HLT/IRET,
 * INT/INT3/INTO, PUSHF/POPF, LAHF/SAHF and carry/direction control are implemented.
 * Real-mode far CALL 9A/FF /3 and RETF CA/CB preserve FLAGS/NMI blocking;
 * registers commit only after all pointer/stack accesses succeed.
 * Real-mode memory XCHG implicitly locks; F0 supports memory-destination
 * ADD/OR/ADC/SBB/AND/SUB/XOR, INC/DEC/NOT/NEG, XCHG, MOV (including memory
 * segment-register transfers), and documented memory shifts/rotates.
 * Masked-zero shifts retain the unlocked read/no-write functional policy;
 * register-only LOCK and undocumented shift /6 remain unsupported. Other forms
 * remain UNSUPPORTED, not a fabricated guest #UD.
 * LOCK MOVS/INS/OUTS support F2/F3 with exclusion retained across iteration
 * boundaries. It releases on completion, accepted interrupt, host failure,
 * reset, valid state import, destruction or strict-clock refusal. Invalid
 * calls leave an active continuation intact. The adapter must outlive CPU
 * destruction. HOLD cannot split the block; accepted events discard decode
 * and IRET restarts from committed CX/SI/DI. Combined event/pin ordering is
 * a functional policy pending silicon traces, not cycle-exact validation.
 * Memory strings and INS/OUTS support F2/F3 repetition, one element per step.
 * INS fixes ES:DI; OUTS uses DS:SI or override; DX and FLAGS are unchanged.
 * Host I/O effects are irreversible, including input consumed before a failed
 * memory write. Such failures stop without replay; segment-fault preflight
 * before I/O is functional policy, not silicon fault-order certification.
 * CX=0 completes without data accesses. Incomplete REP retains prefix IP;
 * HOLD preserves decode, accepted interrupt/reset/import discards it.
 * Import may resume architecturally from CX/SI/DI/IP and unchanged code, but
 * is not a full saved microarchitectural/prefetch checkpoint.
 * Entry and IRET stage registers until all accesses succeed; completed bus
 * writes/acknowledgements are not undone on host errors, and retry is latched
 * off. Task-load faults may retain a selected partial task context as defined
 * in the task contract. Reserved encodings, populated 80287 execution and
 * revision-specific microstate outside the selected profile are not certified.
 * Timing remains UNKNOWN for every boundary. */
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
