/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Authored real-mode IVT-limit/#8/shutdown tests, not physical timing traces.
 */
#include <blumach/components/cpu_80286.h>
#include <blumach/platforms/null_host.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct fixture {
    bm_cpu_t cpu;
    uint8_t *ram;
    bm_bus_transaction_t trace[32];
    unsigned count, fail_at, after, acks, lock_edges, locked, shut_on, shut_off;
} fixture_t;
static bm_286_arch_state_t state(fixture_t *f)
{
    bm_286_arch_state_t s;
    assert(bm_286_get_arch_state(&f->cpu, &s) == BM_STATUS_OK); return s;
}
static void same(const bm_286_arch_state_t *a, const bm_286_arch_state_t *b)
{
#define EQ(x) assert(a->x == b->x)
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
    assert(t->space != BM_ADDRESS_IO && t->address+t->size <= 0x100000);
    assert(f->count < 32); f->trace[f->count++] = *t;
    if (f->count == f->fail_at && !f->after) return BM_STATUS_DEVICE_ERROR;
    if (t->operation != BM_BUS_WRITE) t->value = 0;
    for (unsigned i = 0; i < t->size; ++i)
        if (t->operation == BM_BUS_WRITE) f->ram[(size_t)t->address+i] = (uint8_t)(t->value >> (8*i));
        else t->value |= (uint64_t)f->ram[(size_t)t->address+i] << (8*i);
    if (f->count == f->fail_at) return BM_STATUS_DEVICE_ERROR;
    t->wait_states = 2; return BM_STATUS_OK;
}
static void lock_changed(void *context, int active)
{
    fixture_t *f = context; f->locked = (unsigned)active; ++f->lock_edges;
}
static void shutdown_changed(void *context, int active)
{
    fixture_t *f = context;
    if (active) { assert(state(f).shutdown && !f->locked); ++f->shut_on; }
    else ++f->shut_off;
}
static bm_status_t ack(void *context, unsigned phase, uint8_t *vector, uint32_t *waits)
{
    fixture_t *f = context;
    assert(f->locked && phase == f->acks); ++f->acks;
    *vector = 0x80; *waits = 3; return BM_STATUS_OK;
}
static void word(fixture_t *f, unsigned address, uint16_t value)
{
    f->ram[address] = (uint8_t)value; f->ram[address+1] = (uint8_t)(value >> 8);
}
static uint16_t read_word(fixture_t *f, unsigned address)
{
    return (uint16_t)(f->ram[address] | (unsigned)f->ram[address+1] << 8);
}
static bm_286_arch_state_t setup(fixture_t *f, unsigned limit, unsigned vector)
{
    assert(f->cpu.ops.reset(f->cpu.context) == BM_STATUS_OK);
    f->count = f->fail_at = f->after = f->acks = f->lock_edges = f->locked = 0;
    f->shut_on = f->shut_off = 0;
    bm_286_arch_state_t s = state(f);
    s.cs.selector = 0x3000; s.cs.base = 0x30000; s.ip = 0x100;
    s.ss.selector = 0x1000; s.ss.base = 0x10000; s.sp = 0x800;
    s.idtr.base = 0x6000; s.idtr.limit = (uint16_t)limit; s.flags = 0x602;
    f->ram[0x30100] = 0x26; f->ram[0x30101] = 0xcd;
    f->ram[0x30102] = (uint8_t)vector; f->ram[0x30103] = 0xf4;
    word(f, 0x6000+vector*4, 0x300); word(f, 0x6002+vector*4, 0x4000);
    word(f, 0x6020, 0x200); word(f, 0x6022, 0x4000);
    word(f, 0x6008, 0x400); word(f, 0x600a, 0x4000);
    return s;
}
static void set(fixture_t *f, const bm_286_arch_state_t *s)
{
    assert(bm_286_set_arch_state(&f->cpu, s) == BM_STATUS_OK);
}
static void matrix(fixture_t *f)
{
    for (unsigned limit = 0; limit < 1024; ++limit) for (unsigned vector = 0; vector < 256; ++vector) {
        bm_286_arch_state_t s = setup(f, limit, vector), a, e = s;
        bm_286_boundary_t b; set(f, &s);
        assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK); a = state(f);
        unsigned fits = vector*4+3 <= limit;
        if (!fits && limit < 35) {
            e.shutdown = 1; same(&a, &e);
            assert(b.kind == BM_286_BOUNDARY_SHUTDOWN && !b.has_vector && f->count == 3);
            assert(f->shut_on == 1 && f->shut_off == 0);
            assert(bm_286_step(&f->cpu, &b) == BM_STATUS_IDLE && f->count == 3 && f->shut_on == 1);
        } else {
            unsigned delivered = fits ? vector : 8;
            e.sp -= 6; e.flags &= 0xfcffU;
            e.cs.selector = 0x4000; e.cs.base = 0x40000;
            e.ip = (uint16_t)(delivered == 8 ? 0x200 : delivered == 2 ? 0x400 : 0x300);
            same(&a, &e);
            assert(b.has_vector && b.vector == delivered);
            assert(b.kind == (fits ? BM_286_BOUNDARY_INSTRUCTION : BM_286_BOUNDARY_EXCEPTION));
            assert(read_word(f, 0x107fa) == (fits ? 0x103U : 0x100U));
            assert(read_word(f, 0x107fc) == s.cs.selector && read_word(f, 0x107fe) == s.flags);
            assert(f->count == 8 && !f->shut_on && !f->acks);
            assert(f->trace[6].address == 0x6000+delivered*4);
            assert(b.timing == BM_286_TIMING_UNKNOWN && b.cpu_cycles == 16 && b.bus_wait_cycles == 16);
        }
    }
}
static void recovery(fixture_t *f)
{
    bm_286_arch_state_t s = setup(f, 35, 0x80), a;
    bm_286_boundary_t b;
    const uint8_t handler[] = {0x0f,1,0x1e,0,5,0xcf}; /* LIDT [0500]; IRET */
    memcpy(f->ram+0x40200, handler, sizeof(handler)); f->ram[0x40300] = 0xcf;
    word(f, 0x500, 0x3ff); word(f, 0x502, 0x6000); word(f, 0x504, 0);
    set(f, &s);
    for (unsigned i = 0; i < 6; ++i) {
        f->count = 0; assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK);
    }
    a = state(f); assert(a.halted && a.ip == 0x104 && a.sp == s.sp && a.flags == s.flags);
    assert(a.idtr.limit == 0x3ff && !a.shutdown && !f->shut_on);
    for (unsigned valid_nmi = 0; valid_nmi < 2; ++valid_nmi) {
        s = setup(f, valid_nmi ? 11 : 10, 0x80); set(f, &s);
        assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK && state(f).shutdown);
        assert(f->cpu.ops.signal(f->cpu.context, BM_286_SIGNAL_INTR, 1) == BM_STATUS_OK);
        assert(bm_286_step(&f->cpu, &b) == BM_STATUS_IDLE && !f->acks);
        assert(f->cpu.ops.signal(f->cpu.context, BM_286_SIGNAL_HOLD, 1) == BM_STATUS_OK);
        assert(bm_286_step(&f->cpu, &b) == BM_STATUS_IDLE && b.kind == BM_286_BOUNDARY_HOLD);
        assert(f->cpu.ops.signal(f->cpu.context, BM_286_SIGNAL_HOLD, 0) == BM_STATUS_OK);
        assert(f->cpu.ops.signal(f->cpu.context, BM_286_SIGNAL_NMI, 1) == BM_STATUS_OK);
        f->count = 0;
        assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK); a = state(f);
        assert(a.nmi_blocked && !a.nmi_pending && f->shut_on == 1);
        if (valid_nmi) {
            assert(!a.shutdown && a.ip == 0x400 && f->shut_off == 1);
            assert(b.kind == BM_286_BOUNDARY_INTERRUPT && b.has_vector && b.vector == 2);
            assert(read_word(f, 0x107fa) == s.ip && f->count == 5);
        } else {
            assert(a.shutdown && b.kind == BM_286_BOUNDARY_SHUTDOWN && !f->count && !f->shut_off);
            assert(f->cpu.ops.signal(f->cpu.context, BM_286_SIGNAL_NMI, 0) == BM_STATUS_OK);
            assert(f->cpu.ops.signal(f->cpu.context, BM_286_SIGNAL_NMI, 1) == BM_STATUS_OK);
            assert(bm_286_step(&f->cpu, &b) == BM_STATUS_IDLE && !f->count);
        }
        assert(f->cpu.ops.reset(f->cpu.context) == BM_STATUS_OK);
        a = state(f); assert(!a.shutdown && !a.nmi_blocked && a.ip == 0xfff0 && f->shut_off == 1);
    }
}
static void fault_and_trap(fixture_t *f)
{
    bm_286_arch_state_t s = setup(f, 35, 0x80), a;
    bm_286_boundary_t b;
    const uint8_t code[] = {0x26,0x0f,1,0x26,0xff,0xff}; /* SMSW ES:[FFFF] -> #13 -> #8 */
    memcpy(f->ram+0x30100, code, sizeof(code)); set(f, &s);
    assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK);
    assert(b.kind == BM_286_BOUNDARY_EXCEPTION && b.has_vector && b.vector == 8);
    a = state(f); assert(a.ip == 0x200 && a.sp == s.sp-6 && !a.shutdown);
    assert(read_word(f, 0x107fa) == s.ip && f->count == 11 && !f->acks);
    s = setup(f, 6, 0x80); s.trap_pending = 1; set(f, &s);
    assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK);
    a = state(f); s.trap_pending = 0; s.shutdown = 1; same(&a, &s);
    assert(b.kind == BM_286_BOUNDARY_SHUTDOWN && !b.has_vector && !f->count && f->shut_on == 1);
    /* A host failure during NMI recovery must not masquerade as #8/success. */
    for (unsigned fail = 1; fail <= 5; ++fail) {
        s = setup(f, 11, 0x80); set(f, &s);
        assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK);
        assert(f->cpu.ops.signal(f->cpu.context, BM_286_SIGNAL_NMI, 1) == BM_STATUS_OK);
        s = state(f); f->count = 0; f->fail_at = fail;
        assert(bm_286_step(&f->cpu, &b) == BM_STATUS_DEVICE_ERROR);
        a = state(f); same(&a, &s);
        assert(f->count == fail && f->shut_on == 1 && !f->shut_off);
        assert(bm_286_step(&f->cpu, &b) == BM_STATUS_INVALID_STATE && f->count == fail);
    }
}
static void external_and_errors(fixture_t *f)
{
    for (unsigned external = 0; external < 2; ++external) for (unsigned odd = 0; odd < 2; ++odd) {
        bm_286_arch_state_t s = setup(f, 35, 0x80), a;
        bm_286_boundary_t b; s.sp += (uint16_t)odd; set(f, &s);
        if (external) assert(f->cpu.ops.signal(f->cpu.context, BM_286_SIGNAL_INTR, 1) == BM_STATUS_OK);
        assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK);
        assert(b.kind == BM_286_BOUNDARY_EXCEPTION && b.vector == 8 && b.has_vector);
        assert(f->acks == 2*external && !f->locked && f->lock_edges == 2*external);
        assert(read_word(f, 0x107fa+odd) == s.ip);
        bm_bus_transaction_t trace[32]; memcpy(trace, f->trace, sizeof(trace));
        unsigned total = f->count;
        for (unsigned fail = 1; fail <= total; ++fail) for (unsigned after = 0; after < 2; ++after) {
            s = setup(f, 35, 0x80); s.sp += (uint16_t)odd; set(f, &s);
            memset(f->ram+0x107f8, 0xa5, 16);
            uint8_t expected[16]; memset(expected, 0xa5, sizeof(expected));
            for (unsigned t = 0; t < fail-1+after; ++t) if (trace[t].operation == BM_BUS_WRITE)
                for (unsigned j = 0; j < trace[t].size; ++j)
                    expected[(size_t)trace[t].address+j-0x107f8] = (uint8_t)(trace[t].value >> (8*j));
            f->fail_at = fail; f->after = after;
            if (external) assert(f->cpu.ops.signal(f->cpu.context, BM_286_SIGNAL_INTR, 1) == BM_STATUS_OK);
            assert(bm_286_step(&f->cpu, &b) == BM_STATUS_DEVICE_ERROR);
            a = state(f); same(&a, &s);
            assert(!f->locked && !f->shut_on && f->acks == 2*external && f->count == fail);
            assert(!memcmp(expected, f->ram+0x107f8, 16));
            assert(bm_286_step(&f->cpu, &b) == BM_STATUS_INVALID_STATE && f->count == fail);
        }
    }
}
static void operand_faults(fixture_t *f)
{
    static const uint8_t codes[][4] = {
        {0x8b,7}, {0x89,7}, {0xc7,7,0x34,0x12}, {0x87,7},
        {3,7}, {1,7}, {0x39,7}, {0x85,7}, {0xff,7}, {0xff,0x0f},
        {0xf7,0x1f}, {0xf7,0x17}, {0xd1,0x27}, {0xf7,0x27}, {0xf7,0x37},
        {0x8e,0x17}, {0x8c,0x17}, {0x69,7,0x34,0x12}, {0x8f,7}, {0xff,0x37}, {0xff,0x17}
    };
    const uint8_t prefixes[] = {0x26,0x2e,0x36,0x3e};
    for (unsigned op = 0; op < sizeof(codes)/sizeof(codes[0]); ++op)
    for (unsigned p = 0; p < 4; ++p) for (unsigned odd = 0; odd < 2; ++odd) {
        bm_286_arch_state_t s = setup(f, 0x3ff, 0x80), a, e;
        bm_286_boundary_t b; s.bx = 0xffff; s.ax = 0x1234; s.dx = 0x4567;
        s.sp += (uint16_t)odd; s.es.base = 0x20000; s.es.selector = 0x2000;
        word(f, 0x6034, 0x200); word(f, 0x6036, 0x4000);
        f->ram[0x30100] = prefixes[p]; memcpy(f->ram+0x30101, codes[op], 4);
        set(f, &s); assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK);
        e = s; e.sp -= 6; e.flags &= 0xfcffU;
        e.cs.base = 0x40000; e.cs.selector = 0x4000; e.ip = 0x200;
        a = state(f); same(&a, &e);
        assert(b.kind == BM_286_BOUNDARY_EXCEPTION && b.has_vector && b.vector == 13);
        assert(read_word(f, 0x107fa+odd) == s.ip && !f->acks && !f->lock_edges);
        unsigned forbidden = (p == 0 ? 0x20000U : p == 1 ? 0x30000U : p == 2 ? 0x10000U : 0U)+0xffff;
        for (unsigned i = 0; i < f->count; ++i) {
            assert(f->trace[i].address != forbidden && f->trace[i].address != forbidden+1);
            assert(!(f->trace[i].attributes & BM_BUS_TRANSACTION_LOCKED));
        }
        bm_bus_transaction_t trace[32]; memcpy(trace, f->trace, sizeof(trace));
        unsigned total = f->count;
        for (unsigned fail = 1; fail <= total; ++fail) for (unsigned after = 0; after < 2; ++after) {
            assert(f->cpu.ops.reset(f->cpu.context) == BM_STATUS_OK);
            f->count = f->lock_edges = 0; f->fail_at = fail; f->after = after;
            uint8_t expected[16]; memset(expected, 0xa5, sizeof(expected));
            memset(f->ram+0x107f8, 0xa5, 16);
            for (unsigned t = 0; t < fail-1+after; ++t) if (trace[t].operation == BM_BUS_WRITE)
                for (unsigned j = 0; j < trace[t].size; ++j)
                    expected[(size_t)trace[t].address+j-0x107f8] = (uint8_t)(trace[t].value >> (8*j));
            set(f, &s); assert(bm_286_step(&f->cpu, &b) == BM_STATUS_DEVICE_ERROR);
            a = state(f); same(&a, &s);
            assert(f->count == fail && !memcmp(expected, f->ram+0x107f8, 16));
            assert(!f->locked && !f->lock_edges);
            assert(bm_286_step(&f->cpu, &b) == BM_STATUS_INVALID_STATE && f->count == fail);
        }
    }
    /* Guest-only address repair: faulting store, MOV BX,0500 in handler, IRET,
     * retry store, HLT. No test-side state import while recovering. */
    bm_286_arch_state_t s = setup(f, 0x3ff, 0x80), a;
    bm_286_boundary_t b;
    const uint8_t program[] = {0x26,0x89,7,0xf4}, handler[] = {0xbb,0,5,0xcf};
    memcpy(f->ram+0x30100, program, sizeof(program));
    memcpy(f->ram+0x40200, handler, sizeof(handler));
    word(f, 0x6034, 0x200); word(f, 0x6036, 0x4000);
    s.bx = 0xffff; s.ax = 0xbeef; word(f, 0x500, 0); set(f, &s);
    for (unsigned i = 0; i < 5; ++i) {
        f->count = 0; assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK);
    }
    a = state(f); assert(a.halted && a.bx == 0x500 && a.sp == s.sp && a.flags == s.flags);
    assert(read_word(f, 0x500) == 0xbeef && a.ip == 0x104);
}
static void stack_faults(fixture_t *f)
{
    static const struct {
        uint8_t code[5];
        uint16_t sp, bp;
        unsigned shutdown;
    } cases[] = {
        {{0x50},1,0x500,1}, {{0x16},1,0x500,1}, {{0x68,0x34,0x12},1,0x500,1},
        {{0xff,0x37},1,0x500,1}, {{0x9c},1,0x500,1},
        {{0xe8,0,0},1,0x500,1}, {{0x9a,0,2,0,4},3,0x500,1},
        {{0xff,0x1f},3,0x500,1}, {{0x60},7,0x500,0},
        {{0x61},0xfff1,0x500,0}, {{0x58},0xffff,0x500,0},
        {{0x17},0xffff,0x500,0}, {{0x8f,7},0xffff,0x500,0},
        {{0x9d},0xffff,0x500,0}, {{0xc3},0xffff,0x500,0},
        {{0xcb},0xfffd,0x500,0}, {{0xcf},0xfffb,0x500,0},
        {{0xc8,0,0,2},0x800,1,0}, {{0xc9},0x800,0xffff,0}
    };
    for (unsigned op = 0; op < sizeof(cases)/sizeof(cases[0]); ++op) {
        bm_286_arch_state_t s = setup(f, 0x3ff, 0x80), a, e;
        bm_286_boundary_t b;
        s.sp = cases[op].sp; s.bp = cases[op].bp; s.ax = 0xbeef; s.bx = 0x500;
        s.nmi_blocked = 1; /* Faulting IRET must not unblock NMI. */
        word(f, 0x500, 0x200); word(f, 0x502, 0x4000);
        word(f, 0x6034, 0x200); word(f, 0x6036, 0x4000);
        f->ram[0x30100] = 0x26; /* Restart includes an otherwise irrelevant prefix. */
        memcpy(f->ram+0x30101, cases[op].code, sizeof(cases[op].code));
        set(f, &s); assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK);
        a = state(f); e = s;
        if (cases[op].shutdown) {
            e.shutdown = 1;
            assert(b.kind == BM_286_BOUNDARY_SHUTDOWN && !b.has_vector && f->shut_on == 1);
            for (unsigned t = 0; t < f->count; ++t) assert(f->trace[t].operation != BM_BUS_WRITE);
            unsigned count = f->count;
            assert(bm_286_step(&f->cpu, &b) == BM_STATUS_IDLE && f->count == count);
        } else {
            e.sp = (uint16_t)(s.sp-6); e.flags &= 0xfcffU;
            e.cs.base = 0x40000; e.cs.selector = 0x4000; e.ip = 0x200;
            assert(b.kind == BM_286_BOUNDARY_EXCEPTION && b.has_vector && b.vector == 13);
            assert(read_word(f, s.ss.base+e.sp) == s.ip);
            assert(read_word(f, s.ss.base+e.sp+2) == s.cs.selector);
            assert(read_word(f, s.ss.base+e.sp+4) == s.flags);
            assert(!f->shut_on);
        }
        same(&a, &e);
        bm_bus_transaction_t trace[32]; memcpy(trace, f->trace, sizeof(trace));
        unsigned total = f->count;
        /* Every endpoint may fail before or after its external effect. Neither
         * turns into a guest fault, commits registers, or replays the transfer. */
        for (unsigned fail = 1; fail <= total; ++fail) for (unsigned after = 0; after < 2; ++after) {
            assert(f->cpu.ops.reset(f->cpu.context) == BM_STATUS_OK);
            f->count = f->shut_on = 0; f->fail_at = fail; f->after = after;
            uint8_t expected[6]; memset(expected, 0xa5, sizeof(expected));
            unsigned frame = s.ss.base+(uint16_t)(s.sp-6);
            if (!cases[op].shutdown) memset(f->ram+frame, 0xa5, sizeof(expected));
            for (unsigned t = 0; t < fail-1+after; ++t) if (trace[t].operation == BM_BUS_WRITE)
                for (unsigned j = 0; j < trace[t].size; ++j)
                    expected[(size_t)trace[t].address+j-frame] = (uint8_t)(trace[t].value >> (8*j));
            set(f, &s); assert(bm_286_step(&f->cpu, &b) == BM_STATUS_DEVICE_ERROR);
            a = state(f); same(&a, &s);
            assert(f->count == fail && !f->shut_on && !f->locked);
            if (!cases[op].shutdown) assert(!memcmp(expected, f->ram+frame, sizeof(expected)));
            assert(bm_286_step(&f->cpu, &b) == BM_STATUS_INVALID_STATE && f->count == fail);
        }
    }
    /* Guest-only repair of a bad LEAVE source; the exception stack itself
     * remains usable. Repair BP, IRET, retry LEAVE, HLT. */
    bm_286_arch_state_t s = setup(f, 0x3ff, 0x80), a;
    bm_286_boundary_t b;
    const uint8_t program[] = {0x26,0xc9,0xf4}, handler[] = {0xbd,0,5,0xcf};
    memcpy(f->ram+0x30100, program, sizeof(program));
    memcpy(f->ram+0x40200, handler, sizeof(handler));
    word(f, 0x6034, 0x200); word(f, 0x6036, 0x4000);
    s.bp = 0xffff; word(f, 0x10500, 0xbeef); set(f, &s);
    for (unsigned i = 0; i < 5; ++i) {
        f->count = 0; assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK);
    }
    a = state(f);
    assert(a.halted && a.bp == 0xbeef && a.sp == 0x502 && a.ip == 0x103 && a.flags == s.flags);
    /* Failed NMI recovery on the same unusable stack does not re-notify or
     * repeatedly accept NMI. Reset restores a usable CPU. */
    s = setup(f, 0x3ff, 0x80); s.sp = 1; f->ram[0x30100] = 0x50; set(f, &s);
    assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK && state(f).shutdown);
    f->count = 0;
    assert(f->cpu.ops.signal(f->cpu.context, BM_286_SIGNAL_NMI, 1) == BM_STATUS_OK);
    assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK);
    assert(state(f).shutdown && state(f).nmi_blocked && !f->count && f->shut_on == 1);
    assert(bm_286_step(&f->cpu, &b) == BM_STATUS_IDLE);
    assert(f->cpu.ops.reset(f->cpu.context) == BM_STATUS_OK && !state(f).shutdown);
    for (unsigned sp = 1; sp <= 5; sp += 2) {
        s = setup(f, 0x3ff, 0x80); s.sp = (uint16_t)sp; set(f, &s);
        assert(f->cpu.ops.signal(f->cpu.context, BM_286_SIGNAL_INTR, 1) == BM_STATUS_OK);
        assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK);
        a = state(f); s.shutdown = 1; same(&a, &s);
        assert(b.kind == BM_286_BOUNDARY_SHUTDOWN && !b.has_vector);
        assert(!f->count && f->acks == 2 && f->lock_edges == 2 && !f->locked && f->shut_on == 1);
        assert(bm_286_step(&f->cpu, &b) == BM_STATUS_IDLE && f->acks == 2);
    }
}
static void pointer_faults(fixture_t *f)
{
    static const uint8_t codes[][2] = {{0xc4,7},{0xc5,7},{0xff,0x1f},{0xff,0x2f}};
    const uint8_t prefixes[] = {0x26,0x2e,0x36,0x3e};
    /* Last contiguous pair and independently wrapped second word both remain
     * valid. Do not turn every pair crossing FFFF into a fabricated fault. */
    for (unsigned op = 0; op < 4; ++op) for (unsigned seg = 0; seg < 4; ++seg)
    for (unsigned wrap = 0; wrap < 2; ++wrap) {
        bm_286_arch_state_t s = setup(f, 0x3ff, 0x80), a;
        bm_286_boundary_t b;
        s.es.base = 0x20000; s.es.selector = 0x2000;
        s.bx = (uint16_t)(wrap ? 0xfffe : 0xfffc);
        unsigned base = seg == 0 ? s.es.base : seg == 1 ? s.cs.base : seg == 2 ? s.ss.base : s.ds.base;
        word(f, base+s.bx, 0x300); word(f, base+(uint16_t)(s.bx+2), 0x4000);
        f->ram[0x30100] = prefixes[seg]; memcpy(f->ram+0x30101, codes[op], 2);
        set(f, &s); assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK); a = state(f);
        assert(b.kind == BM_286_BOUNDARY_INSTRUCTION && !b.has_vector);
        assert(f->trace[3].address == base+s.bx);
        assert(f->trace[4].address == base+(uint16_t)(s.bx+2));
        if (op < 2) {
            assert(a.ax == 0x300 && (op == 0 ? a.es.base : a.ds.base) == 0x40000);
            assert(a.ip == 0x103 && a.sp == s.sp && a.cs.base == s.cs.base);
        } else {
            assert(a.ip == 0x300 && a.cs.base == 0x40000);
            assert(a.sp == (uint16_t)(s.sp-(op == 2 ? 4 : 0)));
            if (op == 2) assert(read_word(f, s.ss.base+a.sp) == 0x103);
        }
    }
    for (unsigned op = 0; op < 4; ++op) for (unsigned seg = 0; seg < 4; ++seg)
    for (unsigned edge = 0; edge < 3; ++edge) for (unsigned odd = 0; odd < 2; ++odd) {
        bm_286_arch_state_t s = setup(f, 0x3ff, 0x80), a, e;
        bm_286_boundary_t b;
        s.sp = (uint16_t)(0x400+odd); s.ax = 0xbeef;
        s.es.base = 0x20000; s.es.selector = 0x2000;
        s.bx = (uint16_t)(edge == 0 ? 0xfffd : edge == 1 ? 0xffff : 0x500);
        bm_286_segment_state_t *source = seg == 0 ? &s.es : seg == 1 ? &s.cs : seg == 2 ? &s.ss : &s.ds;
        if (edge == 2) source->limit = 0x502; /* Second word, not first, is invalid. */
        f->ram[0x30100] = prefixes[seg]; memcpy(f->ram+0x30101, codes[op], 2);
        word(f, 0x6034, 0x200); word(f, 0x6036, 0x4000);
        set(f, &s); assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK);
        a = state(f); e = s; e.sp -= 6; e.flags &= 0xfcffU;
        e.cs.selector = 0x4000; e.cs.base = 0x40000; e.cs.limit = 0xffff; e.ip = 0x200;
        same(&a, &e);
        assert(b.kind == BM_286_BOUNDARY_EXCEPTION && b.has_vector && b.vector == 13);
        assert(read_word(f, s.ss.base+e.sp) == s.ip);
        assert(read_word(f, s.ss.base+e.sp+2) == s.cs.selector);
        assert(read_word(f, s.ss.base+e.sp+4) == s.flags);
        assert(f->count == 8+3*odd && !f->locked && !f->acks);
        /* Only instruction fetch, three frame words and IVT: no pointer
         * read, partial segment reload or far-CALL return frame. */
        for (unsigned t = 3; t < f->count-2; ++t) assert(f->trace[t].operation == BM_BUS_WRITE);
        assert(f->trace[f->count-2].address == 0x6034 && f->trace[f->count-1].address == 0x6036);
        bm_bus_transaction_t trace[32]; memcpy(trace, f->trace, sizeof(trace));
        unsigned total = f->count, frame = s.ss.base+e.sp;
        for (unsigned fail = 1; fail <= total; ++fail) for (unsigned after = 0; after < 2; ++after) {
            assert(f->cpu.ops.reset(f->cpu.context) == BM_STATUS_OK);
            f->count = 0; f->fail_at = fail; f->after = after;
            uint8_t expected[6]; memset(expected, 0xa5, sizeof(expected));
            memset(f->ram+frame, 0xa5, sizeof(expected));
            for (unsigned t = 0; t < fail-1+after; ++t) if (trace[t].operation == BM_BUS_WRITE)
                for (unsigned j = 0; j < trace[t].size; ++j)
                    expected[(size_t)trace[t].address+j-frame] = (uint8_t)(trace[t].value >> (8*j));
            set(f, &s); assert(bm_286_step(&f->cpu, &b) == BM_STATUS_DEVICE_ERROR);
            a = state(f); same(&a, &s);
            assert(f->count == fail && !memcmp(expected, f->ram+frame, sizeof(expected)));
            assert(bm_286_step(&f->cpu, &b) == BM_STATUS_INVALID_STATE && f->count == fail);
        }
    }
    for (unsigned op = 0; op < 4; ++op) {
        bm_286_arch_state_t s = setup(f, 0x3ff, 0x80), a;
        bm_286_boundary_t b;
        f->ram[0x30100] = 0x26; memcpy(f->ram+0x30101, codes[op], 2); f->ram[0x30103] = 0xf4;
        const uint8_t handler[] = {0xbb,0,5,0xcf}; /* Repair BX, IRET. */
        memcpy(f->ram+0x40200, handler, sizeof(handler));
        word(f, 0x6034, 0x200); word(f, 0x6036, 0x4000);
        word(f, 0x500, 0x300); word(f, 0x502, 0x4000);
        f->ram[0x40300] = (uint8_t)(op == 2 ? 0xcb : 0xf4);
        s.bx = 0xffff; set(f, &s);
        for (unsigned i = 0; i < (op == 2 ? 6U : 5U); ++i) {
            f->count = 0; assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK);
        }
        a = state(f);
        assert(a.halted && a.sp == s.sp && a.flags == s.flags && a.bx == 0x500);
        if (op < 2) {
            assert(a.ax == 0x300 && a.ip == 0x104 && a.cs.base == s.cs.base);
            assert((op == 0 ? a.es.base : a.ds.base) == 0x40000);
        } else if (op == 2) assert(a.ip == 0x104 && a.cs.base == s.cs.base);
        else assert(a.ip == 0x301 && a.cs.base == 0x40000);
    }
}
static void instruction_faults(fixture_t *f)
{
    /* Each decode byte can be the first rejected byte. No operand effects. */
    const uint8_t code[] = {0x26,0xc7,0x06,0,5,0xef,0xbe};
    for (unsigned cut = 0; cut < sizeof(code); ++cut)
        for (unsigned odd = 0; odd < 2; ++odd) {
            bm_286_arch_state_t s = setup(f, 0x3ff, 13), a, e;
            bm_286_boundary_t b;
            memcpy(f->ram+0x30100, code, sizeof(code));
            s.cs.limit = (uint16_t)(s.ip+cut-1); s.sp += (uint16_t)odd;
            word(f, 0x20500, 0x1234); set(f, &s);
            assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK); a = state(f); e = s;
            e.sp -= 6; e.flags &= 0xfcffU; e.cs.selector = 0x4000;
            e.cs.base = 0x40000; e.cs.limit = 0xffff; e.ip = 0x300;
            same(&a, &e);
            assert(b.kind == BM_286_BOUNDARY_EXCEPTION && b.vector == 13 && b.has_vector);
            assert(f->count == cut + (odd ? 8U : 5U));
            assert(read_word(f, s.ss.base+a.sp) == s.ip && read_word(f, 0x20500) == 0x1234);
            for (unsigned i = 0; i < cut; ++i)
                assert(f->trace[i].operation == BM_BUS_FETCH && f->trace[i].address == 0x30100+i);
        }
    /* Direct/indirect CALL/JMP, RET and taken conditional/loop targets. */
    for (unsigned end = 0; end < 3; ++end) {
        bm_286_arch_state_t s = setup(f, 0x3ff, 13), a;
        bm_286_boundary_t b;
        s.ip = (uint16_t)(0xfffd+end);
        memcpy(f->ram+s.cs.base+s.ip, code, 0x10000U-s.ip);
        set(f, &s); assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK); a = state(f);
        assert(b.has_vector && b.vector == 13 && read_word(f, s.ss.base+a.sp) == s.ip);
        assert(f->count == 0x10000U-s.ip+5);
    }
    static const uint8_t branches[][4] = {
        {0xe8,0x7f,0}, {0xe9,0x7f,0}, {0xeb,0x7f},
        {0xff,0xd0}, {0xff,0xe0}, {0xff,0x17}, {0xff,0x27},
        {0xc3}, {0xc2,8,0}, {0x74,0x7f}, {0xe0,0x7f},
        {0xe1,0x7f}, {0xe2,0x7f}, {0xe3,0x7f}};
    for (unsigned op = 0; op < sizeof(branches)/sizeof(branches[0]); ++op) {
        bm_286_arch_state_t s = setup(f, 0x3ff, 13), a, e;
        bm_286_boundary_t b;
        f->ram[0x30100] = 0x3e; memcpy(f->ram+0x30101, branches[op], 4);
        s.cs.limit = 0x110; s.ax = 0x200; s.bx = 0x500;
        s.cx = (uint16_t)(op == 13 ? 0 : 2);
        if (op == 9 || op == 11) s.flags |= 0x40;
        word(f, 0x500, 0x200); word(f, s.ss.base+s.sp, 0x200); set(f, &s);
        assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK); a = state(f); e = s;
        e.sp -= 6; e.flags &= 0xfcffU; e.cs.selector = 0x4000;
        e.cs.base = 0x40000; e.cs.limit = 0xffff; e.ip = 0x300;
        same(&a, &e); /* LOOP count/RET pop/CALL push not committed. */
        assert(b.kind == BM_286_BOUNDARY_EXCEPTION && b.has_vector && b.vector == 13);
        assert(read_word(f, s.ss.base+a.sp) == s.ip);
        unsigned writes = 0;
        for (unsigned i = 0; i < f->count; ++i) writes += f->trace[i].operation == BM_BUS_WRITE;
        assert(writes == 3); /* Exception frame only. */
    }
    /* Truncated instruction and overlong prefix stream: host failures at
     * every completed fetch/frame transfer, before and after effects. */
    for (unsigned kind = 0; kind < 2; ++kind) {
        unsigned transfers = kind ? 15 : 7;
        for (unsigned after = 0; after < 2; ++after)
            for (unsigned fail = 1; fail <= transfers; ++fail) {
                bm_286_arch_state_t s = setup(f, 0x3ff, 13), a;
                bm_286_boundary_t b;
                if (kind) memset(f->ram+0x30100, 0x26, 11);
                else {f->ram[0x30100] = 0xb8; s.cs.limit = 0x101;}
                f->fail_at = fail; f->after = after; set(f, &s);
                assert(bm_286_step(&f->cpu, &b) == BM_STATUS_DEVICE_ERROR);
                a = state(f); same(&a, &s); assert(f->count == fail);
                assert(bm_286_step(&f->cpu, &b) == BM_STATUS_INVALID_STATE && f->count == fail);
            }
    }
    /* Guest handler replaces an overlong prefix stream, then IRET retries
     * the original prefix address. No state imports during recovery. */
    {
        bm_286_arch_state_t s = setup(f, 0x3ff, 13), a;
        bm_286_boundary_t b;
        const uint8_t handler[] = {0xc6,0x06,0,1,0xf4,0xcf};
        memset(f->ram+0x30100, 0x26, 11); memcpy(f->ram+0x40300, handler, sizeof(handler));
        s.ds = s.cs; set(f, &s);
        for (unsigned i = 0; i < 4; ++i) {
            f->count = 0; assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK);
        }
        a = state(f); assert(a.halted && a.ip == 0x101 && a.sp == s.sp && a.flags == s.flags);
    }
}
int main(void)
{
    fixture_t f = {0}; bm_286_config_t c = {0}; bm_host_services_t host = bm_null_host_services();
    f.ram = calloc(0x100000, 1); assert(f.ram);
    c.size = sizeof(c); c.version = BM_286_CONTRACT_VERSION;
    c.access = access_bus; c.access_context = &f;
    c.interrupt_ack = ack; c.interrupt_context = &f;
    c.bus_lock = lock_changed; c.shutdown = shutdown_changed; c.pin_context = &f;
    assert(bm_286_create(&host, &c, &f.cpu) == BM_STATUS_OK);
    matrix(&f); recovery(&f); fault_and_trap(&f); external_and_errors(&f); operand_faults(&f);
    stack_faults(&f);
    pointer_faults(&f);
    instruction_faults(&f);
    f.cpu.ops.destroy(f.cpu.context); free(f.ram); return 0;
}
