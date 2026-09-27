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

bm_286_pm_descriptor_t bm_286_cached_descriptor(const bm_286_segment_state_t *segment)
{
    uint8_t bytes[8] = {0};
    bm_286_pm_descriptor_t result;
    bytes[5] = segment->access == 0x82 ? 0x92 : segment->access;
    result = bm_286_pm_descriptor_decode(bytes);
    result.access = segment->access;
    result.base = segment->base; result.limit = segment->limit;
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
    plan->access_address = ((s.local ? ldt->base : gdt->base) +
                            s.table_offset + 5u) & 0xffffffu;
    /* Intel PRM 11.1 specifies the locked RMW. Do not elide it based on the
     * earlier snapshot: another bus master may have cleared A meanwhile. */
    plan->needs_accessed_write = target != BM_286_PM_LOAD_LDT;
    plan->fault_vector = 0;
    plan->fault_error = 0;
    plan->prepared = true;
    return BM_STATUS_OK;
}

bm_status_t bm_286_pm_commit_load(bm_286_pm_load_plan_t *plan,
    bm_bus_access_fn access, void *context, bm_286_pin_fn bus_lock,
    void *pin_context, bm_286_segment_state_t *destination)
{
    bm_status_t status = BM_STATUS_OK;
    if (plan == NULL || destination == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    if (!plan->prepared || plan->fault_vector != 0)
        return BM_STATUS_INVALID_STATE;
    /* Consume before any callback; neither recursive use nor a host failure
     * may replay a possibly completed device write. */
    plan->prepared = false;
    if (plan->needs_accessed_write) {
        bm_bus_transaction_t transfer = {0};
        uint8_t access_byte;
        if (access == NULL || bus_lock == NULL)
            return BM_STATUS_UNSUPPORTED;
        if (plan->access_address > 0xffffffu || !plan->segment.valid ||
            !(plan->segment.access & 0x10u))
            return BM_STATUS_INVALID_STATE;
        bus_lock(pin_context, 1);
        transfer.space = BM_ADDRESS_DATA;
        transfer.address = plan->access_address;
        transfer.operation = BM_BUS_READ;
        transfer.endianness = BM_ENDIAN_LITTLE;
        transfer.size = transfer.alignment = 1;
        transfer.attributes = BM_BUS_TRANSACTION_LOCKED;
        status = access(context, &transfer);
        if (status == BM_STATUS_OK) {
            plan->waits += transfer.wait_states;
            access_byte = (uint8_t)transfer.value;
            /* Preserve all other bits from the locked read, not the snapshot.
             * Concurrent descriptor replacement is not made transactional. */
            transfer.operation = BM_BUS_WRITE;
            transfer.value = access_byte | 1u;
            transfer.wait_states = 0;
            status = access(context, &transfer);
            if (status == BM_STATUS_OK)
                plan->waits += transfer.wait_states;
        }
        bus_lock(pin_context, 0);
        if (status != BM_STATUS_OK)
            return status;
        plan->segment.access |= 1u;
        plan->needs_accessed_write = false;
    }
    *destination = plan->segment;
    return BM_STATUS_OK;
}

bm_status_t bm_286_load_segment_state(bm_286_arch_state_t *arch,
    const bm_286_config_t *config, unsigned reg, uint16_t selector,
    bm_286_segment_load_result_t *result)
{
    bm_286_segment_state_t *destination;
    bm_286_pm_load_plan_t plan;
    bm_status_t status;
    if (result == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    memset(result, 0, sizeof(*result));
    if (arch == NULL || config == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    switch (reg) {
    case 0: destination = &arch->es; break;
    case 2: destination = &arch->ss; break;
    case 3: destination = &arch->ds; break;
    case 4: destination = &arch->ldtr; break;
    default: return BM_STATUS_INVALID_ARGUMENT;
    }
    if (!(arch->msw & 1u)) {
        if (reg == 4) {
            result->fault_vector = 6;
            return BM_STATUS_OK;
        }
        destination->selector = selector;
        destination->base = (uint32_t)selector << 4;
        destination->limit = 0xffff;
        destination->access = 0x82; /* Real-compatible cache, not a table LDT. */
        destination->valid = 1;
        result->loaded = true;
        return BM_STATUS_OK;
    }
    status = bm_286_pm_prepare_load(&arch->gdtr, &arch->ldtr, selector, arch->cpl,
        reg == 4 ? BM_286_PM_LOAD_LDT : reg == 2 ? BM_286_PM_LOAD_STACK : BM_286_PM_LOAD_DATA,
        config->access, config->access_context, &plan);
    result->waits = plan.waits;
    result->fault_vector = plan.fault_vector;
    result->fault_error = plan.fault_error;
    if (status != BM_STATUS_OK || !plan.prepared)
        return status;
    status = bm_286_pm_commit_load(&plan, config->access, config->access_context,
        config->bus_lock, config->pin_context, destination);
    result->waits = plan.waits;
    result->loaded = status == BM_STATUS_OK;
    return status;
}

/* Intel 286 PRM 11.3/11.3.1, B-60/B-71/B-111. */
bm_status_t bm_286_pm_query(const bm_286_arch_state_t *arch,
    const bm_286_config_t *config, bm_286_pm_query_kind_t kind,
    uint16_t selector, bm_286_pm_query_result_t *result)
{
    bm_286_pm_lookup_t lookup;
    const bm_286_pm_descriptor_t *d = &lookup.descriptor;
    bm_status_t status;
    bool eligible;
    if (!result) return BM_STATUS_INVALID_ARGUMENT;
    memset(result, 0, sizeof(*result));
    if (!arch || !config || !config->access || kind < BM_286_PM_LAR || kind > BM_286_PM_VERW)
        return BM_STATUS_INVALID_ARGUMENT;
    if (!(arch->msw & 1u) || arch->cpl > 3 || arch->halted || arch->shutdown)
        return BM_STATUS_INVALID_STATE;
    status = bm_286_pm_lookup_descriptor(&arch->gdtr, &arch->ldtr, selector,
        config->access, config->access_context, &lookup);
    result->waits = lookup.waits;
    if (status != BM_STATUS_OK || lookup.reason != BM_286_PM_FOUND) return status;
    /* PRM B-71 explicitly limits LSL to nonconforming segments; apply the
     * instruction entry's condition rather than import later-x86 semantics.
     * B-60/11.3 define LAR for a visible descriptor, including control types. */
    if (kind == BM_286_PM_LSL && d->conforming) return BM_STATUS_OK;
    if (!d->conforming && (d->dpl < arch->cpl || d->dpl < (selector & 3u)))
        return BM_STATUS_OK;
    switch (kind) {
    case BM_286_PM_LAR:
        eligible = d->kind != BM_286_PM_INVALID;
        break;
    case BM_286_PM_LSL:
        eligible = d->kind == BM_286_PM_DATA || d->kind == BM_286_PM_CODE ||
            d->kind == BM_286_PM_LDT || d->kind == BM_286_PM_TSS_AVAILABLE ||
            d->kind == BM_286_PM_TSS_BUSY;
        break;
    case BM_286_PM_VERR: eligible = d->readable; break;
    default: eligible = d->writable; break;
    }
    if (eligible) {
        result->accepted = true;
        if (kind == BM_286_PM_LAR) result->value = (uint16_t)((uint16_t)d->access << 8);
        if (kind == BM_286_PM_LSL) result->value = d->limit;
    }
    return BM_STATUS_OK;
}

bm_status_t bm_286_pm_ltr(bm_286_arch_state_t *arch,
    const bm_286_config_t *config, uint16_t selector,
    bm_286_segment_load_result_t *result)
{
    bm_286_pm_lookup_t lookup;
    bm_286_segment_state_t tr = {0};
    bm_286_pm_selector_t s = bm_286_pm_selector_decode(selector);
    bm_bus_transaction_t t = {0};
    bm_status_t status;
    uint8_t access_byte;
    if (!result) return BM_STATUS_INVALID_ARGUMENT;
    memset(result, 0, sizeof(*result));
    if (!arch || !config || !config->access) return BM_STATUS_INVALID_ARGUMENT;
    if (arch->cpl > 3 || arch->halted || arch->shutdown) return BM_STATUS_INVALID_STATE;
    if (!(arch->msw & 1u)) { result->fault_vector = 6; return BM_STATUS_OK; }
    if (arch->cpl) { result->fault_vector = 13; return BM_STATUS_OK; }
    if (s.null_selector || s.local) {
        result->fault_vector = 13; result->fault_error = (uint16_t)(selector & 0xfffcu);
        return BM_STATUS_OK;
    }
    status = bm_286_pm_lookup_descriptor(&arch->gdtr, &arch->ldtr, selector,
        config->access, config->access_context, &lookup);
    result->waits = lookup.waits;
    if (status != BM_STATUS_OK) return status;
    if (lookup.reason != BM_286_PM_FOUND || lookup.descriptor.kind != BM_286_PM_TSS_AVAILABLE)
        result->fault_vector = 13;
    else if (!lookup.descriptor.present) result->fault_vector = 11;
    if (result->fault_vector) { result->fault_error = lookup.selector_error; return BM_STATUS_OK; }
    if (!config->bus_lock) return BM_STATUS_UNSUPPORTED;
    /* Availability is tested again under exclusion (8.4), not merely ORed
     * into a stale snapshot. Whole-descriptor replacement still requires
     * external serialization. No TSS memory or old descriptor is touched. */
    t.address = (arch->gdtr.base + s.table_offset + 5u) & 0xffffffu;
    t.space = BM_ADDRESS_DATA; t.operation = BM_BUS_READ;
    t.size = t.alignment = 1; t.endianness = BM_ENDIAN_LITTLE;
    t.attributes = BM_BUS_TRANSACTION_LOCKED;
    config->bus_lock(config->pin_context, 1);
    status = config->access(config->access_context, &t);
    if (status == BM_STATUS_OK) {
        result->waits += t.wait_states;
        access_byte = (uint8_t)t.value;
        if ((access_byte & 0x1fu) != 1u) result->fault_vector = 13;
        else if (!(access_byte & 0x80u)) result->fault_vector = 11;
        if (result->fault_vector) result->fault_error = lookup.selector_error;
        else {
            t.operation = BM_BUS_WRITE; t.value = access_byte | 2u; t.wait_states = 0;
            status = config->access(config->access_context, &t);
            if (status == BM_STATUS_OK) {
                result->waits += t.wait_states;
                tr.selector = selector; tr.base = lookup.descriptor.base;
                tr.limit = lookup.descriptor.limit; tr.access = (uint8_t)(access_byte | 2u); tr.valid = 1;
            }
        }
    }
    config->bus_lock(config->pin_context, 0);
    if (status == BM_STATUS_OK && !result->fault_vector) { arch->tr = tr; result->loaded = true; }
    return status;
}
