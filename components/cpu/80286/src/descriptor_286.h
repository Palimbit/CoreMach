/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 BluMach contributors
 */
#ifndef BM_DESCRIPTOR_286_H
#define BM_DESCRIPTOR_286_H

#include <stdbool.h>
#include <stdint.h>
#include <blumach/components/cpu_80286.h>

/* Private 80286 interpretation helpers, not an engine or runtime ABI. */
typedef enum bm_286_pm_kind {
    BM_286_PM_INVALID, BM_286_PM_DATA, BM_286_PM_CODE,
    BM_286_PM_TSS_AVAILABLE, BM_286_PM_LDT, BM_286_PM_TSS_BUSY,
    BM_286_PM_CALL_GATE, BM_286_PM_TASK_GATE,
    BM_286_PM_INTERRUPT_GATE, BM_286_PM_TRAP_GATE
} bm_286_pm_kind_t;

typedef struct bm_286_pm_selector {
    uint16_t index, table_offset;
    uint8_t rpl;
    bool local, null_selector;
} bm_286_pm_selector_t;

typedef struct bm_286_pm_descriptor {
    bm_286_pm_kind_t kind;
    uint32_t base;
    uint16_t limit, reserved;
    uint8_t access, dpl;
    bool present, readable, writable, expand_down, conforming, accessed;
} bm_286_pm_descriptor_t;

bm_286_pm_selector_t bm_286_pm_selector_decode(uint16_t raw);
/* bytes points to eight readable bytes, possibly unaligned. Gate payloads
 * are deliberately not decoded yet. Reserved bytes are diagnostic only. */
bm_286_pm_descriptor_t bm_286_pm_descriptor_decode(const uint8_t bytes[8]);
/* Interpret an already loaded cache, including the real-compatible 82h image.
 * This is NOT a table descriptor decode: table type 82h remains an LDT.
 * Segment must be non-NULL. Validation of usability/register role is separate. */
bm_286_pm_descriptor_t bm_286_cached_descriptor(const bm_286_segment_state_t *segment);
/* Bounds only: does NOT validate presence, privilege or access permissions.
 * descriptor must be non-NULL. Empty ranges are rejected. */
bool bm_286_pm_segment_contains(const bm_286_pm_descriptor_t *descriptor,
                                uint32_t offset, uint32_t length);

typedef enum bm_286_pm_lookup_reason {
    BM_286_PM_NOT_READ, BM_286_PM_FOUND, BM_286_PM_NULL_SELECTOR,
    BM_286_PM_NO_LDT, BM_286_PM_TABLE_LIMIT
} bm_286_pm_lookup_reason_t;

typedef struct bm_286_pm_lookup {
    bm_286_pm_lookup_reason_t reason;
    uint16_t selector_error; /* Selector with RPL cleared; caller supplies EXT. */
    uint64_t waits;         /* Successful transfers only, as in the CPU path. */
    uint8_t bytes[8];       /* Published only after all reads succeed. */
    bm_286_pm_descriptor_t descriptor;
} bm_286_pm_lookup_t;

/* Private lookup, not a segment load. BM_STATUS_OK may carry a selector/table
 * rejection: caller decides #GP/#TS/etc or a non-faulting query result.
 * Non-OK is a host error, never an architectural fault. No writes/LOCK/retry.
 * Cached LDTR.valid is authoritative; loading/validating LDTR is a later step.
 * result is required and cleared on entry; other inputs must not alias it. */
bm_status_t bm_286_pm_lookup_descriptor(const bm_286_table_state_t *gdt,
    const bm_286_segment_state_t *ldt, uint16_t selector,
    bm_bus_access_fn access, void *context, bm_286_pm_lookup_t *result);

typedef enum bm_286_pm_load_target {
    BM_286_PM_LOAD_DATA, /* Shared rules for DS and ES, not CS. */
    BM_286_PM_LOAD_STACK,
    BM_286_PM_LOAD_LDT
} bm_286_pm_load_target_t;

typedef struct bm_286_pm_load_plan {
    bool prepared;
    uint8_t fault_vector; /* 0, #NP 11, #SS 12 or #GP 13; not delivered here. */
    uint16_t fault_error;
    uint64_t waits;
    bm_286_segment_state_t segment;
    uint32_t access_address; /* Captured physical descriptor byte, before A20. */
    bool needs_accessed_write;
} bm_286_pm_load_plan_t;

/* Prepare only: reads descriptors but never writes memory or CPU registers.
 * Caller must perform required accessed writeback BEFORE committing segment.
 * Only ordinary DS/ES/SS loads and LLDT, NOT task switches or stack switches.
 * Inputs are non-aliasing; host errors leave prepared=false/fault_vector=0.
 * LLDT CPL check precedes lookup; operand-fetch precedence is caller-owned.
 * Null-cache base/limit/access zeroing is an internal unusable-cache policy. */
bm_status_t bm_286_pm_prepare_load(const bm_286_table_state_t *gdt,
    const bm_286_segment_state_t *ldt, uint16_t selector, uint8_t cpl,
    bm_286_pm_load_target_t target, bm_bus_access_fn access, void *context,
    bm_286_pm_load_plan_t *plan);

/* Consume a freshly prepared plan synchronously, without an intervening guest
 * boundary. Caller owns serialization and must not already hold bus_lock.
 * Commits destination only after the locked access-byte RMW succeeds. Always
 * releases an acquired lock, including host IDLE/error. Failed plans cannot be
 * replayed: external effects may already have happened. Inputs must not alias.
 * Null/LDTR loads require neither access nor lock callbacks. This is not an
 * instruction dispatcher and does not set the SS shadow or deliver faults. */
bm_status_t bm_286_pm_commit_load(bm_286_pm_load_plan_t *plan,
    bm_bus_access_fn access, void *context, bm_286_pin_fn bus_lock,
    void *pin_context, bm_286_segment_state_t *destination);

typedef struct bm_286_segment_load_result {
    uint64_t waits;
    uint16_t fault_error;
    uint8_t fault_vector;
    bool loaded;
    bool task_context; /* New task selected, including later guest faults. */
} bm_286_segment_load_result_t;

/* CPU-internal common load path: encoded register 0=ES, 2=SS, 3=DS, 4=LDTR.
 * No CS or task/privilege-switch loads. Instruction callers own operand reads,
 * SP/general-register commits, IP and SS shadow. No held LOCK is permitted on
 * entry to the protected path. No public protected-step bypass is provided. */
bm_status_t bm_286_load_segment_state(bm_286_arch_state_t *arch,
    const bm_286_config_t *config, unsigned reg, uint16_t selector,
    bm_286_segment_load_result_t *result);

/* Private far JMP. Ordinary direct code/call gates do not change CPL.
 * Conforming code ignores operand RPL, but CS RPL retains CPL. Validates type/privilege/presence/
 * IP, performs the existing locked A update, then commits CS/IP. Fault metadata
 * is separate from host status; caller owns unwind/stop and no replay. Inputs
 * non-aliasing, serialized, running PE context and no held bus lock. TSS/task
 * gates use task_286.h's context/commit contract; next_ip is the outgoing IP. */
bm_status_t bm_286_pm_jump(bm_286_arch_state_t *arch,
    const bm_286_config_t *config, uint16_t selector, uint16_t ip, uint16_t next_ip,
    bm_286_segment_load_result_t *result);

/* Private ordinary far CALL: direct same-CPL code or same/inner call gate.
 * return_ip is after the decoded instruction. Inner calls require a consistent
 * loaded busy 286 TR cache. E2b checks the complete requested TSS SS:SP slot
 * at use; missing TR/short slot yield #TS. Impossible cache encodings remain
 * host errors. SS:SP slots are read, never explicitly written.
 * Preflight all stack ranges before A updates/frame writes. Parameter copies
 * run backwards, interleaved with writes, after saving old SS:SP. CPU state
 * commits only on success; host failures retain effects and must not replay.
 * TSS/task gates use task_286.h's context/commit contract. No public PE activation. */
bm_status_t bm_286_pm_call(bm_286_arch_state_t *arch,
    const bm_286_config_t *config, uint16_t selector, uint16_t ip,
    uint16_t return_ip, bm_286_segment_load_result_t *result);

typedef enum bm_286_pm_query_kind {
    BM_286_PM_LAR, BM_286_PM_LSL, BM_286_PM_VERR, BM_286_PM_VERW
} bm_286_pm_query_kind_t;
typedef struct bm_286_pm_query_result {
    uint64_t waits;
    uint16_t value; /* LAR high access byte / LSL raw 16-bit limit. */
    bool accepted;  /* ZF outcome, NOT a guest fault or a segment load. */
} bm_286_pm_query_result_t;

/* Private read-only descriptor query. All CPLs; presence is not required.
 * Null, table bounds, unusable LDT, wrong type/visibility => accepted=false.
 * No guest exceptions, cache reload, A-bit update or lock. Caller handles
 * operand faults, real-mode #UD, FLAGS/destination commit and host-stop.
 * PRM-1987 profile: B-71's explicit nonconforming LSL condition rejects
 * conforming code; B-60/11.3's LAR descriptor rule includes defined gates.
 * See pcs286-protected-instruction-policy.md for source precedence/limits.
 * Non-aliasing serialized inputs, running consistent PE state. */
bm_status_t bm_286_pm_query(const bm_286_arch_state_t *arch,
    const bm_286_config_t *config, bm_286_pm_query_kind_t kind,
    uint16_t selector, bm_286_pm_query_result_t *result);

/* Private LTR (PRM 10.2/B-72). PE/CPL0; global non-null available TSS,
 * then presence. Locked busy test/set precedes TR commit. Does not inspect
 * TSS contents/limit, change MSW.TS/NT, clear old busy or switch tasks.
 * Caller owns operand checks and host-stop/no replay; no caller-held LOCK. */
bm_status_t bm_286_pm_ltr(bm_286_arch_state_t *arch,
    const bm_286_config_t *config, uint16_t selector,
    bm_286_segment_load_result_t *result);

typedef struct bm_286_pm_event {
    uint16_t return_ip, error_code;
    uint8_t vector;
    bool software, external, has_error;
} bm_286_pm_event_t;

/* Private same/inner-CPL entry. result.loaded means entered; a returned fault is
 * metadata for future escalation, NOT recursively delivered here. Caller owns
 * event sampling, INTA, NMI blocking and instruction unwind. No LOCK on entry.
 * Host failures retain CPU state but not external memory effects; no replay.
 * Caller supplies a running (not shutdown), internally consistent cached state;
 * inputs must not alias. Inner entry selects TSS SS:SP and adds old SS:SP to
 * the frame; task gates use task_286.h and may publish a new context on a
 * later guest fault. Public PE stays gated. */
bm_status_t bm_286_pm_enter_event(bm_286_arch_state_t *arch,
    const bm_286_config_t *config, const bm_286_pm_event_t *event,
    bm_286_segment_load_result_t *result);

/* Private ordinary same/outer-CPL IRET, Intel 286 PRM B-51/B-52 and 10.1.
 * result.loaded means returned; guest faults are metadata, not delivered.
 * Current NT selects backlink task return under task_286.h, ignoring SS:SP;
 * next_ip is saved in the outgoing TSS. For ordinary return the handler must
 * already have removed any error code. Requires consistent protected state,
 * serialized non-aliasing inputs and no held LOCK, as for event entry.
 * CS/IP/SP/FLAGS and NMI unblock commit only on success. Failed host transfers
 * retain their status and external effects; the caller must stop, never retry.
 * Successful return preserves pending NMI/trap and the interrupt shadow:
 * instruction-boundary sampling/consumption belongs to the future dispatcher.
 * Faulting-IRET NMI unblock ordering is not certified by this private helper.
 * Outer returns stage SS/CPL and invalidate inaccessible cached DS/ES. The
 * new SP is checked only when used. This is not a public PE or task bypass. */
bm_status_t bm_286_pm_iret(bm_286_arch_state_t *arch,
    const bm_286_config_t *config, uint16_t next_ip, bm_286_segment_load_result_t *result);

/* Private same/outer-CPL RETF (B-94/B-95). Same host/commit contract as IRET,
 * but FLAGS/NT/NMI blocking are unchanged. discard removes bytes on both
 * inner and resumed stacks. Same-level RETF has its own check precedence. */
bm_status_t bm_286_pm_retf(bm_286_arch_state_t *arch,
    const bm_286_config_t *config, uint16_t discard, bm_286_segment_load_result_t *result);

typedef enum bm_286_pm_source {
    BM_286_PM_EXCEPTION, /* Already detected cause; caller has unwound instruction. */
    BM_286_PM_SOFTWARE,  /* INT/INT3/INTO; vector alone never implies an error word. */
    BM_286_PM_BOUNDARY   /* Sample #1, NMI, #9, then INTR; no instruction fetch. */
} bm_286_pm_source_t;

typedef struct bm_286_pm_request {
    bm_286_pm_source_t source;
    uint8_t vector;       /* Ignored for BOUNDARY; INTR vector comes from INTA. */
    uint16_t error_code;  /* Only explicit #TS/#NP/#SS/#GP use this word. */
    uint16_t restart_ip;  /* First prefix for a fault/failed software interrupt. */
    uint16_t next_ip;     /* Completed software interrupt. */
    bool intr_line;       /* Sampled input, never acknowledged just by observing it. */
    bool extension_overrun; /* Caller-owned pending #9; synthetic until NPX exists. */
    bool task_fault;      /* Fault follows a task switch with partially loaded caches. */
    /* D9 fault-image corrections, staged in the outgoing context before a
     * task save, or committed with ordinary delivery. Never change the new task. */
    uint8_t string_fault_adjust; /* SI=1, DI=2, CX decrement<<2. */
    uint16_t string_delta;
} bm_286_pm_request_t;

typedef struct bm_286_pm_delivery_state {
    /* Instance-owned host-error latch. Zero initially; clear only on CPU reset
     * or explicit stopped-state import, never to retry a failed transfer. */
    bool stopped;
} bm_286_pm_delivery_state_t;

typedef struct bm_286_pm_delivery_result {
    uint64_t waits;
    uint8_t attempts, vectors[32];
    uint16_t errors[32];
    bool has_error[32];
    bool accepted, entered, shutdown;
    uint8_t vector; /* Delivered vector only when entered, never on shutdown. */
} bm_286_pm_delivery_result_t;

/* Private bounded protected delivery, not an enabled instruction dispatcher.
 * EXCEPTION accepts 0,5,6,7,8,10,11,12,13,16; other causes are UNSUPPORTED.
 * #9 is asynchronous and only selected at BOUNDARY, below NMI and above INTR.
 * Its caller-owned pending indication is consumed only if accepted with
 * vectors[0]==9. #9/#16 metadata creates no coprocessor/signal interface.
 * Task gates, including #DF/NMI recovery, use the shared task mechanism.
 * BOUNDARY respects sampled TF, SS/STI shadows and NMI blocking, then performs
 * exactly two INTA phases if INTR is accepted. No event returns IDLE without
 * consuming shadows. Caller owns HOLD, instruction unwind and TF sampling.
 * Within a context: original -> protection fault -> #DF; guest failure before
 * selecting a new task for #DF asserts shutdown. Selected-task faults belong
 * to that new context. A 32-attempt diagnostic bound returns host UNSUPPORTED
 * and stops without inventing guest shutdown. In shutdown only NMI may enter; a
 * guest failure leaves it blocked until reset. Host errors are never escalated.
 * CPU frame commits are staged; accepted NMI edges and completed endpoint
 * effects have explicit ownership. New NMI edges from callbacks survive.
 * Non-OK after acceptance latches stopped, releases LOCK and forbids replay.
 * Requires consistent protected state, non-aliasing serialized inputs, no
 * caller-owned LOCK. Existing public/internal PE gates are NOT changed. */
bm_status_t bm_286_pm_deliver(bm_286_arch_state_t *arch,
    const bm_286_config_t *config, bm_286_pm_delivery_state_t *state,
    const bm_286_pm_request_t *request, bm_286_pm_delivery_result_t *result);

#endif
