/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 BluMach contributors
 * Authored byte streams through a private, bounded shared decoder. No ROM,
 * external vectors, public PE entry, physical timing or machine acceptance.
 */
#include "execution_286.h"
#include <blumach/platforms/null_host.h>
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef struct fixture {
    uint8_t ram[65536];
    bm_bus_transaction_t trace[256];
    unsigned calls, effects, fail, acks, locks, unlocks, hlda, shutdown;
    bool after, locked;
    bm_status_t failure;
    bm_cpu_t *cpu;
    unsigned nmi_at;
} fixture_t;

static bm_status_t bus(void *context, bm_bus_transaction_t *t)
{
    fixture_t *f = context;
    unsigned i;
    assert(f->calls < 256 && t->address <= 0xffffffu);
    assert(t->size == 1 || (t->size == 2 && !(t->address & 1u)));
    assert(t->alignment == t->size && !t->wait_states);
    assert(t->endianness == BM_ENDIAN_LITTLE);
    assert(t->space == (t->operation == BM_BUS_FETCH ? BM_ADDRESS_PROGRAM : BM_ADDRESS_DATA));
    assert(t->attributes == (f->locked ? BM_BUS_TRANSACTION_LOCKED : 0u));
    f->trace[f->calls++] = *t;
    if (f->calls == f->nmi_at) {
        assert(f->cpu->ops.signal(f->cpu->context, BM_286_SIGNAL_NMI, 0) == BM_STATUS_OK);
        assert(f->cpu->ops.signal(f->cpu->context, BM_286_SIGNAL_NMI, 1) == BM_STATUS_OK);
    }
    if (f->calls == f->fail && !f->after) return f->failure;
    ++f->effects;
    if (t->operation != BM_BUS_WRITE) t->value = 0;
    for (i = 0; i < t->size; ++i) {
        unsigned at = (unsigned)(t->address + i) & 65535u;
        if (t->operation == BM_BUS_WRITE) f->ram[at] = (uint8_t)(t->value >> (8u * i));
        else t->value |= (uint64_t)f->ram[at] << (8u * i);
    }
    t->wait_states = 3;
    return f->calls == f->fail ? f->failure : BM_STATUS_OK;
}

static void pin_lock(void *context, int asserted)
{
    fixture_t *f = context;
    assert(f->locked != (asserted != 0)); f->locked = asserted != 0;
    if (asserted) ++f->locks; else ++f->unlocks;
}
static void hlda(void *context, int asserted) { ((fixture_t *)context)->hlda = (unsigned)asserted; }
static void shutdown_pin(void *context, int asserted) { ((fixture_t *)context)->shutdown = (unsigned)asserted; }
static bm_status_t ack(void *context, unsigned phase, uint8_t *vector, uint32_t *waits)
{
    fixture_t *f = context;
    assert(f->locked && phase == f->acks); ++f->acks;
    *vector = 0x20; *waits = 5;
    return BM_STATUS_OK;
}
static void word(fixture_t *f, unsigned at, unsigned value)
{
    f->ram[at] = (uint8_t)value; f->ram[at + 1] = (uint8_t)(value >> 8);
}
static uint16_t getword(fixture_t *f, unsigned at)
{
    return (uint16_t)(f->ram[at] | ((uint16_t)f->ram[at + 1] << 8));
}
static bm_286_arch_state_t get(bm_cpu_t *cpu)
{
    bm_286_arch_state_t a = {0};
    assert(bm_286_get_arch_state(cpu, &a) == BM_STATUS_OK); return a;
}
static void same(const bm_286_arch_state_t *a, const bm_286_arch_state_t *b)
{
#define EQ(x) assert(a->x == b->x)
    EQ(ax); EQ(bx); EQ(cx); EQ(dx); EQ(sp); EQ(bp); EQ(si); EQ(di);
    EQ(ip); EQ(flags); EQ(msw); EQ(cpl); EQ(halted); EQ(shutdown);
    EQ(interrupt_shadow); EQ(trap_pending); EQ(nmi_pending); EQ(nmi_blocked);
#define SEG(x) EQ(x.selector); EQ(x.base); EQ(x.limit); EQ(x.access); EQ(x.valid)
    SEG(cs); SEG(ds); SEG(es); SEG(ss); SEG(ldtr); SEG(tr);
    EQ(gdtr.base); EQ(gdtr.limit); EQ(idtr.base); EQ(idtr.limit);
#undef SEG
#undef EQ
}
static void descriptor(fixture_t *f, unsigned at, unsigned base, unsigned access)
{
    word(f, at, 0xffff); word(f, at + 2, base);
    f->ram[at + 4] = (uint8_t)(base >> 16); f->ram[at + 5] = (uint8_t)access;
}
static bm_cpu_t create(fixture_t *f, unsigned cpl, unsigned odd)
{
    bm_host_services_t host = bm_null_host_services();
    bm_286_config_t c = {0}; bm_cpu_t cpu; bm_286_arch_state_t a;
    unsigned i;
    memset(f, 0, sizeof(*f));
    c.size = sizeof(c); c.version = BM_286_CONTRACT_VERSION;
    c.access = bus; c.access_context = f; c.bus_lock = pin_lock;
    c.hold_ack = hlda; c.shutdown = shutdown_pin; c.pin_context = f;
    c.interrupt_ack = ack; c.interrupt_context = f;
    assert(bm_286_create(&host, &c, &cpu) == BM_STATUS_OK);
    a = get(&cpu); a.msw = 0xfff1; a.cpl = (uint8_t)cpl;
    a.cs.selector = (uint16_t)(8 + cpl); a.cs.base = 0x3000;
    a.cs.access = (uint8_t)(0x9b | (cpl << 5)); a.cs.limit = 0xffff;
    a.ss.selector = (uint16_t)(16 + cpl); a.ss.base = odd;
    a.ss.access = (uint8_t)(0x93 | (cpl << 5));
    a.ds = a.es = a.ss; a.ds.selector = a.es.selector = (uint16_t)(24 + cpl);
    a.ds.base = a.es.base = 0x4000 + odd;
    a.ip = 0x100; a.sp = 0x8000; a.ax = 0x5678; a.bx = 0x600; a.bp = 0x600;
    a.gdtr.base = 0x2000 + odd; a.gdtr.limit = 31;
    a.idtr.base = 0x1000 + odd; a.idtr.limit = 0x7ff;
    for (i = 0; i < 256; ++i) {
        unsigned at = a.idtr.base + i * 8;
        word(f, at, 0x400); word(f, at + 2, 8);
        f->ram[at + 5] = 0x86;
    }
    descriptor(f, a.gdtr.base + 8, a.cs.base, a.cs.access);
    descriptor(f, a.gdtr.base + 16, a.ss.base, a.ss.access);
    descriptor(f, a.gdtr.base + 24, a.ds.base, a.ds.access & ~1u);
    assert(bm_286_set_arch_state(&cpu, &a) == BM_STATUS_OK);
    return cpu;
}
static void code(fixture_t *f, const uint8_t *bytes, unsigned length)
{
    memcpy(f->ram + 0x3100, bytes, length);
}
static bm_286_boundary_t step(bm_cpu_t *cpu)
{
    bm_286_boundary_t b;
    assert(bm_286_pm_step_subset(cpu, &b) == BM_STATUS_OK);
    assert(b.timing == BM_286_TIMING_UNKNOWN && b.cpu_cycles == b.bus_wait_cycles);
    return b;
}

static void mov_matrix(void)
{
    static const uint8_t prefixes[] = {0x26,0x2e,0x36,0x3e};
    unsigned cpl, odd, r, wide, write;
    for (cpl = 0; cpl < 4; ++cpl) for (odd = 0; odd < 2; ++odd)
    for (r = 0; r < 4; ++r) for (wide = 0; wide < 2; ++wide) for (write = 0; write < 2; ++write) {
        fixture_t f; bm_cpu_t cpu = create(&f, cpl, odd);
        bm_286_arch_state_t a = get(&cpu), after;
        bm_286_boundary_t b;
        unsigned base = r == 1 ? a.cs.base : r == 2 ? a.ss.base : a.ds.base;
        uint8_t bytes[] = {prefixes[r], (uint8_t)(0xa0 + wide + 2 * write),0,6};
        code(&f, bytes, sizeof(bytes)); word(&f, base + 0x600, 0x1234);
        b = step(&cpu); after = get(&cpu);
        if (r == 1 && write) {
            assert(b.kind == BM_286_BOUNDARY_EXCEPTION && b.vector == 13);
            assert(getword(&f, a.ss.base + after.sp + 2) == 0x100);
            assert(getword(&f, base + 0x600) == 0x1234 && after.ax == a.ax);
        } else {
            assert(b.kind == BM_286_BOUNDARY_INSTRUCTION && after.ip == 0x104);
            assert(after.flags == a.flags);
            if (write) assert(getword(&f, base + 0x600) == (wide ? 0x5678 : 0x1278));
            else assert(after.ax == (wide ? 0x1234 : 0x5634));
        }
        assert(!f.locked); cpu.ops.destroy(cpu.context);
    }
}

static void instruction_repair(void)
{
    unsigned cpl, odd;
    for (cpl = 0; cpl < 4; ++cpl) for (odd = 0; odd < 2; ++odd) {
        fixture_t f; bm_cpu_t cpu = create(&f, cpl, odd);
        bm_286_arch_state_t a = get(&cpu);
        /* Null DS -> fault on prefixed MOV AX,[BX]. Handler loads DS from a
         * real descriptor, removes its known error word with MOV SP, then
         * executes IRET. No state import or direct helper call after start. */
        uint8_t program[] = {0xb8,0,0,0x8e,0xd8,0x3e,0x8b,0x07,0x90};
        uint8_t handler[] = {0xb8,(uint8_t)(24+cpl),0,0x8e,0xd8,0xbc,0xfa,0x7f,0xcf};
        code(&f, program, sizeof(program)); memcpy(f.ram + 0x3400, handler, sizeof(handler));
        word(&f, a.ds.base + 0x600, 0xbeef);
        assert(step(&cpu).kind == BM_286_BOUNDARY_INSTRUCTION);
        assert(step(&cpu).kind == BM_286_BOUNDARY_INSTRUCTION);
        a = get(&cpu); assert(!a.ds.valid && a.ip == 0x105);
        assert(step(&cpu).vector == 13);
        a = get(&cpu); assert(a.ip == 0x400 && a.sp == 0x7ff8);
        assert(getword(&f, a.ss.base + a.sp) == 0);
        assert(getword(&f, a.ss.base + a.sp + 2) == 0x105);
        step(&cpu); step(&cpu); step(&cpu); step(&cpu);
        a = get(&cpu); assert(a.ip == 0x105 && a.sp == 0x8000 && a.ds.valid);
        assert(f.ram[0x201d + odd] & 1u); /* Loader performed A writeback. */
        step(&cpu); a = get(&cpu); assert(a.ax == 0xbeef && a.ip == 0x108);
        step(&cpu); assert(get(&cpu).ip == 0x109);
        cpu.ops.destroy(cpu.context);
    }
}

/* Each route supplies a complete single boundary with or without guest entry. */
static void route(fixture_t *f, bm_cpu_t *cpu, unsigned which, unsigned odd)
{
    bm_286_arch_state_t a;
    static const uint8_t store[] = {0x3e,0xc7,0x07,0x34,0x12};
    static const uint8_t load[] = {0x3e,0x8b,0x07};
    static const uint8_t segment_load[] = {0x8e,0xd8};
    static const uint8_t iret[] = {0xcf};
    *cpu = create(f, 0, odd); a = get(cpu);
    if (which == 0) code(f, store, sizeof(store));
    if (which == 1) code(f, load, sizeof(load));
    if (which == 2) { code(f, load, sizeof(load)); a.ds.valid = 0; }
    if (which == 3 || which == 4) {
        code(f, segment_load, sizeof(segment_load)); a.ax = 24;
        if (which == 4) f->ram[a.gdtr.base + 29] &= 0x7f;
    }
    if (which == 5) {
        code(f, iret, sizeof(iret)); a.sp = 0x7000;
        word(f, a.ss.base + a.sp, 0x500); word(f, a.ss.base + a.sp + 2, 8);
        word(f, a.ss.base + a.sp + 4, 2);
    }
    if (which == 6) {
        code(f, load, sizeof(load)); a.nmi_pending = 1;
    }
    assert(bm_286_set_arch_state(cpu, &a) == BM_STATUS_OK);
}

static void transfer_failures(void)
{
    static const bm_status_t errors[] = {BM_STATUS_IDLE,BM_STATUS_UNSUPPORTED,
        BM_STATUS_INVALID_ARGUMENT,BM_STATUS_INVALID_STATE,BM_STATUS_DEVICE_ERROR};
    unsigned which, odd, fail, phase, e, total = 0;
    for (which = 0; which < 7; ++which) for (odd = 0; odd < 2; ++odd) {
        fixture_t f; bm_cpu_t cpu; unsigned calls;
        route(&f, &cpu, which, odd); step(&cpu); calls = f.calls;
        cpu.ops.destroy(cpu.context);
        for (fail = 1; fail <= calls; ++fail) for (phase = 0; phase < 2; ++phase)
        for (e = 0; e < sizeof(errors)/sizeof(errors[0]); ++e) {
            bm_286_arch_state_t before, after;
            bm_286_boundary_t b;
            uint8_t expected[65536]; unsigned t, i;
            route(&f, &cpu, which, odd); before = get(&cpu);
            memcpy(expected, f.ram, sizeof(expected));
            f.fail = fail; f.after = phase != 0; f.failure = errors[e];
            assert(bm_286_pm_step_subset(&cpu, &b) == errors[e]);
            assert(f.calls == fail && f.effects == fail - 1 + phase);
            after = get(&cpu); same(&before, &after);
            for (t = 0; t < f.effects; ++t) if (f.trace[t].operation == BM_BUS_WRITE)
                for (i = 0; i < f.trace[t].size; ++i)
                    expected[(unsigned)(f.trace[t].address + i) & 65535u] =
                        (uint8_t)(f.trace[t].value >> (8u * i));
            assert(memcmp(expected, f.ram, sizeof(expected)) == 0);
            assert(!f.locked && f.locks == f.unlocks);
            assert(bm_286_pm_step_subset(&cpu, &b) == BM_STATUS_INVALID_STATE && f.calls == fail);
            assert(bm_286_step(&cpu, &b) == BM_STATUS_INVALID_STATE && f.calls == fail);
            cpu.ops.destroy(cpu.context); ++total;
        }
    }
    printf("private protected execution: %u before/after transfer failures\n", total);
}

static void events_and_gates(void)
{
    fixture_t f; bm_cpu_t cpu = create(&f, 0, 0);
    bm_286_arch_state_t a = get(&cpu); bm_286_boundary_t b;
    const uint8_t program[] = {0x8e,0xd0,0x90,0x90}; /* MOV SS,AX; NOP; NOP */
    const uint8_t iret[] = {0xcf};
    a.ax = 16; a.flags |= 0x300;
    assert(bm_286_set_arch_state(&cpu, &a) == BM_STATUS_OK);
    code(&f, program, sizeof(program)); memcpy(f.ram + 0x3400, iret, sizeof(iret));
    step(&cpu); a = get(&cpu);
    assert(a.interrupt_shadow == BM_286_SHADOW_SS_LOAD && !a.trap_pending);
    f.cpu = &cpu;
    assert(cpu.ops.signal(cpu.context, BM_286_SIGNAL_NMI, 1) == BM_STATUS_OK);
    assert(cpu.ops.signal(cpu.context, BM_286_SIGNAL_INTR, 1) == BM_STATUS_OK);
    assert(cpu.ops.signal(cpu.context, BM_286_SIGNAL_HOLD, 1) == BM_STATUS_OK);
    { unsigned count = f.calls;
      assert(bm_286_pm_step_subset(&cpu, &b) == BM_STATUS_IDLE && b.kind == BM_286_BOUNDARY_HOLD);
      assert(f.hlda && f.calls == count); }
    assert(cpu.ops.signal(cpu.context, BM_286_SIGNAL_HOLD, 0) == BM_STATUS_OK && !f.hlda);
    assert(step(&cpu).kind == BM_286_BOUNDARY_INSTRUCTION); /* SS shadow delays all three. */
    assert(get(&cpu).trap_pending);
    b = step(&cpu); assert(b.vector == 1 && b.kind == BM_286_BOUNDARY_EXCEPTION && f.acks == 0);
    b = step(&cpu); assert(b.vector == 2 && b.kind == BM_286_BOUNDARY_INTERRUPT);
    /* A fresh NMI edge during IRET survives the frame commit/unblock. */
    f.nmi_at = f.calls + 1;
    step(&cpu); a = get(&cpu); assert(a.nmi_pending && !a.nmi_blocked);
    f.nmi_at = 0; assert(step(&cpu).vector == 2);
    step(&cpu); /* IRET from second NMI. */
    step(&cpu); /* IRET from #1, restoring IF/TF. */
    b = step(&cpu); assert(b.vector == 0x20 && f.acks == 2);
    assert(!f.locked && f.locks == f.unlocks);
    cpu.ops.destroy(cpu.context);
    /* Strict clocks refuse before fetch; neither functional spelling can
     * rescue a latched stop. Only reset starts a new execution interval. */
    cpu = create(&f, 0, 0); code(&f, iret, 1);
    uint64_t cycles = 99;
    assert(bm_286_step_clocked(cpu.context, 0, &cycles) == BM_STATUS_UNSUPPORTED && !cycles && f.calls == 0);
    assert(bm_286_pm_step_subset(&cpu, &b) == BM_STATUS_INVALID_STATE && f.calls == 0);
    cpu.ops.destroy(cpu.context);
    {
        const uint8_t gaps[] = {0x64,0x65,0x66,0x67,0x9a,0xca,0xcb,0xd6,0x82};
        unsigned i;
        for (i = 0; i < sizeof(gaps); ++i) {
            cpu = create(&f, 0, 0); code(&f, gaps + i, 1); a = get(&cpu);
            if (gaps[i] == 0xca || gaps[i] == 0xcb || gaps[i] == 0x9a) {
                /* E1/E2a admit RETF/CALL; retain these formerly unsupported cases
                 * as guest #GP tests for the fixture's null selector. */
                b = step(&cpu);
                assert(b.kind == BM_286_BOUNDARY_EXCEPTION && b.vector == 13);
                assert(get(&cpu).sp == 0x7ff8 && getword(&f,0x7ffa) == a.ip);
                cpu.ops.destroy(cpu.context); continue;
            }
            assert(bm_286_pm_step_subset(&cpu, &b) == BM_STATUS_UNSUPPORTED);
            { bm_286_arch_state_t after = get(&cpu); same(&a, &after); }
            assert(f.calls == 1 && f.trace[0].operation == BM_BUS_FETCH);
            cpu.ops.destroy(cpu.context);
        }
    }
}

static void fault_matrix(void)
{
    unsigned cpl, odd, which;
    for (cpl = 0; cpl < 4; ++cpl) for (odd = 0; odd < 2; ++odd)
    for (which = 0; which < 11; ++which) {
        fixture_t f; bm_cpu_t cpu = create(&f, cpl, odd);
        bm_286_arch_state_t a = get(&cpu), after;
        bm_286_boundary_t b;
        uint8_t bytes[11] = {0x3e,0x8b,0x07};
        unsigned n = 3, vector = 13, error = 0, fetched = 3, i;
        switch (which) {
            case 0: a.ds.limit = 0x600; break; /* Word crosses DS limit. */
            case 1: bytes[0] = 0x36; a.ss.limit = 0x7fff; a.bx = 0x7fff; vector = 12; break;
            case 2: bytes[0] = 0x2e; a.cs.access &= 0xfdu; break; /* Execute-only CS read. */
            case 3: bytes[1] = 0x89; a.ds.access &= 0xfdu; break; /* Read-only write. */
            case 4: bytes[0] = 0x8e; bytes[1] = 0xd0; n = fetched = 2;
                a.ax = (uint16_t)(24 + cpl); f.ram[a.gdtr.base + 29] &= 0x7f;
                vector = 12; error = 24; break; /* MOV SS absent descriptor. */
            case 5: bytes[0] = 0x8e; bytes[1] = 0xd8; n = fetched = 2;
                a.ax = (uint16_t)(24 + cpl); f.ram[a.gdtr.base + 29] &= 0x7f;
                vector = 11; error = 24; break;
            case 6: bytes[0] = 0x8e; bytes[1] = 0xc8; n = fetched = 2; vector = 6; break;
            case 7: bytes[0] = 0x8d; bytes[1] = 0xc0; n = fetched = 2; vector = 6; break;
            case 8: a.cs.limit = 0x101; fetched = 2; break; /* ModR/M not fetched. */
            case 9: memset(bytes, 0x3e, 10); bytes[10] = 0x90; n = 11; fetched = 10; break;
            default: a.ds.access |= 4u; a.ds.limit = 0x600; break; /* Expand-down lower boundary. */
        }
        code(&f, bytes, n);
        assert(bm_286_set_arch_state(&cpu, &a) == BM_STATUS_OK);
        b = step(&cpu); after = get(&cpu);
        assert(b.kind == BM_286_BOUNDARY_EXCEPTION && b.vector == vector);
        assert(b.instruction_ip == 0x100 && after.ip == 0x400);
        assert(after.ax == a.ax && after.bx == a.bx && after.bp == a.bp);
        assert(after.ds.selector == a.ds.selector && after.ds.valid == a.ds.valid);
        assert(after.ss.selector == a.ss.selector && after.ss.limit == a.ss.limit);
        assert(after.sp == a.sp - (vector == 6 ? 6 : 8));
        if (vector != 6) assert(getword(&f, after.ss.base + after.sp) == error);
        assert(getword(&f, after.ss.base + after.sp + (vector == 6 ? 0u : 2u)) == 0x100);
        for (i = 0; i < fetched; ++i) assert(f.trace[i].operation == BM_BUS_FETCH);
        /* First non-fetch is descriptor loading (4/5) or IDT, never a rejected
         * operand read/write or a speculative fetch beyond the required bytes. */
        assert(f.trace[fetched].address ==
            (which == 4 || which == 5 ? a.gdtr.base + 24u : a.idtr.base + vector * 8u));
        cpu.ops.destroy(cpu.context);
    }
}

static void remaining_forms(void)
{
    /* Exercise every allowed opcode family without adding an alternate
     * interpreter oracle: authored streams assert their concrete results. */
    fixture_t f; bm_cpu_t cpu = create(&f, 0, 1);
    bm_286_arch_state_t a = get(&cpu);
    const uint8_t bytes[] = {
        0xb8,0x34,0x12,       /* AX=1234 */
        0xb3,0x56,            /* BL=56, BX=0656 */
        0x88,0xc4,            /* AH=AL -> AX=3434 */
        0x89,0xc1,            /* CX=AX */
        0x8a,0xe3,            /* AH=BL -> AX=5634 */
        0x8b,0xd1,            /* DX=CX */
        0xc6,0xc0,0xab,       /* AL=AB */
        0xc7,0xc6,0x20,0,     /* SI=0020 */
        0x8c,0xd7,            /* DI=SS selector */
        0x8d,0x68,0x02,       /* BP=BX+SI+2=0678 (no segment check) */
        0x91,                 /* AX<->CX */
        0xd7,                 /* AL=[DS:BX+AL]=[068a] */
        0x90
    };
    unsigned i;
    code(&f, bytes, sizeof(bytes)); f.ram[a.ds.base + 0x68a] = 0xef;
    for (i = 0; i < 13; ++i) assert(step(&cpu).kind == BM_286_BOUNDARY_INSTRUCTION);
    a = get(&cpu);
    assert(a.ax == 0x34ef && a.cx == 0x56ab && a.dx == 0x3434);
    assert(a.bx == 0x656 && a.bp == 0x678 && a.si == 0x20 && a.di == 16);
    assert(a.ip == 0x100 + sizeof(bytes) && a.flags == 2);
    cpu.ops.destroy(cpu.context);
    /* Expand-down access succeeds strictly above its limit. */
    cpu = create(&f, 0, 1); a = get(&cpu); a.ds.limit = 0x5ff; a.ds.access |= 4u;
    assert(bm_286_set_arch_state(&cpu, &a) == BM_STATUS_OK);
    { const uint8_t load[] = {0x8b,0x07}; code(&f, load, 2); }
    word(&f, a.ds.base + 0x600, 0x9876); step(&cpu);
    assert(get(&cpu).ax == 0x9876); cpu.ops.destroy(cpu.context);
    /* Signals latched by an operand callback survive its register commit. */
    cpu = create(&f, 0, 0); a = get(&cpu); f.cpu = &cpu; f.nmi_at = 3;
    { const uint8_t load[] = {0x8b,0x07}; code(&f, load, 2); }
    word(&f, a.ds.base + 0x600, 0xcafe); step(&cpu);
    a = get(&cpu); assert(a.ax == 0xcafe && a.nmi_pending && a.ip == 0x102);
    assert(step(&cpu).vector == 2); cpu.ops.destroy(cpu.context);
}

int main(void)
{
    mov_matrix(); instruction_repair(); transfer_failures(); events_and_gates();
    fault_matrix(); remaining_forms();
    puts("private protected MOV/IRET execution, faults, events and public gates passed");
    return 0;
}
