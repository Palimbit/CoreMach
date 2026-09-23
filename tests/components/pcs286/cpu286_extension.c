/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Authored tests of the unpopulated processor-extension fault gate.
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
    e.cs.limit = 0xffff; e.cs.access = 0; e.cs.valid = 1;
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
                assert(status == BM_STATUS_UNSUPPORTED);
                a = state(f); same(&a, &s); assert(f->count == prefix+1);
            }
            if (status == BM_STATUS_OK) {
                assert(b.timing == BM_286_TIMING_UNKNOWN);
                assert(b.cpu_cycles == f->count*2 && b.bus_wait_cycles == b.cpu_cycles);
            }
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
        if (bad == 2) s.msw |= 1;
        if (bad == 3) f->ram[0x30100] = 0xf0;
        if (bad == 4) f->ram[0x30100] = 0xf3;
        set(f, &s);
        assert(bm_286_step(&f->cpu, &b) == BM_STATUS_UNSUPPORTED);
        a = state(f); same(&a, &s);
        for (unsigned t = 0; t < f->count; ++t) assert(f->trace[t].operation == BM_BUS_FETCH);
    }
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
    matrix(&f); retry_and_failure(&f); boundaries(&f);
    f.cpu.ops.destroy(f.cpu.context); free(f.ram); return 0;
}
