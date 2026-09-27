/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 BluMach contributors
 * Authored protected ENTER cases; fixture adapted from private execution tests. No ROM,
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

static void enter_code(fixture_t *f,unsigned allocation,unsigned level)
{
    uint8_t bytes[]={0x3e,0xc8,(uint8_t)allocation,(uint8_t)(allocation>>8),(uint8_t)level};
    code(f,bytes,sizeof(bytes));
}
static void oracle_push(fixture_t *f,unsigned base,uint16_t *sp,uint16_t value)
{
    *sp-=2;word(f,base+*sp,value);
}
static void matrix(void)
{
    static const unsigned allocs[]={0,1,31};unsigned total=0;
    for(unsigned cpl=0;cpl<4;++cpl) for(unsigned odd=0;odd<2;++odd)
    for(unsigned level=0;level<256;++level) for(unsigned alloc=0;alloc<3;++alloc)
    for(unsigned alias=0;alias<3;++alias) {
        fixture_t f,model;bm_cpu_t cpu=create(&f,cpl,odd);bm_286_arch_state_t a=get(&cpu),b,e;
        unsigned count=level%32;uint16_t sp=a.sp,frame;
        a.flags=0x4cd7;
        a.bp=(uint16_t)(alias==0?0x7000:alias==1?a.sp:a.sp-4);
        for(unsigned i=0;i<32;++i)word(&f,a.ss.base+(uint16_t)(a.bp-2*i),0x1234+i);
        enter_code(&f,allocs[alloc],level);memcpy(&model,&f,sizeof(f));
        oracle_push(&model,a.ss.base,&sp,a.bp);frame=sp;
        for(unsigned i=1;i<count;++i)
            oracle_push(&model,a.ss.base,&sp,getword(&model,a.ss.base+(uint16_t)(a.bp-2*i)));
        if(count)oracle_push(&model,a.ss.base,&sp,frame);
        sp-=(uint16_t)allocs[alloc];e=a;e.bp=frame;e.sp=sp;e.ip+=5;
        assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);step(&cpu);b=get(&cpu);same(&e,&b);
        assert(memcmp(f.ram,model.ram,sizeof(f.ram))==0);
        /* LEAVE must restore the incoming BP/SP regardless of nesting. */
        f.ram[0x3105]=0xc9;step(&cpu);b=get(&cpu);e=a;e.ip+=6;same(&e,&b);
        cpu.ops.destroy(cpu.context);++total;
    }
    printf("%u ENTER/LEAVE nesting, CPL, alignment, allocation and overlap cases\n",total);
}
static void setup(fixture_t *f,bm_cpu_t *cpu,unsigned route,unsigned odd)
{
    bm_286_arch_state_t a;unsigned level=route==0?0:route==1?1:31,allocation=32;
    *cpu=create(f,0,odd);a=get(cpu);a.bp=0x7000;
    if(route==3)a.bp=a.sp;
    if(route==4){a.ss.access|=4;a.ss.limit=0x7fb0;a.bp=0x9000;} /* Frame valid, locals invalid. */
    if(route==5){a.ss.limit=0x8fff;a.bp=0x9002;level=2;} /* Source invalid. */
    if(route==6){allocation=0xffff;level=0;} /* More than 64KiB. */
    if(route==7){a.sp=0x10;allocation=0;level=8;} /* Frame wraps. */
    for(unsigned i=0;i<32;++i)word(f,a.ss.base+(uint16_t)(a.bp-2*i),0x1200+i);
    enter_code(f,allocation,level);assert(bm_286_set_arch_state(cpu,&a)==BM_STATUS_OK);
}
static void boundaries(void)
{
    for(unsigned odd=0;odd<2;++odd) for(unsigned route=4;route<8;++route) {
        fixture_t f;bm_cpu_t cpu;setup(&f,&cpu,route,odd);bm_286_arch_state_t a=get(&cpu),b;
        assert(step(&cpu).vector==12);b=get(&cpu);assert(b.ip==0x400 && b.sp==(uint16_t)(a.sp-8));
        assert(b.bp==a.bp && getword(&f,b.ss.base+b.sp)==0 && getword(&f,b.ss.base+b.sp+2)==0x100);
        for(unsigned t=0;t<f.calls && f.trace[t].address!=a.idtr.base+12*8;++t)
            assert(f.trace[t].operation!=BM_BUS_WRITE);
        cpu.ops.destroy(cpu.context);
    }
    for(unsigned odd=0;odd<2;++odd) for(unsigned down=0;down<2;++down) {
        fixture_t f;bm_cpu_t cpu=create(&f,0,odd);bm_286_arch_state_t a=get(&cpu),b;
        a.sp=0;a.bp=0;enter_code(&f,down?0xfffd:0xfffe,0);
        if(down){a.ss.access|=4;a.ss.limit=0;}
        assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);step(&cpu);b=get(&cpu);
        assert(b.sp==(down?1:0) && b.bp==0xfffe && b.ip==0x105);
        assert(getword(&f,a.ss.base+0xfffe)==a.bp);
        cpu.ops.destroy(cpu.context);
    }
    /* Display BP subtraction wraps as 16-bit offsets; each source word remains valid. */
    for(unsigned odd=0;odd<2;++odd) {
        fixture_t f;bm_cpu_t cpu=create(&f,0,odd);bm_286_arch_state_t a=get(&cpu),b;
        a.bp=2;word(&f,a.ss.base,0xabcd);word(&f,a.ss.base+0xfffe,0x1234);enter_code(&f,0,3);
        assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);step(&cpu);b=get(&cpu);
        assert(b.sp==a.sp-8 && getword(&f,a.ss.base+a.sp-4)==0xabcd && getword(&f,a.ss.base+a.sp-6)==0x1234);
        cpu.ops.destroy(cpu.context);
    }
}
static void failures(void)
{
    static const bm_status_t errors[]={BM_STATUS_IDLE,BM_STATUS_UNSUPPORTED,
        BM_STATUS_INVALID_ARGUMENT,BM_STATUS_INVALID_STATE,BM_STATUS_DEVICE_ERROR};unsigned total=0;
    for(unsigned route=0;route<8;++route) for(unsigned odd=0;odd<2;++odd) {
        fixture_t f;bm_cpu_t cpu;setup(&f,&cpu,route,odd);step(&cpu);unsigned count=f.calls;cpu.ops.destroy(cpu.context);
        for(unsigned fail=1;fail<=count;++fail)for(unsigned phase=0;phase<2;++phase)for(unsigned e=0;e<5;++e) {
            bm_286_arch_state_t a,b;bm_286_boundary_t boundary;uint8_t ram[65536];
            setup(&f,&cpu,route,odd);a=get(&cpu);memcpy(ram,f.ram,sizeof(ram));f.fail=fail;f.after=phase!=0;f.failure=errors[e];
            assert(bm_286_pm_step_subset(&cpu,&boundary)==errors[e]);b=get(&cpu);same(&a,&b);
            assert(f.calls==fail && f.effects==fail-1+phase && !f.locked && f.locks==f.unlocks);
            for(unsigned t=0;t<f.effects;++t)if(f.trace[t].operation==BM_BUS_WRITE)
                for(unsigned i=0;i<f.trace[t].size;++i)ram[(unsigned)(f.trace[t].address+i)&65535u]=(uint8_t)(f.trace[t].value>>(8*i));
            assert(memcmp(ram,f.ram,sizeof(ram))==0);
            assert(bm_286_pm_step_subset(&cpu,&boundary)==BM_STATUS_INVALID_STATE && f.calls==fail);
            cpu.ops.destroy(cpu.context);++total;
        }
    }
    printf("%u ENTER before/after every-transfer failures\n",total);
}

static void repair_and_signal(void)
{
    for(unsigned odd=0;odd<2;++odd) {
        fixture_t f;bm_cpu_t cpu;setup(&f,&cpu,5,odd);bm_286_arch_state_t a=get(&cpu),b;
        const uint8_t handler[]={0xb8,16,0,0x8e,0xd0,0xbc,0xfa,0x7f,0xcf};
        memcpy(f.ram+0x3400,handler,sizeof(handler));assert(step(&cpu).vector==12);
        for(unsigned i=0;i<4;++i)step(&cpu);
        b=get(&cpu);assert(b.ip==a.ip && b.sp==a.sp && b.bp==a.bp && b.ss.limit==0xffff);
        step(&cpu);b=get(&cpu);assert(b.ip==0x105 && b.sp==0x7fda && b.bp==0x7ffe);
        assert(getword(&f,a.ss.base+0x7ffe)==a.bp && getword(&f,a.ss.base+0x7ffc)==0x1201);
        assert(getword(&f,a.ss.base+0x7ffa)==0x7ffe);
        cpu.ops.destroy(cpu.context);
        setup(&f,&cpu,2,odd);f.cpu=&cpu;f.nmi_at=8;step(&cpu);b=get(&cpu);
        assert(b.nmi_pending && b.ip==0x105 && b.bp==0x7ffe && b.sp==0x7fa0);
        f.nmi_at=0;assert(step(&cpu).vector==2);cpu.ops.destroy(cpu.context);
    }
}
int main(void)
{
    setvbuf(stdout,NULL,_IONBF,0);matrix();boundaries();failures();repair_and_signal();puts("private protected ENTER tests passed");return 0;
}
