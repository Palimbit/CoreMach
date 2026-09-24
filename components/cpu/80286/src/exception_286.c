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

static bm_status_t enter_event(bm_286_arch_state_t *arch,
    const bm_286_config_t *config, const bm_286_pm_event_t *event,
    bm_286_segment_load_result_t *result, bool *inta_locked)
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
        if (i == 0 && inta_locked != NULL && *inta_locked) {
            config->bus_lock(config->pin_context, 0);
            *inta_locked = false;
        }
    }
    arch->cs = new_cs; arch->ip = ip; arch->sp = sp;
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
    return enter_event(arch, config, event, result, NULL);
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
    arch->cs = new_cs; arch->ip = ip; arch->sp = (uint16_t)(arch->sp + 6u);
    arch->flags = flags; arch->nmi_blocked = 0;
    result->loaded = true;
    return BM_STATUS_OK;
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
    bool was_shutdown;
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
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        bm_286_arch_state_t candidate = *arch;
        bm_286_segment_load_result_t entered;
        candidate.shutdown = 0; /* Recovery is staged; external state stays asserted. */
        result->vectors[attempt] = event.vector;
        result->errors[attempt] = event.has_error ? event.error_code : 0;
        result->has_error[attempt] = event.has_error;
        ++result->attempts;
        status = enter_event(&candidate, &routed, &event, &entered, &inta_locked);
        result->waits += entered.waits;
        if (status != BM_STATUS_OK) goto done;
        if (entered.loaded) {
            /* Copy only entry-owned fields: callbacks may have latched an NMI. */
            arch->cs = candidate.cs; arch->ip = candidate.ip; arch->sp = candidate.sp;
            arch->flags = candidate.flags; arch->halted = 0; arch->shutdown = 0;
            arch->trap_pending = 0; arch->interrupt_shadow = BM_286_SHADOW_NONE;
            if (nmi) arch->nmi_blocked = 1;
            result->entered = true; result->vector = event.vector;
            if (was_shutdown && config->shutdown != NULL)
                config->shutdown(config->pin_context, 0);
            goto done;
        }
        /* No writes occur on guest rejection. A bus error after any write
         * already exited above, so escalation cannot replay a partial frame. */
        if (was_shutdown || (exception && event.vector == 8)) {
            if (inta_locked) { delivery_lock(&bus, 0); inta_locked = false; }
            arch->shutdown = 1; arch->halted = 0; arch->trap_pending = 0;
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
    /* Unreachable under same-CPL entry's #NP/#SS/#GP-only rejection contract.
     * A future unhandled cause must stop the host, never fabricate shutdown. */
    status = BM_STATUS_INVALID_STATE;
done:
    if (inta_locked) delivery_lock(&bus, 0);
    if (status != BM_STATUS_OK) {
        state->stopped = true;
        if (nmi) arch->nmi_pending = 1;
    }
    return status;
}
