/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 BluMach contributors
 */
#include "descriptor_286.h"
#include <assert.h>
#include <string.h>

static void selectors(void)
{
    uint32_t raw;
    for (raw = 0; raw < 65536u; ++raw) {
        bm_286_pm_selector_t s = bm_286_pm_selector_decode((uint16_t)raw);
        assert(s.index == raw / 8u);
        assert(s.table_offset == (raw / 8u) * 8u);
        assert(s.rpl == raw % 4u);
        assert(s.local == (raw % 8u >= 4u));
        assert(s.null_selector == (raw < 4u));
    }
}

static void descriptors(void)
{
    static const bm_286_pm_kind_t system_types[16] = {
        BM_286_PM_INVALID, BM_286_PM_TSS_AVAILABLE, BM_286_PM_LDT,
        BM_286_PM_TSS_BUSY, BM_286_PM_CALL_GATE, BM_286_PM_TASK_GATE,
        BM_286_PM_INTERRUPT_GATE, BM_286_PM_TRAP_GATE,
        BM_286_PM_INVALID, BM_286_PM_INVALID, BM_286_PM_INVALID,
        BM_286_PM_INVALID, BM_286_PM_INVALID, BM_286_PM_INVALID,
        BM_286_PM_INVALID, BM_286_PM_INVALID
    };
    uint32_t access, reserved;
    uint8_t storage[9] = {0x55, 0xcd, 0xab, 0x56, 0x34, 0x12, 0, 0, 0};
    uint8_t copy[9];
    /* Intentionally byte-addressed; no alignment promise. */
    uint8_t *bytes = storage + 1;
    for (access = 0; access < 256u; ++access) {
        unsigned type = access % 16u;
        bool segment = (access % 32u) >= 16u;
        bool code = segment && type >= 8u;
        bm_286_pm_descriptor_t d;
        bytes[5] = (uint8_t)access;
        memcpy(copy, storage, sizeof(copy));
        d = bm_286_pm_descriptor_decode(bytes);
        assert(memcmp(copy, storage, sizeof(copy)) == 0);
        assert(d.kind == (segment ? (code ? BM_286_PM_CODE : BM_286_PM_DATA)
                                 : system_types[type]));
        assert(d.access == access && d.dpl == (access / 32u) % 4u);
        assert(d.present == (access >= 128u));
        assert(d.accessed == (segment && type % 2u != 0));
        assert(d.readable == (segment && (!code || type % 4u >= 2u)));
        assert(d.writable == (segment && !code && type % 4u >= 2u));
        assert(d.expand_down == (segment && !code && type >= 4u));
        assert(d.conforming == (code && type >= 12u));
        if (segment || (type >= 1u && type <= 3u)) {
            assert(d.base == 0x123456u && d.limit == 0xabcdu);
        } else {
            assert(d.base == 0 && d.limit == 0);
        }
    }
    bytes[5] = 0x92;
    for (reserved = 0; reserved < 65536u; ++reserved) {
        bm_286_pm_descriptor_t d;
        bytes[6] = (uint8_t)reserved;
        bytes[7] = (uint8_t)(reserved >> 8);
        d = bm_286_pm_descriptor_decode(bytes);
        assert(d.reserved == reserved);
        assert(d.base == 0x123456u && d.limit == 0xabcdu);
        assert(d.kind == BM_286_PM_DATA && d.writable && d.present);
    }
}

/* Independent per-byte range oracle, using wide arithmetic. */
static bool range_oracle(uint16_t limit, bool down, uint32_t offset, uint32_t size)
{
    uint32_t i;
    if (size == 0 || size > 65536u)
        return false;
    for (i = 0; i < size; ++i) {
        uint64_t address = (uint64_t)offset + i;
        if (address >= 65536u || (down ? address <= limit : address > limit))
            return false;
    }
    return true;
}

static void ranges(void)
{
    bm_286_pm_descriptor_t d = {0};
    uint32_t limit, offset, direction, size;
    static const uint16_t limits[] = {0, 1, 0x7fff, 0xfffe, 0xffff};
    unsigned n;
    d.kind = BM_286_PM_DATA;
    for (direction = 0; direction < 2; ++direction) {
        d.expand_down = direction != 0;
        for (limit = 0; limit < 65536u; ++limit) {
            uint32_t offsets[4] = {0, limit, limit + 1u, 0xffffu};
            d.limit = (uint16_t)limit;
            for (n = 0; n < 4; ++n)
                for (size = 1; size <= 2; ++size)
                    assert(bm_286_pm_segment_contains(&d, offsets[n], size) ==
                           range_oracle(d.limit, d.expand_down, offsets[n], size));
        }
        for (n = 0; n < sizeof(limits) / sizeof(limits[0]); ++n) {
            d.limit = limits[n];
            for (offset = 0; offset < 65536u; ++offset)
                for (size = 1; size <= 8; ++size)
                    assert(bm_286_pm_segment_contains(&d, offset, size) ==
                           range_oracle(d.limit, d.expand_down, offset, size));
        }
        assert(!bm_286_pm_segment_contains(&d, 0, 0));
        assert(!bm_286_pm_segment_contains(&d, UINT32_MAX, 1));
        assert(!bm_286_pm_segment_contains(&d, 0, UINT32_MAX));
        assert(!bm_286_pm_segment_contains(&d, UINT32_MAX, UINT32_MAX));
    }
    d.expand_down = false;
    d.limit = 0xffff;
    d.kind = BM_286_PM_CODE;
    assert(bm_286_pm_segment_contains(&d, 0, 65536));
    assert(!bm_286_pm_segment_contains(&d, 1, 65536));
    /* Presence and access permissions deliberately do not affect bounds. */
    assert(!d.present && !d.readable);
    assert(bm_286_pm_segment_contains(&d, 0xffff, 1));
    d.kind = BM_286_PM_LDT;
    assert(!bm_286_pm_segment_contains(&d, 0, 1));
    d.kind = BM_286_PM_INVALID;
    assert(!bm_286_pm_segment_contains(&d, 0, 1));
}

typedef struct table_fixture {
    uint32_t first;
    unsigned calls, effects, fail_at;
    bool after;
    bm_status_t failure;
    uint8_t bytes[8];
} table_fixture_t;

static bm_status_t table_access(void *context, bm_bus_transaction_t *t)
{
    table_fixture_t *f = context;
    unsigned size = (f->first & 1u) ? 1u : 2u;
    unsigned offset = f->calls * size;
    assert(offset < 8u);
    assert(t->address == ((f->first + offset) & 0xffffffu));
    assert(t->size == size && t->alignment == size);
    assert(t->space == BM_ADDRESS_DATA && t->operation == BM_BUS_READ);
    assert(t->endianness == BM_ENDIAN_LITTLE && t->attributes == 0);
    assert(t->wait_states == 0 && t->value == 0);
    ++f->calls;
    if (f->calls == f->fail_at && !f->after)
        return f->failure;
    ++f->effects;
    t->value = f->bytes[offset];
    if (size == 2u)
        t->value |= (uint32_t)f->bytes[offset + 1u] << 8;
    t->wait_states = 3;
    return f->calls == f->fail_at ? f->failure : BM_STATUS_OK;
}

static void tables(void)
{
    bm_286_table_state_t gdt = {0x1000, 0xffff};
    bm_286_segment_state_t ldt = {8, 0x2000, 0xffff, 0x82, 1};
    bm_286_pm_lookup_t result;
    table_fixture_t f = {0};
    uint32_t raw, limit;
    unsigned odd, local, failure, after, kind;
    static const bm_status_t failures[] = {
        BM_STATUS_DEVICE_ERROR, BM_STATUS_UNMAPPED, BM_STATUS_UNSUPPORTED,
        BM_STATUS_IDLE, BM_STATUS_READ_ONLY
    };
    for (raw = 0; raw < 65536u; ++raw) {
        bool is_local = (raw % 8u) >= 4u;
        f.calls = f.effects = 0;
        f.first = (is_local ? ldt.base : gdt.base) + (raw / 8u) * 8u;
        assert(bm_286_pm_lookup_descriptor(&gdt, &ldt, (uint16_t)raw,
               table_access, &f, &result) == BM_STATUS_OK);
        assert(result.selector_error == (raw / 4u) * 4u);
        assert(result.reason == (raw < 4u ? BM_286_PM_NULL_SELECTOR : BM_286_PM_FOUND));
        assert(f.calls == (raw < 4u ? 0u : 4u));
        assert(result.waits == f.calls * 3u);
    }
    for (local = 0; local < 2; ++local) {
        for (limit = 0; limit < 65536u; ++limit) {
            uint16_t selector = (uint16_t)((limit & 0xfff8u) | (local ? 4u : 0u));
            bool fits = limit % 8u == 7u;
            gdt.limit = ldt.limit = (uint16_t)limit;
            f.first = (local ? ldt.base : gdt.base) + (selector & 0xfff8u);
            f.calls = f.effects = 0;
            assert(bm_286_pm_lookup_descriptor(&gdt, &ldt, selector,
                   table_access, &f, &result) == BM_STATUS_OK);
            assert(result.reason == (selector == 0 ? BM_286_PM_NULL_SELECTOR :
                   fits ? BM_286_PM_FOUND : BM_286_PM_TABLE_LIMIT));
            assert(f.calls == ((selector != 0 && fits) ? 4u : 0u));
        }
    }
    gdt.limit = ldt.limit = 0xffff;
    ldt.valid = 0;
    f.calls = 0;
    assert(bm_286_pm_lookup_descriptor(&gdt, &ldt, 7, table_access, &f, &result) == BM_STATUS_OK);
    assert(result.reason == BM_286_PM_NO_LDT && result.selector_error == 4 && f.calls == 0);
    ldt.valid = 1;
    for (local = 0; local < 2; ++local) {
        for (odd = 0; odd < 2; ++odd) {
            unsigned count = odd ? 8u : 4u;
            /* Entry crosses physical 24-bit wrap; A20 must remain untouched. */
            gdt.base = ldt.base = 0xfffff4u + odd;
            f.first = 0xfffffcu + odd;
            for (kind = 0; kind < sizeof(failures) / sizeof(failures[0]); ++kind)
                for (after = 0; after < 2; ++after)
                    for (failure = 1; failure <= count; ++failure) {
                        unsigned i;
                        f.calls = f.effects = 0;
                        f.fail_at = failure;
                        f.after = after != 0;
                        f.failure = failures[kind];
                        memset(&result, 0xff, sizeof(result));
                        assert(bm_286_pm_lookup_descriptor(&gdt, &ldt,
                               (uint16_t)(local ? 12 : 8), table_access, &f, &result) == failures[kind]);
                        assert(result.reason == BM_286_PM_NOT_READ);
                        assert(result.waits == (failure - 1u) * 3u);
                        assert(f.calls == failure && f.effects == failure - (after ? 0u : 1u));
                        for (i = 0; i < 8; ++i) assert(result.bytes[i] == 0);
                        assert(result.descriptor.kind == BM_286_PM_INVALID);
                    }
            f.fail_at = 0;
            f.calls = f.effects = 0;
            f.bytes[0] = 0x34; f.bytes[1] = 0x12;
            f.bytes[2] = 0x78; f.bytes[3] = 0x56; f.bytes[4] = 0x34; f.bytes[5] = 0x92;
            assert(bm_286_pm_lookup_descriptor(&gdt, &ldt,
                   (uint16_t)(local ? 12 : 8), table_access, &f, &result) == BM_STATUS_OK);
            assert(result.reason == BM_286_PM_FOUND && result.waits == count * 3u);
            assert(result.descriptor.base == 0x345678 && result.descriptor.limit == 0x1234);
            assert(memcmp(result.bytes, f.bytes, 8) == 0);
        }
    }
    gdt.base = 0x1000000;
    f.calls = 0;
    assert(bm_286_pm_lookup_descriptor(&gdt, &ldt, 8, table_access, &f, &result) == BM_STATUS_INVALID_STATE);
    assert(f.calls == 0 && result.reason == BM_286_PM_NOT_READ);
    assert(bm_286_pm_lookup_descriptor(NULL, &ldt, 8, table_access, &f, &result) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_286_pm_lookup_descriptor(&gdt, &ldt, 8, NULL, &f, &result) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_286_pm_lookup_descriptor(&gdt, &ldt, 8, table_access, &f, NULL) == BM_STATUS_INVALID_ARGUMENT);
}

static void load_plans(void)
{
    bm_286_table_state_t gdt = {0x1000, 15};
    bm_286_segment_state_t ldt = {8, 0x2000, 15, 0x82, 1};
    bm_286_pm_load_plan_t plan;
    table_fixture_t f = {0};
    unsigned target, access, cpl, rpl, local, odd;
    /* Allowed low-five-bit access classes, independently enumerated. */
    static const bool data_types[32] = {
        false,false,false,false,false,false,false,false,
        false,false,false,false,false,false,false,false,
        true,true,true,true,true,true,true,true,
        false,false,true,true,false,false,true,true
    };
    f.bytes[0] = 0xcd; f.bytes[1] = 0xab;
    f.bytes[2] = 0x56; f.bytes[3] = 0x34; f.bytes[4] = 0x12;
    for (target = 0; target < 3; ++target)
    for (access = 0; access < 256; ++access)
    for (cpl = 0; cpl < 4; ++cpl)
    for (rpl = 0; rpl < 4; ++rpl)
    for (local = 0; local < 2; ++local)
    for (odd = 0; odd < 2; ++odd) {
        uint16_t selector = (uint16_t)(8u + local * 4u + rpl);
        unsigned type = access % 32u, dpl = (access / 32u) % 4u;
        bool readable_conforming = type == 30 || type == 31;
        bool allowed;
        unsigned vector = 0, error = 0;
        bool early = target == BM_286_PM_LOAD_LDT && (cpl != 0 || local);
        gdt.base = 0x1000u + odd; ldt.base = 0x2000u + odd;
        f.first = (local ? ldt.base : gdt.base) + 8u;
        f.calls = f.effects = 0;
        f.bytes[5] = (uint8_t)access;
        if (target == BM_286_PM_LOAD_DATA)
            allowed = data_types[type] &&
                (readable_conforming || (dpl >= cpl && dpl >= rpl));
        else if (target == BM_286_PM_LOAD_STACK)
            allowed = (type == 18 || type == 19 || type == 22 || type == 23) &&
                dpl == cpl && rpl == cpl;
        else
            allowed = cpl == 0 && !local && type == 2;
        if (!allowed) vector = 13;
        else if (access < 128) vector = target == BM_286_PM_LOAD_STACK ? 12u : 11u;
        if (vector != 0 && !(target == BM_286_PM_LOAD_LDT && cpl != 0))
            error = 8u + local * 4u;
        memset(&plan, 0xff, sizeof(plan));
        assert(bm_286_pm_prepare_load(&gdt, &ldt, selector, (uint8_t)cpl,
               (bm_286_pm_load_target_t)target, table_access, &f, &plan) == BM_STATUS_OK);
        assert(plan.fault_vector == vector && plan.fault_error == error);
        assert(plan.prepared == (vector == 0));
        assert(f.calls == (early ? 0u : odd ? 8u : 4u));
        assert(plan.waits == f.calls * 3u);
        assert(f.bytes[5] == access); /* No accessed write hidden in prepare. */
        if (plan.prepared) {
            assert(plan.segment.selector == selector && plan.segment.valid);
            assert(plan.segment.base == 0x123456 && plan.segment.limit == 0xabcd);
            assert(plan.segment.access == access);
            assert(plan.needs_accessed_write ==
                   (target != BM_286_PM_LOAD_LDT && access % 2u == 0));
        } else {
            assert(!plan.segment.valid && !plan.needs_accessed_write);
        }
    }
    for (target = 0; target < 3; ++target)
    for (cpl = 0; cpl < 4; ++cpl)
    for (rpl = 0; rpl < 4; ++rpl) {
        bool reject = target == BM_286_PM_LOAD_STACK ||
                      (target == BM_286_PM_LOAD_LDT && cpl != 0);
        f.calls = 0;
        assert(bm_286_pm_prepare_load(&gdt, &ldt, (uint16_t)rpl, (uint8_t)cpl,
               (bm_286_pm_load_target_t)target, table_access, &f, &plan) == BM_STATUS_OK);
        assert(plan.prepared == !reject && plan.fault_vector == (reject ? 13 : 0));
        assert(plan.fault_error == 0 && !plan.segment.valid && f.calls == 0);
        if (!reject) assert(plan.segment.selector == rpl);
    }
    for (odd = 0; odd < 2; ++odd) {
        unsigned fail, after;
        gdt.base = 0x1000u + odd;
        f.first = gdt.base + 8;
        f.bytes[5] = 0x92;
        for (after = 0; after < 2; ++after)
        for (fail = 1; fail <= (odd ? 8u : 4u); ++fail) {
            f.calls = f.effects = 0; f.fail_at = fail;
            f.after = after != 0; f.failure = BM_STATUS_DEVICE_ERROR;
            assert(bm_286_pm_prepare_load(&gdt, &ldt, 8, 0, BM_286_PM_LOAD_DATA,
                   table_access, &f, &plan) == BM_STATUS_DEVICE_ERROR);
            assert(!plan.prepared && plan.fault_vector == 0 && !plan.segment.valid);
            assert(plan.waits == (fail - 1u) * 3u);
            assert(f.effects == fail - (after ? 0u : 1u));
        }
    }
    f.fail_at = 0; f.calls = 0; gdt.limit = 14;
    assert(bm_286_pm_prepare_load(&gdt, &ldt, 11, 0, BM_286_PM_LOAD_DATA,
           table_access, &f, &plan) == BM_STATUS_OK);
    assert(plan.fault_vector == 13 && plan.fault_error == 8 && f.calls == 0);
    ldt.valid = 0;
    assert(bm_286_pm_prepare_load(&gdt, &ldt, 15, 0, BM_286_PM_LOAD_STACK,
           table_access, &f, &plan) == BM_STATUS_OK);
    assert(plan.fault_vector == 13 && plan.fault_error == 12 && f.calls == 0);
    assert(bm_286_pm_prepare_load(&gdt, &ldt, 8, 4, BM_286_PM_LOAD_DATA,
           table_access, &f, &plan) == BM_STATUS_INVALID_ARGUMENT);
    assert(!plan.prepared && !plan.fault_vector);
}

int main(void)
{
    selectors();
    descriptors();
    ranges();
    tables();
    load_plans();
    selectors();
    return 0;
}
