/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_COMPONENTS_CPU_808X_H
#define BLUMACH_COMPONENTS_CPU_808X_H

#include <stdint.h>
#include <blumach/components/bus.h>
#include <blumach/engine/cpu.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum bm_808x_model {
    BM_808X_NEC_V30 = 0
} bm_808x_model_t;

typedef enum bm_808x_signal {
    BM_808X_SIGNAL_INT = 0,
    BM_808X_SIGNAL_NMI = 1
} bm_808x_signal_t;

typedef struct bm_808x_trace {
    uint16_t cs;
    uint16_t ip;
    uint16_t ds;
    uint16_t es;
    uint16_t ss;
    uint16_t sp;
    uint16_t ax;
    uint16_t bx;
    uint16_t cx;
    uint16_t dx;
    uint16_t bp;
    uint16_t si;
    uint16_t di;
    uint16_t flags;
    uint32_t physical_address;
    uint8_t opcode;
    uint8_t effective_opcode;
    uint8_t prefix_count;
} bm_808x_trace_t;

typedef void (*bm_808x_trace_fn)(void *context, const bm_808x_trace_t *trace);
typedef bm_status_t (*bm_808x_interrupt_ack_fn)(void *context, uint8_t *vector);

typedef enum bm_808x_fpo_family {
    BM_808X_FPO1 = 1,
    BM_808X_FPO2 = 2
} bm_808x_fpo_family_t;

/* Host-neutral observation delivered to an optional floating-point component.
 * For a memory form the CPU has already performed the documented auxiliary
 * address calculation and memory-read cycle. memory_value is the word seen on
 * that cycle; the CPU itself discards it. Register forms set memory_operand to
 * zero and leave all address/value fields zero. */
typedef struct bm_808x_fpo_request {
    uint32_t size;
    bm_808x_fpo_family_t family;
    uint8_t opcode;
    uint8_t modrm;
    uint8_t memory_operand;
    uint8_t reserved;
    uint16_t segment;
    uint16_t offset;
    uint32_t physical_address;
    uint16_t memory_value;
} bm_808x_fpo_request_t;

typedef bm_status_t (*bm_808x_fpo_fn)(
    void *context, const bm_808x_fpo_request_t *request);
/* ready is one when the active-low V30 POLL input is asserted. A callback must
 * write exactly zero or one. The callback may return a device status. */
typedef bm_status_t (*bm_808x_poll_fn)(void *context, int *ready);

typedef enum bm_808x_boundary_kind {
    BM_808X_BOUNDARY_INSTRUCTION = 0,
    BM_808X_BOUNDARY_INTERRUPT = 1
} bm_808x_boundary_kind_t;

#define BM_808X_TIMING_OBSERVATION_VERSION 1U
#define BM_808X_V30_PREFETCH_QUEUE_CAPACITY 6U

/* Host-neutral timing observation for one completed architectural boundary.
 * execution_clocks are NEC execution-unit clocks and deliberately exclude
 * prefetch, pre-decode and bus waits. A zero execution_clocks_known value is
 * an explicit unimplemented timing classification, never a zero-cycle claim.
 * logical_bus_transactions describe the current portable bus API and are not
 * yet a claim about physical V30 bus cycles. */
typedef struct bm_808x_timing_observation {
    uint32_t size;
    uint32_t version;
    bm_808x_boundary_kind_t kind;
    uint8_t opcode;
    uint8_t effective_opcode;
    uint8_t prefix_count;
    uint8_t execution_clocks_known;
    uint8_t prefetch_queue_flushed;
    uint8_t prefetch_pointer_known;
    uint8_t prefetch_queue_capacity;
    uint8_t reserved;
    uint16_t prefetch_pointer;
    uint32_t execution_clocks;
    uint64_t logical_bus_transactions;
    uint64_t reported_wait_states;
} bm_808x_timing_observation_t;

typedef void (*bm_808x_timing_fn)(
    void *context, const bm_808x_timing_observation_t *observation);

typedef struct bm_808x_config {
    bm_808x_model_t model;
    uint32_t frequency_hz;
    bm_bus_t *bus;
    bm_808x_trace_fn trace;
    void *trace_context;
    bm_808x_interrupt_ack_fn interrupt_ack;
    void *interrupt_context;
    /* A null FPO callback models no attached coprocessor: register forms are
     * CPU no-ops and memory forms still issue their documented read cycle. A
     * null POLL callback is different: the external pin level is unknown, so
     * POLL returns BM_STATUS_UNSUPPORTED instead of assuming ready or busy. */
    bm_808x_fpo_fn fpo;
    bm_808x_poll_fn poll;
    void *coprocessor_context;
    /* Optional observer. It cannot affect execution and is called only after
     * a successfully completed instruction or accepted interrupt boundary. */
    bm_808x_timing_fn timing;
    void *timing_context;
} bm_808x_config_t;

#define BM_808X_ARCH_STATE_VERSION 4U

typedef struct bm_808x_arch_state {
    uint32_t size;
    uint32_t version;
    bm_808x_model_t model;
    uint16_t ax;
    uint16_t cx;
    uint16_t dx;
    uint16_t bx;
    uint16_t sp;
    uint16_t bp;
    uint16_t si;
    uint16_t di;
    uint16_t es;
    uint16_t cs;
    uint16_t ss;
    uint16_t ds;
    uint16_t ip;
    uint16_t flags;
    uint8_t halted;
    /* Remaining completed instruction boundaries before a maskable interrupt
     * may be accepted. This is observable state after EI and segment-register
     * transfers, so snapshots must preserve it. */
    uint8_t interrupt_inhibit;
    /* Segment-register transfers also suppress NMI and single-step recognition
     * through the following instruction. EI deliberately does not set this. */
    uint8_t boundary_inhibit;
    /* Internally latched requests at an architectural boundary. The NMI input
     * level itself remains an external bus pin and is not part of a snapshot. */
    uint8_t nmi_pending;
    uint8_t trap_pending;
    /* BRKEM enables writes to MD so CALLN/interrupt plus IRET can return to
     * emulation mode. RESET and RETEM disable them again. */
    uint8_t md_write_enabled;
} bm_808x_arch_state_t;

bm_status_t bm_808x_create(const bm_host_services_t *host,
                           const bm_808x_config_t *config,
                           bm_cpu_t *out_cpu);

/* Architectural state transfer is defined only at an instruction boundary.
 * It deliberately excludes bus pins, trace bookkeeping and host callbacks,
 * but includes the architecturally observable interrupt shadows and latched
 * NMI/single-step requests and the MD write gate needed for mode transitions.
 * The NEC PSW fixed bits are canonicalized in both native and 8080 emulation
 * modes. */
bm_status_t bm_808x_get_arch_state(const bm_cpu_t *cpu,
                                   bm_808x_arch_state_t *out_state);
bm_status_t bm_808x_set_arch_state(bm_cpu_t *cpu,
                                   const bm_808x_arch_state_t *state);

/* Execute one architectural boundary. A pending accepted interrupt consumes
 * the boundary instead of an opcode, matching bm_cpu_ops.run with budget 1. */
bm_status_t bm_808x_step(bm_cpu_t *cpu, bm_tick_t *consumed);

#ifdef __cplusplus
}
#endif

#endif
