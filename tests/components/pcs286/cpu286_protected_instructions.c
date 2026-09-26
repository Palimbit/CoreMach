/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 BluMach contributors
 * Authored protected instruction integration cases; fixture adapted from private execution tests. No ROM,
 * external vectors, public PE entry, physical timing or machine acceptance.
 */
#include "execution_286.h"
#include <blumach/platforms/null_host.h>
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef struct fixture {
    uint8_t ram[65536], ports[65536];
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
    assert(t->space == BM_ADDRESS_IO || t->space == (t->operation == BM_BUS_FETCH ? BM_ADDRESS_PROGRAM : BM_ADDRESS_DATA));
    if(t->space == BM_ADDRESS_IO) assert(t->address <= 0xffff);
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
        if (t->operation == BM_BUS_WRITE) (t->space == BM_ADDRESS_IO ? f->ports : f->ram)[at] = (uint8_t)(t->value >> (8u * i));
        else t->value |= (uint64_t)(t->space == BM_ADDRESS_IO ? f->ports : f->ram)[at] << (8u * i);
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
    f->ram[at & 65535u] = (uint8_t)value; f->ram[(at + 1) & 65535u] = (uint8_t)(value >> 8);
}
static uint16_t getword(fixture_t *f, unsigned at)
{
    return (uint16_t)(f->ram[at & 65535u] | ((uint16_t)f->ram[(at + 1) & 65535u] << 8));
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

static const struct { unsigned length; uint8_t bytes[4]; } forms[] = {
    {2,{0x0,0xc1}},
    {2,{0x0,0x7}},
    {2,{0x1,0xc1}},
    {2,{0x1,0x7}},
    {2,{0x2,0xc1}},
    {2,{0x2,0x7}},
    {2,{0x3,0xc1}},
    {2,{0x3,0x7}},
    {2,{0x4,0x7}},
    {3,{0x5,0x7,0x0}},
    {2,{0x8,0xc1}},
    {2,{0x8,0x7}},
    {2,{0x9,0xc1}},
    {2,{0x9,0x7}},
    {2,{0xa,0xc1}},
    {2,{0xa,0x7}},
    {2,{0xb,0xc1}},
    {2,{0xb,0x7}},
    {2,{0xc,0x7}},
    {3,{0xd,0x7,0x0}},
    {2,{0x10,0xc1}},
    {2,{0x10,0x7}},
    {2,{0x11,0xc1}},
    {2,{0x11,0x7}},
    {2,{0x12,0xc1}},
    {2,{0x12,0x7}},
    {2,{0x13,0xc1}},
    {2,{0x13,0x7}},
    {2,{0x14,0x7}},
    {3,{0x15,0x7,0x0}},
    {2,{0x18,0xc1}},
    {2,{0x18,0x7}},
    {2,{0x19,0xc1}},
    {2,{0x19,0x7}},
    {2,{0x1a,0xc1}},
    {2,{0x1a,0x7}},
    {2,{0x1b,0xc1}},
    {2,{0x1b,0x7}},
    {2,{0x1c,0x7}},
    {3,{0x1d,0x7,0x0}},
    {2,{0x20,0xc1}},
    {2,{0x20,0x7}},
    {2,{0x21,0xc1}},
    {2,{0x21,0x7}},
    {2,{0x22,0xc1}},
    {2,{0x22,0x7}},
    {2,{0x23,0xc1}},
    {2,{0x23,0x7}},
    {2,{0x24,0x7}},
    {3,{0x25,0x7,0x0}},
    {2,{0x28,0xc1}},
    {2,{0x28,0x7}},
    {2,{0x29,0xc1}},
    {2,{0x29,0x7}},
    {2,{0x2a,0xc1}},
    {2,{0x2a,0x7}},
    {2,{0x2b,0xc1}},
    {2,{0x2b,0x7}},
    {2,{0x2c,0x7}},
    {3,{0x2d,0x7,0x0}},
    {2,{0x30,0xc1}},
    {2,{0x30,0x7}},
    {2,{0x31,0xc1}},
    {2,{0x31,0x7}},
    {2,{0x32,0xc1}},
    {2,{0x32,0x7}},
    {2,{0x33,0xc1}},
    {2,{0x33,0x7}},
    {2,{0x34,0x7}},
    {3,{0x35,0x7,0x0}},
    {2,{0x38,0xc1}},
    {2,{0x38,0x7}},
    {2,{0x39,0xc1}},
    {2,{0x39,0x7}},
    {2,{0x3a,0xc1}},
    {2,{0x3a,0x7}},
    {2,{0x3b,0xc1}},
    {2,{0x3b,0x7}},
    {2,{0x3c,0x7}},
    {3,{0x3d,0x7,0x0}},
    {1,{0x40}},
    {1,{0x41}},
    {1,{0x42}},
    {1,{0x43}},
    {1,{0x44}},
    {1,{0x45}},
    {1,{0x46}},
    {1,{0x47}},
    {1,{0x48}},
    {1,{0x49}},
    {1,{0x4a}},
    {1,{0x4b}},
    {1,{0x4c}},
    {1,{0x4d}},
    {1,{0x4e}},
    {1,{0x4f}},
    {3,{0x80,0xc1,0x3}},
    {3,{0x80,0x7,0x3}},
    {3,{0x80,0xc9,0x3}},
    {3,{0x80,0xf,0x3}},
    {3,{0x80,0xd1,0x3}},
    {3,{0x80,0x17,0x3}},
    {3,{0x80,0xd9,0x3}},
    {3,{0x80,0x1f,0x3}},
    {3,{0x80,0xe1,0x3}},
    {3,{0x80,0x27,0x3}},
    {3,{0x80,0xe9,0x3}},
    {3,{0x80,0x2f,0x3}},
    {3,{0x80,0xf1,0x3}},
    {3,{0x80,0x37,0x3}},
    {3,{0x80,0xf9,0x3}},
    {3,{0x80,0x3f,0x3}},
    {4,{0x81,0xc1,0x3,0x0}},
    {4,{0x81,0x7,0x3,0x0}},
    {4,{0x81,0xc9,0x3,0x0}},
    {4,{0x81,0xf,0x3,0x0}},
    {4,{0x81,0xd1,0x3,0x0}},
    {4,{0x81,0x17,0x3,0x0}},
    {4,{0x81,0xd9,0x3,0x0}},
    {4,{0x81,0x1f,0x3,0x0}},
    {4,{0x81,0xe1,0x3,0x0}},
    {4,{0x81,0x27,0x3,0x0}},
    {4,{0x81,0xe9,0x3,0x0}},
    {4,{0x81,0x2f,0x3,0x0}},
    {4,{0x81,0xf1,0x3,0x0}},
    {4,{0x81,0x37,0x3,0x0}},
    {4,{0x81,0xf9,0x3,0x0}},
    {4,{0x81,0x3f,0x3,0x0}},
    {3,{0x83,0xc1,0x3}},
    {3,{0x83,0x7,0x3}},
    {3,{0x83,0xc9,0x3}},
    {3,{0x83,0xf,0x3}},
    {3,{0x83,0xd1,0x3}},
    {3,{0x83,0x17,0x3}},
    {3,{0x83,0xd9,0x3}},
    {3,{0x83,0x1f,0x3}},
    {3,{0x83,0xe1,0x3}},
    {3,{0x83,0x27,0x3}},
    {3,{0x83,0xe9,0x3}},
    {3,{0x83,0x2f,0x3}},
    {3,{0x83,0xf1,0x3}},
    {3,{0x83,0x37,0x3}},
    {3,{0x83,0xf9,0x3}},
    {3,{0x83,0x3f,0x3}},
    {2,{0x84,0xc1}},
    {2,{0x84,0x7}},
    {2,{0x85,0xc1}},
    {2,{0x85,0x7}},
    {2,{0x86,0xc1}},
    {2,{0x86,0x7}},
    {2,{0x87,0xc1}},
    {2,{0x87,0x7}},
    {3,{0xf6,0xc1,0x7}},
    {3,{0xf6,0x7,0x7}},
    {2,{0xf6,0xd1}},
    {2,{0xf6,0x17}},
    {2,{0xf6,0xd9}},
    {2,{0xf6,0x1f}},
    {2,{0xf6,0xe1}},
    {2,{0xf6,0x27}},
    {2,{0xf6,0xe9}},
    {2,{0xf6,0x2f}},
    {2,{0xf6,0xf1}},
    {2,{0xf6,0x37}},
    {2,{0xf6,0xf9}},
    {2,{0xf6,0x3f}},
    {4,{0xf7,0xc1,0x7,0x0}},
    {4,{0xf7,0x7,0x7,0x0}},
    {2,{0xf7,0xd1}},
    {2,{0xf7,0x17}},
    {2,{0xf7,0xd9}},
    {2,{0xf7,0x1f}},
    {2,{0xf7,0xe1}},
    {2,{0xf7,0x27}},
    {2,{0xf7,0xe9}},
    {2,{0xf7,0x2f}},
    {2,{0xf7,0xf1}},
    {2,{0xf7,0x37}},
    {2,{0xf7,0xf9}},
    {2,{0xf7,0x3f}},
    {2,{0xfe,0xc1}},
    {2,{0xfe,0x7}},
    {2,{0xfe,0xc9}},
    {2,{0xfe,0xf}},
    {2,{0xff,0xc1}},
    {2,{0xff,0x7}},
    {2,{0xff,0xc9}},
    {2,{0xff,0xf}},
    {3,{0xc0,0xc1,0x3}},
    {3,{0xc0,0x7,0x3}},
    {3,{0xc0,0xc9,0x3}},
    {3,{0xc0,0xf,0x3}},
    {3,{0xc0,0xd1,0x3}},
    {3,{0xc0,0x17,0x3}},
    {3,{0xc0,0xd9,0x3}},
    {3,{0xc0,0x1f,0x3}},
    {3,{0xc0,0xe1,0x3}},
    {3,{0xc0,0x27,0x3}},
    {3,{0xc0,0xe9,0x3}},
    {3,{0xc0,0x2f,0x3}},
    {3,{0xc0,0xf9,0x3}},
    {3,{0xc0,0x3f,0x3}},
    {3,{0xc1,0xc1,0x3}},
    {3,{0xc1,0x7,0x3}},
    {3,{0xc1,0xc9,0x3}},
    {3,{0xc1,0xf,0x3}},
    {3,{0xc1,0xd1,0x3}},
    {3,{0xc1,0x17,0x3}},
    {3,{0xc1,0xd9,0x3}},
    {3,{0xc1,0x1f,0x3}},
    {3,{0xc1,0xe1,0x3}},
    {3,{0xc1,0x27,0x3}},
    {3,{0xc1,0xe9,0x3}},
    {3,{0xc1,0x2f,0x3}},
    {3,{0xc1,0xf9,0x3}},
    {3,{0xc1,0x3f,0x3}},
    {2,{0xd0,0xc1}},
    {2,{0xd0,0x7}},
    {2,{0xd0,0xc9}},
    {2,{0xd0,0xf}},
    {2,{0xd0,0xd1}},
    {2,{0xd0,0x17}},
    {2,{0xd0,0xd9}},
    {2,{0xd0,0x1f}},
    {2,{0xd0,0xe1}},
    {2,{0xd0,0x27}},
    {2,{0xd0,0xe9}},
    {2,{0xd0,0x2f}},
    {2,{0xd0,0xf9}},
    {2,{0xd0,0x3f}},
    {2,{0xd1,0xc1}},
    {2,{0xd1,0x7}},
    {2,{0xd1,0xc9}},
    {2,{0xd1,0xf}},
    {2,{0xd1,0xd1}},
    {2,{0xd1,0x17}},
    {2,{0xd1,0xd9}},
    {2,{0xd1,0x1f}},
    {2,{0xd1,0xe1}},
    {2,{0xd1,0x27}},
    {2,{0xd1,0xe9}},
    {2,{0xd1,0x2f}},
    {2,{0xd1,0xf9}},
    {2,{0xd1,0x3f}},
    {2,{0xd2,0xc1}},
    {2,{0xd2,0x7}},
    {2,{0xd2,0xc9}},
    {2,{0xd2,0xf}},
    {2,{0xd2,0xd1}},
    {2,{0xd2,0x17}},
    {2,{0xd2,0xd9}},
    {2,{0xd2,0x1f}},
    {2,{0xd2,0xe1}},
    {2,{0xd2,0x27}},
    {2,{0xd2,0xe9}},
    {2,{0xd2,0x2f}},
    {2,{0xd2,0xf9}},
    {2,{0xd2,0x3f}},
    {2,{0xd3,0xc1}},
    {2,{0xd3,0x7}},
    {2,{0xd3,0xc9}},
    {2,{0xd3,0xf}},
    {2,{0xd3,0xd1}},
    {2,{0xd3,0x17}},
    {2,{0xd3,0xd9}},
    {2,{0xd3,0x1f}},
    {2,{0xd3,0xe1}},
    {2,{0xd3,0x27}},
    {2,{0xd3,0xe9}},
    {2,{0xd3,0x2f}},
    {2,{0xd3,0xf9}},
    {2,{0xd3,0x3f}},
    {4,{0x69,0xc1,0x3,0x0}},
    {4,{0x69,0x7,0x3,0x0}},
    {3,{0x6b,0xc1,0x3}},
    {3,{0x6b,0x7,0x3}},
    {1,{0x27}},
    {1,{0x2f}},
    {1,{0x37}},
    {1,{0x3f}},
    {1,{0x98}},
    {1,{0x99}},
    {2,{0xd4,0xa}},
    {2,{0xd5,0xa}},
    {2,{0xa8,0xa}},
    {3,{0xa9,0xa,0x0}},
};
/* Real arithmetic already has independent exhaustive oracles. This comparison
 * verifies protected decoder/access integration, not an independent ALU oracle. */
static void scalar_integration(void)
{
    unsigned total=0;
    for(unsigned k=0;k<sizeof(forms)/sizeof(forms[0]);++k)
    for(unsigned cpl=0;cpl<4;++cpl)for(unsigned odd=0;odd<2;++odd)for(unsigned carry=0;carry<2;++carry) {
        fixture_t f,r;bm_cpu_t cpu=create(&f,cpl,odd),ref=create(&r,0,odd);
        bm_286_arch_state_t a=get(&cpu),b,ra=get(&ref),rb;
        a.ax=ra.ax=0x00a5;a.cx=ra.cx=7;a.dx=ra.dx=0;a.flags=ra.flags=(uint16_t)(0x4cd6|carry);
        ra.msw=0xfff0;code(&f,forms[k].bytes,forms[k].length);code(&r,forms[k].bytes,forms[k].length);
        word(&f,a.ds.base+a.bx,7);word(&r,ra.ds.base+ra.bx,7);
        assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);assert(bm_286_set_arch_state(&ref,&ra)==BM_STATUS_OK);
        assert(step(&cpu).kind==BM_286_BOUNDARY_INSTRUCTION);{bm_286_boundary_t bound;assert(bm_286_step(&ref,&bound)==BM_STATUS_OK);}
        b=get(&cpu);rb=get(&ref);
        assert(b.ax==rb.ax && b.bx==rb.bx && b.cx==rb.cx && b.dx==rb.dx && b.flags==rb.flags && b.ip==rb.ip);
        assert(b.sp==rb.sp && b.bp==rb.bp && b.si==rb.si && b.di==rb.di);
        assert(memcmp(f.ram+0x4000+odd,r.ram+0x4000+odd,0x1000)==0);
        assert(!f.locked && f.locks==f.unlocks);cpu.ops.destroy(cpu.context);ref.ops.destroy(ref.context);++total;
    }
    printf("%u scalar protected/real integration comparisons\n",total);
}
static void doublewords(void)
{
    for(unsigned cpl=0;cpl<4;++cpl)for(unsigned odd=0;odd<2;++odd)
    for(unsigned op=0;op<3;++op)for(unsigned edge=0;edge<5;++edge)for(unsigned ss=0;ss<2;++ss) {
        fixture_t f;bm_cpu_t cpu=create(&f,cpl,odd);bm_286_arch_state_t a=get(&cpu),b;
        uint16_t offset=(uint16_t)(edge?0xfffb+edge:0x600);unsigned base=ss?a.ss.base:a.ds.base;
        uint8_t bytes[]={(uint8_t)(ss?0x36:0x3e),(uint8_t)(op==0?0xc5:op==1?0xc4:0x62),0x1e,(uint8_t)offset,(uint8_t)(offset>>8)};
        a.bx=0x1234;word(&f,base+offset,op==2?0x8000:0xabcd);word(&f,base+(uint16_t)(offset+2),op==2?0x7fff:24+cpl);
        code(&f,bytes,5);assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);bm_286_boundary_t boundary=step(&cpu);b=get(&cpu);
        if(edge>1){assert(boundary.vector==(ss?12:13) && b.bx==a.bx && b.ip==0x400);assert(getword(&f,b.ss.base+b.sp+2)==0x100);}
        else {assert(b.ip==0x105 && b.flags==a.flags);if(op<2){assert(b.bx==0xabcd);assert((op==0?b.ds.selector:b.es.selector)==(uint16_t)(24+cpl));}else assert(b.bx==a.bx);}
        cpu.ops.destroy(cpu.context);
    }
    /* Source register/segment aliases destination; full pointer is read first. */
    for(unsigned odd=0;odd<2;++odd) {
        fixture_t f;bm_cpu_t cpu=create(&f,0,odd);bm_286_arch_state_t a=get(&cpu),b;
        uint8_t bytes[]={0xc5,0x1f};code(&f,bytes,2);word(&f,a.ds.base+a.bx,0x1234);word(&f,a.ds.base+a.bx+2,0);
        step(&cpu);b=get(&cpu);assert(b.bx==0x1234 && !b.ds.valid && b.ip==0x102);cpu.ops.destroy(cpu.context);
    }
}
static void arpl_and_stores(void)
{
    for(unsigned cpl=0;cpl<4;++cpl)for(unsigned rpl=0;rpl<4;++rpl)for(unsigned src=0;src<4;++src)
    for(unsigned memory=0;memory<2;++memory)for(unsigned odd=0;odd<2;++odd) {
        fixture_t f;bm_cpu_t cpu=create(&f,cpl,odd);bm_286_arch_state_t a=get(&cpu),b;
        const uint8_t bytes[]={0x63,(uint8_t)(memory?7:0xc1)};a.ax=(uint16_t)(0x100+src);a.cx=(uint16_t)(0x200+rpl);a.flags=0x4cd7;
        code(&f,bytes,2);word(&f,a.ds.base+a.bx,a.cx);assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);step(&cpu);b=get(&cpu);
        unsigned v=0x200+(rpl<src?src:rpl);assert((memory?getword(&f,a.ds.base+a.bx):b.cx)==(uint16_t)v);
        assert(b.flags==(uint16_t)((a.flags&~0x40)|(rpl<src?0x40:0)));cpu.ops.destroy(cpu.context);
    }
    for(unsigned cpl=0;cpl<4;++cpl)for(unsigned op=0;op<2;++op)for(unsigned odd=0;odd<2;++odd) {
        fixture_t f;bm_cpu_t cpu=create(&f,cpl,odd);bm_286_arch_state_t a=get(&cpu),b;
        uint8_t bytes[]={0x0f,0,(uint8_t)(7|(op<<3))};a.ldtr.selector=0x28;a.tr.selector=0x30;
        code(&f,bytes,3);assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);step(&cpu);b=get(&cpu);
        assert(getword(&f,a.ds.base+a.bx)==(op?0x30:0x28) && b.flags==a.flags);cpu.ops.destroy(cpu.context);
    }
}
static void fault_setup(fixture_t *f,bm_cpu_t *cpu,unsigned route,unsigned odd)
{
    static const uint8_t ops[][5]={{0x3e,0xf7,0xf1},{0x3e,0xd4,0},{0x3e,0x62,7},
      {0x3e,0xc5,0xc0},{0x3e,0xc5,7},{0x3e,0xc5,7},{0x3e,1,7},{0x3e,0x9b},{0x3e,0xd8},
      {0x3e,0xcc},{0x3e,0xcd,13},{0x3e,0xce},{0x3e,0xcd,0x21}};
    bm_286_arch_state_t a;*cpu=create(f,route==12?3:0,odd);a=get(cpu);code(f,ops[route],5);
    word(f,a.ds.base+a.bx,10);word(f,a.ds.base+a.bx+2,route==4?32:route==5?24:20);
    if(route<2)a.cx=0;
    if(route==2)a.ax=21;
    if(route==5)f->ram[a.gdtr.base+29]&=0x7f;
    if(route==6)a.ds.access&=0xfdu;
    if(route==7)a.msw|=10;
    if(route==8)a.msw|=4;
    if(route==11)a.flags|=0x800;
    assert(bm_286_set_arch_state(cpu,&a)==BM_STATUS_OK);
}
static void faults_and_software(void)
{
    static const unsigned vectors[]={0,0,5,6,13,11,13,7,7,3,13,4,13};
    for(unsigned route=0;route<13;++route)for(unsigned odd=0;odd<2;++odd) {
        fixture_t f;bm_cpu_t cpu;fault_setup(&f,&cpu,route,odd);bm_286_arch_state_t a=get(&cpu),b;
        bm_286_boundary_t boundary=step(&cpu);b=get(&cpu);unsigned error=(route>=4&&route<=6)||route==12;
        unsigned ip=route==9||route==11?0x102:route==10?0x103:0x100;
        assert(boundary.has_vector && boundary.vector==vectors[route] && b.ip==0x400);
        assert(b.sp==a.sp-(error?8:6) && getword(&f,b.ss.base+b.sp+(error?2:0))==ip);
        assert(b.ax==a.ax && b.cx==a.cx && b.ds.selector==a.ds.selector);
        if(error)assert(getword(&f,b.ss.base+b.sp)==(route==4?32:route==5?24:route==12?0x10a:0));
        assert(!f.acks);cpu.ops.destroy(cpu.context);
    }
    for(unsigned odd=0;odd<2;++odd) {
        fixture_t f;bm_cpu_t cpu=create(&f,0,odd);bm_286_arch_state_t a=get(&cpu),b;
        const uint8_t bytes[]={0x3e,0xf7,0xf1},handler[]={0xb9,7,0,0xcf};
        a.ax=21;a.cx=0;code(&f,bytes,3);memcpy(f.ram+0x3400,handler,4);assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);
        assert(step(&cpu).vector==0);step(&cpu);step(&cpu);assert(get(&cpu).ip==0x100);step(&cpu);b=get(&cpu);assert(b.ax==3 && b.dx==0 && b.ip==0x103);
        cpu.ops.destroy(cpu.context);
    }
}
static void write_permissions(void)
{
    /* PRM #GP delivery also applies when hardware restart is not guaranteed.
     * Exact preserved registers here test our deterministic model policy. */
    static const uint8_t ops[][3] = {
        {0x87,7}, {0x11,7}, {0x19,7}, {0xd1,0x17}, {0xd1,0x1f},
        {0x83,0x17,1}, {0x83,0x1f,1}, {0xf7,0x17}, {0x01,7}, {0x63,7}
    };
    for (unsigned k=0;k<sizeof(ops)/sizeof(ops[0]);++k)
    for (unsigned cpl=0;cpl<4;++cpl) for (unsigned odd=0;odd<2;++odd)
    for (unsigned code_segment=0;code_segment<2;++code_segment) {
        fixture_t f; bm_cpu_t cpu=create(&f,cpl,odd);
        bm_286_arch_state_t a=get(&cpu),b; bm_286_boundary_t boundary;
        a.ds.access=(uint8_t)((code_segment?0x9b:0x91)|(cpl<<5));
        code(&f,ops[k],3); word(&f,a.ds.base+a.bx,0x1234);
        assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);
        {
            boundary=step(&cpu); b=get(&cpu);
            assert(boundary.has_vector && boundary.vector==13);
            assert(getword(&f,b.ss.base+b.sp)==0);
            assert(getword(&f,b.ss.base+b.sp+2)==a.ip);
            assert(getword(&f,b.ss.base+b.sp+6)==a.flags);
        }
        assert(getword(&f,a.ds.base+a.bx)==0x1234);
        cpu.ops.destroy(cpu.context);
    }
}
static void software_vectors(void)
{
    for (unsigned vector=0;vector<256;++vector) for (unsigned cpl=0;cpl<4;++cpl)
    for (unsigned dpl=0;dpl<4;++dpl) for (unsigned odd=0;odd<2;++odd) {
        fixture_t f; bm_cpu_t cpu=create(&f,cpl,odd);
        bm_286_arch_state_t a=get(&cpu),b;
        const uint8_t bytes[]={0x3e,0xcd,(uint8_t)vector};
        f.ram[a.idtr.base+vector*8+5]=(uint8_t)(0x86|(dpl<<5));
        code(&f,bytes,3); bm_286_boundary_t boundary=step(&cpu); b=get(&cpu);
        assert(boundary.has_vector && boundary.vector==(cpl>dpl?13:vector));
        assert(b.sp==a.sp-(cpl>dpl?8:6));
        assert(getword(&f,b.ss.base+b.sp+(cpl>dpl?2:0))==(cpl>dpl?a.ip:a.ip+3));
        if (cpl>dpl) assert(getword(&f,b.ss.base+b.sp)==vector*8+2);
        assert(boundary.kind==(cpl>dpl?BM_286_BOUNDARY_EXCEPTION:BM_286_BOUNDARY_INSTRUCTION));
        assert(!f.acks); cpu.ops.destroy(cpu.context);
    }
    puts("8192 software vector/CPL/gate-DPL/alignment cases");
}
static void bound_and_pointer_recovery(void)
{
    static const int values[]={-32768,-1,0,1,32767};
    for (unsigned lo=0;lo<5;++lo) for (unsigned hi=0;hi<5;++hi)
    for (unsigned value=0;value<5;++value) {
        fixture_t f; bm_cpu_t cpu=create(&f,0,0); bm_286_arch_state_t a=get(&cpu),b;
        const uint8_t bytes[]={0x62,7};
        a.ax=(uint16_t)values[value]; code(&f,bytes,2);
        word(&f,a.ds.base+a.bx,(uint16_t)values[lo]);
        word(&f,a.ds.base+a.bx+2,(uint16_t)values[hi]);
        assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);
        bm_286_boundary_t boundary=step(&cpu); b=get(&cpu);
        if (values[value]<values[lo] || values[value]>values[hi]) {
            assert(boundary.has_vector && boundary.vector==5 && b.ax==a.ax);
            assert(getword(&f,b.ss.base+b.sp)==a.ip);
        } else assert(!boundary.has_vector && b.ip==a.ip+2 && b.flags==a.flags);
        cpu.ops.destroy(cpu.context);
    }
    for (unsigned op=0;op<3;++op) for (unsigned odd=0;odd<2;++odd)
    for (unsigned invalid=0;invalid<2;++invalid) {
        fixture_t f; bm_cpu_t cpu=create(&f,0,odd); bm_286_arch_state_t a=get(&cpu),b;
        const uint8_t bytes[]={(uint8_t)(op==0?0x62:op==1?0xc4:0xc5),7};
        a.ds.access=0x97; a.ds.limit=(uint16_t)(a.bx-1+invalid);
        word(&f,a.ds.base+a.bx,0); word(&f,a.ds.base+a.bx+2,op?24:0x7fff);
        code(&f,bytes,2); assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);
        bm_286_boundary_t boundary=step(&cpu); b=get(&cpu);
        if (invalid) assert(boundary.has_vector && boundary.vector==13 && b.ax==a.ax);
        else assert(!boundary.has_vector && b.ip==a.ip+2);
        cpu.ops.destroy(cpu.context);
    }
    for (unsigned op=0;op<3;++op) for (unsigned odd=0;odd<2;++odd) {
        fixture_t f; bm_cpu_t cpu=create(&f,0,odd); bm_286_arch_state_t a=get(&cpu),b;
        const uint8_t bytes[]={0x3e,(uint8_t)(op==0?0x62:op==1?0xc4:0xc5),7};
        const uint8_t bound_handler[]={0xb8,2,0,0xcf};
        /* Repair the pointer's selector, discard #GP's error word, return. */
        const uint8_t pointer_handler[]={0xc7,0x47,2,24,0,0x83,0xc4,2,0xcf};
        code(&f,bytes,3); word(&f,a.ds.base+a.bx,op?0x1234:1);
        word(&f,a.ds.base+a.bx+2,op?32:3);
        memcpy(f.ram+0x3400,op?pointer_handler:bound_handler,op?sizeof(pointer_handler):sizeof(bound_handler));
        assert(step(&cpu).vector==(op?13:5)); step(&cpu);
        if(op) step(&cpu);
        step(&cpu); assert(get(&cpu).ip==a.ip);
        step(&cpu); b=get(&cpu);
        assert(b.ip==a.ip+3 && b.ax==(op?0x1234:2));
        cpu.ops.destroy(cpu.context);
    }
}
static void failure_setup(fixture_t *f,bm_cpu_t *cpu,unsigned route,unsigned odd)
{
    static const uint8_t bytes[][5]={{0xc5,7},{0xc4,7},{0x62,7},{0x63,7},{0x87,7},{0x0f,0,7},{0x0f,0,15},
        {0x01,7},{0x83,7,3},{0xf7,0x17},{0xf7,0x1f},{0xf7,0x27},{0xf7,0x37},{0xc1,0x17,3}};
    if(route>=14){fault_setup(f,cpu,route-14,odd);return;}
    *cpu=create(f,0,odd);bm_286_arch_state_t a=get(cpu);code(f,bytes[route],5);
    word(f,a.ds.base+a.bx,route<2?0x1234:7);word(f,a.ds.base+a.bx+2,route<2?24:0x7fff);
    a.ax=route==3?3:21;assert(bm_286_set_arch_state(cpu,&a)==BM_STATUS_OK);
}
static void failures(void)
{
    static const bm_status_t errors[]={BM_STATUS_IDLE,BM_STATUS_UNSUPPORTED,BM_STATUS_INVALID_ARGUMENT,BM_STATUS_INVALID_STATE,BM_STATUS_DEVICE_ERROR};unsigned total=0;
    for(unsigned route=0;route<27;++route)for(unsigned odd=0;odd<2;++odd) {
        fixture_t f;bm_cpu_t cpu;failure_setup(&f,&cpu,route,odd);step(&cpu);unsigned count=f.calls;cpu.ops.destroy(cpu.context);
        for(unsigned fail=1;fail<=count;++fail)for(unsigned phase=0;phase<2;++phase)for(unsigned e=0;e<5;++e) {
            uint8_t ram[65536];bm_286_arch_state_t a,b;bm_286_boundary_t boundary;
            failure_setup(&f,&cpu,route,odd);a=get(&cpu);memcpy(ram,f.ram,sizeof(ram));f.fail=fail;f.after=phase!=0;f.failure=errors[e];
            assert(bm_286_pm_step_subset(&cpu,&boundary)==errors[e]);b=get(&cpu);same(&a,&b);
            assert(f.calls==fail && f.effects==fail-1+phase && !f.locked && f.locks==f.unlocks);
            for(unsigned t=0;t<f.effects;++t)if(f.trace[t].operation==BM_BUS_WRITE)
                for(unsigned i=0;i<f.trace[t].size;++i)ram[(unsigned)(f.trace[t].address+i)&65535u]=(uint8_t)(f.trace[t].value>>(8*i));
            assert(memcmp(ram,f.ram,sizeof(ram))==0);assert(bm_286_pm_step_subset(&cpu,&boundary)==BM_STATUS_INVALID_STATE && f.calls==fail);
            cpu.ops.destroy(cpu.context);++total;
        }
    }
    printf("%u protected instruction before/after transfer failures\n",total);
}
int main(void)
{
    setvbuf(stdout,NULL,_IONBF,0);scalar_integration();doublewords();arpl_and_stores();faults_and_software();
    write_permissions();software_vectors();bound_and_pointer_recovery();failures();
    puts("protected scalar, doubleword and software-event integration passed");return 0;
}
