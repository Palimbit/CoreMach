/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 BluMach contributors
 */
#ifndef BM_TASK_286_H
#define BM_TASK_286_H
#include "descriptor_286.h"

typedef enum bm_286_task_kind {
    BM_286_TASK_CALL, BM_286_TASK_JUMP, BM_286_TASK_RETURN
} bm_286_task_kind_t;
typedef enum bm_286_task_phase {
    BM_286_TASK_OLD, BM_286_TASK_SELECTED, BM_286_TASK_REGISTERS,
    BM_286_TASK_LDT, BM_286_TASK_STACK, BM_286_TASK_CODE,
    BM_286_TASK_DATA, BM_286_TASK_COMPLETE
} bm_286_task_phase_t;
typedef struct bm_286_task_request {
    bm_286_task_kind_t kind;
    uint16_t selector, return_ip, error_code;
    bool direct, external, has_error, event;
} bm_286_task_request_t;
typedef struct bm_286_task_result {
    bm_286_arch_state_t candidate;
    uint64_t waits;
    uint16_t fault_error;
    uint8_t fault_vector;
    bm_286_task_phase_t phase;
} bm_286_task_result_t;

/* Private SWITCH_TASKS mechanism, PRM-1987 B12/B13 and table8-2. Caller
 * validates a gate before passing its TSS selector. RETURN reads the backlink.
 * direct enables CALL/JMP TSS DPL/RPL checks; gated targets ignore these.
 * OLD faults leave the task unchanged; later faults belong to candidate.
 * Host non-OK never authorizes candidate publication, but memory effects stand.
 * No replay after any failed callback. Non-aliasing, serialized inputs, no
 * caller-owned LOCK except through a depth-aware delivery adapter. Public
 * execution remains gated. Phase/bus sequencing is explicit functional policy. */
bm_status_t bm_286_pm_switch_task(const bm_286_arch_state_t *arch,
    const bm_286_config_t *config, const bm_286_task_request_t *request,
    bm_286_task_result_t *result);

/* Publish task-owned fields only: asynchronous signals are not TSS state. */
void bm_286_pm_publish_task(bm_286_arch_state_t *arch,
    const bm_286_arch_state_t *candidate);
#endif
