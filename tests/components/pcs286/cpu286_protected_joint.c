/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 BluMach contributors
 * Joint access/delivery/return audit. Fixture adapted from protected_execution. No ROM,
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
    unsigned nmi_at, ack_fail, shutdown_changes, program_events; uint8_t irq_vector;
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
static void shutdown_pin(void *context, int asserted)
{
    fixture_t *f = context; f->shutdown = (unsigned)asserted; ++f->shutdown_changes;
}
static bm_status_t ack(void *context, unsigned phase, uint8_t *vector, uint32_t *waits)
{
    fixture_t *f = context;
    assert(f->locked && phase == (f->acks & 1u)); ++f->acks;
    if (f->acks == f->ack_fail) return f->failure;
    *vector = f->irq_vector; *waits = 5;
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
    memset(f, 0, sizeof(*f)); f->irq_vector = 0x20;
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

/* An INTA vector is data, not the origin of the event. Reserved vector
 * values are deliberately adversarial controller inputs, not recommended
 * software assignments (PRM 9.1/9.2 reserve 0..31). */
static void interrupt_origins(void)
{
    unsigned cpl, odd, trap_gate, vector, total = 0;
    for (cpl = 0; cpl < 4; ++cpl) for (odd = 0; odd < 2; ++odd)
    for (trap_gate = 0; trap_gate < 2; ++trap_gate) for (vector = 0; vector < 256; ++vector) {
        fixture_t f; bm_cpu_t cpu = create(&f, cpl, odd);
        bm_286_arch_state_t a = get(&cpu), after; bm_286_boundary_t b;
        a.flags = (uint16_t)(0x4202u | (cpl << 12));
        assert(bm_286_set_arch_state(&cpu, &a) == BM_STATUS_OK);
        f.irq_vector = (uint8_t)vector;
        f.ram[a.idtr.base + vector * 8 + 5] = (uint8_t)(0x86 + trap_gate);
        f.ram[0x3400] = 0xcf;
        assert(cpu.ops.signal(cpu.context, BM_286_SIGNAL_INTR, 1) == BM_STATUS_OK);
        b = step(&cpu); after = get(&cpu);
        assert(b.kind == BM_286_BOUNDARY_INTERRUPT && b.has_vector && b.vector == vector);
        assert(after.sp == a.sp - 6 && after.ip == 0x400 && f.acks == 2);
        assert(getword(&f, after.ss.base + after.sp) == a.ip);
        assert(getword(&f, after.ss.base + after.sp + 2) == a.cs.selector);
        assert(getword(&f, after.ss.base + after.sp + 4) == a.flags);
        assert(after.flags == (uint16_t)(a.flags & ~(0x4000u | (trap_gate ? 0u : 0x200u))));
        assert(cpu.ops.signal(cpu.context, BM_286_SIGNAL_INTR, 0) == BM_STATUS_OK);
        b = step(&cpu); after = get(&cpu);
        assert(b.kind == BM_286_BOUNDARY_INSTRUCTION && !b.has_vector);
        same(&a, &after);
        assert(!f.locked && f.locks == f.unlocks);
        cpu.ops.destroy(cpu.context); ++total;
    }
    printf("joint audit: %u INTA origin/frame/IRET cases\n", total);
}

/* No fixture repair after execution starts: the nested guest handler fixes
 * the original return frame, discards only its own error word and IRETs to
 * the faulting IRET, which then returns to the interrupted program. */
static void iret_repair(void)
{
    unsigned cpl, odd, cause, total = 0;
    for (cpl = 0; cpl < 4; ++cpl) for (odd = 0; odd < 2; ++odd)
    for (cause = 0; cause < 6; ++cause) {
        fixture_t f; bm_cpu_t cpu = create(&f, cpl, odd);
        bm_286_arch_state_t a = get(&cpu), after; bm_286_boundary_t b;
        uint8_t program[] = {0x3e,0xcf};
        uint8_t handler[] = {0x36,0xc7,0x06,0x02,0x80,(uint8_t)(8+cpl),0,
                            0xbc,0,0x80,0x83,0xec,6,0xcf};
        unsigned vector = cause == 2 ? 11 : cause >= 4 ? 12 : 13;
        unsigned selector = cause == 0 ? cpl : cause == 1 ? 16+cpl : 24+cpl;
        unsigned error = cause == 1 ? 16 : cause == 2 ? 24 : 0;
        a.flags = (uint16_t)(0x202 | (cpl << 12));
        /* Return data/code at selector 24 is separate from the valid handler
         * CS descriptor, so a rejected return can still enter the handler. */
        descriptor(&f, a.gdtr.base + 24, a.cs.base,
            (cause == 2 ? 0x1b : 0x9b) | (cpl << 5));
        if (cause == 3) word(&f, a.gdtr.base + 24, 0x50);
        if (cause >= 4) {
            a.ss.limit = (uint16_t)(cause == 4 ? 0x8002 : 0x8004);
            /* MOV AX,SS; MOV SS,AX reloads the full descriptor. */
            const uint8_t reload[] = {0x8c,0xd0,0x8e,0xd0,0xbc,0xfa,0x7f,0xcf};
            memcpy(f.ram + 0x3400, reload, sizeof(reload));
            selector = 8+cpl;
        } else memcpy(f.ram + 0x3400, handler, sizeof(handler));
        code(&f, program, sizeof(program));
        word(&f, a.ss.base + 0x8000, 0x200);
        word(&f, a.ss.base + 0x8002, selector);
        word(&f, a.ss.base + 0x8004, a.flags);
        f.ram[0x3200] = 0x90;
        assert(bm_286_set_arch_state(&cpu, &a) == BM_STATUS_OK);
        b = step(&cpu); after = get(&cpu);
        assert(b.kind == BM_286_BOUNDARY_EXCEPTION && b.vector == vector);
        assert(after.sp == 0x7ff8 && after.ip == 0x400);
        assert(getword(&f, after.ss.base + after.sp) == error);
        assert(getword(&f, after.ss.base + after.sp + 2) == 0x100);
        assert(getword(&f, after.ss.base + after.sp + 4) == a.cs.selector);
        step(&cpu); step(&cpu); step(&cpu); step(&cpu);
        after = get(&cpu); assert(after.ip == 0x100 && after.sp == 0x8000);
        b = step(&cpu); after = get(&cpu);
        assert(b.kind == BM_286_BOUNDARY_INSTRUCTION && after.ip == 0x200 && after.sp == 0x8006);
        assert(after.flags == a.flags && after.cs.selector == a.cs.selector);
        step(&cpu); assert(get(&cpu).ip == 0x201);
        cpu.ops.destroy(cpu.context); ++total;
    }
    printf("joint audit: %u nested IRET fault/guest repair/retry programs\n", total);
}

typedef struct outcome { unsigned vector, error, attempts; bool shutdown, error_word; } outcome_t;

static outcome_t route(fixture_t *f, bm_cpu_t *cpu, unsigned which, unsigned cpl, unsigned odd)
{
    bm_286_arch_state_t a;
    outcome_t expected = {8,0,2,false,true};
    const uint8_t load[] = {0x3e,0x8b,0x07}, ud[] = {0x3e,0x8d,0xc0};
    const uint8_t iret[] = {0x3e,0xcf}, software[] = {0xcd,8};
    *cpu = create(f, cpl, odd); a = get(cpu); a.flags |= 0x200;
    code(f, load, sizeof(load));
    switch (which) {
    case 0: case 5: /* Operand #GP cannot enter its absent handler. */
        a.ds.valid = 0; f->ram[a.idtr.base + 13*8 + 5] &= 0x7f;
        if (which == 5) { f->ram[a.idtr.base + 8*8 + 5] &= 0x7f; expected.shutdown = true; }
        break;
    case 1: case 2: /* Invalid LEA -> #NP; only rejection of #NP -> #DF. */
        code(f, ud, sizeof(ud)); f->ram[a.idtr.base + 6*8 + 5] &= 0x7f;
        if (which == 1) { expected.vector = 11; expected.error = 6*8+2; }
        else { f->ram[a.idtr.base + 11*8 + 5] &= 0x7f; expected.attempts = 3; }
        break;
    case 3: case 4:
        code(f, iret, sizeof(iret)); word(f, a.ss.base+a.sp+2, cpl);
        if (which == 3) { expected.vector = 13; expected.attempts = 1; }
        else f->ram[a.idtr.base + 13*8 + 5] &= 0x7f;
        break;
    case 6: case 7: case 8:
        expected.vector = 11; expected.error = (which == 8 ? 2 : 0x20)*8+3;
        f->ram[a.idtr.base + (which == 8 ? 2 : 0x20)*8 + 5] &= 0x7f;
        if (which == 7) {
            f->ram[a.idtr.base + 11*8 + 5] &= 0x7f;
            expected.vector = 8; expected.error = 0; expected.attempts = 3;
        }
        if (which == 8) a.nmi_pending = 1;
        else assert(cpu->ops.signal(cpu->context, BM_286_SIGNAL_INTR, 1) == BM_STATUS_OK);
        break;
    case 9: /* PUSH word rejected; even #DF cannot use this stack. */
        f->ram[0x3100] = 0x50; a.sp = 1; expected.shutdown = true;
        break;
    case 10:
        code(f, software, sizeof(software));
        f->ram[a.idtr.base+8*8+5] |= (uint8_t)(cpl << 5);
        expected.attempts = 1; expected.error_word = false;
        break;
    case 11:
        a.trap_pending = 1; expected.vector = 1; expected.attempts = 1; expected.error_word = false;
        break;
    default: assert(false);
    }
    assert(bm_286_set_arch_state(cpu, &a) == BM_STATUS_OK);
    return expected;
}

static void escalation_and_shutdown(void)
{
    unsigned which, cpl, odd, total = 0;
    for (which = 0; which < 12; ++which) for (cpl = 0; cpl < 4; ++cpl)
    for (odd = 0; odd < 2; ++odd) {
        fixture_t f; bm_cpu_t cpu; outcome_t expected = route(&f, &cpu, which, cpl, odd);
        bm_286_arch_state_t before = get(&cpu), after; bm_286_boundary_t b;
        unsigned i, attempts = 0;
        b = step(&cpu); after = get(&cpu);
        assert(b.has_vector == !expected.shutdown);
        assert(b.kind == (expected.shutdown ? BM_286_BOUNDARY_SHUTDOWN :
            which == 10 ? BM_286_BOUNDARY_INSTRUCTION : BM_286_BOUNDARY_EXCEPTION));
        for (i = 0; i < f.calls; ++i)
            if (f.trace[i].operation == BM_BUS_READ && f.trace[i].address >= before.idtr.base &&
                f.trace[i].address < before.idtr.base + 0x800 &&
                (f.trace[i].address-before.idtr.base)%8 == 0) ++attempts;
        assert(attempts == expected.attempts);
        assert(f.acks == ((which == 6 || which == 7) ? 2u : 0u));
        if (!expected.shutdown) {
            unsigned frame = expected.error_word ? 8u : 6u;
            assert(b.vector == expected.vector && after.ip == 0x400 && after.sp == before.sp-frame);
            if (expected.error_word) assert(getword(&f, after.ss.base+after.sp) == expected.error);
            assert(getword(&f, after.ss.base+after.sp+(expected.error_word ? 2 : 0)) ==
                (which == 10 ? 0x102 : 0x100));
            assert(getword(&f, after.ss.base+after.sp+frame-2) == before.flags);
            assert(!f.shutdown && !after.shutdown);
        } else {
            unsigned calls = f.calls;
            assert(after.shutdown && after.sp == before.sp && after.ip == before.ip);
            assert(f.shutdown && f.shutdown_changes == 1);
            assert(bm_286_pm_step_subset(&cpu, &b) == BM_STATUS_IDLE && f.calls == calls);
            assert(cpu.ops.signal(cpu.context, BM_286_SIGNAL_INTR, 1) == BM_STATUS_OK);
            assert(bm_286_pm_step_subset(&cpu, &b) == BM_STATUS_IDLE && f.calls == calls && !f.acks);
        }
        assert(!f.locked && f.locks == f.unlocks);
        cpu.ops.destroy(cpu.context); ++total;
    }
    printf("joint audit: %u executed escalation/shutdown/origin cases\n", total);
}

static void host_failures(void)
{
    static const bm_status_t errors[] = {BM_STATUS_IDLE,BM_STATUS_UNSUPPORTED,
        BM_STATUS_INVALID_ARGUMENT,BM_STATUS_INVALID_STATE,BM_STATUS_DEVICE_ERROR};
    unsigned which, odd, fail, phase, e, total = 0;
    for (which = 0; which < 12; ++which) for (odd = 0; odd < 2; ++odd) {
        fixture_t f; bm_cpu_t cpu; unsigned calls;
        route(&f, &cpu, which, 0, odd); step(&cpu); calls = f.calls;
        cpu.ops.destroy(cpu.context);
        for (fail = 1; fail <= calls; ++fail) for (phase = 0; phase < 2; ++phase)
        for (e = 0; e < sizeof(errors)/sizeof(errors[0]); ++e) {
            bm_286_arch_state_t before, after; bm_286_boundary_t b;
            uint8_t expected[65536]; unsigned t, i, acks;
            route(&f, &cpu, which, 0, odd); before = get(&cpu);
            memcpy(expected, f.ram, sizeof(expected));
            f.fail = fail; f.after = phase != 0; f.failure = errors[e];
            assert(bm_286_pm_step_subset(&cpu, &b) == errors[e]);
            assert(f.calls == fail && f.effects == fail-1+phase);
            after = get(&cpu); same(&before, &after);
            for (t = 0; t < f.effects; ++t) if (f.trace[t].operation == BM_BUS_WRITE)
                for (i = 0; i < f.trace[t].size; ++i)
                    expected[(unsigned)(f.trace[t].address+i)&65535u] =
                        (uint8_t)(f.trace[t].value >> (8*i));
            assert(memcmp(expected, f.ram, sizeof(expected)) == 0);
            assert(!f.shutdown_changes && !f.locked && f.locks == f.unlocks);
            acks = f.acks;
            assert(bm_286_pm_step_subset(&cpu, &b) == BM_STATUS_INVALID_STATE);
            assert(bm_286_step(&cpu, &b) == BM_STATUS_INVALID_STATE && f.calls == fail && f.acks == acks);
            cpu.ops.destroy(cpu.context); ++total;
        }
    }
    /* INTA transport failures are never converted into guest exceptions,
     * including an IDLE returned after acceptance. */
    for (fail = 1; fail <= 2; ++fail) for (e = 0; e < sizeof(errors)/sizeof(errors[0]); ++e) {
        fixture_t f; bm_cpu_t cpu; bm_286_arch_state_t a, after; bm_286_boundary_t b;
        route(&f, &cpu, 7, 0, 0); a = get(&cpu); f.ack_fail = fail; f.failure = errors[e];
        assert(bm_286_pm_step_subset(&cpu, &b) == errors[e]);
        after = get(&cpu); same(&a, &after);
        assert(!f.calls && f.acks == fail && !f.locked && f.locks == f.unlocks);
        assert(bm_286_pm_step_subset(&cpu, &b) == BM_STATUS_INVALID_STATE && f.acks == fail);
        cpu.ops.destroy(cpu.context); ++total;
    }
    printf("joint audit: %u escalation/IRET/INTA host failures, no replay\n", total);
}

static void shutdown_recovery(void)
{
    unsigned odd, broken;
    for (odd = 0; odd < 2; ++odd) for (broken = 0; broken < 2; ++broken) {
        fixture_t f; bm_cpu_t cpu; bm_286_arch_state_t a; bm_286_boundary_t b;
        const uint8_t handler[] = {0x36,0xc7,0x06,0xfa,0x7f,0,2,0xcf};
        route(&f, &cpu, broken ? 9 : 5, 0, odd);
        memcpy(f.ram+0x3400,handler,sizeof(handler)); f.ram[0x3200] = 0x90;
        assert(step(&cpu).kind == BM_286_BOUNDARY_SHUTDOWN);
        assert(cpu.ops.signal(cpu.context, BM_286_SIGNAL_NMI, 1) == BM_STATUS_OK);
        b = step(&cpu); a = get(&cpu);
        if (broken) {
            unsigned calls = f.calls;
            assert(b.kind == BM_286_BOUNDARY_SHUTDOWN && a.shutdown && a.nmi_blocked);
            assert(f.shutdown && f.shutdown_changes == 1);
            assert(cpu.ops.signal(cpu.context, BM_286_SIGNAL_NMI, 0) == BM_STATUS_OK);
            assert(cpu.ops.signal(cpu.context, BM_286_SIGNAL_NMI, 1) == BM_STATUS_OK);
            assert(bm_286_pm_step_subset(&cpu, &b) == BM_STATUS_IDLE && f.calls == calls);
        } else {
            assert(b.kind == BM_286_BOUNDARY_INTERRUPT && b.vector == 2);
            assert(!a.shutdown && a.nmi_blocked && !f.shutdown && f.shutdown_changes == 2);
            step(&cpu); step(&cpu); a = get(&cpu);
            assert(a.ip == 0x200 && a.sp == 0x8000 && !a.nmi_blocked);
            step(&cpu); assert(get(&cpu).ip == 0x201);
        }
        assert(cpu.ops.reset(cpu.context) == BM_STATUS_OK);
        a = get(&cpu); assert(!(a.msw&1) && !a.shutdown && !a.nmi_blocked && a.ip == 0xfff0);
        assert(!f.shutdown && !f.locked);
        cpu.ops.destroy(cpu.context);
    }
}

static bm_cpu_t reset_program(fixture_t *f, unsigned odd)
{
    bm_cpu_t cpu = create(f, 0, odd); bm_286_arch_state_t a = get(&cpu);
    const uint8_t reset_jump[] = {0xea,0,1,0,3};
    const uint8_t real[] = {
        0xb8,0,0, 0x8e,0xd8, 0x8e,0xd0, 0xbc,0,0x80,
        0x0f,1,0x16,0,8, 0x0f,1,0x1e,6,8,
        0xb8,1,0, 0x0f,1,0xf0, 0xeb,0, 0xea,0,2,8,0
    };
    const uint8_t program[] = {
        0xb8,16,0, 0x8e,0xd0, 0xbc,0,0x80,
        0xb8,24,0, 0x8e,0xd8, 0x8e,0xc0,
        0xbb,0,6, 0xb9,2,0, 0xbe,0,6, 0xbf,0,7,
        0xb8,0,0, 0x8e,0xd8, 0x3e,0x8b,7,
        0xfb,0x90, 0xf3,0xa5, 0xcc, 0x68,2,3,0x9d,0x90,0xf4
    };
    const uint8_t repair[] = {0xb8,24,0,0x8e,0xd8,0x83,0xc4,2,0xcf};
    const uint8_t trap[] = {0x36,0x81,0x26,0xfe,0x7f,0xff,0xfe,0xcf};
    word(f, a.idtr.base+2*8, 0x500); word(f, a.idtr.base+0x20*8, 0x520);
    word(f, a.idtr.base+1*8, 0x540); word(f, a.idtr.base+3*8, 0x560);
    memcpy(f->ram+0xfff0,reset_jump,sizeof(reset_jump));
    memcpy(f->ram+0x3100,real,sizeof(real)); memcpy(f->ram+0x3200,program,sizeof(program));
    memcpy(f->ram+0x3400,repair,sizeof(repair)); memcpy(f->ram+0x3540,trap,sizeof(trap));
    f->ram[0x3500] = f->ram[0x3520] = f->ram[0x3560] = 0xcf;
    word(f,0x800,31); word(f,0x802,a.gdtr.base); word(f,0x804,0);
    word(f,0x806,0x7ff); word(f,0x808,a.idtr.base); word(f,0x80a,0);
    word(f,0x4600+odd,0xbeef); word(f,0x4602+odd,0xcafe);
    /* Start at architectural reset, no subsequent state imports or fixture
     * memory writes. The bus mirrors authored RAM for the reset-vector alias. */
    assert(cpu.ops.reset(cpu.context) == BM_STATUS_OK);
    f->shutdown_changes = 0;
    return cpu;
}

static void program_signals(fixture_t *f, bm_cpu_t *cpu)
{
    bm_286_arch_state_t a = get(cpu);
    if (a.ip == 0x225 && a.cx == 1 && !f->program_events) {
        assert(cpu->ops.signal(cpu->context, BM_286_SIGNAL_NMI, 1) == BM_STATUS_OK);
        assert(cpu->ops.signal(cpu->context, BM_286_SIGNAL_INTR, 1) == BM_STATUS_OK);
        f->program_events = 1;
    }
    f->calls = f->effects = 0;
}

static void program_boundary(fixture_t *f, bm_cpu_t *cpu, const bm_286_boundary_t *b)
{
    if (b->has_vector && b->vector == 0x20)
        assert(cpu->ops.signal(cpu->context, BM_286_SIGNAL_INTR, 0) == BM_STATUS_OK);
    assert(!f->locked && f->locks == f->unlocks);
}

static void reset_program_audit(void)
{
    static const bm_status_t errors[] = {BM_STATUS_IDLE,BM_STATUS_UNSUPPORTED,
        BM_STATUS_INVALID_ARGUMENT,BM_STATUS_INVALID_STATE,BM_STATUS_DEVICE_ERROR};
    unsigned odd, total = 0;
    for (odd = 0; odd < 2; ++odd) {
        fixture_t f; bm_cpu_t cpu = reset_program(&f, odd);
        bm_286_arch_state_t a; unsigned counts[80], boundaries = 0, events = 0;
        const unsigned expected_events[] = {13,2,0x20,3,1};
        do {
            bm_286_boundary_t b;
            assert(boundaries < 80); program_signals(&f,&cpu); b = step(&cpu);
            counts[boundaries++] = f.calls; program_boundary(&f,&cpu,&b);
            if (b.has_vector) {
                assert(events < 5 && b.vector == expected_events[events++]);
                assert(b.kind == (b.vector == 13 || b.vector == 1 ? BM_286_BOUNDARY_EXCEPTION :
                    b.vector == 3 ? BM_286_BOUNDARY_INSTRUCTION : BM_286_BOUNDARY_INTERRUPT));
            }
            a = get(&cpu);
        } while (!a.halted);
        assert(events == 5 && a.ip == 0x22e && a.sp == 0x8000 && a.cx == 0);
        assert(a.si == 0x604 && a.di == 0x704 && a.ax == 0xbeef && !a.trap_pending && !a.nmi_blocked);
        assert(getword(&f,0x4700+odd) == 0xbeef && getword(&f,0x4702+odd) == 0xcafe);
        assert(f.acks == 2 && !f.shutdown_changes);
        cpu.ops.destroy(cpu.context);
        /* Replay only fresh CPUs to reach each test boundary; a stopped CPU
         * is never retried. All already completed guest instructions survive. */
        for (unsigned at = 0; at < boundaries; ++at)
        for (unsigned fail = 1; fail <= counts[at]; ++fail)
        for (unsigned phase = 0; phase < 2; ++phase)
        for (unsigned e = 0; e < sizeof(errors)/sizeof(errors[0]); ++e) {
            bm_286_arch_state_t before, after; bm_286_boundary_t b;
            uint8_t expected[65536]; unsigned acks;
            cpu = reset_program(&f,odd);
            for (unsigned i = 0; i < at; ++i) {
                program_signals(&f,&cpu); b = step(&cpu); program_boundary(&f,&cpu,&b);
            }
            program_signals(&f,&cpu); before = get(&cpu); memcpy(expected,f.ram,sizeof(expected));
            f.fail = fail; f.after = phase != 0; f.failure = errors[e];
            assert(bm_286_pm_step_subset(&cpu,&b) == errors[e]);
            after = get(&cpu); same(&before,&after);
            assert(f.calls == fail && f.effects == fail-1+phase);
            for (unsigned t = 0; t < f.effects; ++t) if (f.trace[t].operation == BM_BUS_WRITE)
                for (unsigned i = 0; i < f.trace[t].size; ++i)
                    expected[(unsigned)(f.trace[t].address+i)&65535u] = (uint8_t)(f.trace[t].value >> (8*i));
            assert(memcmp(expected,f.ram,sizeof(expected)) == 0);
            assert(!f.locked && f.locks == f.unlocks && !f.shutdown_changes);
            acks = f.acks;
            assert(bm_286_pm_step_subset(&cpu,&b) == BM_STATUS_INVALID_STATE);
            assert(bm_286_step(&cpu,&b) == BM_STATUS_INVALID_STATE && f.calls == fail && f.acks == acks);
            cpu.ops.destroy(cpu.context); ++total;
        }
        printf("joint audit: reset->PE->repair->REP/NMI/IRQ->INT3/TF->HLT, %u boundaries, alignment %u\n",boundaries,odd);
    }
    printf("joint audit: %u whole-program boundary transport failures\n",total);
}

static void fault_time_nmi(void)
{
    const unsigned routes[] = {0,3,4,5};
    unsigned r, odd, blocked, at, mode, total = 0;
    for (r = 0; r < sizeof(routes)/sizeof(routes[0]); ++r) for (odd = 0; odd < 2; ++odd) {
        fixture_t f; bm_cpu_t cpu; unsigned calls;
        route(&f,&cpu,routes[r],0,odd); step(&cpu); calls = f.calls;
        cpu.ops.destroy(cpu.context);
        for (blocked = 0; blocked < 2; ++blocked) for (at = 1; at <= calls; ++at)
        for (mode = 0; mode < 3; ++mode) {
            bm_286_arch_state_t a, after; bm_286_boundary_t b;
            route(&f,&cpu,routes[r],0,odd); a = get(&cpu); a.nmi_blocked = (uint8_t)blocked;
            assert(bm_286_set_arch_state(&cpu,&a) == BM_STATUS_OK);
            f.cpu = &cpu; f.nmi_at = at;
            if (mode) { f.fail = at; f.after = mode == 2; f.failure = BM_STATUS_DEVICE_ERROR; }
            assert(bm_286_pm_step_subset(&cpu,&b) == (mode ? BM_STATUS_DEVICE_ERROR : BM_STATUS_OK));
            after = get(&cpu);
            assert(after.nmi_pending && after.nmi_blocked == blocked);
            if (mode) {
                a.nmi_pending = 1; same(&a,&after);
                assert(bm_286_pm_step_subset(&cpu,&b) == BM_STATUS_INVALID_STATE);
            }
            assert(!f.locked && f.locks == f.unlocks);
            cpu.ops.destroy(cpu.context); ++total;
        }
    }
    printf("joint audit: %u callback NMI edges during faulting IRET/escalation, success and host failure\n",total);
}

static void clock_gates(void)
{
    unsigned shadow, state, mask, total = 0;
    for (shadow = 0; shadow < 3; ++shadow)
    for (state = 0; state < 3; ++state) for (mask = 0; mask < 32; ++mask) {
        fixture_t f; bm_cpu_t cpu = create(&f,0,0);
        bm_286_arch_state_t a = get(&cpu), after; bm_286_boundary_t b; bm_status_t status;
        uint64_t cycles = 99;
        a.interrupt_shadow = (uint8_t)shadow;
        a.halted = state == 1; a.shutdown = state == 2;
        a.trap_pending = (mask & 1u) != 0; a.nmi_pending = (mask & 2u) != 0;
        a.nmi_blocked = (mask & 4u) != 0; a.flags |= 0x200;
        assert(bm_286_set_arch_state(&cpu,&a) == BM_STATUS_OK);
        assert(cpu.ops.signal(cpu.context,BM_286_SIGNAL_INTR,(mask & 8u) != 0) == BM_STATUS_OK);
        assert(cpu.ops.signal(cpu.context,BM_286_SIGNAL_HOLD,(mask & 16u) != 0) == BM_STATUS_OK);
        status = bm_286_step_clocked(cpu.context,0,&cycles); assert(!cycles);
        assert(status == BM_STATUS_UNSUPPORTED || status == BM_STATUS_IDLE);
        after = get(&cpu); same(&a,&after);
        assert(!f.calls && !f.acks && !f.locks && !f.shutdown_changes);
        if (status == BM_STATUS_UNSUPPORTED) {
            assert(bm_286_pm_step_subset(&cpu,&b) == BM_STATUS_INVALID_STATE);
            assert(bm_286_set_arch_state(&cpu,&a) == BM_STATUS_INVALID_STATE);
        }
        cpu.ops.destroy(cpu.context); ++total;
    }
    printf("joint audit: %u strict-clock gates across signals, shadows, HLT/shutdown\n",total);
}

int main(void)
{
    interrupt_origins(); iret_repair();
    escalation_and_shutdown(); host_failures(); shutdown_recovery();
    reset_program_audit();
    fault_time_nmi(); clock_gates();
    return 0;
}
