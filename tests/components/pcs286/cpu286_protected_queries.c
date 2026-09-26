/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 BluMach contributors
 * Authored descriptor query cases; fixture adapted from the D2 tests; no firmware or media assets.
 */
#include "execution_286.h"
#include "access_286.h"
#include <blumach/platforms/null_host.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct fixture {
    uint8_t ram[65536];
    bm_bus_transaction_t trace[256];
    unsigned calls, effects, fail, locks, unlocks;
    bool after, locked;
    bm_status_t failure;
} fixture_t;
static bm_status_t bus(void *context, bm_bus_transaction_t *t)
{
    fixture_t *f = context;
    unsigned i;
    assert(f->calls < 256 && t->address <= 0xffffffu);
    assert(t->size == 1 || (t->size == 2 && !(t->address & 1u)));
    assert(t->alignment == t->size && t->endianness == BM_ENDIAN_LITTLE && !t->wait_states);
    assert(t->attributes == (f->locked ? BM_BUS_TRANSACTION_LOCKED : 0u));
    assert(t->space == (t->operation == BM_BUS_FETCH ? BM_ADDRESS_PROGRAM : BM_ADDRESS_DATA));
    f->trace[f->calls++] = *t;
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
static void lock(void *context, int asserted)
{
    fixture_t *f = context;
    assert(f->locked != (asserted != 0)); f->locked = asserted != 0;
    if (asserted) ++f->locks; else ++f->unlocks;
}
static void word(fixture_t *f, unsigned at, unsigned value)
{
    f->ram[at] = (uint8_t)value; f->ram[at+1] = (uint8_t)(value >> 8);
}
static uint16_t readword(fixture_t *f, unsigned at)
{
    return (uint16_t)(f->ram[at] | ((uint16_t)f->ram[at+1] << 8));
}
static void descriptor(fixture_t *f, unsigned at, unsigned base, unsigned limit, unsigned access)
{
    word(f, at, limit); word(f, at+2, base);
    f->ram[at+4] = (uint8_t)(base >> 16); f->ram[at+5] = (uint8_t)access;
}
static bm_286_config_t config(fixture_t *f)
{
    bm_286_config_t c = {0};
    c.size = sizeof(c); c.version = BM_286_CONTRACT_VERSION;
    c.access = bus; c.access_context = f; c.bus_lock = lock; c.pin_context = f;
    return c;
}
static bm_286_arch_state_t get(bm_cpu_t *cpu)
{
    bm_286_arch_state_t a;
    assert(bm_286_get_arch_state(cpu, &a) == BM_STATUS_OK); return a;
}
static bm_cpu_t create(fixture_t *f)
{
    bm_host_services_t h = bm_null_host_services(); bm_cpu_t cpu;
    bm_286_config_t c = config(f);
    memset(f, 0, sizeof(*f));
    assert(bm_286_create(&h, &c, &cpu) == BM_STATUS_OK); return cpu;
}
static void tables(fixture_t *f, unsigned cpl, unsigned odd)
{
    unsigned v;
    for (v = 0; v < 256; ++v) {
        unsigned at = 0x1000 + odd + v * 8;
        word(f, at, 0x400); word(f, at+2, 8); f->ram[at+5] = 0x86;
    }
    descriptor(f, 0x2008+odd, 0x3000, 0xffff, 0x9a | (cpl << 5));
    descriptor(f, 0x2010+odd, odd, 0xffff, 0x92 | (cpl << 5));
    descriptor(f, 0x2018+odd, 0x4000+odd, 0xffff, 0x92 | (cpl << 5));
    descriptor(f, 0x2020+odd, 0x6000+odd, 15, 0x82);
    descriptor(f, 0x6000+odd, 0x4000+odd, 0xffff, 0x92 | (cpl << 5));
}
static bm_cpu_t protected_cpu(fixture_t *f, unsigned cpl, unsigned odd)
{
    bm_cpu_t cpu = create(f); bm_286_arch_state_t a = get(&cpu);
    tables(f, cpl, odd);
    a.msw = 0xfff1; a.cpl = (uint8_t)cpl; a.ip = 0x100; a.sp = 0x8000;
    a.cs.selector = (uint16_t)(8+cpl); a.cs.base = 0x3000;
    a.cs.access = (uint8_t)(0x9b | (cpl << 5));
    a.ss.selector = (uint16_t)(16+cpl); a.ss.base = odd;
    a.ss.access = (uint8_t)(0x93 | (cpl << 5));
    a.ds = a.es = a.ss; a.ds.base = a.es.base = 0x4000+odd;
    a.ds.selector = a.es.selector = (uint16_t)(24+cpl);
    a.gdtr.base = 0x2000+odd; a.gdtr.limit = 39;
    a.idtr.base = 0x1000+odd; a.idtr.limit = 0x7ff;
    assert(bm_286_set_arch_state(&cpu, &a) == BM_STATUS_OK); return cpu;
}
static bm_286_boundary_t step(bm_cpu_t *cpu)
{
    bm_286_boundary_t b;
    assert(bm_286_pm_step_subset(cpu, &b) == BM_STATUS_OK);
    assert(b.timing == BM_286_TIMING_UNKNOWN); return b;
}
static void same_cache(const bm_286_segment_state_t *a, const bm_286_segment_state_t *b)
{
    assert(a->selector == b->selector && a->base == b->base && a->limit == b->limit);
    assert(a->valid == b->valid && a->access == b->access);
}
static void same(const bm_286_arch_state_t *a, const bm_286_arch_state_t *b)
{
#define EQ(x) assert(a->x == b->x)
    EQ(ax); EQ(bx); EQ(cx); EQ(dx); EQ(sp); EQ(bp); EQ(si); EQ(di);
    EQ(ip); EQ(flags); EQ(msw); EQ(cpl); EQ(halted); EQ(shutdown);
    EQ(interrupt_shadow); EQ(trap_pending); EQ(nmi_pending); EQ(nmi_blocked);
    EQ(gdtr.base); EQ(gdtr.limit); EQ(idtr.base); EQ(idtr.limit);
#undef EQ
    same_cache(&a->cs,&b->cs); same_cache(&a->ss,&b->ss); same_cache(&a->ds,&b->ds);
    same_cache(&a->es,&b->es); same_cache(&a->ldtr,&b->ldtr); same_cache(&a->tr,&b->tr);
}

static uint16_t *reg(bm_286_arch_state_t *a,unsigned n)
{
    switch(n) {
    case 0:return &a->ax;case 1:return &a->cx;case 2:return &a->dx;case 3:return &a->bx;
    case 4:return &a->sp;case 5:return &a->bp;case 6:return &a->si;default:return &a->di;
    }
}
static unsigned instruction(fixture_t *f,unsigned kind,bool memory,unsigned source,unsigned dest,unsigned prefix)
{
    unsigned n=0;
    if(prefix) f->ram[0x3100+n++]=(uint8_t)prefix;
    f->ram[0x3100+n++]=0x0f;
    f->ram[0x3100+n++]=(uint8_t)(kind<2?kind+2:0);
    f->ram[0x3100+n++]=(uint8_t)((memory?6:0xc0+source)|((kind<2?dest:kind+2)<<3));
    if(memory) {word(f,0x3100+n,0x600);n+=2;}
    return n;
}
static void query_matrix(void)
{
    /* Independently tabulated eligible type bits, indexed by raw S/type.
     * Includes absent descriptors: P is deliberately not an eligibility bit. */
    static const unsigned masks[4][2]={{0x00fe,0xffff},{0x000e,0x0fff},{0,0xccff},{0,0x00cc}};
    unsigned ac,cpl,rpl,kind,local,odd,count=0;
    for(ac=0;ac<256;++ac) for(cpl=0;cpl<4;++cpl) for(rpl=0;rpl<4;++rpl)
    for(kind=0;kind<4;++kind) for(local=0;local<2;++local) for(odd=0;odd<2;++odd) {
        fixture_t f;bm_cpu_t cpu=protected_cpu(&f,cpl,odd);
        bm_286_arch_state_t a=get(&cpu),expected,after;bm_286_boundary_t b;
        unsigned at=local?0x6000+odd:0x2018+odd;
        bool conforming=(ac&0x1c)==0x1c;
        bool accepted=(masks[kind][(ac>>4)&1] & (1u<<(ac&15)))!=0;
        if(!conforming && ((ac>>5&3)<cpl || (ac>>5&3)<rpl)) accepted=false;
        a.ldtr.valid=1;a.ldtr.selector=32;a.ldtr.base=0x6000+odd;a.ldtr.limit=15;a.ldtr.access=0x82;
        a.ax=(uint16_t)((local?4:24)+rpl);a.dx=0xface;
        a.flags=(uint16_t)(0x7cd7^(odd?0x40:0));
        descriptor(&f,at,0x123456,0xbeef,ac);word(&f,at+6,0xffff); /* No 386 limit bits. */
        instruction(&f,kind,false,0,2,0);
        assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);expected=a;
        {
            bm_status_t status=bm_286_pm_step_subset(&cpu,&b);
            if(status!=BM_STATUS_OK) fprintf(stderr,"query ac=%02x cpl=%u rpl=%u kind=%u local=%u odd=%u status=%u\n",
                ac,cpl,rpl,kind,local,odd,(unsigned)status);
            assert(status==BM_STATUS_OK && b.kind==BM_286_BOUNDARY_INSTRUCTION);
            expected.ip+=3;expected.flags=(uint16_t)((a.flags&~0x40)|(accepted?0x40:0));
            if(accepted && kind<2) expected.dx=(uint16_t)(kind==0?ac<<8:0xbeef);
            assert(b.bus_wait_cycles==f.calls*3u);++count;
        }
        after=get(&cpu);same(&expected,&after);
        assert(f.calls==3+(odd?8u:4u) && !f.locks && f.ram[at+5]==ac);
        for(unsigned i=0;i<f.calls;++i) assert(f.trace[i].operation!=(unsigned)BM_BUS_WRITE);
        cpu.ops.destroy(cpu.context);
    }
    printf("query matrix: %u PRM-profile cases\n",count);
}

static void edges(void)
{
    unsigned kind,which,odd,dest,local;
    for(kind=0;kind<4;++kind) for(which=0;which<8;++which) for(odd=0;odd<2;++odd) {
        fixture_t f;bm_cpu_t cpu=protected_cpu(&f,3,odd);
        bm_286_arch_state_t a=get(&cpu),after;
        a.ax=(uint16_t)(which<4?which:which==4?4:which==5?0xfffc:24);
        if(which==6) a.gdtr.limit=30; /* Descriptor needs last byte at31. */
        if(which==7) a.gdtr.limit=31;
        a.dx=0xcafe;a.flags=2;instruction(&f,kind,false,0,2,0);
        assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);step(&cpu);after=get(&cpu);
        assert((after.flags&0x40)==(which==7?0x40:0));
        assert(after.dx==(which==7&&kind<2?(kind?0xffff:0xf200):0xcafe));
        assert(f.calls==(which==7?3+(odd?8u:4u):3));assert(!f.locks);
        cpu.ops.destroy(cpu.context);
    }
    /* Source/destination alias for every general register, including SP. */
    for(kind=0;kind<2;++kind) for(dest=0;dest<8;++dest) {
        fixture_t f;bm_cpu_t cpu=protected_cpu(&f,0,0);bm_286_arch_state_t a=get(&cpu);
        *reg(&a,dest)=24;instruction(&f,kind,false,dest,dest,0);
        assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);step(&cpu);a=get(&cpu);
        assert(*reg(&a,dest)==(kind?0xffff:0x9200));cpu.ops.destroy(cpu.context);
    }
    /* Query uses current table bytes, not an already-loaded ordinary cache. */
    for(kind=0;kind<4;++kind) for(local=0;local<2;++local) {
        fixture_t f;bm_cpu_t cpu=protected_cpu(&f,0,1);
        bm_286_arch_state_t a=get(&cpu),after;
        a.ax=(uint16_t)(local?4:24);a.ldtr.valid=1;a.ldtr.base=0x6001;a.ldtr.limit=7;
        instruction(&f,kind,false,0,2,0);
        descriptor(&f,local?0x6001:0x2019,0,0x1234,0x12); /* P=0, A=0, writable. */
        assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);step(&cpu);after=get(&cpu);
        assert(after.flags&0x40);same_cache(&a.ds,&after.ds);
        if(kind<2) assert(after.dx==(kind?0x1234:0x1200));
        assert(!f.locks);cpu.ops.destroy(cpu.context);
    }
}

static void memory_faults(void)
{
    unsigned kind,which,odd;
    for(kind=0;kind<4;++kind) for(which=0;which<5;++which) for(odd=0;odd<2;++odd) {
        fixture_t f;bm_cpu_t cpu=protected_cpu(&f,0,odd);
        bm_286_arch_state_t a=get(&cpu),after;bm_286_boundary_t b;unsigned n;
        a.dx=0xcafe;a.flags=0x42;
        n=instruction(&f,kind,true,0,2,which==3?0x36:which==2?0x2e:0x3e);
        if(which==0) a.ds.valid=0;
        if(which==1) a.ds.limit=0x600; /* second byte outside */
        if(which==2) a.cs.access=0x99; /* Legal execute-only CS, unreadable operand. */
        if(which==3) {a.ss.limit=0x600;a.sp=0x500;}
        word(&f,(which==3?a.ss.base:a.ds.base)+0x600,24);
        assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);b=step(&cpu);after=get(&cpu);
        if(which==4) {
            assert(b.kind==BM_286_BOUNDARY_INSTRUCTION && after.flags==0x42);
            assert(after.dx==(kind<2?(kind?0xffff:0x9200):0xcafe));
            assert(f.trace[n].address==a.ds.base+0x600 && !f.locks);
        } else {
            unsigned vector=which==3?12:13;
            assert(b.has_vector && b.vector==vector && b.instruction_ip==0x100);
            assert(after.dx==a.dx && after.ax==a.ax && after.sp==a.sp-8);
            assert(readword(&f,after.ss.base+after.sp)==0);
            assert(readword(&f,after.ss.base+after.sp+2)==0x100);
            assert(readword(&f,after.ss.base+after.sp+6)==a.flags);
            assert(f.trace[n].address==a.idtr.base+vector*8); /* no operand/table access */
        }
        cpu.ops.destroy(cpu.context);
    }
    /* Real-mode #UD occurs before reading a memory selector or any table. */
    for(kind=0;kind<4;++kind) {
        fixture_t f;bm_cpu_t cpu=protected_cpu(&f,0,0);bm_286_arch_state_t a=get(&cpu),after;
        bm_286_boundary_t b;unsigned n=instruction(&f,kind,true,0,2,0);
        a.msw=0xfff0;a.ds.valid=0;a.idtr.base=0;a.idtr.limit=0x3ff;
        word(&f,24,0x400);word(&f,26,0x300);
        assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);b=step(&cpu);after=get(&cpu);
        assert(b.vector==6 && after.sp==a.sp-6 && after.dx==a.dx);
        for(unsigned i=n;i<f.calls;++i) assert(f.trace[i].address<0x4000 || f.trace[i].address>=0x7000);
        cpu.ops.destroy(cpu.context);
    }
}

static void failure_setup(fixture_t *f,bm_cpu_t *cpu,unsigned kind,unsigned odd,bool fault)
{
    bm_286_arch_state_t a;
    *cpu=protected_cpu(f,0,odd);a=get(cpu);a.flags=0x42;a.dx=0xface;
    instruction(f,kind,true,0,2,0x3e);word(f,0x4600+odd,24);
    if(fault) a.ds.limit=0x600;
    assert(bm_286_set_arch_state(cpu,&a)==BM_STATUS_OK);
}

static void repair_and_retry(void)
{
    unsigned kind,odd;
    for(kind=0;kind<4;++kind) for(odd=0;odd<2;++odd) {
        fixture_t f;bm_cpu_t cpu=protected_cpu(&f,0,odd);
        bm_286_arch_state_t a=get(&cpu);bm_286_boundary_t b;
        const uint8_t handler[]={0xb8,24,0,0x8e,0xd8,0xbc,0xfa,0x7f,0xcf};
        unsigned n=instruction(&f,kind,true,0,2,0x3e);
        a.ds.valid=0;a.dx=0xcafe;a.flags=2;
        word(&f,0x4600+odd,24);memcpy(f.ram+0x3400,handler,sizeof(handler));
        assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);
        /* From here onward, only decoded instructions change the machine. */
        b=step(&cpu);assert(b.vector==13 && b.instruction_ip==0x100);
        for(unsigned i=0;i<4;++i) step(&cpu);
        a=get(&cpu);assert(a.ip==0x100 && a.sp==0x8000 && a.ds.valid && a.flags==2);
        b=step(&cpu);a=get(&cpu);
        assert(b.kind==BM_286_BOUNDARY_INSTRUCTION && a.ip==0x100+n && a.flags==0x42);
        assert(a.dx==(kind<2?(kind?0xffff:0x9300):0xcafe));
        cpu.ops.destroy(cpu.context);
    }
    /* Impossible imported DS cache is a host error, never guest #GP/#NP. */
    for(kind=0;kind<4;++kind) {
        fixture_t f;bm_cpu_t cpu=protected_cpu(&f,0,0);
        bm_286_arch_state_t a=get(&cpu),after;bm_286_boundary_t b;
        unsigned n=instruction(&f,kind,true,0,2,0);
        a.ds.access=0x99;
        assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);
        assert(bm_286_pm_step_subset(&cpu,&b)==BM_STATUS_INVALID_STATE);
        after=get(&cpu);same(&a,&after);assert(f.calls==n && !f.locks);
        assert(bm_286_pm_step_subset(&cpu,&b)==BM_STATUS_INVALID_STATE && f.calls==n);
        cpu.ops.destroy(cpu.context);
    }
}
static void failures(void)
{
    static const bm_status_t errors[]={BM_STATUS_IDLE,BM_STATUS_UNSUPPORTED,
        BM_STATUS_INVALID_ARGUMENT,BM_STATUS_INVALID_STATE,BM_STATUS_DEVICE_ERROR};
    unsigned kind,odd,route,fail,phase,e,total=0;
    for(kind=0;kind<4;++kind) for(odd=0;odd<2;++odd) for(route=0;route<2;++route) {
        fixture_t f;bm_cpu_t cpu;unsigned count;
        failure_setup(&f,&cpu,kind,odd,route!=0);step(&cpu);count=f.calls;cpu.ops.destroy(cpu.context);
        for(fail=1;fail<=count;++fail) for(phase=0;phase<2;++phase) for(e=0;e<5;++e) {
            bm_286_arch_state_t before,after;bm_286_boundary_t b;uint8_t expected[65536];
            failure_setup(&f,&cpu,kind,odd,route!=0);before=get(&cpu);memcpy(expected,f.ram,sizeof(expected));
            f.fail=fail;f.after=phase!=0;f.failure=errors[e];
            assert(bm_286_pm_step_subset(&cpu,&b)==errors[e]);after=get(&cpu);same(&before,&after);
            assert(f.calls==fail && f.effects==fail-1+phase && !f.locked && f.locks==f.unlocks);
            for(unsigned t=0;t<f.effects;++t) if(f.trace[t].operation==BM_BUS_WRITE)
                for(unsigned i=0;i<f.trace[t].size;++i) expected[(unsigned)(f.trace[t].address+i)&65535u]=
                    (uint8_t)(f.trace[t].value>>(8u*i));
            assert(memcmp(expected,f.ram,sizeof(expected))==0);
            assert(bm_286_pm_step_subset(&cpu,&b)==BM_STATUS_INVALID_STATE && f.calls==fail);
            cpu.ops.destroy(cpu.context);++total;
        }
    }
    printf("queries: %u before/after transfer failures\n",total);
}

int main(void)
{
    setvbuf(stdout,NULL,_IONBF,0);
    query_matrix();edges();memory_faults();repair_and_retry();failures();
    puts("private descriptor queries, ZF/destination preservation and faults passed");
    return 0;
}
