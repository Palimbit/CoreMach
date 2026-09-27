/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 BluMach contributors
 * Intel 80286/80287 PRM 1987, chapters 9/10 and INT/IRET in appendix B.
 * Functional ordinary/task transfers and protected events, not physical bus timing.
 */
#include "access_286.h"
#include "task_286.h"
#include <string.h>

static bm_status_t word_transfer(const bm_286_config_t *config, uint32_t address,
    bool write, uint16_t *value, uint64_t *waits)
{
    unsigned pos, size = (address & 1u) ? 1u : 2u;
    uint16_t read_value = 0;
    for (pos = 0; pos < 2; pos += size) {
        bm_bus_transaction_t t = {0};
        bm_status_t status;
        t.address = (address + pos) & 0xffffffu;
        t.space = BM_ADDRESS_DATA;
        t.operation = write ? BM_BUS_WRITE : BM_BUS_READ;
        t.size = t.alignment = size;
        t.endianness = BM_ENDIAN_LITTLE;
        t.value = write ? (size == 2 ? *value : (uint8_t)(*value >> (pos * 8u))) : 0;
        status = config->access(config->access_context, &t);
        if (status != BM_STATUS_OK) return status;
        *waits += t.wait_states;
        if (!write) read_value |= (uint16_t)((size == 2 ? (uint16_t)t.value : (uint8_t)t.value) << (pos * 8u));
    }
    if (!write) *value = read_value;
    return BM_STATUS_OK;
}

static bm_status_t reject(bm_286_segment_load_result_t *r, uint8_t vector, uint16_t error)
{
    r->fault_vector = vector; r->fault_error = error;
    return BM_STATUS_OK;
}

static bm_status_t check_stack(const bm_286_arch_state_t *arch,
    bm_286_pm_access_kind_t kind, uint32_t offset, uint32_t length,
    bm_286_segment_load_result_t *result)
{
    bm_286_pm_access_check_t check;
    bm_status_t status = bm_286_pm_check_access(arch, 2, kind, offset, length, &check);
    if (status == BM_STATUS_OK && !check.allowed)
        return reject(result, check.fault_vector, 0);
    return status;
}

static bm_286_pm_load_plan_t code_or_stack_plan(const bm_286_arch_state_t *arch,
    uint16_t selector, uint16_t visible, const bm_286_pm_descriptor_t *d)
{
    bm_286_pm_load_plan_t p = {0};
    p.prepared = true; p.needs_accessed_write = true;
    p.access_address = (((selector & 4u) ? arch->ldtr.base : arch->gdtr.base) +
        (selector & 0xfff8u) + 5u) & 0xffffffu;
    p.segment.selector = visible; p.segment.base = d->base;
    p.segment.limit = d->limit; p.segment.access = d->access; p.segment.valid = 1;
    return p;
}

/* Shared ordinary inner-stack selection (CALL B-25 / INT B-49, figure 8-1).
 * LTR does not validate TSS contents. Check the complete requested SS:SP slot
 * at use, not the 43-byte incoming-task minimum from SWITCH_TASKS. This bounded
 * use-time #TS policy is explicit in the E2b contract; no later-x86 fields.
 * Impossible loaded cache encodings are host errors, never guest exceptions. */
static bm_status_t inner_stack(const bm_286_arch_state_t *arch,
    const bm_286_config_t *config, uint8_t cpl, bool external,
    bm_286_pm_load_plan_t *plan, uint16_t *sp, bm_286_segment_load_result_t *r)
{
    bm_286_pm_lookup_t stack;
    uint16_t selector = 0, ext = external ? 1u : 0u;
    uint32_t slot = 2u + 4u * cpl;
    bm_status_t status;
    if (cpl >= arch->cpl || arch->tr.valid > 1 || arch->tr.base > 0xffffffu)
        return BM_STATUS_INVALID_STATE;
    if (!arch->tr.valid) return reject(r, 10, (uint16_t)((arch->tr.selector & 0xfffcu) | ext));
    if ((arch->tr.selector & 0xfffcu) == 0 || (arch->tr.selector & 4u) ||
        (arch->tr.access & 0x9fu) != 0x83u) return BM_STATUS_INVALID_STATE;
    if (slot + 3u > arch->tr.limit)
        return reject(r, 10, (uint16_t)((arch->tr.selector & 0xfffcu) | ext));
    status = word_transfer(config, arch->tr.base + slot + 2u, false, &selector, &r->waits);
    if (status != BM_STATUS_OK) return status;
    status = bm_286_pm_lookup_descriptor(&arch->gdtr, &arch->ldtr, selector,
        config->access, config->access_context, &stack);
    r->waits += stack.waits;
    if (status != BM_STATUS_OK) return status;
    if (stack.reason != BM_286_PM_FOUND || (selector & 3u) != cpl ||
        stack.descriptor.kind != BM_286_PM_DATA || !stack.descriptor.writable ||
        stack.descriptor.dpl != cpl) return reject(r, 10, (uint16_t)(stack.selector_error | ext));
    if (!stack.descriptor.present) return reject(r, 12, (uint16_t)(stack.selector_error | ext));
    status = word_transfer(config, arch->tr.base + slot, false, sp, &r->waits);
    if (status != BM_STATUS_OK) return status;
    *plan = code_or_stack_plan(arch, selector, selector, &stack.descriptor);
    return BM_STATUS_OK;
}

/* CALL/JMP B-24/B-25/B-57; 7.5.1 and figure 8-1.
 * Bus order and staged CPU commit are functional policy, not silicon timing. */
static bm_status_t task_transfer(bm_286_arch_state_t *arch,const bm_286_config_t *config,
    const bm_286_task_request_t *request,bm_286_segment_load_result_t *result)
{
    bm_286_task_result_t task;
    bm_status_t status=bm_286_pm_switch_task(arch,config,request,&task);
    result->waits+=task.waits;
    if(status!=BM_STATUS_OK) return status;
    result->fault_vector=task.fault_vector; result->fault_error=task.fault_error;
    result->task_context=task.phase!=BM_286_TASK_OLD;
    result->loaded=task.phase==BM_286_TASK_COMPLETE;
    if(result->task_context) bm_286_pm_publish_task(arch,&task.candidate);
    if(result->loaded && request->event) arch->halted=0;
    if(result->loaded && request->kind==BM_286_TASK_RETURN) arch->nmi_blocked=0;
    return BM_STATUS_OK;
}

static bm_status_t protected_transfer(bm_286_arch_state_t *arch,
    const bm_286_config_t *config, uint16_t selector, uint16_t ip,
    bool call, uint16_t return_ip, bm_286_segment_load_result_t *result)
{
    bm_286_pm_lookup_t selected, code;
    bm_286_pm_load_plan_t cs_plan, ss_plan = {0};
    bm_286_segment_state_t new_cs = {0}, new_ss = {0};
    uint16_t sp, value;
    uint32_t top, frame = 4, stack_base;
    uint8_t cpl;
    unsigned count = 0, i;
    bool inner = false, gate;
    bm_status_t status;
    if (!result) return BM_STATUS_INVALID_ARGUMENT;
    memset(result, 0, sizeof(*result));
    if (!arch || !config || !config->access) return BM_STATUS_INVALID_ARGUMENT;
    if (!(arch->msw & 1u) || arch->cpl > 3 || arch->halted || arch->shutdown)
        return BM_STATUS_INVALID_STATE;
    cpl = arch->cpl; sp = arch->sp; stack_base = arch->ss.base;
    status = bm_286_pm_lookup_descriptor(&arch->gdtr, &arch->ldtr, selector,
        config->access, config->access_context, &selected);
    result->waits = selected.waits;
    if (status != BM_STATUS_OK) return status;
    if (selected.reason != BM_286_PM_FOUND) return reject(result, 13, selected.selector_error);
    if (selected.descriptor.kind == BM_286_PM_TASK_GATE ||
        selected.descriptor.kind == BM_286_PM_TSS_AVAILABLE || selected.descriptor.kind == BM_286_PM_TSS_BUSY) {
        bm_286_task_request_t request={0};
        request.kind=call?BM_286_TASK_CALL:BM_286_TASK_JUMP;
        request.return_ip=return_ip; request.selector=selector;
        request.direct=selected.descriptor.kind!=BM_286_PM_TASK_GATE;
        if(!request.direct) {
            if(selected.descriptor.dpl<cpl || selected.descriptor.dpl<(selector&3u))
                return reject(result,13,selected.selector_error);
            if(!selected.descriptor.present) return reject(result,11,selected.selector_error);
            request.selector=(uint16_t)(selected.bytes[2]|((uint16_t)selected.bytes[3]<<8));
        }
        return task_transfer(arch,config,&request,result);
    }
    gate = selected.descriptor.kind == BM_286_PM_CALL_GATE;
    if (gate) {
        if (selected.descriptor.dpl < cpl || selected.descriptor.dpl < (selector & 3u))
            return reject(result, 13, selected.selector_error);
        if (!selected.descriptor.present) return reject(result, 11, selected.selector_error);
        selector = (uint16_t)(selected.bytes[2] | ((uint16_t)selected.bytes[3] << 8));
        ip = (uint16_t)(selected.bytes[0] | ((uint16_t)selected.bytes[1] << 8));
        count = selected.bytes[4] & 31u; /* Reserved bits have no 386 meaning. */
        status = bm_286_pm_lookup_descriptor(&arch->gdtr, &arch->ldtr, selector,
            config->access, config->access_context, &code);
        result->waits += code.waits;
        if (status != BM_STATUS_OK) return status;
        if (code.reason != BM_286_PM_FOUND || code.descriptor.kind != BM_286_PM_CODE ||
            code.descriptor.dpl > cpl ||
            (!call && !code.descriptor.conforming && code.descriptor.dpl != cpl))
            return reject(result, 13, code.selector_error);
        /* Table 7-3 supplies the code-presence check omitted in CALL B-24. */
    } else {
        code = selected;
        if (code.descriptor.kind != BM_286_PM_CODE ||
            (code.descriptor.conforming ? code.descriptor.dpl > cpl :
             code.descriptor.dpl != cpl || (selector & 3u) > cpl))
            return reject(result, 13, code.selector_error);
    }
    if (!code.descriptor.present) return reject(result, 11, code.selector_error);
    inner = call && gate && !code.descriptor.conforming && code.descriptor.dpl < cpl;
    if (inner) {
        bm_286_pm_descriptor_t stack;
        cpl = code.descriptor.dpl;
        status = inner_stack(arch, config, cpl, false, &ss_plan, &sp, result);
        if (status != BM_STATUS_OK || result->fault_vector) return status;
        stack = bm_286_cached_descriptor(&ss_plan.segment);
        frame = 8u + 2u * count; top = sp ? sp : 65536u;
        if (top < frame || !bm_286_pm_segment_contains(&stack, top - frame, frame))
            return reject(result, 12, 0);
        /* The source is a complete protected range too, never wrapped. For
         * count zero there is no old-stack access or corresponding limit test. */
        if (count) {
            status = check_stack(arch, BM_286_PM_READ, arch->sp, 2u * count, result);
            if (status != BM_STATUS_OK || result->fault_vector) return status;
        }
        stack_base = stack.base;
    } else if (call) {
        top = sp ? sp : 65536u;
        if (top < frame) return reject(result, 12, 0);
        status = check_stack(arch, BM_286_PM_WRITE, top - frame, frame, result);
        if (status != BM_STATUS_OK || result->fault_vector) return status;
    }
    if (ip > code.descriptor.limit) return reject(result, 13, 0);
    cs_plan = code_or_stack_plan(arch, selector, (uint16_t)((selector & 0xfffcu) | cpl),
        &code.descriptor);
    status = bm_286_pm_commit_load(&cs_plan, config->access, config->access_context,
        config->bus_lock, config->pin_context, &new_cs);
    result->waits += cs_plan.waits;
    if (status != BM_STATUS_OK) return status;
    if (inner) {
        status = bm_286_pm_commit_load(&ss_plan, config->access, config->access_context,
            config->bus_lock, config->pin_context, &new_ss);
        result->waits += ss_plan.waits;
        if (status != BM_STATUS_OK) return status;
        for (i = 0; i < 2; ++i) {
            value = i ? arch->sp : arch->ss.selector; sp = (uint16_t)(sp - 2u);
            status = word_transfer(config, stack_base + sp, true, &value, &result->waits);
            if (status != BM_STATUS_OK) return status;
        }
        /* Descending copy keeps argument zero nearest the return address.
         * Preserve read/write interleaving when physical stack aliases exist. */
        for (i = count; i > 0; --i) {
            status = word_transfer(config, arch->ss.base + arch->sp + 2u * (i - 1u),
                false, &value, &result->waits);
            if (status != BM_STATUS_OK) return status;
            sp = (uint16_t)(sp - 2u);
            status = word_transfer(config, stack_base + sp, true, &value, &result->waits);
            if (status != BM_STATUS_OK) return status;
        }
    }
    if (call) {
        for (i = 0; i < 2; ++i) {
            value = i ? return_ip : arch->cs.selector; sp = (uint16_t)(sp - 2u);
            status = word_transfer(config, stack_base + sp, true, &value, &result->waits);
            if (status != BM_STATUS_OK) return status;
        }
        arch->sp = sp;
    }
    if (inner) arch->ss = new_ss;
    arch->cs = new_cs; arch->ip = ip; arch->cpl = cpl;
    result->loaded = true;
    return BM_STATUS_OK;
}

bm_status_t bm_286_pm_jump(bm_286_arch_state_t *arch,
    const bm_286_config_t *config, uint16_t selector, uint16_t ip, uint16_t next_ip,
    bm_286_segment_load_result_t *result)
{
    return protected_transfer(arch, config, selector, ip, false, next_ip, result);
}

bm_status_t bm_286_pm_call(bm_286_arch_state_t *arch,
    const bm_286_config_t *config, uint16_t selector, uint16_t ip,
    uint16_t return_ip, bm_286_segment_load_result_t *result)
{
    return protected_transfer(arch, config, selector, ip, true, return_ip, result);
}

static bm_status_t enter_event(bm_286_arch_state_t *arch,
    const bm_286_config_t *config, const bm_286_pm_event_t *event,
    bm_286_segment_load_result_t *result, bool *inta_locked, bool task_fault)
{
    uint8_t bytes[8];
    bm_286_pm_descriptor_t gate, stack;
    bm_286_pm_lookup_t code;
    bm_286_pm_load_plan_t plan = {0}, ss_plan = {0};
    bm_286_segment_state_t new_cs = {0}, new_ss = {0};
    uint16_t selector, ip, words[6], sp;
    uint32_t offset, top, frame, stack_base;
    uint16_t gate_error, code_error;
    uint8_t cpl;
    bool inner;
    unsigned i, count;
    bm_status_t status;
    if (result == NULL) return BM_STATUS_INVALID_ARGUMENT;
    memset(result, 0, sizeof(*result));
    if (arch == NULL || config == NULL || event == NULL || config->access == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    if (!(arch->msw & 1u) || arch->shutdown || arch->cpl > 3 || arch->idtr.base > 0xffffffu)
        return BM_STATUS_INVALID_STATE;
    offset = (uint32_t)event->vector * 8u;
    gate_error = (uint16_t)(offset | 2u | (event->external ? 1u : 0u));
    if (offset + 7u > arch->idtr.limit) return reject(result, 13, gate_error);
    for (i = 0; i < 8; i += 2) {
        uint16_t word = 0;
        status = word_transfer(config, arch->idtr.base + offset + i, false, &word, &result->waits);
        if (status != BM_STATUS_OK) return status;
        bytes[i] = (uint8_t)word; bytes[i + 1] = (uint8_t)(word >> 8);
    }
    gate = bm_286_pm_descriptor_decode(bytes);
    if (gate.kind != BM_286_PM_INTERRUPT_GATE && gate.kind != BM_286_PM_TRAP_GATE &&
        gate.kind != BM_286_PM_TASK_GATE) return reject(result, 13, gate_error);
    if (event->software && gate.dpl < arch->cpl) return reject(result, 13, gate_error);
    if (!gate.present) return reject(result, 11, gate_error);
    selector = (uint16_t)((uint16_t)bytes[2] | ((uint16_t)bytes[3] << 8));
    if (gate.kind == BM_286_PM_TASK_GATE) {
        bm_286_task_request_t request={0};
        request.kind=BM_286_TASK_CALL; request.selector=selector; request.return_ip=event->return_ip;
        request.event=true;
        request.external=event->external; request.has_error=event->has_error; request.error_code=event->error_code;
        return task_transfer(arch,config,&request,result);
    }
    ip = (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
    status = bm_286_pm_lookup_descriptor(&arch->gdtr, &arch->ldtr, selector,
        config->access, config->access_context, &code);
    result->waits += code.waits;
    if (status != BM_STATUS_OK) return status;
    code_error = (uint16_t)(code.selector_error | (event->external ? 1u : 0u));
    if (code.reason != BM_286_PM_FOUND || code.descriptor.kind != BM_286_PM_CODE ||
        code.descriptor.dpl > arch->cpl) return reject(result, 13, code_error);
    if (!code.descriptor.present) return reject(result, 11, code_error);
    inner = !code.descriptor.conforming && code.descriptor.dpl < arch->cpl;
    cpl = inner ? code.descriptor.dpl : arch->cpl;
    count = event->has_error ? 4u : 3u;
    sp = arch->sp; stack_base = arch->ss.base;
    if (inner) {
        status = inner_stack(arch, config, cpl, event->external, &ss_plan, &sp, result);
        if (status != BM_STATUS_OK || result->fault_vector) return status;
        stack = bm_286_cached_descriptor(&ss_plan.segment);
        stack_base = stack.base; count += 2;
    } else {
        /* No SS reload for same-level/conforming entry. */
        if (!arch->ss.valid && task_fault)
            return reject(result,10,(uint16_t)((arch->ss.selector&0xfffcu)|(event->external?1u:0u)));
        if (!arch->ss.valid || arch->ss.base > 0xffffffu) return BM_STATUS_INVALID_STATE;
        stack = bm_286_cached_descriptor(&arch->ss);
        if (stack.kind != BM_286_PM_DATA || !stack.writable || !stack.present ||
            stack.dpl != arch->cpl ||
            (arch->ss.access != 0x82 && (arch->ss.selector & 3u) != arch->cpl))
            return BM_STATUS_INVALID_STATE;
    }
    frame = count * 2u; top = sp ? sp : 65536u;
    if (top < frame) return reject(result, 12, 0);
    if (!bm_286_pm_segment_contains(&stack, top - frame, frame)) return reject(result, 12, 0);
    if (ip > code.descriptor.limit) return reject(result, 13, 0);
    /* Preflight before any write; accessed update precedes frame writes as
     * functional policy. External partial writes survive transport errors. */
    plan.prepared = true; plan.needs_accessed_write = true;
    plan.access_address = (((selector & 4u) ? arch->ldtr.base : arch->gdtr.base) +
        (selector & 0xfff8u) + 5u) & 0xffffffu;
    plan.segment.selector = (uint16_t)((selector & 0xfffcu) | cpl);
    plan.segment.base = code.descriptor.base; plan.segment.limit = code.descriptor.limit;
    plan.segment.access = code.descriptor.access; plan.segment.valid = 1;
    status = bm_286_pm_commit_load(&plan, config->access, config->access_context,
        config->bus_lock, config->pin_context, &new_cs);
    result->waits += plan.waits;
    if (status != BM_STATUS_OK) return status;
    if (inner) {
        status = bm_286_pm_commit_load(&ss_plan, config->access, config->access_context,
            config->bus_lock, config->pin_context, &new_ss);
        result->waits += ss_plan.waits;
        if (status != BM_STATUS_OK) return status;
        words[0] = arch->ss.selector; words[1] = arch->sp;
    }
    i = inner ? 2u : 0u;
    words[i] = arch->flags; words[i + 1] = arch->cs.selector;
    words[i + 2] = event->return_ip; words[i + 3] = event->error_code;
    for (i = 0; i < count; ++i) {
        sp = (uint16_t)(sp - 2u);
        status = word_transfer(config, stack_base + sp, true, &words[i], &result->waits);
        if (status != BM_STATUS_OK) return status;
        if (i == 0 && inta_locked != NULL && *inta_locked) {
            config->bus_lock(config->pin_context, 0);
            *inta_locked = false;
        }
    }
    if (inner) arch->ss = new_ss;
    arch->cs = new_cs; arch->ip = ip; arch->sp = sp; arch->cpl = cpl;
    arch->flags &= (uint16_t)~(0x0100u | 0x4000u |
        (gate.kind == BM_286_PM_INTERRUPT_GATE ? 0x0200u : 0u));
    arch->halted = 0; arch->trap_pending = 0; arch->interrupt_shadow = BM_286_SHADOW_NONE;
    result->loaded = true;
    return BM_STATUS_OK;
}

bm_status_t bm_286_pm_enter_event(bm_286_arch_state_t *arch,
    const bm_286_config_t *config, const bm_286_pm_event_t *event,
    bm_286_segment_load_result_t *result)
{
    return enter_event(arch, config, event, result, NULL, false);
}

/* Return cleanup uses cached access rights, not a descriptor reload. Table
 * bounds still apply to the visible selector (PRM B-52/B-95). Enforce both
 * CPL and RPL for ordinary data/nonconforming code, as in 7.3/7.4: the printed
 * "or" cannot permit an outer caller to retain an inner data cache. */
static void outer_data_cache(bm_286_segment_state_t *segment,
    const bm_286_arch_state_t *arch, uint8_t new_cpl)
{
    bm_286_pm_selector_t s = bm_286_pm_selector_decode(segment->selector);
    bm_286_pm_descriptor_t d = bm_286_cached_descriptor(segment);
    uint32_t limit = s.local ? arch->ldtr.limit : arch->gdtr.limit;
    if (!segment->valid || s.null_selector || (s.local && !arch->ldtr.valid) ||
        (uint32_t)s.table_offset + 7u > limit ||
        (d.kind != BM_286_PM_DATA && !(d.kind == BM_286_PM_CODE && d.readable)) ||
        (!d.conforming && (d.dpl < new_cpl || d.dpl < s.rpl)))
        memset(segment, 0, sizeof(*segment));
}

static bm_status_t protected_return(bm_286_arch_state_t *arch,
    const bm_286_config_t *config, bool iret, uint16_t discard,
    bm_286_segment_load_result_t *result)
{
    bm_286_pm_descriptor_t stack;
    bm_286_pm_selector_t selected;
    bm_286_pm_lookup_t code, outer_stack = {0};
    bm_286_pm_load_plan_t plan = {0}, stack_plan = {0};
    bm_286_segment_state_t new_cs, new_ss;
    uint16_t selector = 0, ip = 0, flags = 0, mask = 0x4fd5u;
    uint16_t stack_selector = 0, new_sp = 0;
    uint32_t frame, stack_offset;
    bool outer;
    bm_status_t status;
    if (result == NULL) return BM_STATUS_INVALID_ARGUMENT;
    memset(result, 0, sizeof(*result));
    if (arch == NULL || config == NULL || config->access == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    if (!(arch->msw & 1u) || arch->shutdown || arch->halted || arch->cpl > 3)
        return BM_STATUS_INVALID_STATE;
    /* Current-NT IRET is dispatched to task_transfer by bm_286_pm_iret. */
    if (!arch->ss.valid || arch->ss.base > 0xffffffu)
        return BM_STATUS_INVALID_STATE;
    stack = bm_286_cached_descriptor(&arch->ss);
    if (stack.kind != BM_286_PM_DATA || !stack.writable || !stack.present ||
        stack.dpl != arch->cpl ||
        (arch->ss.access != 0x82 && (arch->ss.selector & 3u) != arch->cpl))
        return BM_STATUS_INVALID_STATE;
    /* B-52 first checks the second stack word and RPL, THEN the six-byte
     * same-level frame. Widen before adding: no wrapped protected frame. */
    status = check_stack(arch, BM_286_PM_READ, (uint32_t)arch->sp + 2u, 2, result);
    if (status != BM_STATUS_OK || result->fault_vector) return status;
    status = word_transfer(config, arch->ss.base + arch->sp + 2u,
        false, &selector, &result->waits);
    if (status != BM_STATUS_OK) return status;
    selected = bm_286_pm_selector_decode(selector);
    if (selected.rpl < arch->cpl)
        return reject(result, 13, (uint16_t)(selector & 0xfffcu));
    outer = selected.rpl > arch->cpl;
    frame = iret ? (outer ? 10u : 6u) : 8u + discard;
    /* Same-level RETF checks its top word only after code type/presence.
     * IRET and outer RETF validate the complete frame first. */
    if (iret || outer) {
        status = check_stack(arch, BM_286_PM_READ, arch->sp, frame, result);
        if (status != BM_STATUS_OK || result->fault_vector) return status;
    }
    status = bm_286_pm_lookup_descriptor(&arch->gdtr, &arch->ldtr, selector,
        config->access, config->access_context, &code);
    result->waits += code.waits;
    if (status != BM_STATUS_OK) return status;
    if (code.reason != BM_286_PM_FOUND || code.descriptor.kind != BM_286_PM_CODE ||
        /* 11.2.1 and RET B-95 specify DPL <= return RPL for conforming
         * returns, clarifying IRET B-52's inconsistent outer-CPL inequality. */
        (code.descriptor.conforming ? code.descriptor.dpl > selected.rpl :
                                     code.descriptor.dpl != selected.rpl))
        return reject(result, 13, code.selector_error);
    if (!code.descriptor.present) return reject(result, 11, code.selector_error);
    if (outer) {
        stack_offset = (uint32_t)arch->sp + (iret ? 8u : 6u + discard);
        status = word_transfer(config, arch->ss.base + stack_offset,
            false, &stack_selector, &result->waits);
        if (status != BM_STATUS_OK) return status;
        status = bm_286_pm_lookup_descriptor(&arch->gdtr, &arch->ldtr, stack_selector,
            config->access, config->access_context, &outer_stack);
        result->waits += outer_stack.waits;
        if (status != BM_STATUS_OK) return status;
        if (outer_stack.reason != BM_286_PM_FOUND ||
            (stack_selector & 3u) != selected.rpl ||
            outer_stack.descriptor.kind != BM_286_PM_DATA || !outer_stack.descriptor.writable ||
            outer_stack.descriptor.dpl != selected.rpl)
            return reject(result, 13, outer_stack.selector_error);
        if (!outer_stack.descriptor.present) return reject(result, 12, outer_stack.selector_error);
    } else if (!iret) {
        status = check_stack(arch, BM_286_PM_READ, arch->sp, 2, result);
        if (status != BM_STATUS_OK || result->fault_vector) return status;
    }
    status = word_transfer(config, arch->ss.base + arch->sp, false, &ip, &result->waits);
    if (status != BM_STATUS_OK) return status;
    if (ip > code.descriptor.limit) return reject(result, 13, 0);
    if (outer) {
        status = word_transfer(config, arch->ss.base + (uint32_t)arch->sp +
            (iret ? 6u : 4u + discard), false, &new_sp, &result->waits);
        if (status != BM_STATUS_OK) return status;
        /* 7.5.2: RET discards the parameter bytes on both stacks. The new
         * SP itself is not range-checked until a later stack access. */
        if (!iret) new_sp = (uint16_t)(new_sp + discard);
    }
    if (iret) {
        status = word_transfer(config, arch->ss.base + arch->sp + 4u,
            false, &flags, &result->waits);
        if (status != BM_STATUS_OK) return status;
    }
    /* Section 10.1: NT is restorable at all CPLs; IOPL only at CPL0;
     * IF only when CPL <= the incoming IOPL. Bits 15/5/3 are reserved zero,
     * bit 1 reserved one, retaining the core's canonical 286 FLAGS policy. */
    if (arch->cpl == 0) mask |= 0x3000u;
    if (arch->cpl > ((arch->flags >> 12) & 3u)) mask &= 0xfdffu;
    flags = (uint16_t)(((flags & mask) | (arch->flags & (uint16_t)~mask)) & 0x7fd5u);
    flags |= 2u;
    /* All guest checks and frame reads precede the accessed RMW. Reuse its
     * locked current-byte update, never the DS/SS privilege-load rules for CS.
     * Transaction order/atomic CPU commit are functional policies, not traces. */
    plan.prepared = true; plan.needs_accessed_write = true;
    plan.access_address = ((selected.local ? arch->ldtr.base : arch->gdtr.base) +
        selected.table_offset + 5u) & 0xffffffu;
    plan.segment.selector = selector; plan.segment.base = code.descriptor.base;
    plan.segment.limit = code.descriptor.limit; plan.segment.access = code.descriptor.access;
    plan.segment.valid = 1;
    status = bm_286_pm_commit_load(&plan, config->access, config->access_context,
        config->bus_lock, config->pin_context, &new_cs);
    result->waits += plan.waits;
    if (status != BM_STATUS_OK) return status;
    if (outer) {
        stack_plan.prepared = true; stack_plan.needs_accessed_write = true;
        stack_plan.access_address = (((stack_selector & 4u) ? arch->ldtr.base : arch->gdtr.base) +
            (stack_selector & 0xfff8u) + 5u) & 0xffffffu;
        stack_plan.segment.selector = stack_selector;
        stack_plan.segment.base = outer_stack.descriptor.base;
        stack_plan.segment.limit = outer_stack.descriptor.limit;
        stack_plan.segment.access = outer_stack.descriptor.access; stack_plan.segment.valid = 1;
        status = bm_286_pm_commit_load(&stack_plan, config->access, config->access_context,
            config->bus_lock, config->pin_context, &new_ss);
        result->waits += stack_plan.waits;
        if (status != BM_STATUS_OK) return status;
        /* Both A-bit updates may have external effects, but no CPU field
         * changes until both succeed. Never replay after a host failure. */
        outer_data_cache(&arch->ds, arch, selected.rpl);
        outer_data_cache(&arch->es, arch, selected.rpl);
        arch->ss = new_ss; arch->sp = new_sp; arch->cpl = selected.rpl;
    } else arch->sp = (uint16_t)(arch->sp + (iret ? 6u : 4u + discard));
    arch->cs = new_cs; arch->ip = ip;
    if (iret) { arch->flags = flags; arch->nmi_blocked = 0; }
    result->loaded = true;
    return BM_STATUS_OK;
}

bm_status_t bm_286_pm_iret(bm_286_arch_state_t *arch,
    const bm_286_config_t *config, uint16_t next_ip, bm_286_segment_load_result_t *result)
{
    if(arch && result && (arch->flags&0x4000u)) {
        bm_286_task_request_t request={0}; memset(result,0,sizeof(*result));
        request.kind=BM_286_TASK_RETURN; request.return_ip=next_ip;
        return task_transfer(arch,config,&request,result);
    }
    return protected_return(arch, config, true, 0, result);
}

bm_status_t bm_286_pm_retf(bm_286_arch_state_t *arch,
    const bm_286_config_t *config, uint16_t discard, bm_286_segment_load_result_t *result)
{
    return protected_return(arch, config, false, discard, result);
}

/* Local adapter joins INTA through the first stack word, including escalation.
 * An accessed-byte RMW can nest inside this exclusion without toggling the pin.
 * This is the existing B-2/later functional lock policy, not timed bus edges. */
typedef struct delivery_bus {
    const bm_286_config_t *config;
    unsigned depth;
} delivery_bus_t;

static void delivery_lock(void *context, int asserted)
{
    delivery_bus_t *bus = context;
    if (asserted) {
        if (bus->depth++ == 0) bus->config->bus_lock(bus->config->pin_context, 1);
    } else if (--bus->depth == 0) {
        bus->config->bus_lock(bus->config->pin_context, 0);
    }
}

static bm_status_t delivery_access(void *context, bm_bus_transaction_t *transaction)
{
    delivery_bus_t *bus = context;
    if (bus->depth) transaction->attributes |= BM_BUS_TRANSACTION_LOCKED;
    return bus->config->access(bus->config->access_context, transaction);
}

static bool contributes_double_fault(uint8_t vector)
{
    /* Intel 286 9.6.2, not the later 386 page-fault/contributory matrix. */
    return vector == 0 || (vector >= 10 && vector <= 13);
}

bm_status_t bm_286_pm_deliver(bm_286_arch_state_t *arch,
    const bm_286_config_t *config, bm_286_pm_delivery_state_t *state,
    const bm_286_pm_request_t *request, bm_286_pm_delivery_result_t *result)
{
    bm_286_pm_event_t event = {0};
    bm_286_config_t routed;
    delivery_bus_t bus;
    bm_status_t status = BM_STATUS_OK;
    bool nmi = false, intr = false, exception = false, inta_locked = false;
    bool was_shutdown, task_fault, adjust_restart = true;
    uint16_t restart_ip;
    if (result == NULL) return BM_STATUS_INVALID_ARGUMENT;
    memset(result, 0, sizeof(*result));
    if (arch == NULL || config == NULL || state == NULL || request == NULL ||
        config->access == NULL) return BM_STATUS_INVALID_ARGUMENT;
    if (state->stopped || !(arch->msw & 1u) || arch->cpl > 3 ||
        arch->idtr.base > 0xffffffu || arch->shutdown > 1 || arch->halted > 1 ||
        (arch->shutdown && arch->halted) || arch->nmi_pending > 1 ||
        arch->nmi_blocked > 1 || arch->trap_pending > 1 ||
        arch->interrupt_shadow > BM_286_SHADOW_SS_LOAD) return BM_STATUS_INVALID_STATE;
    was_shutdown = arch->shutdown != 0;
    task_fault=request->task_fault;
    restart_ip = request->restart_ip;
    switch (request->source) {
    case BM_286_PM_EXCEPTION:
        if (was_shutdown || arch->halted) return BM_STATUS_INVALID_STATE;
        event.vector = request->vector;
        switch (event.vector) {
        case 0: case 5: case 6: case 7: case 8:
        case 10: case 11: case 12: case 13: case 16: break;
        default: return BM_STATUS_UNSUPPORTED;
        }
        exception = true;
        event.has_error = event.vector == 8 || (event.vector >= 10 && event.vector <= 13);
        event.error_code = event.vector == 8 ? 0 : request->error_code;
        event.external = event.vector == 7 || event.vector == 16;
        event.return_ip = restart_ip;
        break;
    case BM_286_PM_SOFTWARE:
        if (was_shutdown || arch->halted) return BM_STATUS_INVALID_STATE;
        event.vector = request->vector; event.software = true;
        event.return_ip = request->next_ip;
        break;
    case BM_286_PM_BOUNDARY:
        restart_ip = arch->ip; event.return_ip = arch->ip; event.external = true;
        if (!was_shutdown && arch->trap_pending &&
            arch->interrupt_shadow != BM_286_SHADOW_SS_LOAD) {
            event.vector = 1; exception = true;
        } else if (arch->nmi_pending && !arch->nmi_blocked &&
                   arch->interrupt_shadow != BM_286_SHADOW_SS_LOAD) {
            event.vector = 2; nmi = true;
        } else if (!was_shutdown && request->extension_overrun &&
                   arch->interrupt_shadow != BM_286_SHADOW_SS_LOAD) {
            event.vector = 9; exception = true;
        } else if (!was_shutdown && request->intr_line && (arch->flags & 0x200u) &&
                   arch->interrupt_shadow == BM_286_SHADOW_NONE) {
            intr = true;
        } else {
            return BM_STATUS_IDLE;
        }
        break;
    default: return BM_STATUS_INVALID_ARGUMENT;
    }
    /* A missing lock is an implementation/callback gap, not a guest #DF. */
    if (config->bus_lock == NULL || (intr && config->interrupt_ack == NULL))
        return BM_STATUS_UNSUPPORTED;
    bus.config = config; bus.depth = 0;
    routed = *config;
    routed.access = delivery_access; routed.access_context = &bus;
    routed.bus_lock = delivery_lock; routed.pin_context = &bus;
    result->accepted = true;
    if (intr) {
        delivery_lock(&bus, 1); inta_locked = true;
        for (unsigned phase = 0; phase < 2; ++phase) {
            uint32_t waits = 0;
            status = config->interrupt_ack(config->interrupt_context, phase,
                &event.vector, &waits);
            if (status != BM_STATUS_OK) goto done;
            result->waits += waits;
        }
    }
    /* Consume before callbacks: a newly signalled NMI remains pending. */
    if (nmi) arch->nmi_pending = 0;
    for (unsigned attempt = 0; attempt < 32; ++attempt) {
        bm_286_arch_state_t candidate = *arch;
        bm_286_segment_load_result_t entered;
        if(adjust_restart) {
            if(request->string_fault_adjust&1u) candidate.si=(uint16_t)(candidate.si+request->string_delta);
            if(request->string_fault_adjust&2u) candidate.di=(uint16_t)(candidate.di+request->string_delta);
            candidate.cx=(uint16_t)(candidate.cx-(request->string_fault_adjust>>2));
        }
        candidate.shutdown = 0; /* Recovery is staged; external state stays asserted. */
        result->vectors[attempt] = event.vector;
        result->errors[attempt] = event.has_error ? event.error_code : 0;
        result->has_error[attempt] = event.has_error;
        ++result->attempts;
        status = enter_event(&candidate, &routed, &event, &entered, &inta_locked, task_fault);
        result->waits += entered.waits;
        if (status != BM_STATUS_OK) goto done;
        if (entered.task_context) {
            bm_286_pm_publish_task(arch,&candidate);
            restart_ip=arch->ip;
            /* Faults after task selection belong to the new context. */
            exception=false;
            task_fault=true;
            adjust_restart=false;
            if(nmi) arch->nmi_blocked=1;
        }
        if (entered.loaded) {
            /* Copy only entry-owned fields: callbacks may have latched an NMI. */
            arch->cs = candidate.cs; arch->ip = candidate.ip; arch->sp = candidate.sp;
            arch->ss = candidate.ss; arch->cpl = candidate.cpl;
            arch->flags = candidate.flags; arch->halted = 0; arch->shutdown = 0;
            arch->si=candidate.si; arch->di=candidate.di; arch->cx=candidate.cx;
            arch->trap_pending = 0; arch->interrupt_shadow = BM_286_SHADOW_NONE;
            if (nmi) arch->nmi_blocked = 1;
            result->entered = true; result->vector = event.vector;
            if (was_shutdown && config->shutdown != NULL)
                config->shutdown(config->pin_context, 0);
            goto done;
        }
        /* Ordinary rejection has no writes. Task faults may already have saved
         * the outgoing task and published a new context; never replay them. */
        if (was_shutdown || (exception && event.vector == 8)) {
            if (inta_locked) { delivery_lock(&bus, 0); inta_locked = false; }
            arch->shutdown = 1; arch->halted = 0; arch->trap_pending = 0;
            if(adjust_restart) {arch->si=candidate.si; arch->di=candidate.di; arch->cx=candidate.cx;}
            arch->interrupt_shadow = BM_286_SHADOW_NONE;
            if (nmi) arch->nmi_blocked = 1;
            result->shutdown = true;
            if (!was_shutdown && config->shutdown != NULL)
                config->shutdown(config->pin_context, 1);
            goto done;
        }
        if (exception && contributes_double_fault(event.vector)) {
            event.vector = 8; event.error_code = 0;
        } else {
            event.vector = entered.fault_vector; event.error_code = entered.fault_error;
        }
        exception = true; event.software = false; event.has_error = true;
        event.return_ip = restart_ip;
    }
    /* Cyclic task-fault chains exhaust this private diagnostic budget;
     * never manufacture a double fault or guest shutdown for a host bound. */
    status = BM_STATUS_UNSUPPORTED;
done:
    if (inta_locked) delivery_lock(&bus, 0);
    if (status != BM_STATUS_OK) {
        state->stopped = true;
        if (nmi) arch->nmi_pending = 1;
    }
    return status;
}
