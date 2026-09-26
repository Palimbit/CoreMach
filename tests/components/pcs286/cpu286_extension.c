/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Authored tests of real-mode MSW control and the extension fault gate.
 * No floating-point emulation or physical bus/timing evidence.
 */
#include <blumach/components/cpu_80286.h>
#include <blumach/platforms/null_host.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct fixture {
    bm_cpu_t cpu;
    uint8_t *ram;
    bm_bus_transaction_t trace[24];
    unsigned count, fail_at, fail_after;
} fixture_t;
static bm_status_t access_bus(void *context, bm_bus_transaction_t *t)
{
    fixture_t *f = context;
    assert(t->space != BM_ADDRESS_IO && t->address + t->size <= 0x100000);
    assert(f->count < 24 && !t->wait_states);
    f->trace[f->count++] = *t;
    if (f->count == f->fail_at && !f->fail_after) return BM_STATUS_DEVICE_ERROR;
    if (t->operation != BM_BUS_WRITE) t->value = 0;
    for (unsigned i = 0; i < t->size; ++i)
        if (t->operation == BM_BUS_WRITE)
            f->ram[(size_t)t->address+i] = (uint8_t)(t->value >> (8U*i));
        else t->value |= (uint64_t)f->ram[(size_t)t->address+i] << (8U*i);
    if (f->count == f->fail_at) return BM_STATUS_DEVICE_ERROR;
    t->wait_states = 2;
    return BM_STATUS_OK;
}
static bm_286_arch_state_t state(fixture_t *f)
{
    bm_286_arch_state_t s;
    assert(bm_286_get_arch_state(&f->cpu, &s) == BM_STATUS_OK);
    return s;
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
static void word(fixture_t *f, unsigned address, uint16_t value)
{
    f->ram[address] = (uint8_t)value; f->ram[address+1] = (uint8_t)(value >> 8);
}
static uint16_t get_word(fixture_t *f, unsigned address)
{
    return (uint16_t)(f->ram[address] | (unsigned)f->ram[address+1] << 8);
}
static bm_286_arch_state_t setup(fixture_t *f, uint8_t opcode, unsigned bits, unsigned prefix)
{
    assert(f->cpu.ops.reset(f->cpu.context) == BM_STATUS_OK);
    bm_286_arch_state_t s = state(f);
    f->count = f->fail_at = f->fail_after = 0;
    s.cs.selector = 0x3000; s.cs.base = 0x30000; s.ip = 0x100;
    s.ss.selector = 0x1000; s.ss.base = 0x10000; s.sp = 0x800;
    s.ax = 0xa55a; s.bx = 0xffff; s.cx = 23; s.dx = 0x1234;
    s.flags = 0xed7; s.msw = (uint16_t)(0xfff0 | bits << 1);
    s.es.valid = 0; /* Prefix never forces a data read for #7 or WAIT. */
    f->ram[0x30100] = 0x26; f->ram[0x30100 + prefix] = opcode;
    f->ram[0x30101 + prefix] = 0x06; /* ESC memory ModRM followed by FFFF. */
    f->ram[0x30102 + prefix] = f->ram[0x30103 + prefix] = 0xff;
    word(f, 28, 0x200); word(f, 30, 0x4000);
    f->ram[0x40200] = 0xcf;
    return s;
}
static void set(fixture_t *f, const bm_286_arch_state_t *s)
{
    assert(bm_286_set_arch_state(&f->cpu, s) == BM_STATUS_OK);
}
static void check_fault(fixture_t *f, const bm_286_arch_state_t *s, const bm_286_boundary_t *b)
{
    bm_286_arch_state_t a = state(f), e = *s;
    e.sp = (uint16_t)(s->sp - 6); e.flags &= 0xfcffU; e.ip = 0x200;
    e.cs.selector = 0x4000; e.cs.base = 0x40000;
    e.cs.limit = 0xffff; e.cs.access = 0x82; e.cs.valid = 1;
    e.trap_pending = 0; e.interrupt_shadow = BM_286_SHADOW_NONE;
    same(&a, &e);
    assert(b->kind == BM_286_BOUNDARY_EXCEPTION && b->has_vector && b->vector == 7);
    assert(b->instruction_ip == s->ip && b->instruction_address == s->cs.base+s->ip);
    assert(get_word(f, s->ss.base+s->sp-2) == s->flags);
    assert(get_word(f, s->ss.base+s->sp-4) == s->cs.selector);
    assert(get_word(f, s->ss.base+s->sp-6) == s->ip);
}
static void matrix(fixture_t *f)
{
    /* Explicit truth tables indexed by TS:EM:MP, not production conditions. */
    const unsigned esc_fault[8] = {0,0,1,1,1,1,1,1};
    const unsigned wait_fault[8] = {0,0,0,0,0,1,0,1};
    for (unsigned bits = 0; bits < 8; ++bits) for (unsigned op = 0; op < 9; ++op)
        for (unsigned prefix = 0; prefix < 2; ++prefix) {
            uint8_t opcode = (uint8_t)(op == 8 ? 0x9b : 0xd8 + op);
            bm_286_arch_state_t s = setup(f, opcode, bits, prefix), a, e;
            bm_286_boundary_t b; set(f, &s);
            bm_status_t status = bm_286_step(&f->cpu, &b);
            if (op == 8 ? wait_fault[bits] : esc_fault[bits]) {
                assert(status == BM_STATUS_OK); check_fault(f, &s, &b);
                assert(f->count == prefix + 6); /* opcode, stack and IVT only */
            } else if (op == 8) {
                assert(status == BM_STATUS_OK); a = state(f); e = s;
                e.ip += (uint16_t)(prefix+1); same(&a, &e);
                assert(!b.has_vector && b.kind == BM_286_BOUNDARY_INSTRUCTION);
                assert(f->count == prefix+1);
            } else {
                assert(status == BM_STATUS_OK); a = state(f); e = s;
                e.ip += (uint16_t)(prefix+4); same(&a, &e);
                assert(!b.has_vector && b.kind == BM_286_BOUNDARY_INSTRUCTION);
                /* Fetch the complete direct-address form; absent PEREQ means
                 * no access to ES:FFFF and no fabricated x87 store. */
                assert(f->count == prefix+4);
            }
            if (status == BM_STATUS_OK) {
                assert(b.timing == BM_286_TIMING_UNKNOWN);
                assert(b.cpu_cycles == f->count*2 && b.bus_wait_cycles == b.cpu_cycles);
            }
    }
}
static void absent_extension(fixture_t *f)
{
    static const struct {
        uint8_t bytes[4];
        unsigned length;
    } forms[] = {
        {{0xd8,0xc0,0,0},2},       /* Register form. */
        {{0xd9,0x00,0,0},2},       /* [BX+SI]. */
        {{0xda,0x46,0x80,0},3},    /* [BP-80h]. */
        {{0xdc,0x86,0x34,0x12},4}, /* [BP+1234h]. */
        {{0xdf,0x06,0xff,0xff},4}  /* Direct FFFFh. */
    };
    for (unsigned mode = 0; mode < 2; ++mode)
        for (unsigned i = 0; i < sizeof(forms)/sizeof(forms[0]); ++i) {
            bm_286_arch_state_t s = setup(f, forms[i].bytes[0], 0, 0), e, a;
            bm_286_boundary_t b;
            memcpy(f->ram+0x30100, forms[i].bytes, forms[i].length);
            s.msw |= (uint16_t)mode; /* Same absence semantics in RM and PE. */
            s.ds.valid = s.ss.valid = 0; /* Address calculation is not access. */
            e = s; e.ip += (uint16_t)forms[i].length; set(f, &s);
            assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK);
            a = state(f); same(&a, &e);
            assert(f->count == forms[i].length);
            assert(!b.has_vector && b.kind == BM_286_BOUNDARY_INSTRUCTION);
        }

    /* The BIOS detection form must leave its destination unchanged when no
     * 80287 can request an operand transfer. */
    {
        static const uint8_t detect[] = {0xdb,0xe3,0xd9,0x3e,0x06,0x00};
        bm_286_arch_state_t s = setup(f, detect[0], 0, 0), a;
        bm_286_boundary_t b;
        memcpy(f->ram+0x30100, detect, sizeof(detect));
        word(f, 6, 0xa500); set(f, &s);
        assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK);
        assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK);
        a = state(f); assert(a.ip == 0x106 && get_word(f, 6) == 0xa500);
        assert(f->count == sizeof(detect));
    }

    /* Host fetch failures stay host failures and never become guest #7/#13. */
    for (unsigned fail = 1; fail <= 4; ++fail) for (unsigned after = 0; after < 2; ++after) {
        bm_286_arch_state_t s = setup(f, 0xd9, 0, 0), a;
        bm_286_boundary_t b;
        f->ram[0x30101]=0x06; f->ram[0x30102]=0x34; f->ram[0x30103]=0x12;
        f->fail_at=fail; f->fail_after=after; set(f, &s);
        assert(bm_286_step(&f->cpu, &b) == BM_STATUS_DEVICE_ERROR);
        a=state(f); same(&a,&s); assert(f->count==fail);
        assert(bm_286_step(&f->cpu,&b)==BM_STATUS_INVALID_STATE && f->count==fail);
    }
}
static void retry_and_failure(fixture_t *f)
{
    /* Correct TS externally while in handler; IRET retries WAIT, not its successor. */
    bm_286_arch_state_t s = setup(f, 0x9b, 5, 1), a;
    bm_286_boundary_t b;
    s.flags = 0x302; set(f, &s);
    assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK); check_fault(f, &s, &b);
    a = state(f); a.msw &= 0xfff7U; set(f, &a);
    assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK);
    a = state(f); s.msw &= 0xfff7U; same(&a, &s);
    assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK);
    a = state(f); assert(a.ip == 0x102 && a.trap_pending && !b.has_vector);
    for (unsigned odd = 0; odd < 2; ++odd) for (unsigned op = 0; op < 2; ++op) {
        s = setup(f, (uint8_t)(op ? 0x9b : 0xd9), 7, 1); s.sp += (uint16_t)odd;
        set(f, &s); assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK);
        bm_bus_transaction_t trace[24]; memcpy(trace, f->trace, sizeof(trace));
        unsigned total = f->count;
        for (unsigned fail = 1; fail <= total; ++fail) for (unsigned after = 0; after < 2; ++after) {
            s = setup(f, (uint8_t)(op ? 0x9b : 0xd9), 7, 1); s.sp += (uint16_t)odd;
            uint8_t expected[16]; memset(expected, 0xa5, sizeof(expected));
            memset(f->ram+0x107f8, 0xa5, sizeof(expected));
            for (unsigned t = 0; t < fail-1+after; ++t) if (trace[t].operation == BM_BUS_WRITE)
                for (unsigned j = 0; j < trace[t].size; ++j)
                    expected[(size_t)trace[t].address+j-0x107f8] = (uint8_t)(trace[t].value >> (8U*j));
            f->fail_at = fail; f->fail_after = after; set(f, &s);
            assert(bm_286_step(&f->cpu, &b) == BM_STATUS_DEVICE_ERROR);
            a = state(f); same(&a, &s);
            assert(f->count == fail && !memcmp(expected, f->ram+0x107f8, sizeof(expected)));
            assert(bm_286_step(&f->cpu, &b) != BM_STATUS_OK && f->count == fail);
        }
    }
}
static void boundaries(fixture_t *f)
{
    bm_286_boundary_t b;
    bm_286_arch_state_t s = setup(f, 0xd8, 2, 1), a;
    s.flags = 0x302; s.trap_pending = 1;
    s.interrupt_shadow = BM_286_SHADOW_SS_LOAD;
    set(f, &s);
    assert(f->cpu.ops.signal(f->cpu.context, BM_286_SIGNAL_INTR, 1) == BM_STATUS_OK);
    assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK); check_fault(f, &s, &b);
    for (unsigned bad = 0; bad < 5; ++bad) {
        s = setup(f, 0xd8, 2, 1);
        if (bad == 0) s.idtr.limit = 30;
        if (bad == 1) s.sp = 5;
        /* Imported PE refusal now tests strict clocks; functional PE is enabled. */
        uint64_t gate_cycles = 99;
        if (bad == 2) s.msw |= 1;
        if (bad == 3) f->ram[0x30100] = 0xf0;
        if (bad == 4) f->ram[0x30100] = 0xf3;
        set(f, &s);
        if (bad == 0 || bad == 1) {
            assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK);
            a = state(f); s.shutdown = 1; same(&a, &s);
            assert(b.kind == BM_286_BOUNDARY_SHUTDOWN && !b.has_vector && f->count == 2);
            continue;
        }
        assert((s.msw & 1U ? bm_286_step_clocked(f->cpu.context, 0, &gate_cycles) : bm_286_step(&f->cpu, &b)) == BM_STATUS_UNSUPPORTED);
        if (s.msw & 1U) assert(gate_cycles == 0);
        a = state(f); same(&a, &s);
        for (unsigned t = 0; t < f->count; ++t) assert(f->trace[t].operation == BM_BUS_FETCH);
    }
}
static uint16_t *reg(fixture_t *f, bm_286_arch_state_t *s, unsigned n)
{
    uint16_t *registers[] = {&s->ax,&s->cx,&s->dx,&s->bx,&s->sp,&s->bp,&s->si,&s->di};
    (void)f;
    return registers[n];
}
static void system_registers(fixture_t *f)
{
    for (unsigned bits = 0; bits < 8; ++bits) for (unsigned n = 0; n < 8; ++n)
        for (unsigned load = 0; load < 2; ++load) for (unsigned v = 0; v < 16; ++v) {
            bm_286_arch_state_t s = setup(f, 0x0f, bits, 0), e, a;
            bm_286_boundary_t b;
            f->ram[0x30101] = 1; f->ram[0x30102] = (uint8_t)((load ? 0xf0 : 0xe0) | n);
            *reg(f, &s, n) = (uint16_t)(0xa5b0 | v); e = s;
            if (load) e.msw = (uint16_t)(0xfff0 | v);
            else *reg(f, &e, n) = s.msw;
            e.ip += 3; set(f, &s);
            assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK);
            a = state(f); same(&a, &e);
            assert(f->count == 3 && !b.has_vector && b.timing == BM_286_TIMING_UNKNOWN);
            if (load && (v & 1U)) {
                uint64_t cycles = 99;
                assert(bm_286_step_clocked(f->cpu.context, 0, &cycles) == BM_STATUS_UNSUPPORTED && !cycles);
                a = state(f); same(&a, &e); assert(f->count == 3);
            }
        }
    for (unsigned value = 0; value < 65536; ++value) {
        bm_286_arch_state_t s = setup(f, 0x0f, 0, 0), e, a;
        bm_286_boundary_t b;
        f->ram[0x30101] = 1; f->ram[0x30102] = 0xf0; /* LMSW AX */
        s.ax = (uint16_t)value; e = s; e.ip += 3; e.msw = (uint16_t)(0xfff0 | (value % 16));
        set(f, &s); assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK);
        a = state(f); same(&a, &e);
    }
    for (unsigned bits = 0; bits < 8; ++bits) {
        bm_286_arch_state_t s = setup(f, 0x0f, bits, 1), e = s, a;
        bm_286_boundary_t b; f->ram[0x30102] = 6;
        e.msw = (uint16_t)(0xfff0 | ((bits % 4) << 1)); e.ip += 3;
        set(f, &s); assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK);
        a = state(f); same(&a, &e); assert(f->count == 3);
    }
}
static void system_memory(fixture_t *f)
{
    const uint8_t prefixes[] = {0,0x26,0x2e,0x36,0x3e};
    for (unsigned p = 0; p < 5; ++p) for (unsigned bp = 0; bp < 2; ++bp)
        for (unsigned odd = 0; odd < 2; ++odd) for (unsigned load = 0; load < 2; ++load) {
            bm_286_arch_state_t s = setup(f, 0x0f, 7, p != 0), e, a;
            bm_286_boundary_t b; unsigned start = p != 0;
            s.es.valid = 1; s.es.base = 0x20000; s.es.selector = 0x2000;
            s.bx = s.bp = (uint16_t)(0x4f0 + odd);
            if (p) f->ram[0x30100] = prefixes[p];
            f->ram[0x30101+start] = 1;
            f->ram[0x30102+start] = (uint8_t)(0x40 | (load ? 0x30 : 0x20) | (bp ? 6 : 7));
            f->ram[0x30103+start] = 0x10;
            unsigned base = p == 1 ? s.es.base : p == 2 ? s.cs.base :
                p == 3 || (!p && bp) ? s.ss.base : s.ds.base;
            unsigned address = base+0x500+odd;
            word(f, address, 0x5aa2); e = s; e.ip += (uint16_t)(4+start);
            if (load) e.msw = 0xfff2;
            set(f, &s); assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK);
            a = state(f); same(&a, &e);
            assert(get_word(f, address) == (load ? 0x5aa2 : s.msw));
            assert(f->count == 4+start+(odd ? 2 : 1));
            assert(f->trace[4+start].address == address);
            assert(f->trace[4+start].operation == (load ? BM_BUS_READ : BM_BUS_WRITE));
            unsigned total = f->count;
            for (unsigned fail = 1; fail <= total; ++fail) for (unsigned after = 0; after < 2; ++after) {
                assert(f->cpu.ops.reset(f->cpu.context) == BM_STATUS_OK);
                word(f, address, 0x5aa2); set(f, &s); f->count = 0;
                f->fail_at = fail; f->fail_after = after;
                assert(bm_286_step(&f->cpu, &b) == BM_STATUS_DEVICE_ERROR);
                a = state(f); same(&a, &s); assert(f->count == fail);
                unsigned completed = fail-1+after;
                uint16_t expected = 0x5aa2;
                if (!load && completed > 4+start)
                    expected = (uint16_t)(odd && completed == 5+start ?
                        (0x5a00 | (s.msw & 0xff)) : s.msw);
                assert(get_word(f, address) == expected);
                assert(bm_286_step(&f->cpu, &b) != BM_STATUS_OK && f->count == fail);
            }
        }
}
static void system_guest_program(fixture_t *f)
{
    /* Entire #7 recovery is now guest code; no state import inside handler. */
    const uint8_t program[] = {0xb8,0x0a,0, 0x0f,1,0xf0, 0x9b, 0x0f,1,0xe3, 0xf4};
    const uint8_t handler[] = {0x0f,6,0xcf}; /* CLTS; IRET */
    bm_286_arch_state_t s = setup(f, 0x90, 0, 0), a;
    bm_286_boundary_t b;
    memcpy(f->ram+0x30100, program, sizeof(program));
    memcpy(f->ram+0x40200, handler, sizeof(handler)); set(f, &s);
    for (unsigned i = 0; i < 2; ++i) assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK);
    a = state(f); assert(a.msw == 0xfffa && a.ip == 0x106);
    assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK); check_fault(f, &a, &b);
    f->count = 0;
    for (unsigned i = 0; i < 5; ++i) assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK);
    a = state(f); assert(a.halted && a.msw == 0xfff2 && a.bx == 0xfff2);
    assert(a.sp == s.sp && a.flags == s.flags && a.ip == 0x10b);
    assert(f->cpu.ops.reset(f->cpu.context) == BM_STATUS_OK);
    assert(state(f).msw == 0xfff0);
}
static void system_limits(fixture_t *f)
{
    for (unsigned load = 0; load < 2; ++load) {
        bm_286_arch_state_t s = setup(f, 0x0f, 0, 0), a;
        bm_286_boundary_t b;
        f->ram[0x30101] = 1; f->ram[0x30102] = (uint8_t)(load ? 0x36 : 0x26);
        f->ram[0x30103] = f->ram[0x30104] = 0xff;
        word(f, 52, 0x200); word(f, 54, 0x4000); set(f, &s);
        assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK);
        a = state(f); assert(b.has_vector && b.vector == 13 && a.ip == 0x200);
        assert(a.sp == s.sp-6 && a.msw == s.msw && f->count == 10);
        assert(get_word(f, s.ss.base+s.sp-6) == s.ip);
    }
    for (unsigned bad = 0; bad < 4; ++bad) {
        bm_286_arch_state_t s = setup(f, 0x0f, 0, 1), a;
        bm_286_boundary_t b;
        f->ram[0x30102] = 1; f->ram[0x30103] = 0xf0;
        /* Imported PE refusal now tests strict clocks; functional PE is enabled. */
        uint64_t gate_cycles = 99;
        if (bad == 0) s.msw |= 1;
        if (bad == 1) f->ram[0x30100] = 0xf0;
        if (bad == 2) f->ram[0x30100] = 0xf3;
        if (bad == 3) f->ram[0x30103] = 0xe8; /* 0F 01 /5 remains unsupported. */
        set(f, &s); assert((s.msw & 1U ? bm_286_step_clocked(f->cpu.context, 0, &gate_cycles) : bm_286_step(&f->cpu, &b)) == BM_STATUS_UNSUPPORTED);
        if (s.msw & 1U) assert(gate_cycles == 0);
        a = state(f); same(&a, &s);
    }
}
static void table_memory(fixture_t *f)
{
    const uint8_t prefixes[] = {0,0x26,0x2e,0x36,0x3e};
    for (unsigned group = 0; group < 4; ++group)
    for (unsigned p = 0; p < 5; ++p) for (unsigned bp = 0; bp < 2; ++bp)
    for (unsigned odd = 0; odd < 2; ++odd) {
        bm_286_arch_state_t s = setup(f, 0x0f, 0, p != 0), a, e;
        bm_286_boundary_t b;
        unsigned start = p != 0, load = group >= 2;
        s.es.valid = 1; s.es.base = 0x20000; s.es.selector = 0x2000;
        s.bx = s.bp = (uint16_t)(0x4f0+odd);
        s.gdtr.base = 0xabcdef; s.gdtr.limit = 0x8765;
        s.idtr.base = 0x123456; s.idtr.limit = 0x4321;
        if (p) f->ram[0x30100] = prefixes[p];
        f->ram[0x30101+start] = 1;
        f->ram[0x30102+start] = (uint8_t)(0x40 | group*8 | (bp ? 6 : 7));
        f->ram[0x30103+start] = 0x10;
        unsigned base = p == 1 ? s.es.base : p == 2 ? s.cs.base :
            p == 3 || (!p && bp) ? s.ss.base : s.ds.base;
        unsigned address = base+0x500+odd;
        const uint8_t input[] = {0xfe,0xca,0x98,0xba,0xdc,0x23};
        memcpy(f->ram+address, input, 6); e = s; e.ip += (uint16_t)(4+start);
        bm_286_table_state_t *target = (group & 1) ? &e.idtr : &e.gdtr;
        uint8_t expected[6] = {(uint8_t)target->limit,(uint8_t)(target->limit >> 8),
            (uint8_t)target->base,(uint8_t)(target->base >> 8),(uint8_t)(target->base >> 16),0xff};
        if (load) { target->limit = 0xcafe; target->base = 0xdcba98; }
        set(f, &s); assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK);
        a = state(f); same(&a, &e);
        assert(!memcmp(f->ram+address, load ? input : expected, 6));
        assert(f->count == 4+start+(odd ? 6 : 3));
        assert(!b.has_vector && b.timing == BM_286_TIMING_UNKNOWN);
        assert(b.cpu_cycles == f->count*2 && b.bus_wait_cycles == b.cpu_cycles);
        for (unsigned i = 4+start; i < f->count; ++i) {
            assert(f->trace[i].address == address+(i-4-start)*(odd ? 1 : 2));
            assert(f->trace[i].size == (odd ? 1U : 2U));
            assert(f->trace[i].operation == (load ? BM_BUS_READ : BM_BUS_WRITE));
        }
        bm_bus_transaction_t trace[24]; memcpy(trace, f->trace, sizeof(trace));
        unsigned total = f->count;
        for (unsigned fail = 1; fail <= total; ++fail) for (unsigned after = 0; after < 2; ++after) {
            assert(f->cpu.ops.reset(f->cpu.context) == BM_STATUS_OK);
            memcpy(f->ram+address, input, 6); memcpy(expected, input, 6);
            for (unsigned t = 0; t < fail-1+after; ++t) if (trace[t].operation == BM_BUS_WRITE)
                for (unsigned j = 0; j < trace[t].size; ++j)
                    expected[(size_t)trace[t].address+j-address] = (uint8_t)(trace[t].value >> (8*j));
            f->count = 0; f->fail_at = fail; f->fail_after = after; set(f, &s);
            assert(bm_286_step(&f->cpu, &b) == BM_STATUS_DEVICE_ERROR);
            a = state(f); same(&a, &s);
            assert(f->count == fail && !memcmp(f->ram+address, expected, 6));
            assert(bm_286_step(&f->cpu, &b) != BM_STATUS_OK && f->count == fail);
        }
    }
}
static void table_faults(fixture_t *f)
{
    for (unsigned group = 0; group < 4; ++group) for (unsigned n = 0; n < 15; ++n) {
        bm_286_arch_state_t s = setup(f, 0x0f, 0, 1), a, e;
        bm_286_boundary_t b;
        unsigned memory = n >= 8, vector = memory ? 13 : 6;
        s.es.valid = 1; s.es.base = 0x20000; s.es.selector = 0x2000;
        f->ram[0x30102] = 1;
        f->ram[0x30103] = (uint8_t)(group*8 | (memory ? 6 : 0xc0+n));
        unsigned offset = 0xfff9+n-8;
        if (memory) word(f, 0x30104, (uint16_t)offset);
        word(f, vector*4, 0x200); word(f, vector*4+2, 0x4000);
        memset(f->ram+0x2fff9, 0xa5, 7); set(f, &s);
        assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK); a = state(f); e = s;
        if (memory && offset <= 0xfffa) {
            e.ip += 6;
            if (group >= 2) {
                bm_286_table_state_t *table = (group & 1) ? &e.idtr : &e.gdtr;
                table->base = 0xa5a5a5; table->limit = 0xa5a5;
            }
            assert(!b.has_vector);
        } else {
            e.sp -= 6; e.flags &= 0xfcffU; e.ip = 0x200;
            e.cs.selector = 0x4000; e.cs.base = 0x40000;
            e.cs.limit = 0xffff; e.cs.access = 0x82; e.cs.valid = 1;
            assert(b.kind == BM_286_BOUNDARY_EXCEPTION && b.has_vector && b.vector == vector);
            assert(get_word(f, s.ss.base+s.sp-6) == s.ip);
            assert(f->count == (memory ? 11U : 9U));
            for (unsigned i = 0; i < 7; ++i) assert(f->ram[0x2fff9+i] == 0xa5);
        }
        same(&a, &e);
    }
}
static void table_interrupt_program(fixture_t *f)
{
    /* LIDT [0500]; INT 20h; HLT. Handler from relocated IVT: IRET. */
    const uint8_t program[] = {0x0f,1,0x1e,0,5,0xcd,0x20,0xf4};
    bm_286_arch_state_t s = setup(f, 0x0f, 0, 0), a;
    bm_286_boundary_t b;
    memcpy(f->ram+0x30100, program, sizeof(program));
    word(f, 0x500, 0x83); word(f, 0x502, 0x6000); word(f, 0x504, 0xee00);
    word(f, 0x6080, 0x200); word(f, 0x6082, 0x4000);
    set(f, &s);
    for (unsigned i = 0; i < 4; ++i) {
        f->count = 0; assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK);
        a = state(f);
        if (i == 1) assert(a.cs.base == 0x40000 && a.ip == 0x200);
    }
    assert(a.idtr.base == 0x6000 && a.idtr.limit == 0x83);
    assert(a.halted && a.ip == 0x108 && a.cs.base == s.cs.base);
    assert(a.sp == s.sp && a.flags == s.flags);
    assert(f->cpu.ops.reset(f->cpu.context) == BM_STATUS_OK);
    a = state(f); assert(a.idtr.base == 0 && a.idtr.limit == 0x3ff);
}
int main(void)
{
    fixture_t f = {0}; bm_286_config_t c = {0};
    bm_host_services_t host = bm_null_host_services();
    f.ram = calloc(0x100000, 1); assert(f.ram);
    c.size = sizeof(c); c.version = BM_286_CONTRACT_VERSION;
    c.access = access_bus; c.access_context = &f;
    /* No INTA or lock adapter: synchronous #7 must not need either. */
    assert(bm_286_create(&host, &c, &f.cpu) == BM_STATUS_OK);
    matrix(&f); absent_extension(&f); retry_and_failure(&f); boundaries(&f);
    system_registers(&f); system_memory(&f); system_guest_program(&f); system_limits(&f);
    table_memory(&f); table_faults(&f); table_interrupt_program(&f);
    f.cpu.ops.destroy(f.cpu.context); free(f.ram); return 0;
}
