/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 BluMach contributors
 * Intel 80286/80287 PRM 1987, chapters 9/10 and INT/IRET in appendix B.
 * Functional same-privilege entry/return, not physical bus/fault timing.
 */
#include "descriptor_286.h"
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

bm_status_t bm_286_pm_enter_event(bm_286_arch_state_t *arch,
    const bm_286_config_t *config, const bm_286_pm_event_t *event,
    bm_286_segment_load_result_t *result)
{
    uint8_t bytes[8];
    bm_286_pm_descriptor_t gate, stack;
    bm_286_pm_lookup_t code;
    bm_286_pm_load_plan_t plan = {0};
    bm_286_segment_state_t new_cs;
    uint16_t selector, ip, words[4], sp;
    uint32_t offset, top, frame;
    uint16_t gate_error, code_error;
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
    if (gate.kind == BM_286_PM_TASK_GATE) return BM_STATUS_UNSUPPORTED;
    selector = (uint16_t)((uint16_t)bytes[2] | ((uint16_t)bytes[3] << 8));
    ip = (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
    status = bm_286_pm_lookup_descriptor(&arch->gdtr, &arch->ldtr, selector,
        config->access, config->access_context, &code);
    result->waits += code.waits;
    if (status != BM_STATUS_OK) return status;
    code_error = (uint16_t)(code.selector_error | (event->external ? 1u : 0u));
    if (code.reason != BM_286_PM_FOUND || code.descriptor.kind != BM_286_PM_CODE ||
        code.descriptor.dpl > arch->cpl) return reject(result, 13, code_error);
    if (!code.descriptor.present) return reject(result, 11, code_error);
    if (!code.descriptor.conforming && code.descriptor.dpl < arch->cpl)
        return BM_STATUS_UNSUPPORTED;
    /* Cached SS validity is a host-state invariant, not a descriptor reload. */
    if (!arch->ss.valid || arch->ss.base > 0xffffffu) return BM_STATUS_INVALID_STATE;
    memset(bytes, 0, sizeof(bytes)); bytes[5] = arch->ss.access;
    stack = bm_286_pm_descriptor_decode(bytes); stack.limit = arch->ss.limit;
    if (stack.kind != BM_286_PM_DATA || !stack.writable || !stack.present ||
        stack.dpl != arch->cpl || (arch->ss.selector & 3u) != arch->cpl)
        return BM_STATUS_INVALID_STATE;
    count = event->has_error ? 4u : 3u; frame = count * 2u;
    top = arch->sp ? arch->sp : 65536u;
    if (top < frame || !bm_286_pm_segment_contains(&stack, top - frame, frame))
        return reject(result, 12, 0);
    if (ip > code.descriptor.limit) return reject(result, 13, 0);
    /* Preflight before any write; accessed update precedes frame writes as
     * functional policy. External partial writes survive transport errors. */
    plan.prepared = true; plan.needs_accessed_write = true;
    plan.access_address = (((selector & 4u) ? arch->ldtr.base : arch->gdtr.base) +
        (selector & 0xfff8u) + 5u) & 0xffffffu;
    plan.segment.selector = (uint16_t)((selector & 0xfffcu) | arch->cpl);
    plan.segment.base = code.descriptor.base; plan.segment.limit = code.descriptor.limit;
    plan.segment.access = code.descriptor.access; plan.segment.valid = 1;
    status = bm_286_pm_commit_load(&plan, config->access, config->access_context,
        config->bus_lock, config->pin_context, &new_cs);
    result->waits += plan.waits;
    if (status != BM_STATUS_OK) return status;
    words[0] = arch->flags; words[1] = arch->cs.selector;
    words[2] = event->return_ip; words[3] = event->error_code;
    sp = arch->sp;
    for (i = 0; i < count; ++i) {
        sp = (uint16_t)(sp - 2u);
        status = word_transfer(config, arch->ss.base + sp, true, &words[i], &result->waits);
        if (status != BM_STATUS_OK) return status;
    }
    arch->cs = new_cs; arch->ip = ip; arch->sp = sp;
    arch->flags &= (uint16_t)~(0x0100u | 0x4000u |
        (gate.kind == BM_286_PM_INTERRUPT_GATE ? 0x0200u : 0u));
    arch->halted = 0; arch->trap_pending = 0; arch->interrupt_shadow = BM_286_SHADOW_NONE;
    result->loaded = true;
    return BM_STATUS_OK;
}

bm_status_t bm_286_pm_iret(bm_286_arch_state_t *arch,
    const bm_286_config_t *config, bm_286_segment_load_result_t *result)
{
    uint8_t bytes[8] = {0};
    bm_286_pm_descriptor_t stack;
    bm_286_pm_selector_t selected;
    bm_286_pm_lookup_t code;
    bm_286_pm_load_plan_t plan = {0};
    bm_286_segment_state_t new_cs;
    uint16_t selector = 0, ip = 0, flags = 0, mask = 0x4fd5u;
    bm_status_t status;
    if (result == NULL) return BM_STATUS_INVALID_ARGUMENT;
    memset(result, 0, sizeof(*result));
    if (arch == NULL || config == NULL || config->access == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    if (!(arch->msw & 1u) || arch->shutdown || arch->halted || arch->cpl > 3)
        return BM_STATUS_INVALID_STATE;
    /* Select using CURRENT NT, not the saved FLAGS image. No backlink access
     * or ordinary-frame fallback can stand in for an unimplemented task return. */
    if (arch->flags & 0x4000u) return BM_STATUS_UNSUPPORTED;
    if (!arch->ss.valid || arch->ss.base > 0xffffffu)
        return BM_STATUS_INVALID_STATE;
    bytes[5] = arch->ss.access;
    stack = bm_286_pm_descriptor_decode(bytes); stack.limit = arch->ss.limit;
    if (stack.kind != BM_286_PM_DATA || !stack.writable || !stack.present ||
        stack.dpl != arch->cpl || (arch->ss.selector & 3u) != arch->cpl)
        return BM_STATUS_INVALID_STATE;
    /* B-52 first checks the second stack word and RPL, THEN the six-byte
     * same-level frame. Widen before adding: no wrapped protected frame. */
    if (!bm_286_pm_segment_contains(&stack, (uint32_t)arch->sp + 2u, 2))
        return reject(result, 12, 0);
    status = word_transfer(config, arch->ss.base + arch->sp + 2u,
        false, &selector, &result->waits);
    if (status != BM_STATUS_OK) return status;
    selected = bm_286_pm_selector_decode(selector);
    if (selected.rpl < arch->cpl)
        return reject(result, 13, (uint16_t)(selector & 0xfffcu));
    if (selected.rpl > arch->cpl) return BM_STATUS_UNSUPPORTED;
    if (!bm_286_pm_segment_contains(&stack, arch->sp, 6))
        return reject(result, 12, 0);
    status = bm_286_pm_lookup_descriptor(&arch->gdtr, &arch->ldtr, selector,
        config->access, config->access_context, &code);
    result->waits += code.waits;
    if (status != BM_STATUS_OK) return status;
    if (code.reason != BM_286_PM_FOUND || code.descriptor.kind != BM_286_PM_CODE ||
        (code.descriptor.conforming ? code.descriptor.dpl > arch->cpl :
                                     code.descriptor.dpl != arch->cpl))
        return reject(result, 13, code.selector_error);
    if (!code.descriptor.present) return reject(result, 11, code.selector_error);
    status = word_transfer(config, arch->ss.base + arch->sp, false, &ip, &result->waits);
    if (status != BM_STATUS_OK) return status;
    if (ip > code.descriptor.limit) return reject(result, 13, 0);
    status = word_transfer(config, arch->ss.base + arch->sp + 4u,
        false, &flags, &result->waits);
    if (status != BM_STATUS_OK) return status;
    /* Section 10.1: NT is restorable at all CPLs; IOPL only at CPL0;
     * IF only when CPL <= the incoming IOPL. Bits 15/5/3 are reserved zero,
     * bit 1 reserved one, retaining the core's canonical 286 FLAGS policy. */
    if (arch->cpl == 0) mask |= 0x3000u;
    if (arch->cpl > ((arch->flags >> 12) & 3u)) mask &= (uint16_t)~0x0200u;
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
    arch->cs = new_cs; arch->ip = ip; arch->sp = (uint16_t)(arch->sp + 6u);
    arch->flags = flags; arch->nmi_blocked = 0;
    result->loaded = true;
    return BM_STATUS_OK;
}
