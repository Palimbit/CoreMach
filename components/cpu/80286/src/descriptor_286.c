/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 BluMach contributors
 * Intel 80286/80287 Programmer's Reference Manual (1987), chapters 6, 7,
 * 11 and appendix B. See doc/architecture/pcs286-protected-mode-plan.md.
 */
#include "descriptor_286.h"
#include <string.h>

bm_286_pm_selector_t bm_286_pm_selector_decode(uint16_t raw)
{
    bm_286_pm_selector_t result;
    result.index = (uint16_t)(raw >> 3);
    result.table_offset = (uint16_t)(raw & 0xfff8u);
    result.rpl = (uint8_t)(raw & 3u);
    result.local = (raw & 4u) != 0;
    result.null_selector = (raw & 0xfffcu) == 0;
    return result;
}

bm_286_pm_descriptor_t bm_286_pm_descriptor_decode(const uint8_t bytes[8])
{
    bm_286_pm_descriptor_t result = {0};
    const uint8_t type = bytes[5] & 15u;
    result.access = bytes[5];
    result.dpl = (uint8_t)((bytes[5] >> 5) & 3u);
    result.present = (bytes[5] & 0x80u) != 0;
    result.reserved = (uint16_t)((uint16_t)bytes[6] | ((uint16_t)bytes[7] << 8));
    if ((bytes[5] & 0x10u) != 0) {
        result.kind = (type & 8u) ? BM_286_PM_CODE : BM_286_PM_DATA;
        result.accessed = (type & 1u) != 0;
        result.readable = result.kind == BM_286_PM_DATA || (type & 2u) != 0;
        result.writable = result.kind == BM_286_PM_DATA && (type & 2u) != 0;
        result.expand_down = result.kind == BM_286_PM_DATA && (type & 4u) != 0;
        result.conforming = result.kind == BM_286_PM_CODE && (type & 4u) != 0;
    } else {
        switch (type) {
        case 1: result.kind = BM_286_PM_TSS_AVAILABLE; break;
        case 2: result.kind = BM_286_PM_LDT; break;
        case 3: result.kind = BM_286_PM_TSS_BUSY; break;
        case 4: result.kind = BM_286_PM_CALL_GATE; break;
        case 5: result.kind = BM_286_PM_TASK_GATE; break;
        case 6: result.kind = BM_286_PM_INTERRUPT_GATE; break;
        case 7: result.kind = BM_286_PM_TRAP_GATE; break;
        default: result.kind = BM_286_PM_INVALID; break;
        }
    }
    if (result.kind == BM_286_PM_DATA || result.kind == BM_286_PM_CODE ||
        result.kind == BM_286_PM_LDT || result.kind == BM_286_PM_TSS_AVAILABLE ||
        result.kind == BM_286_PM_TSS_BUSY) {
        result.limit = (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
        result.base = (uint32_t)bytes[2] | ((uint32_t)bytes[3] << 8) |
                      ((uint32_t)bytes[4] << 16);
    }
    return result;
}

bool bm_286_pm_segment_contains(const bm_286_pm_descriptor_t *descriptor,
                                uint32_t offset, uint32_t length)
{
    if ((descriptor->kind != BM_286_PM_DATA && descriptor->kind != BM_286_PM_CODE) ||
        length == 0 || offset > 0xffffu || length - 1u > 0xffffu - offset)
        return false;
    if (descriptor->expand_down)
        return offset > descriptor->limit;
    return offset + length - 1u <= descriptor->limit;
}

bm_status_t bm_286_pm_lookup_descriptor(const bm_286_table_state_t *gdt,
    const bm_286_segment_state_t *ldt, uint16_t selector,
    bm_bus_access_fn access, void *context, bm_286_pm_lookup_t *result)
{
    bm_286_pm_selector_t selected = bm_286_pm_selector_decode(selector);
    uint32_t base, limit, position;
    uint8_t bytes[8];
    if (result == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    memset(result, 0, sizeof(*result));
    if (gdt == NULL || ldt == NULL || access == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    result->selector_error = (uint16_t)(selector & 0xfffcu);
    if (selected.null_selector) {
        result->reason = BM_286_PM_NULL_SELECTOR;
        return BM_STATUS_OK;
    }
    if (selected.local && !ldt->valid) {
        result->reason = BM_286_PM_NO_LDT;
        return BM_STATUS_OK;
    }
    base = selected.local ? ldt->base : gdt->base;
    limit = selected.local ? ldt->limit : gdt->limit;
    if (base > 0xffffffu)
        return BM_STATUS_INVALID_STATE;
    if ((uint32_t)selected.table_offset + 7u > limit) {
        result->reason = BM_286_PM_TABLE_LIMIT;
        return BM_STATUS_OK;
    }
    /* Logical increasing-address reads. Not a silicon bus ordering claim.
     * Physical 24-bit wrap is distinct from descriptor-table limit checks;
     * motherboard A20 gating remains in the endpoint. */
    for (position = 0; position < 8u;) {
        bm_bus_transaction_t transfer = {0};
        bm_status_t status;
        uint32_t address = (base + selected.table_offset + position) & 0xffffffu;
        unsigned size = (address & 1u) ? 1u : 2u;
        /* Preserve the CPU's odd logical-word split: all bytes on odd base. */
        if ((base & 1u) != 0)
            size = 1u;
        transfer.address = address;
        transfer.space = BM_ADDRESS_DATA;
        transfer.operation = BM_BUS_READ;
        transfer.endianness = BM_ENDIAN_LITTLE;
        transfer.size = size;
        transfer.alignment = size;
        status = access(context, &transfer);
        if (status != BM_STATUS_OK)
            return status;
        result->waits += transfer.wait_states;
        bytes[position] = (uint8_t)transfer.value;
        if (size == 2u)
            bytes[position + 1u] = (uint8_t)(transfer.value >> 8);
        position += size;
    }
    memcpy(result->bytes, bytes, sizeof(bytes));
    result->descriptor = bm_286_pm_descriptor_decode(bytes);
    result->reason = BM_286_PM_FOUND;
    return BM_STATUS_OK;
}

bm_status_t bm_286_pm_prepare_load(const bm_286_table_state_t *gdt,
    const bm_286_segment_state_t *ldt, uint16_t selector, uint8_t cpl,
    bm_286_pm_load_target_t target, bm_bus_access_fn access, void *context,
    bm_286_pm_load_plan_t *plan)
{
    bm_286_pm_lookup_t lookup;
    bm_286_pm_descriptor_t *d = &lookup.descriptor;
    bm_286_pm_selector_t s = bm_286_pm_selector_decode(selector);
    bm_status_t status;
    if (plan == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    memset(plan, 0, sizeof(*plan));
    if (gdt == NULL || ldt == NULL || access == NULL || cpl > 3 ||
        (target != BM_286_PM_LOAD_DATA && target != BM_286_PM_LOAD_STACK &&
         target != BM_286_PM_LOAD_LDT))
        return BM_STATUS_INVALID_ARGUMENT;
    if (target == BM_286_PM_LOAD_LDT && cpl != 0) {
        plan->fault_vector = 13;
        return BM_STATUS_OK;
    }
    if (s.null_selector) {
        if (target == BM_286_PM_LOAD_STACK) {
            plan->fault_vector = 13;
        } else {
            plan->segment.selector = selector;
            plan->prepared = true;
        }
        return BM_STATUS_OK;
    }
    if (target == BM_286_PM_LOAD_LDT && s.local) {
        plan->fault_vector = 13;
        plan->fault_error = (uint16_t)(selector & 0xfffcu);
        return BM_STATUS_OK;
    }
    status = bm_286_pm_lookup_descriptor(gdt, ldt, selector, access, context, &lookup);
    plan->waits = lookup.waits;
    if (status != BM_STATUS_OK)
        return status;
    /* Stage guest rejection independently of transport statuses. */
    plan->fault_vector = 13;
    plan->fault_error = lookup.selector_error;
    if (lookup.reason != BM_286_PM_FOUND)
        return BM_STATUS_OK;
    if (target == BM_286_PM_LOAD_LDT) {
        if (d->kind != BM_286_PM_LDT)
            return BM_STATUS_OK;
    } else if (target == BM_286_PM_LOAD_STACK) {
        if (d->kind != BM_286_PM_DATA || !d->writable ||
            s.rpl != cpl || d->dpl != cpl)
            return BM_STATUS_OK;
    } else {
        if (d->kind != BM_286_PM_DATA &&
            !(d->kind == BM_286_PM_CODE && d->readable))
            return BM_STATUS_OK;
        if (!d->conforming && (d->dpl < cpl || d->dpl < s.rpl))
            return BM_STATUS_OK;
    }
    if (!d->present) {
        plan->fault_vector = target == BM_286_PM_LOAD_STACK ? 12 : 11;
        return BM_STATUS_OK;
    }
    plan->segment.selector = selector;
    plan->segment.base = d->base;
    plan->segment.limit = d->limit;
    plan->segment.access = d->access;
    plan->segment.valid = 1;
    plan->needs_accessed_write = target != BM_286_PM_LOAD_LDT && !d->accessed;
    plan->fault_vector = 0;
    plan->fault_error = 0;
    plan->prepared = true;
    return BM_STATUS_OK;
}
