/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 BluMach contributors
 */
#include "descriptor_286.h"
#include <assert.h>
#include <string.h>

typedef struct fixture {
    uint8_t ram[65536];
    unsigned calls, writes, fail, locks;
    bool locked, after;
} fixture_t;

static bm_status_t access_bus(void *context, bm_bus_transaction_t *t)
{
    fixture_t *f = context;
    unsigned i;
    assert(t->space == BM_ADDRESS_DATA && t->endianness == BM_ENDIAN_LITTLE);
    assert(t->size == 1 || (t->size == 2 && !(t->address & 1)));
    assert(t->alignment == t->size && t->wait_states == 0);
    assert(t->attributes == (f->locked ? BM_BUS_TRANSACTION_LOCKED : 0u));
    ++f->calls;
    if (f->calls == f->fail && !f->after) return BM_STATUS_DEVICE_ERROR;
    if (t->operation == BM_BUS_READ) t->value = 0;
    else { assert(t->operation == BM_BUS_WRITE); ++f->writes; }
    for (i = 0; i < t->size; ++i) {
        unsigned at = (unsigned)(t->address + i) & 65535u;
        if (t->operation == BM_BUS_READ) t->value |= (uint64_t)f->ram[at] << (i * 8);
        else f->ram[at] = (uint8_t)(t->value >> (i * 8));
    }
    t->wait_states = 1;
    return f->calls == f->fail ? BM_STATUS_DEVICE_ERROR : BM_STATUS_OK;
}

static void lock_bus(void *context, int asserted)
{
    fixture_t *f = context;
    assert(f->locked != (asserted != 0));
    f->locked = asserted != 0;
    if (asserted) ++f->locks;
}

static void setup(fixture_t *f, bm_286_arch_state_t *a, bm_286_config_t *c,
    bm_286_pm_event_t *e, unsigned cpl, unsigned odd)
{
    uint8_t *gate, *code;
    memset(f, 0, sizeof(*f)); memset(a, 0, sizeof(*a));
    memset(c, 0, sizeof(*c)); memset(e, 0, sizeof(*e));
    a->msw = 1; a->cpl = (uint8_t)cpl; a->sp = 0x8000;
    a->flags = 0x4702; a->cs.selector = (uint16_t)(24 + cpl);
    a->ss.selector = (uint16_t)(16 + cpl); a->ss.valid = 1;
    a->ss.limit = 0xffff; a->ss.access = (uint8_t)(0x92 | (cpl << 5));
    a->ss.base = odd; a->idtr.base = 0x1000 + odd; a->idtr.limit = 0x7ff;
    a->gdtr.base = 0x2000 + odd; a->gdtr.limit = 0x1f;
    a->halted = a->trap_pending = 1;
    e->vector = 13; e->return_ip = 0x5678; e->error_code = 0xbeef;
    gate = f->ram + a->idtr.base + 13 * 8;
    gate[0] = 0x34; gate[1] = 0x12; gate[2] = 11;
    gate[5] = (uint8_t)(0x86 | (cpl << 5));
    code = f->ram + a->gdtr.base + 8;
    code[0] = code[1] = 0xff; code[3] = 0x30;
    code[5] = (uint8_t)(0x9a | (cpl << 5));
    c->access = access_bus; c->access_context = f;
    c->bus_lock = lock_bus; c->pin_context = f;
}

static uint16_t word(const fixture_t *f, uint32_t at)
{
    return (uint16_t)(f->ram[at & 65535u] | ((uint16_t)f->ram[(at + 1) & 65535u] << 8));
}

static void entries(void)
{
    fixture_t f;
    bm_286_arch_state_t a, before;
    bm_286_config_t c;
    bm_286_pm_event_t e;
    bm_286_segment_load_result_t r;
    unsigned cpl, odd, trap, error, fail, after, calls;
    for (cpl = 0; cpl < 4; ++cpl) for (odd = 0; odd < 2; ++odd)
    for (trap = 0; trap < 2; ++trap) for (error = 0; error < 2; ++error) {
        setup(&f, &a, &c, &e, cpl, odd);
        e.has_error = error != 0; e.software = true;
        f.ram[a.idtr.base + 13 * 8 + 5] |= (uint8_t)trap;
        assert(bm_286_pm_enter_event(&a, &c, &e, &r) == BM_STATUS_OK && r.loaded);
        assert(r.waits == f.calls && !f.locked && f.locks == 1);
        assert(a.ip == 0x1234 && a.cs.selector == 8 + cpl && a.cs.base == 0x3000);
        assert(a.cs.access & 1); assert(a.cpl == cpl);
        assert(a.flags == (trap ? 0x602 : 0x402));
        assert(!a.halted && !a.trap_pending && !a.interrupt_shadow);
        assert(a.sp == 0x8000 - 6 - error * 2);
        assert(word(&f, odd + 0x7ffe) == 0x4702);
        assert(word(&f, odd + 0x7ffc) == 24 + cpl);
        assert(word(&f, odd + 0x7ffa) == 0x5678);
        if (error) assert(word(&f, odd + 0x7ff8) == 0xbeef);
        calls = f.calls;
        for (fail = 1; fail <= calls; ++fail) for (after = 0; after < 2; ++after) {
            setup(&f, &a, &c, &e, cpl, odd);
            e.has_error = error != 0;
            f.ram[a.idtr.base + 13 * 8 + 5] |= (uint8_t)trap;
            memcpy(&before, &a, sizeof(a)); f.fail = fail; f.after = after != 0;
            assert(bm_286_pm_enter_event(&a, &c, &e, &r) == BM_STATUS_DEVICE_ERROR);
            assert(!r.loaded && !r.fault_vector && r.waits == fail - 1);
            assert(!memcmp(&a, &before, sizeof(a)) && !f.locked);
            assert(f.calls == fail);
        }
    }
}

static void rejections(void)
{
    fixture_t f;
    bm_286_arch_state_t a, before;
    bm_286_config_t c;
    bm_286_pm_event_t e;
    bm_286_segment_load_result_t r;
    unsigned n;
    for (n = 0; n < 12; ++n) {
        bm_status_t expected = BM_STATUS_OK;
        uint8_t vector = 13;
        uint16_t error = 107;
        setup(&f, &a, &c, &e, 0, 0); e.external = true;
        switch (n) {
        case 0: a.idtr.limit = 110; break;
        case 1: f.ram[0x106d] = 0x82; break;
        case 2: f.ram[0x106d] = 6; vector = 11; break;
        case 3: f.ram[0x106d] = 0x85; expected = BM_STATUS_UNSUPPORTED; break;
        case 4: f.ram[0x106a] = 0; error = 1; break;
        case 5: f.ram[0x200d] = 0x92; error = 9; break;
        case 6: f.ram[0x200d] = 0x1a; vector = 11; error = 9; break;
        case 7: a.sp = 5; vector = 12; error = 0; break;
        case 8: f.ram[0x2008] = f.ram[0x2009] = 0; error = 0; break;
        case 9: c.bus_lock = NULL; expected = BM_STATUS_UNSUPPORTED; break;
        case 10: a.cpl = 3; e.software = true; break;
        case 11: a.cpl = 3; expected = BM_STATUS_UNSUPPORTED; break;
        }
        memcpy(&before, &a, sizeof(a));
        assert(bm_286_pm_enter_event(&a, &c, &e, &r) == expected);
        assert(!r.loaded && !f.writes && !f.locked && !memcmp(&before, &a, sizeof(a)));
        if (expected == BM_STATUS_OK) assert(r.fault_vector == vector && r.fault_error == error);
    }
    setup(&f, &a, &c, &e, 3, 0);
    f.ram[0x200d] = 0x9e; /* Conforming DPL0 stays at CPL3. */
    a.sp = 0;
    assert(bm_286_pm_enter_event(&a, &c, &e, &r) == BM_STATUS_OK && r.loaded);
    assert(a.sp == 0xfffa && a.cpl == 3 && a.cs.selector == 11);
}

static void boundaries(void)
{
    fixture_t f;
    bm_286_arch_state_t a;
    bm_286_config_t c;
    bm_286_pm_event_t e;
    bm_286_segment_load_result_t r;
    unsigned down, error, invalid;
    for (down = 0; down < 2; ++down) for (error = 0; error < 2; ++error)
    for (invalid = 0; invalid < 2; ++invalid) {
        setup(&f, &a, &c, &e, 0, 0);
        e.has_error = error != 0; a.sp = 0x8001;
        if (down) {
            a.ss.access |= 4;
            a.ss.limit = (uint16_t)(a.sp - 6 - error * 2 - 1 + invalid);
        } else a.ss.limit = (uint16_t)(a.sp - 1 - invalid);
        assert(bm_286_pm_enter_event(&a, &c, &e, &r) == BM_STATUS_OK);
        assert(r.loaded == (invalid == 0));
        if (invalid) assert(r.fault_vector == 12 && !r.fault_error && !f.writes);
    }
    /* Stack preflight takes precedence over an invalid target offset. */
    setup(&f, &a, &c, &e, 0, 0); a.sp = 4; f.ram[0x2009] = 0;
    assert(bm_286_pm_enter_event(&a, &c, &e, &r) == BM_STATUS_OK);
    assert(r.fault_vector == 12 && !f.writes);
    /* LDT index zero is not a null selector, and CS retains TI, not RPL. */
    setup(&f, &a, &c, &e, 0, 0);
    a.ldtr.valid = 1; a.ldtr.base = 0x4000; a.ldtr.limit = 7;
    memcpy(f.ram + 0x4000, f.ram + 0x2008, 8); f.ram[0x106a] = 7;
    assert(bm_286_pm_enter_event(&a, &c, &e, &r) == BM_STATUS_OK && r.loaded);
    assert(a.cs.selector == 4 && (f.ram[0x4005] & 1));
    setup(&f, &a, &c, &e, 0, 0); a.shutdown = 1;
    assert(bm_286_pm_enter_event(&a, &c, &e, &r) == BM_STATUS_INVALID_STATE);
    assert(!f.calls && !r.loaded);
}

int main(void) { entries(); rejections(); boundaries(); return 0; }
