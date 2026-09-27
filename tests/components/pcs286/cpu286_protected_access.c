/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 BluMach contributors
 * Authored private access tests, not executed protected programs or chip traces.
 */
#include "access_286.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct fixture {
    uint8_t ram[65536]; /* Synthetic alias; full physical addresses traced. */
    bm_bus_transaction_t trace[128];
    unsigned calls, effects, fail;
    bool after, locked;
    bm_status_t failure;
} fixture_t;

static bm_status_t bus(void *context, bm_bus_transaction_t *t)
{
    fixture_t *f = context;
    unsigned i;
    assert(f->calls < 128 && t->address <= 0xffffffu);
    assert(t->size == 1 || (t->size == 2 && !(t->address & 1u)));
    assert(t->alignment == t->size && t->endianness == BM_ENDIAN_LITTLE);
    assert(t->wait_states == 0);
    assert(t->attributes == (f->locked ? BM_BUS_TRANSACTION_LOCKED : 0u));
    assert(t->space == (t->operation == BM_BUS_FETCH ? BM_ADDRESS_PROGRAM : BM_ADDRESS_DATA));
    f->trace[f->calls++] = *t;
    if (f->calls == f->fail && !f->after) return f->failure;
    ++f->effects;
    if (t->operation != BM_BUS_WRITE) t->value = 0;
    for (i = 0; i < t->size; ++i) {
        unsigned at = (unsigned)(t->address + i) & 65535u;
        if (t->operation == BM_BUS_WRITE) f->ram[at] = (uint8_t)(t->value >> (i * 8u));
        else t->value |= (uint64_t)f->ram[at] << (i * 8u);
    }
    t->wait_states = 3;
    return f->calls == f->fail ? f->failure : BM_STATUS_OK;
}

static void lock(void *context, int asserted)
{
    fixture_t *f = context;
    assert(f->locked != (asserted != 0)); f->locked = asserted != 0;
}

static bm_286_segment_state_t *seg(bm_286_arch_state_t *a, unsigned r)
{
    switch (r) {
        case 0: return &a->es;
        case 1: return &a->cs;
        case 2: return &a->ss;
        default: return &a->ds;
    }
}

static bm_286_arch_state_t arch(void)
{
    bm_286_arch_state_t a = {0};
    a.size = sizeof(a); a.version = BM_286_STATE_VERSION; a.msw = 1;
    a.ds.valid = 1; a.ds.access = 0x93; a.ds.limit = 0xffff; a.ds.selector = 16;
    a.es = a.ss = a.cs = a.ds; a.cs.access = 0x9b; a.cs.selector = 8;
    a.ip = 0x1234; a.sp = 0x8000; a.flags = 2;
    return a;
}

static void expect(bm_286_arch_state_t *a, unsigned r, bm_286_pm_access_kind_t k,
    uint32_t offset, uint32_t length, bm_status_t status, unsigned fault)
{
    bm_286_pm_access_check_t c;
    bm_286_arch_state_t before;
    memcpy(&before, a, sizeof(before));
    assert(bm_286_pm_check_access(a, r, k, offset, length, &c) == status);
    assert(c.fault_vector == fault);
    assert(c.allowed == (status == BM_STATUS_OK && fault == 0));
    assert(memcmp(&before, a, sizeof(before)) == 0);
}

static void permissions(void)
{
    /* Explicit sets of legal cached types and permitted operations. DPL,
     * accessed bit and visible selector do not change per-access rights. */
    static const unsigned legal[] = {0xccffu, 0xff00u, 0x00ccu, 0xccffu};
    unsigned r, ac, k, cpl;
    for (r = 0; r < 4; ++r) for (ac = 0; ac < 256; ++ac)
    for (cpl = 0; cpl < 4; ++cpl) for (k = 0; k < 3; ++k) {
        bm_286_arch_state_t a = arch();
        unsigned type = ac & 15u, fault = 0;
        bm_status_t status = BM_STATUS_OK;
        a.cpl = (uint8_t)cpl; seg(&a, r)->access = (uint8_t)ac;
        seg(&a, r)->selector = (uint16_t)cpl; /* Including visible null. */
        seg(&a, r)->limit = 0x7fff;
        if (k == BM_286_PM_FETCH && r != 1) status = BM_STATUS_INVALID_ARGUMENT;
        else if (ac == 0x82) { /* Real-compatible hidden cache retained through LMSW. */ }
        else if ((ac & 0x90u) != 0x90u || !(legal[r] & (1u << type)))
            status = BM_STATUS_INVALID_STATE;
        else if ((k == BM_286_PM_READ && (type == 8 || type == 9 || type == 12 || type == 13)) ||
                 (k == BM_286_PM_WRITE && !(0x00ccu & (1u << type)))) fault = 13;
        expect(&a, r, (bm_286_pm_access_kind_t)k,
            type >= 4 && type <= 7 ? 0x8000 : 0, 2, status, fault);
    }
    for (r = 0; r < 4; ++r) {
        bm_286_arch_state_t a = arch();
        seg(&a, r)->valid = 0; seg(&a, r)->selector = 0xabcd;
        expect(&a, r, BM_286_PM_READ, 0, 1,
            r == 1 || r == 2 ? BM_STATUS_INVALID_STATE : BM_STATUS_OK,
            r == 1 || r == 2 ? 0 : 13);
    }
}

static void ranges(void)
{
    unsigned limit, r, down, j;
    for (limit = 0; limit < 65536; ++limit) for (r = 0; r < 4; ++r)
    for (down = 0; down <= (r != 1 ? 1u : 0u); ++down) {
        bm_286_arch_state_t a = arch();
        uint32_t offsets[] = {0, limit, limit + 1u, 65534, 65535, 65536, UINT32_MAX};
        seg(&a, r)->limit = (uint16_t)limit;
        if (down) seg(&a, r)->access = 0x97;
        for (j = 0; j < sizeof(offsets) / sizeof(offsets[0]); ++j) {
            unsigned n;
            for (n = 1; n <= 6; ++n) {
                unsigned b; bool ok = true;
                /* Independent wide per-byte oracle, never wraps offsets. */
                for (b = 0; b < n; ++b) {
                    uint64_t at = (uint64_t)offsets[j] + b;
                    if (at > 65535 || (down ? at <= limit : at > limit)) ok = false;
                }
                expect(&a, r, BM_286_PM_READ, offsets[j], n, BM_STATUS_OK,
                    ok ? 0 : (r == 2 ? 12 : 13));
            }
        }
    }
    {
        bm_286_arch_state_t a = arch();
        expect(&a, 1, BM_286_PM_FETCH, 0, 65536, BM_STATUS_OK, 0);
        expect(&a, 2, BM_286_PM_WRITE, 0, UINT32_MAX, BM_STATUS_OK, 12);
        expect(&a, 3, BM_286_PM_READ, UINT32_MAX, UINT32_MAX, BM_STATUS_OK, 13);
        expect(&a, 3, BM_286_PM_READ, 0, 0, BM_STATUS_INVALID_ARGUMENT, 0);
    }
}

static void transfers(void)
{
    static const bm_status_t failures[] = {BM_STATUS_IDLE, BM_STATUS_UNSUPPORTED,
        BM_STATUS_INVALID_ARGUMENT, BM_STATUS_INVALID_STATE, BM_STATUS_DEVICE_ERROR};
    unsigned odd, kind, length, locked, failures_tested = 0;
    for (odd = 0; odd < 2; ++odd) for (kind = 0; kind < 3; ++kind)
    for (length = 1; length <= 10; ++length) for (locked = 0; locked < 2; ++locked) {
        fixture_t f = {0};
        bm_286_arch_state_t a = arch(), before;
        bm_286_pm_access_state_t s = {0};
        bm_286_pm_access_result_t out;
        uint8_t input[10] = {1,2,3,4,5,6,7,8,9,10};
        unsigned r = kind == BM_286_PM_FETCH ? 1 : 3, calls, i, fail, phase, code;
        uint32_t start = 0xfffffcu + odd;
        if (kind == BM_286_PM_FETCH && locked) continue;
        seg(&a, r)->base = start - 0x20;
        memcpy(&before, &a, sizeof(a));
        for (i = 0; i < 10; ++i) f.ram[(start + i) & 65535u] = (uint8_t)(0x80u + i);
        f.locked = locked != 0;
        assert(bm_286_pm_access(&a, r, (bm_286_pm_access_kind_t)kind, 0x20, length,
            locked != 0, input, bus, &f, &s, &out) == BM_STATUS_OK);
        assert(out.completed && !out.fault_vector && !s.stopped);
        calls = f.calls; assert(out.waits == calls * 3u);
        for (i = 0; i < length; ++i) {
            if (kind == BM_286_PM_WRITE) assert(f.ram[(start + i) & 65535u] == input[i]);
            else assert(out.bytes[i] == 0x80u + i);
        }
        for (i = 0; i < calls; ++i) {
            unsigned stride = kind != BM_286_PM_FETCH && !odd ? 2u : 1u;
            assert(f.trace[i].address == ((start + i * stride) & 0xffffffu));
            assert(f.trace[i].size == (length - i * stride < stride ? 1 : stride));
        }
        assert(memcmp(&a, &before, sizeof(a)) == 0);
        for (fail = 1; fail <= calls; ++fail) for (phase = 0; phase < 2; ++phase)
        for (code = 0; code < sizeof(failures) / sizeof(failures[0]); ++code) {
            uint8_t expected[65536] = {0};
            bm_286_pm_access_state_t independent = {0};
            memset(&f, 0, sizeof(f)); s.stopped = false;
            f.fail = fail; f.after = phase != 0; f.failure = failures[code];
            f.locked = locked != 0;
            assert(bm_286_pm_access(&a, r, (bm_286_pm_access_kind_t)kind, 0x20, length,
                locked != 0, input, bus, &f, &s, &out) == failures[code]);
            assert(!out.completed && !out.fault_vector && s.stopped);
            assert(f.calls == fail && f.effects == fail - 1u + phase);
            assert(out.waits == (fail - 1u) * 3u);
            for (i = 0; i < 10; ++i) assert(out.bytes[i] == 0);
            if (kind == BM_286_PM_WRITE) {
                unsigned t;
                for (t = 0; t < f.effects; ++t) for (i = 0; i < f.trace[t].size; ++i)
                    expected[(unsigned)(f.trace[t].address + i) & 65535u] =
                        (uint8_t)(f.trace[t].value >> (i * 8u));
            }
            assert(memcmp(f.ram, expected, sizeof(expected)) == 0);
            assert(memcmp(&a, &before, sizeof(a)) == 0);
            assert(bm_286_pm_access(&a, r, (bm_286_pm_access_kind_t)kind, 0x20, length,
                locked != 0, input, bus, &f, &s, &out) == BM_STATUS_INVALID_STATE);
            assert(f.calls == fail);
            /* A different instance is not poisoned by the stopped instance. */
            f.fail = 0;
            assert(bm_286_pm_access(&a, r, (bm_286_pm_access_kind_t)kind, 0x20, length,
                locked != 0, input, bus, &f, &independent, &out) == BM_STATUS_OK);
            assert(out.completed);
            ++failures_tested;
        }
    }
    printf("protected access: %u per-transfer before/after host failures\n", failures_tested);
}

static void put_word(fixture_t *f, unsigned at, unsigned v)
{
    f->ram[at] = (uint8_t)v; f->ram[at + 1] = (uint8_t)(v >> 8);
}

static void descriptor(fixture_t *f, unsigned at, unsigned base, unsigned limit, unsigned ac)
{
    put_word(f, at, limit); put_word(f, at + 2, base);
    f->ram[at + 4] = (uint8_t)(base >> 16); f->ram[at + 5] = (uint8_t)ac;
}

static void cache_and_retry(void)
{
    unsigned reg;
    for (reg = 1; reg <= 3; ++reg) {
        fixture_t f = {0};
        bm_286_arch_state_t a = arch();
        bm_286_config_t config = {0};
        bm_286_segment_load_result_t load;
        bm_286_pm_delivery_state_t ds = {0};
        bm_286_pm_delivery_result_t delivered;
        bm_286_pm_request_t request = {0};
        bm_286_pm_access_state_t as = {0};
        bm_286_pm_access_result_t out;
        bm_286_pm_access_kind_t kind = reg == 1 ? BM_286_PM_FETCH : BM_286_PM_READ;
        unsigned vector = reg == 2 ? 12u : 13u, before_calls;
        a.gdtr.base = 0x2000; a.gdtr.limit = 31;
        a.idtr.base = 0x1000; a.idtr.limit = 0x7ff;
        descriptor(&f, 0x2008, 0, 0xffff, 0x9b);
        descriptor(&f, 0x2010, 0, 0xffff, 0x93);
        descriptor(&f, 0x2018, 0x4000, 0xff, 0x93);
        config.access = bus; config.access_context = &f;
        config.bus_lock = lock; config.pin_context = &f;
        assert(bm_286_load_segment_state(&a, &config, 3, 24, &load) == BM_STATUS_OK && load.loaded);
        /* Changing the table cannot mutate an already loaded hidden cache. */
        descriptor(&f, 0x2018, 0x5000, 0xffff, 0x91);
        before_calls = f.calls;
        expect(&a, 3, BM_286_PM_WRITE, 0xff, 1, BM_STATUS_OK, 0);
        expect(&a, 3, BM_286_PM_READ, 0x100, 1, BM_STATUS_OK, 13);
        assert(a.ds.base == 0x4000 && f.calls == before_calls);
        /* Failed presence load preserves the usable cache; #NP only here. */
        f.ram[0x201d] = 0x11;
        assert(bm_286_load_segment_state(&a, &config, 3, 24, &load) == BM_STATUS_OK);
        assert(!load.loaded && load.fault_vector == 11 && load.fault_error == 24);
        assert(a.ds.base == 0x4000 && a.ds.limit == 0xff);
        descriptor(&f, 0x2018, 0x5000, 0xffff, 0x93);
        if (reg == 1) a.cs.limit = 0x7fff;
        if (reg == 2) a.ss.limit = 0x7fff; /* Handler stack still fits. */
        before_calls = f.calls;
        assert(bm_286_pm_access(&a, reg, kind, 0xffff, 2, false, NULL,
            bus, &f, &as, &out) == BM_STATUS_OK);
        assert(!out.completed && out.fault_vector == vector && !as.stopped);
        assert(f.calls == before_calls); /* No wrapped second byte/side effect. */
        put_word(&f, 0x1000 + vector * 8, 0x400);
        put_word(&f, 0x1002 + vector * 8, 8); f.ram[0x1005 + vector * 8] = 0x86;
        request.source = BM_286_PM_EXCEPTION; request.vector = (uint8_t)vector;
        request.restart_ip = a.ip; request.error_code = 0;
        assert(bm_286_pm_deliver(&a, &config, &ds, &request, &delivered) == BM_STATUS_OK);
        assert(delivered.entered && delivered.vector == vector && a.ip == 0x400);
        assert(f.ram[a.sp] == 0 && f.ram[a.sp + 1] == 0);
        /* Synthetic handler actions, NOT dispatched protected instructions. */
        a.sp = (uint16_t)(a.sp + 2); /* Remove error word before ordinary IRET. */
        if (reg == 2) {
            assert(bm_286_load_segment_state(&a, &config, 2, 16, &load) == BM_STATUS_OK && load.loaded);
        } else if (reg == 3) {
            assert(bm_286_load_segment_state(&a, &config, 3, 24, &load) == BM_STATUS_OK && load.loaded);
            assert(a.ds.base == 0x5000 && a.ds.limit == 0xffff);
        }
        assert(bm_286_pm_iret(&a, &config, 0x105, &load) == BM_STATUS_OK && load.loaded);
        assert(a.ip == 0x1234 && a.sp == 0x8000);
        /* Caller repairs the bad effective offset; no 16-bit operand wrap. */
        before_calls = f.calls;
        assert(bm_286_pm_access(&a, reg, kind, 0xfffe, 2, false, NULL,
            bus, &f, &as, &out) == BM_STATUS_OK && out.completed);
        assert(f.calls == before_calls + (reg == 1 ? 2u : 1u));
    }
}

static void boundaries_and_invalid(void)
{
    fixture_t f = {0};
    bm_286_arch_state_t a = arch();
    bm_286_pm_access_state_t s = {0};
    bm_286_pm_access_result_t out;
    unsigned i;
    a.cs.limit = 9;
    for (i = 0; i < 10; ++i) {
        assert(bm_286_pm_access(&a, 1, BM_286_PM_FETCH, i, 1, false, NULL,
            bus, &f, &s, &out) == BM_STATUS_OK && out.completed);
    }
    assert(bm_286_pm_access(&a, 1, BM_286_PM_FETCH, 10, 1, false, NULL,
        bus, &f, &s, &out) == BM_STATUS_OK && out.fault_vector == 13);
    assert(f.calls == 10); /* No speculative read past the last requested byte. */
    a.ds.base = 0x100000; f.calls = 0;
    assert(bm_286_pm_access(&a, 3, BM_286_PM_READ, 0, 2, false, NULL,
        bus, &f, &s, &out) == BM_STATUS_OK && out.completed);
    assert(f.trace[0].address == 0x100000); /* No CPU-owned A20 masking. */
    a.ds.base = 0x1000000;
    expect(&a, 3, BM_286_PM_READ, 0, 1, BM_STATUS_INVALID_STATE, 0);
    a = arch(); a.ds.valid = 2;
    expect(&a, 3, BM_286_PM_READ, 0, 1, BM_STATUS_INVALID_STATE, 0);
    a = arch(); a.msw = 0;
    expect(&a, 3, BM_286_PM_READ, 0, 1, BM_STATUS_INVALID_STATE, 0);
    a = arch(); a.shutdown = 1;
    expect(&a, 3, BM_286_PM_READ, 0, 1, BM_STATUS_INVALID_STATE, 0);
    a = arch(); a.cpl = 4;
    expect(&a, 3, BM_286_PM_READ, 0, 1, BM_STATUS_INVALID_STATE, 0);
    a = arch();
    expect(&a, 4, BM_286_PM_READ, 0, 1, BM_STATUS_INVALID_ARGUMENT, 0);
    assert(bm_286_pm_access(&a, 1, BM_286_PM_FETCH, 0, 1, true, NULL,
        bus, &f, &s, &out) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_286_pm_access(&a, 3, BM_286_PM_READ, 0, 11, false, NULL,
        bus, &f, &s, &out) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_286_pm_access(&a, 3, BM_286_PM_WRITE, 0, 1, false, NULL,
        bus, &f, &s, &out) == BM_STATUS_INVALID_ARGUMENT);
}

int main(void)
{
    permissions(); ranges(); transfers(); cache_and_retry(); boundaries_and_invalid();
    puts("protected access: cache permissions, ranges, transfers and private repair/IRET passed");
    return 0;
}
