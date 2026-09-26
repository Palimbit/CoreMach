/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 BluMach contributors
 * Authored Intel 286 PRM B-51/B-52 and 10.1 checks. Private helper tests,
 * not executed protected programs, silicon traces or PCS286 firmware.
 */
#include "descriptor_286.h"
#include <blumach/platforms/null_host.h>
#include <assert.h>
#include <string.h>

typedef struct fixture {
    /* Synthetic low-16 address alias; trace assertions also check high bits. */
    uint8_t ram[65536];
    bm_bus_transaction_t trace[32];
    unsigned calls, effects, writes, fail, locks, unlocks;
    bool locked, after, change_access;
    uint32_t access_address;
    uint8_t replacement;
    bm_status_t failure;
    const bm_286_arch_state_t *observed;
    bm_286_arch_state_t before;
} fixture_t;

static void same(const bm_286_arch_state_t *a, const bm_286_arch_state_t *b)
{
#define EQ(x) assert(a->x == b->x)
    EQ(size); EQ(version);
    EQ(ax); EQ(cx); EQ(dx); EQ(bx); EQ(sp); EQ(bp); EQ(si); EQ(di);
    EQ(ip); EQ(flags); EQ(msw); EQ(cpl); EQ(halted); EQ(shutdown);
    EQ(interrupt_shadow); EQ(trap_pending); EQ(nmi_pending); EQ(nmi_blocked);
    EQ(gdtr.base); EQ(gdtr.limit); EQ(idtr.base); EQ(idtr.limit);
#define SEG(x) EQ(x.selector); EQ(x.base); EQ(x.limit); EQ(x.valid); EQ(x.access)
    SEG(cs); SEG(ds); SEG(ss); SEG(es); SEG(ldtr); SEG(tr);
#undef SEG
#undef EQ
}

static bm_status_t access_bus(void *context, bm_bus_transaction_t *t)
{
    fixture_t *f = context;
    unsigned i;
    if (f->observed) same(f->observed, &f->before);
    assert(t->space == BM_ADDRESS_DATA && t->endianness == BM_ENDIAN_LITTLE);
    assert(t->address <= 0xffffffu);
    assert(t->size == 1 || (t->size == 2 && !(t->address & 1)));
    assert(t->alignment == t->size && !t->wait_states);
    assert(t->attributes == (f->locked ? BM_BUS_TRANSACTION_LOCKED : 0u));
    assert(f->calls < 32); f->trace[f->calls++] = *t;
    if (f->calls == f->fail && !f->after) return f->failure;
    ++f->effects;
    if (t->operation == BM_BUS_READ) t->value = 0;
    else { assert(t->operation == BM_BUS_WRITE); ++f->writes; }
    for (i = 0; i < t->size; ++i) {
        unsigned at = (unsigned)(t->address + i) & 65535u;
        if (t->operation == BM_BUS_READ) t->value |= (uint64_t)f->ram[at] << (i * 8);
        else f->ram[at] = (uint8_t)(t->value >> (i * 8));
    }
    t->wait_states = 3;
    return f->calls == f->fail ? f->failure : BM_STATUS_OK;
}

static void lock_bus(void *context, int asserted)
{
    fixture_t *f = context;
    if (f->observed) same(f->observed, &f->before);
    assert(f->locked != (asserted != 0)); f->locked = asserted != 0;
    if (asserted) {
        ++f->locks;
        if (f->change_access) f->ram[f->access_address & 65535u] = f->replacement;
    } else ++f->unlocks;
}

static void word(fixture_t *f, uint32_t at, uint16_t v)
{
    f->ram[at & 65535u] = (uint8_t)v;
    f->ram[(at + 1) & 65535u] = (uint8_t)(v >> 8);
}

static void descriptor(fixture_t *f, uint32_t at, uint8_t access)
{
    word(f, at, 0x789a); word(f, at + 2, 0x3456);
    f->ram[(at + 4) & 65535u] = 0x12;
    f->ram[(at + 5) & 65535u] = access;
    word(f, at + 6, 0xa5ff); /* Reserved word must not become 386 attributes. */
}

static void frame(fixture_t *f, const bm_286_arch_state_t *a, uint16_t selector, uint16_t flags)
{
    word(f, a->ss.base + a->sp, 0x5678);
    word(f, a->ss.base + a->sp + 2, selector);
    word(f, a->ss.base + a->sp + 4, flags);
}

static void setup(fixture_t *f, bm_286_arch_state_t *a, bm_286_config_t *c,
    unsigned cpl, unsigned odd_stack, unsigned odd_table)
{
    memset(f, 0, sizeof(*f)); memset(a, 0, sizeof(*a)); memset(c, 0, sizeof(*c));
    a->size = sizeof(*a); a->version = BM_286_STATE_VERSION;
    a->msw = 0xfff1; a->cpl = (uint8_t)cpl; a->sp = 0x8000;
    a->flags = 0x0202; a->ip = 0x100; a->ax = 0xabcd; a->cx = 3;
    a->cs.selector = (uint16_t)(24 + cpl); a->cs.base = 0x4000;
    a->cs.access = (uint8_t)(0x9b | (cpl << 5)); a->cs.limit = 0xffff; a->cs.valid = 1;
    a->ss.selector = (uint16_t)(16 + cpl); a->ss.valid = 1;
    a->ss.limit = 0xffff; a->ss.access = (uint8_t)(0x93 | (cpl << 5));
    a->ss.base = odd_stack; a->gdtr.base = 0x2000 + odd_table; a->gdtr.limit = 31;
    a->nmi_blocked = a->nmi_pending = a->trap_pending = 1;
    a->interrupt_shadow = BM_286_SHADOW_SS_LOAD;
    f->access_address = a->gdtr.base + 13;
    descriptor(f, a->gdtr.base + 8, (uint8_t)(0x9a | (cpl << 5)));
    frame(f, a, (uint16_t)(8 + cpl), 0x4703);
    c->size = sizeof(*c); c->version = BM_286_CONTRACT_VERSION;
    c->access = access_bus; c->access_context = f;
    c->bus_lock = lock_bus; c->pin_context = f;
    f->failure = BM_STATUS_DEVICE_ERROR;
}

static void observe(fixture_t *f, const bm_286_arch_state_t *a)
{
    f->before = *a; f->observed = a;
}

static void success_and_failures(void)
{
    static const bm_status_t errors[] = {BM_STATUS_DEVICE_ERROR, BM_STATUS_UNMAPPED,
        BM_STATUS_READ_ONLY, BM_STATUS_IDLE, BM_STATUS_UNSUPPORTED};
    fixture_t f;
    bm_286_arch_state_t a, expected;
    bm_286_config_t c;
    bm_286_segment_load_result_t r;
    unsigned cpl, os, ot, fail, after, kind, calls, i;
    for (cpl = 0; cpl < 4; ++cpl) for (os = 0; os < 2; ++os)
    for (ot = 0; ot < 2; ++ot) {
        setup(&f, &a, &c, cpl, os, ot); observe(&f, &a); expected = a;
        expected.cs.selector = (uint16_t)(8 + cpl); expected.cs.base = 0x123456;
        expected.cs.limit = 0x789a; expected.cs.access = (uint8_t)(0x9b | (cpl << 5));
        expected.ip = 0x5678; expected.sp += 6; expected.flags = 0x4703;
        expected.nmi_blocked = 0;
        assert(bm_286_pm_iret(&a, &c, 0x105, &r) == BM_STATUS_OK && r.loaded);
        same(&a, &expected);
        calls = (os ? 6u : 3u) + (ot ? 8u : 4u) + 2u;
        assert(f.calls == calls && f.writes == 1 && r.waits == calls * 3u);
        assert(!r.fault_vector && !r.fault_error && !f.locked && f.locks == 1 && f.unlocks == 1);
        assert(f.trace[0].address == a.ss.base + 0x8002);
        for (i = 0; i < calls - 2; ++i)
            assert(f.trace[i].operation == BM_BUS_READ && !f.trace[i].attributes);
        assert(f.trace[calls - 2].address == f.access_address);
        assert(f.trace[calls - 1].address == f.access_address);
        assert(f.trace[calls - 1].operation == BM_BUS_WRITE);
        for (kind = 0; kind < sizeof(errors) / sizeof(errors[0]); ++kind)
        for (fail = 1; fail <= calls; ++fail) for (after = 0; after < 2; ++after) {
            setup(&f, &a, &c, cpl, os, ot); observe(&f, &a);
            f.fail = fail; f.after = after != 0; f.failure = errors[kind];
            assert(bm_286_pm_iret(&a, &c, 0x105, &r) == errors[kind]);
            same(&a, &f.before);
            assert(!r.loaded && !r.fault_vector && !r.fault_error);
            assert(r.waits == (fail - 1) * 3u && f.calls == fail);
            assert(f.effects == fail - (after ? 0u : 1u));
            assert(!f.locked && f.locks == f.unlocks);
            assert(f.locks == (fail > calls - 2 ? 1u : 0u));
            assert(f.writes == (fail == calls && after ? 1u : 0u));
            assert(f.ram[f.access_address] == ((0x9a | (cpl << 5)) |
                (fail == calls && after ? 1u : 0u)));
        }
    }
}

/* Bit-by-bit oracle, independently listing architectural flag permissions. */
static uint16_t restored_flags(uint16_t old, uint16_t saved, unsigned cpl)
{
    uint16_t result = 2;
    unsigned bit;
    for (bit = 0; bit < 16; ++bit) {
        bool value;
        if (bit == 1 || bit == 3 || bit == 5 || bit == 15) continue;
        value = (saved & (1u << bit)) != 0;
        if ((bit == 12 || bit == 13) && cpl != 0) value = (old & (1u << bit)) != 0;
        if (bit == 9 && cpl > ((old >> 12) & 3u)) value = (old & (1u << bit)) != 0;
        if (value) result |= (uint16_t)(1u << bit);
    }
    return result;
}

static void flags_matrix(void)
{
    fixture_t f;
    bm_286_arch_state_t a, initial;
    bm_286_config_t c;
    bm_286_segment_load_result_t r;
    unsigned cpl, iopl, old_if, saved;
    for (cpl = 0; cpl < 4; ++cpl) for (iopl = 0; iopl < 4; ++iopl)
    for (old_if = 0; old_if < 2; ++old_if) {
        setup(&f, &initial, &c, cpl, cpl & 1, iopl & 1);
        initial.flags = (uint16_t)(0x0dd7 | (iopl << 12) | (old_if << 9));
        for (saved = 0; saved < 65536; ++saved) {
            a = initial; f.calls = f.effects = f.writes = f.locks = f.unlocks = 0;
            word(&f, a.ss.base + a.sp + 4, (uint16_t)saved);
            assert(bm_286_pm_iret(&a, &c, 0x105, &r) == BM_STATUS_OK && r.loaded);
            assert(a.flags == restored_flags(initial.flags, (uint16_t)saved, cpl));
            assert(a.cpl == cpl && a.nmi_pending && !a.nmi_blocked && a.trap_pending);
            assert(a.interrupt_shadow == BM_286_SHADOW_SS_LOAD);
        }
    }
}

static void selectors_and_types(void)
{
    fixture_t f;
    bm_286_arch_state_t a, before;
    bm_286_config_t c;
    bm_286_segment_load_result_t r;
    unsigned cpl, rpl, access, local, null_value;
    for (cpl = 0; cpl < 4; ++cpl) for (rpl = 0; rpl < 4; ++rpl)
    for (access = 0; access < 256; ++access) for (local = 0; local < 2; ++local) {
        unsigned selector = (local ? 4u : 8u) + rpl;
        unsigned dpl = (access >> 5) & 3u;
        bool code = (access & 0x18u) == 0x18u;
        bool privilege = (access & 4u) ? dpl <= rpl : dpl == rpl;
        bool null_outer_ss = false;
        uint8_t vector = 0;
        bm_status_t status = BM_STATUS_OK;
        setup(&f, &a, &c, cpl, rpl & 1, access & 1);
        a.ldtr.valid = 1; a.ldtr.base = 0x4000 + (access & 1); a.ldtr.limit = 7;
        descriptor(&f, local ? a.ldtr.base : a.gdtr.base + 8, (uint8_t)access);
        frame(&f, &a, (uint16_t)selector, 2); before = a;
        if (rpl < cpl) vector = 13;
        else if (!code || !privilege) vector = 13;
        else if (!(access & 0x80u)) vector = 11;
        else if (rpl > cpl) { vector = 13; null_outer_ss = true; } /* Fixture SS word is null. */
        assert(bm_286_pm_iret(&a, &c, 0x105, &r) == status);
        assert(r.fault_vector == vector && r.loaded == (status == BM_STATUS_OK && !vector));
        if (r.loaded) {
            assert(a.cs.selector == selector && a.cs.access == (access | 1u));
            assert(a.cs.base == 0x123456 && a.cpl == cpl && f.writes == 1);
        } else {
            same(&a, &before); assert(!f.writes && !f.locks);
            assert(r.fault_error == (vector && !null_outer_ss ? (selector & 0xfffcu) : 0u));
        }
    }
    for (cpl = 0; cpl < 4; ++cpl) for (null_value = 0; null_value < 4; ++null_value) {
        setup(&f, &a, &c, cpl, 0, 0); frame(&f, &a, (uint16_t)null_value, 2); before = a;
        assert(bm_286_pm_iret(&a, &c, 0x105, &r) == BM_STATUS_OK);
        assert(!r.loaded && r.fault_vector == 13);
        assert(!r.fault_error && !f.writes && f.calls == 1); same(&a, &before);
    }
}

static void rejections_and_precedence(void)
{
    fixture_t f;
    bm_286_arch_state_t a;
    bm_286_config_t c;
    bm_286_segment_load_result_t r;
    unsigned n;
    for (n = 0; n < 25; ++n) {
        bm_status_t status = BM_STATUS_OK;
        uint8_t vector = 13; uint16_t error = 8;
        setup(&f, &a, &c, 1, 0, 0);
        switch (n) {
        case 0: a.gdtr.limit = 14; break;
        case 1: frame(&f, &a, 5, 2); error = 4; break; /* Unusable LDT */
        case 2: a.ldtr.valid = 1; a.ldtr.base = 0x4000; a.ldtr.limit = 6;
                frame(&f, &a, 5, 2); error = 4; break;
        case 3: word(&f, a.ss.base + a.sp, 0x789b); error = 0; break;
        case 4: a.ss.limit = 0x8004; vector = 12; error = 0; break;
        case 5: a.sp = 0xffff; vector = 12; error = 0; break;
        case 6: a.ss.limit = 0x8002; frame(&f, &a, 8, 2); vector = 12; error = 0; break;
        case 7: a.ss.limit = 0x8003; frame(&f, &a, 8, 2); break; /* RPL before full frame */
        case 8: a.ss.limit = 0x8003; frame(&f, &a, 1, 2); vector = 12; error = 0; break;
        case 9: f.ram[a.gdtr.base + 13] = 0x1a; break; /* DPL before presence */
        case 10: f.ram[a.gdtr.base + 13] = 0x3a;
                 word(&f, a.sp, 0xffff); vector = 11; break; /* Presence before IP */
        case 11: a.flags |= 0x4000; a.ss.valid = 0; vector = 10; error = 0; break;
        case 12: c.bus_lock = NULL; status = BM_STATUS_UNSUPPORTED; break;
        case 13: a.ss.valid = 0; status = BM_STATUS_INVALID_STATE; break;
        case 14: a.ss.base = 0x1000000; status = BM_STATUS_INVALID_STATE; break;
        case 15: a.ss.access = 0xb0; status = BM_STATUS_INVALID_STATE; break;
        case 16: a.gdtr.base = 0x1000000; status = BM_STATUS_INVALID_STATE; break;
        case 17: a.msw &= 0xfffe; status = BM_STATUS_INVALID_STATE; break;
        case 18: a.shutdown = 1; status = BM_STATUS_INVALID_STATE; break;
        case 19: a.halted = 1; status = BM_STATUS_INVALID_STATE; break;
        case 20: a.cpl = 4; status = BM_STATUS_INVALID_STATE; break;
        case 21: a.ss.access = 0x33; status = BM_STATUS_INVALID_STATE; break;
        case 22: a.ss.access = 0x93; status = BM_STATUS_INVALID_STATE; break;
        case 23: a.ss.selector = 16; status = BM_STATUS_INVALID_STATE; break;
        case 24: a.ss.access = 0xbb; status = BM_STATUS_INVALID_STATE; break;
        }
        observe(&f, &a);
        assert(bm_286_pm_iret(&a, &c, 0x105, &r) == status); same(&a, &f.before);
        assert(!r.loaded && !f.writes && !f.locked);
        assert(r.fault_vector == (status == BM_STATUS_OK ? vector : 0));
        assert(r.fault_error == (status == BM_STATUS_OK ? error : 0));
    }
    setup(&f, &a, &c, 0, 0, 0); observe(&f, &a);
    assert(bm_286_pm_iret(NULL, &c, 0x105, &r) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_286_pm_iret(&a, NULL, 0x105, &r) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_286_pm_iret(&a, &c, 0x105, NULL) == BM_STATUS_INVALID_ARGUMENT);
    c.access = NULL;
    assert(bm_286_pm_iret(&a, &c, 0x105, &r) == BM_STATUS_INVALID_ARGUMENT);
    assert(!f.calls && !r.loaded && !r.fault_vector); same(&a, &f.before);
}

static void stack_and_ip_boundaries(void)
{
    static const uint16_t limits[] = {0, 1, 2, 3, 4, 5, 6, 0x7fff, 0xfff9, 0xfffa, 0xfffe, 0xffff};
    fixture_t f;
    bm_286_arch_state_t a, initial;
    bm_286_config_t c;
    bm_286_segment_load_result_t r;
    unsigned down, l, sp;
    for (down = 0; down < 2; ++down) for (l = 0; l < sizeof(limits)/sizeof(limits[0]); ++l) {
        setup(&f, &initial, &c, 0, l & 1, down);
        initial.ss.access |= down ? 4 : 0; initial.ss.limit = limits[l];
        for (sp = 0; sp < 65536; ++sp) {
            unsigned j;
            bool fits = true;
            a = initial; a.sp = (uint16_t)sp;
            f.calls = f.effects = f.writes = f.locks = f.unlocks = 0;
            /* Keep this range oracle's descriptor clear of every stack frame. */
            a.gdtr.base = (sp < 0x8000 ? 0xc000u : 0x2000u) + down;
            frame(&f, &a, 8, 2);
            descriptor(&f, a.gdtr.base + 8, 0x9a);
            for (j = 0; j < 6; ++j)
                if (sp + j > 65535 || (down ? sp + j <= limits[l] : sp + j > limits[l])) fits = false;
            assert(bm_286_pm_iret(&a, &c, 0x105, &r) == BM_STATUS_OK);
            assert(r.loaded == fits);
            if (fits) assert(a.sp == (uint16_t)(sp + 6));
            else assert(r.fault_vector == 12 && !r.fault_error && !f.writes);
        }
    }
    /* Every 16-bit IP against a code limit, with last valid inclusive. */
    setup(&f, &initial, &c, 0, 1, 1);
    for (sp = 0; sp < 65536; ++sp) {
        a = initial; f.calls = f.effects = f.writes = f.locks = f.unlocks = 0;
        word(&f, a.ss.base + a.sp, (uint16_t)sp);
        assert(bm_286_pm_iret(&a, &c, 0x105, &r) == BM_STATUS_OK);
        assert(r.loaded == (sp <= 0x789a));
        if (!r.loaded) assert(r.fault_vector == 13 && !r.fault_error && !f.writes);
    }
}

static void roundtrip(void)
{
    fixture_t f;
    bm_286_arch_state_t a, original;
    bm_286_config_t c;
    bm_286_segment_load_result_t r;
    bm_286_pm_event_t event = {0};
    unsigned cpl, odd, trap, error;
    for (cpl = 0; cpl < 4; ++cpl) for (odd = 0; odd < 2; ++odd)
    for (trap = 0; trap < 2; ++trap) for (error = 0; error < 2; ++error) {
        setup(&f, &a, &c, cpl, odd, odd);
        a.cs.selector = (uint16_t)(8 + cpl); a.cs.base = 0x123456;
        a.cs.limit = 0x789a; a.flags = 0x7fd7; a.ip = 0x5678;
        a.trap_pending = 0; a.interrupt_shadow = BM_286_SHADOW_NONE;
        original = a;
        a.idtr.base = 0x1000 + odd; a.idtr.limit = 7;
        original.idtr = a.idtr;
        word(&f, a.idtr.base, 0x1234); word(&f, a.idtr.base + 2, (uint16_t)(24 + cpl));
        f.ram[a.idtr.base + 5] = (uint8_t)(0x86 | trap | (cpl << 5));
        descriptor(&f, a.gdtr.base + 24, (uint8_t)(0x9a | (cpl << 5)));
        event.return_ip = a.ip; event.has_error = error != 0; event.error_code = 0;
        assert(bm_286_pm_enter_event(&a, &c, &event, &r) == BM_STATUS_OK && r.loaded);
        assert(!(a.flags & 0x4100) && a.nmi_blocked);
        f.calls = f.effects = f.writes = f.locks = f.unlocks = 0;
        if (error) {
            /* A private synthetic handler must explicitly discard its error word.
             * Without that action saved IP becomes CS and must fail validation. */
            bm_286_arch_state_t before = a;
            assert(bm_286_pm_iret(&a, &c, 0x105, &r) == BM_STATUS_OK && !r.loaded);
            assert(r.fault_vector == 13); same(&a, &before);
            a.sp += 2; f.calls = f.effects = f.writes = f.locks = f.unlocks = 0;
        }
        assert(bm_286_pm_iret(&a, &c, 0x105, &r) == BM_STATUS_OK && r.loaded);
        original.nmi_blocked = 0; same(&a, &original);
    }
}

static void wrapping_and_accessed(void)
{
    fixture_t f;
    bm_286_arch_state_t a;
    bm_286_config_t c;
    bm_286_segment_load_result_t r;
    unsigned odd, byte, already;
    for (odd = 0; odd < 2; ++odd) {
        setup(&f, &a, &c, 0, odd, odd);
        a.ss.base = 0xff7ffbu + odd; /* Physical frame crosses FFFFFF. */
        a.sp = 0x8002; /* IP physical FFFFFD/FFFFFE; FLAGS at 1/2. */
        a.gdtr.base = 0x12000 + odd;
        descriptor(&f, a.gdtr.base + 8, 0x9a); frame(&f, &a, 8, 2);
        assert(bm_286_pm_iret(&a, &c, 0x105, &r) == BM_STATUS_OK && r.loaded);
        assert(f.trace[0].address == ((0xff7ffbu + odd + 0x8004) & 0xffffffu));
        setup(&f, &a, &c, 0, odd, odd);
        a.gdtr.base = 0xfffff4u + odd;
        descriptor(&f, a.gdtr.base + 8, 0x9a);
        assert(bm_286_pm_iret(&a, &c, 0x105, &r) == BM_STATUS_OK && r.loaded);
        assert(f.trace[f.calls - 1].address == 1u + odd);
    }
    for (already = 0; already < 2; ++already) for (byte = 0; byte < 256; ++byte) {
        setup(&f, &a, &c, 0, 0, 0);
        f.ram[f.access_address] |= (uint8_t)already;
        f.change_access = true; f.replacement = (uint8_t)byte;
        assert(bm_286_pm_iret(&a, &c, 0x105, &r) == BM_STATUS_OK && r.loaded);
        assert(f.ram[f.access_address] == (byte | 1u) && a.cs.access == 0x9b);
    }
    /* FLAGS aliases the descriptor access byte: all frame reads precede A. */
    setup(&f, &a, &c, 0, 0, 0);
    a.sp = 0x2009; word(&f, 0x2009, 0); word(&f, 0x200b, 8);
    word(&f, 0x200d, 0x029a); /* P=1, executable; returned FLAGS must see A=0. */
    assert(bm_286_pm_iret(&a, &c, 0x105, &r) == BM_STATUS_OK && r.loaded);
    assert(a.flags == 0x0292 && f.ram[0x200d] == 0x9b);
    /* Restoring NT is legal; a subsequent return selects the task
     * path BEFORE ordinary stack reads, even when that stack is now invalid. */
    setup(&f, &a, &c, 3, 1, 0); frame(&f, &a, 11, 0x4002);
    assert(bm_286_pm_iret(&a, &c, 0x105, &r) == BM_STATUS_OK && r.loaded);
    assert(a.flags & 0x4000); f.calls = 0; a.ss.valid = 0;
    observe(&f, &a);
    assert(bm_286_pm_iret(&a, &c, 0x105, &r) == BM_STATUS_OK);
    assert(!f.calls && !r.loaded && r.fault_vector==10 && !r.fault_error); same(&a, &f.before);
}

static void public_gate_and_isolation(void)
{
    fixture_t f, other;
    bm_286_arch_state_t a, b, inspected;
    bm_286_config_t c, d;
    bm_286_segment_load_result_t r;
    bm_host_services_t host; bm_cpu_t cpu;
    bm_286_boundary_t boundary; uint64_t cycles = 99;
    setup(&f, &a, &c, 0, 0, 0); setup(&other, &b, &d, 3, 1, 1);
    assert(bm_286_pm_iret(&a, &c, 0x105, &r) == BM_STATUS_OK && r.loaded);
    assert(!other.calls && b.ip == 0x100 && b.sp == 0x8000 && b.nmi_blocked);
    assert(bm_286_pm_iret(&b, &d, 0x105, &r) == BM_STATUS_OK && r.loaded && b.cpl == 3);
    host = bm_null_host_services();
    assert(bm_286_create(&host, &c, &cpu) == BM_STATUS_OK);
    a.nmi_pending = a.trap_pending = 0; a.interrupt_shadow = BM_286_SHADOW_NONE;
    assert(bm_286_set_arch_state(&cpu, &a) == BM_STATUS_OK);
    f.calls = 0;
    assert(bm_286_step_clocked(cpu.context, 0, &cycles) == BM_STATUS_UNSUPPORTED && !cycles && !f.calls);
    assert(bm_286_get_arch_state(&cpu, &inspected) == BM_STATUS_OK); same(&a, &inspected);
    assert(bm_286_step(&cpu, &boundary) == BM_STATUS_INVALID_STATE && !f.calls);
    cpu.ops.destroy(cpu.context);
}

int main(void)
{
    success_and_failures(); flags_matrix(); selectors_and_types();
    rejections_and_precedence(); stack_and_ip_boundaries(); roundtrip();
    wrapping_and_accessed(); public_gate_and_isolation();
    return 0;
}
