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

int main(void)
{
    selectors();
    descriptors();
    ranges();
    selectors();
    return 0;
}
