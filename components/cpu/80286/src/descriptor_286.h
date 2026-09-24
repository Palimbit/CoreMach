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
} bm_286_segment_load_result_t;

/* CPU-internal common load path: encoded register 0=ES, 2=SS, 3=DS, 4=LDTR.
 * No CS or task/privilege-switch loads. Instruction callers own operand reads,
 * SP/general-register commits, IP and SS shadow. No held LOCK is permitted on
 * entry to the protected path. No public protected-step bypass is provided. */
bm_status_t bm_286_load_segment_state(bm_286_arch_state_t *arch,
    const bm_286_config_t *config, unsigned reg, uint16_t selector,
    bm_286_segment_load_result_t *result);

typedef struct bm_286_pm_event {
    uint16_t return_ip, error_code;
    uint8_t vector;
    bool software, external, has_error;
} bm_286_pm_event_t;

/* Private same-CPL entry. result.loaded means entered; a returned fault is
 * metadata for future escalation, NOT recursively delivered here. Caller owns
 * event sampling, INTA, NMI blocking and instruction unwind. No LOCK on entry.
 * Host failures retain CPU state but not external memory effects; no replay.
 * Caller supplies a running (not shutdown), internally consistent cached state;
 * inputs must not alias. Task/inner transfers return UNSUPPORTED. Public PE gated. */
bm_status_t bm_286_pm_enter_event(bm_286_arch_state_t *arch,
    const bm_286_config_t *config, const bm_286_pm_event_t *event,
    bm_286_segment_load_result_t *result);

#endif
