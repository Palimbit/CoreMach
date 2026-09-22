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
    BM_808X_NEC_V30 = 0,
    BM_808X_INTEL_8088 = 1
} bm_808x_model_t;

typedef enum bm_8088_opcode_class {
    BM_8088_OPCODE_DOCUMENTED = 0,
    BM_8088_OPCODE_SILICON_ALIAS = 1,
    BM_8088_OPCODE_SILICON_UNDOCUMENTED = 2,
    BM_8088_OPCODE_UNDEFINED = 3
} bm_8088_opcode_class_t;

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

#define BM_808X_TIMING_OBSERVATION_VERSION 38U
#define BM_808X_MAX_PREFETCH_QUEUE_CAPACITY 6U
#define BM_808X_V30_PREFETCH_QUEUE_CAPACITY BM_808X_MAX_PREFETCH_QUEUE_CAPACITY
#define BM_808X_8088_PREFETCH_QUEUE_CAPACITY 4U

typedef enum bm_808x_execution_clock_kind {
    BM_808X_EXECUTION_CLOCKS_UNKNOWN = 0,
    BM_808X_EXECUTION_CLOCKS_EXACT = 1,
    BM_808X_EXECUTION_CLOCKS_RANGE = 2
} bm_808x_execution_clock_kind_t;

typedef enum bm_808x_prefetch_phase {
    BM_808X_PREFETCH_IDLE = 0,
    BM_808X_PREFETCH_T1,
    BM_808X_PREFETCH_T2,
    BM_808X_PREFETCH_T3,
    BM_808X_PREFETCH_TW,
    BM_808X_PREFETCH_T4
} bm_808x_prefetch_phase_t;

/* Host-neutral timing observation for one completed architectural boundary.
 * NEC execution_clocks_min/max are execution-unit clocks and deliberately
 * exclude prefetch, pre-decode and bus waits. The Intel 8088 currently has
 * exact execution and boundary clocks only for the independently checked
 * full-queue 90h and accumulator/immediate ALU baseline and continuation
 * while all instruction bytes are already prefetched. Other Intel forms and
 * demand-prefetch boundaries remain UNKNOWN. EXACT means
 * min == max. RANGE
 * preserves a documented data-dependent interval without pretending that the
 * realised value is known. UNKNOWN is an explicit unimplemented timing
 * classification, never a zero-cycle claim.
 * prefetch_pointer and prefetch_queue_count expose the real per-CPU instruction
 * queue after the boundary. The pointer is always known from version 3.
 * logical_bus_transactions describe successful portable-bus accesses at T3.
 * Completed synchronous operand transfers contribute their four base clocks
 * plus waits to bus_active_clocks; phased prefetch contributes the actual
 * T-state clocks advanced in this boundary, including a partial in-flight
 * transfer. This measures bus occupancy, not boundary duration, because BCU
 * work can overlap EXU work. demand_prefetch_* is the subset issued only
 * because an empty instruction queue blocked byte consumption.
 * instruction_queue_reads
 * counts consumed instruction bytes, each of which has a documented one-clock
 * queue-read cost. Version 5 exposes the next prefetch phase, all successful
 * prefetch transactions and the BCU phase clocks advanced in this boundary.
 * Safe bus-free instructions with exact documented execution clocks now let
 * the BCU progress concurrently; data-bus contention and timings that are
 * ranges or unknown remain deliberately unscheduled.
 * boundary_clocks_min/max compose demand-prefetch stalls, instruction-queue
 * reads and documented execution clocks only when every transaction in the
 * boundary belongs to prefetch. This is the first scheduler-ready duration:
 * EXACT is safe to consume, RANGE preserves uncertainty, and UNKNOWN means
 * operand/I/O contention or another unresolved timing source still prevents
 * an elapsed-time claim. Version 7 routes every operand and I/O transaction
 * through the BCU. operand_* reports its exclusive bus ownership, while
 * prefetch_handoff_clocks is the subset of prefetch clocks required to finish
 * a transfer that was already in flight when an operand requested the bus.
 * Version 8 also advances that same BCU during every instruction-queue read,
 * matching the V30 predecoder clock instead of counting it only in the final
 * elapsed-boundary total.
 * Version 9 begins placing operand transfers on an explicit EXU timeline.
 * execution_timeline_complete is set only when every execution clock and
 * operand transfer in this boundary has a known position. placed clocks
 * include the four base clocks of each operand transaction but exclude wait
 * states; operand_wait_states reports those extensions separately.
 * Version 10 places the documented execution cost of every native prefix
 * immediately after that prefix is decoded. Prefixed direct I/O can therefore
 * complete the same explicit timeline, while other prefixed operand forms stay
 * unresolved until their individual transfers are placed.
 * Version 11 places the four direct accumulator-memory MOV forms and XLAT,
 * including inherited internal clocks and both bus cycles of an odd word.
 * Version 12 places the four MOV r/m,reg forms when r/m names memory. Register
 * forms retain the ordinary aggregate execution path because they do not
 * arbitrate for the BCU.
 * Version 13 places the read-only ModR/M ALU memory forms, including CMP in
 * either encoding direction. Read-modify-write ALU destinations remain
 * unresolved until both transfers can be placed.
 * Version 14 places both transfers of ModR/M ALU read-modify-write memory
 * destinations, preserving the inherited computation interval between them.
 * Version 15 places ModR/M TEST reads and both XCHG memory transfers, including
 * the inherited internal interval before each exchange write.
 * Version 16 restores the inherited operand-before-immediate order for groups
 * 80h-83h and places their memory read, compute and optional write intervals.
 * Version 17 places memory transfers for segment-register MOV and immediate
 * MOV groups C6h-C7h without changing their bus-free register forms.
 * Version 18 places Group 3 F6h-F7h memory reads and the TEST immediate and
 * NOT/NEG write intervals. Signed multiply and divide ranges remain ranges.
 * Version 19 places byte and word FEh/FFh INC/DEC memory reads, their
 * computation interval and writes; other FFh control forms remain unresolved.
 * Version 20 places single-word register, segment, flags and immediate stack
 * pushes and pops, including the odd-stack transfer split.
 * Version 21 places STOS and SCAS memory transfers, including repeated and
 * zero-count forms plus both physical transfers of an odd word.
 * Version 22 places relative near CALL and both near RET forms around an
 * explicit prefetch suspension and target-queue flush.
 * Version 23 places MOVS and LODS source transfers, including repeated,
 * segment-overridden, zero-count and odd-word forms.
 * Version 24 classifies the documented four-clock accumulator-immediate TEST
 * forms; these are bus-free and need no operand placement.
 * Version 25 places both word reads of LES/LDS, including odd pointers and
 * accepted segment overrides.
 * Version 26 places software INT vector reads and interrupt-frame writes in
 * their inherited order, and places IRET stack reads around prefetch
 * suspension and the target-queue flush.
 * Version 27 places register and memory indirect near CALL operand reads and
 * stack writes around prefetch suspension and the target-queue flush.
 * Version 28 resolves unsigned MULU's documented one-clock data-dependent
 * interval using the inherited V30 microcode's high-half condition.
 * Version 29 places accepted NMI and maskable interrupt boundaries, including
 * interrupt acknowledgement and independently aligned stack-frame writes.
 * Version 30 classifies an immediately ready POLL sample as the documented
 * seven-clock case while leaving a repeated busy wait explicitly unresolved.
 * Version 31 places both stack reads and the target-queue flush for far
 * returns, with and without immediate caller cleanup.
 * Version 32 places the source read, inherited internal interval and stack
 * write for Group 5 PUSH, including independent source and stack alignment.
 * Version 33 places register and memory indirect near-jump targets before
 * prefetch suspension and target-queue invalidation.
 * Version 34 places normal, repeated and zero-count CMPS source and
 * destination reads without classifying interrupted repeat fragments.
 * Version 35 places both pointer reads, both stack writes and the target-queue
 * flush for indirect far calls with independent pointer and stack alignment.
 * Version 36 places POP r/m16 stack reads and destination writes, preserving
 * pre-pop effective-address calculation and both independent alignments.
 * Version 37 places both indirect far-jump pointer reads before prefetch
 * suspension and target-queue invalidation, including odd pointers.
 * Version 38 places both stack writes and the target-queue flush of direct
 * far calls in inherited microcode order.
 * These fields expose arbitration resources, not yet a complete elapsed time,
 * because the executor does not expose each access's EXU-clock position. */
typedef struct bm_808x_timing_observation {
    uint32_t size;
    uint32_t version;
    bm_808x_boundary_kind_t kind;
    uint8_t opcode;
    uint8_t effective_opcode;
    uint8_t prefix_count;
    uint8_t prefetch_queue_flushed;
    uint8_t prefetch_pointer_known;
    uint8_t prefetch_queue_capacity;
    uint8_t prefetch_queue_count;
    uint8_t reserved[1];
    uint16_t prefetch_pointer;
    bm_808x_execution_clock_kind_t execution_clock_kind;
    uint32_t execution_clocks_min;
    uint32_t execution_clocks_max;
    uint64_t logical_bus_transactions;
    uint64_t reported_wait_states;
    uint64_t bus_active_clocks;
    uint64_t demand_prefetch_transactions;
    uint64_t demand_prefetch_bus_clocks;
    uint32_t instruction_queue_reads;
    bm_808x_prefetch_phase_t prefetch_phase;
    uint64_t prefetch_transactions;
    uint64_t prefetch_phase_clocks;
    bm_808x_execution_clock_kind_t boundary_clock_kind;
    uint64_t boundary_clocks_min;
    uint64_t boundary_clocks_max;
    uint64_t operand_transactions;
    uint64_t operand_bus_clocks;
    uint64_t prefetch_handoff_clocks;
    uint8_t execution_timeline_complete;
    uint8_t reserved_v9[3];
    uint32_t execution_clocks_placed;
    uint64_t operand_wait_states;
} bm_808x_timing_observation_t;

typedef void (*bm_808x_timing_fn)(
    void *context, const bm_808x_timing_observation_t *observation);

#define BM_808X_BUS_PHASE_OBSERVATION_VERSION 2U

typedef enum bm_808x_bus_phase {
    BM_808X_BUS_PHASE_T1 = 0,
    BM_808X_BUS_PHASE_T2,
    BM_808X_BUS_PHASE_T3,
    BM_808X_BUS_PHASE_TW,
    BM_808X_BUS_PHASE_T4
} bm_808x_bus_phase_t;

/* One active external-bus T-state. This deliberately excludes EU-only idle
 * clocks: it is a bus-phase observation, not an instruction duration. For
 * explicitly clocked Intel boundaries, cpu_clock_index is CPU-local from
 * reset or state import and includes preceding idle clocks; otherwise only
 * bus_active_clock_index is meaningful. The
 * transaction is the logical transfer occupying the bus. The device response
 * (including read/fetch data and wait_states) becomes valid from a successful
 * T3 through the end of the cycle. */
typedef struct bm_808x_bus_phase_observation {
    uint32_t size;
    uint32_t version;
    uint64_t bus_active_clock_index;
    bm_808x_bus_phase_t phase;
    uint8_t response_valid;
    uint8_t reserved[3];
    bm_bus_transaction_t transaction;
    /* Valid only for explicitly clocked Intel boundaries. Other execution
     * modes expose bus-active order, not a CPU-clock position. */
    uint64_t cpu_clock_index;
    uint8_t cpu_clock_known;
    uint8_t reserved_v2[7];
} bm_808x_bus_phase_observation_t;

typedef void (*bm_808x_bus_phase_fn)(
    void *context, const bm_808x_bus_phase_observation_t *observation);

#define BM_8088_QUEUE_EVENT_VERSION 1U

typedef enum bm_8088_queue_event_kind {
    BM_8088_QUEUE_READ_FIRST = 0,
    BM_8088_QUEUE_READ_SUBSEQUENT,
    BM_8088_QUEUE_FLUSH
} bm_8088_queue_event_kind_t;

/* Logical Intel 8088 instruction-queue operation, in callback order. FIRST
 * includes each prefix and the effective opcode. A flush carries no byte.
 * These are not QS0/QS1 pin samples: the electrical status is delayed and a
 * CPU-clock position requires an independently validated EU/BIU schedule. */
typedef struct bm_8088_queue_event {
    uint32_t size;
    uint32_t version;
    bm_8088_queue_event_kind_t kind;
    uint16_t cs;
    uint16_t ip;
    uint8_t value;
    uint8_t count_before;
    uint8_t count_after;
    uint8_t reserved;
} bm_8088_queue_event_t;

typedef void (*bm_8088_queue_event_fn)(
    void *context, const bm_8088_queue_event_t *event);

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
    /* Optional read-only observer for active T1/T2/T3/Tw/T4 bus phases. */
    bm_808x_bus_phase_fn bus_phase;
    void *bus_phase_context;
    /* Optional logical queue observer, called only for the Intel 8088 model. */
    bm_8088_queue_event_fn intel_queue_event;
    void *intel_queue_event_context;
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

#define BM_808X_PREFETCH_STATE_VERSION 1U

/* Versioned microarchitectural queue state for deterministic conformance and
 * save-state tooling. Bytes are stored in consumption order. The prefetch
 * pointer names the next bus address after the queued bytes; it is independent
 * of architectural IP. Import is valid only at an architectural boundary and
 * discards any partially completed fetch. */
typedef struct bm_808x_prefetch_state {
    uint32_t size;
    uint32_t version;
    uint16_t pointer;
    uint8_t count;
    uint8_t capacity;
    uint8_t bytes[BM_808X_MAX_PREFETCH_QUEUE_CAPACITY];
} bm_808x_prefetch_state_t;

bm_status_t bm_808x_create(const bm_host_services_t *host,
                           const bm_808x_config_t *config,
                           bm_cpu_t *out_cpu);

/* Architectural state transfer is defined only at an instruction boundary.
 * It deliberately excludes bus pins, trace bookkeeping and host callbacks,
 * but includes the architecturally observable interrupt shadows and latched
 * NMI/single-step requests and the MD write gate needed for mode transitions.
 * FLAGS/PSW fixed bits are canonicalized by the selected model. */
bm_status_t bm_808x_get_arch_state(const bm_cpu_t *cpu,
                                   bm_808x_arch_state_t *out_state);
bm_status_t bm_808x_set_arch_state(bm_cpu_t *cpu,
                                   const bm_808x_arch_state_t *state);
bm_status_t bm_808x_get_prefetch_state(
    const bm_cpu_t *cpu, bm_808x_prefetch_state_t *out_state);
bm_status_t bm_808x_set_prefetch_state(
    bm_cpu_t *cpu, const bm_808x_prefetch_state_t *state);

/* Classify an original 8088 primary opcode and, for grouped instructions, the
 * supplied ModR/M operation field. Silicon classes are intentionally distinct
 * from Intel's published ISA; undefined forms have no invented semantics. */
bm_8088_opcode_class_t bm_8088_classify_opcode(uint8_t opcode,
                                                uint8_t modrm);

/* Execute one architectural boundary. A pending accepted interrupt consumes
 * the boundary instead of an opcode, matching bm_cpu_ops.run with budget 1. */
bm_status_t bm_808x_step(bm_cpu_t *cpu, bm_tick_t *consumed);

/* Engine callback for one cycle-reported architectural boundary. It succeeds
 * only when the selected model's timing data produced one exact scalar
 * duration. Intel 8088 durations are classified only for the prefetched,
 * unprefixed baseline described above, including queue-fed continuation;
 * ranged or unknown boundaries return BM_STATUS_UNSUPPORTED with zero cycles,
 * so a clocked machine cannot silently turn an unresolved timing into virtual
 * time.
 * The context must be the context owned by a CPU created by bm_808x_create(). */
bm_status_t bm_808x_step_clocked(void *context, bm_tick_t start_ns,
                                 uint64_t *cycles);

#ifdef __cplusplus
}
#endif

#endif
