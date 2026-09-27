/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 BluMach contributors
 * Authored synthetic startup/system/JMP cases; no firmware or media assets.
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

static void reset_to_repair(void)
{
    unsigned odd;
    for (odd = 0; odd < 2; ++odd) {
        fixture_t f; bm_cpu_t cpu = create(&f);
        bm_286_arch_state_t a = get(&cpu), before;
        bm_286_boundary_t b; unsigned i;
        const uint8_t reset_jump[] = {0xea,0,1,0,3};
        const uint8_t real[] = {
            0xb8,0,0, 0x8e,0xd8, 0x8e,0xd0, 0xbc,0,0x80,
            0x0f,1,0x16,0,8, 0x0f,1,0x1e,6,8,
            0xb8,1,0, 0x0f,1,0xf0, 0xeb,0, 0xea,0,2,8,0
        };
        const uint8_t protected_code[] = {
            0xbb,0,6, 0xb8,0x10,0, 0x8e,0xd0, 0xbc,0,0x80,
            0xb8,0x20,0, 0x0f,0,0xd0, 0xb8,4,0, 0x8e,0xd8,
            0xb8,0,0, 0x8e,0xd8, 0x3e,0x8b,7, 0x90
        };
        const uint8_t handler[] = {0xb8,4,0,0x8e,0xd8,0xbc,0xfa,0x7f,0xcf};
        tables(&f, 0, odd);
        memcpy(f.ram+0xfff0,reset_jump,sizeof(reset_jump));
        memcpy(f.ram+0x3100,real,sizeof(real));
        memcpy(f.ram+0x3200,protected_code,sizeof(protected_code));
        memcpy(f.ram+0x3400,handler,sizeof(handler));
        word(&f,0x800,39); word(&f,0x802,0x2000+odd); word(&f,0x804,0xee00);
        word(&f,0x806,0x7ff); word(&f,0x808,0x1000+odd); word(&f,0x80a,0xdd00);
        word(&f,0x4600+odd,0xbeef);
        assert(a.cs.access == 0x82 && a.ds.access == 0x82 && a.ss.access == 0x82);
        step(&cpu); assert(f.trace[0].address == 0xfffff0);
        for (i=0;i<7;++i) step(&cpu);
        before = get(&cpu); assert(!(before.msw & 1) && before.ip == 0x117);
        step(&cpu); a = get(&cpu); assert(a.msw == 0xfff1 && a.ip == 0x11a);
        same_cache(&before.cs,&a.cs); same_cache(&before.ds,&a.ds); same_cache(&before.ss,&a.ss);
        /* No fixture state import anywhere in this reset->fault->retry run. */
        step(&cpu); a=get(&cpu); assert(a.cs.access == 0x82 && a.ip == 0x11c);
        step(&cpu); a=get(&cpu);
        assert(a.cs.selector == 8 && a.cs.access == 0x9b && a.ip == 0x200 && !a.cpl);
        assert(a.ds.access == 0x82 && a.ss.access == 0x82);
        for (i=0;i<10;++i) assert(step(&cpu).kind == BM_286_BOUNDARY_INSTRUCTION);
        a=get(&cpu); assert(a.ip == 0x21b && a.ldtr.valid && a.ldtr.selector == 32);
        assert(a.ldtr.base == 0x6000+odd && !a.ds.valid);
        b=step(&cpu); assert(b.vector == 13 && b.instruction_ip == 0x21b);
        a=get(&cpu); assert(a.sp == 0x7ff8 && readword(&f,a.ss.base+a.sp)==0);
        assert(readword(&f,a.ss.base+a.sp+2)==0x21b);
        for(i=0;i<4;++i) step(&cpu);
        a=get(&cpu); assert(a.ip==0x21b && a.sp==0x8000 && a.ds.selector==4);
        step(&cpu); a=get(&cpu); assert(a.ax==0xbeef && a.ip==0x21e);
        step(&cpu); assert(get(&cpu).ip==0x21f && !f.locked);
        assert(f.ram[0x2025+odd]==0x82); /* LLDT does not set a segment A bit. */
        cpu.ops.destroy(cpu.context);
    }
}

static void jump_matrix(void)
{
    unsigned ac,cpl,rpl,local,odd;
    for(ac=0;ac<256;++ac) for(cpl=0;cpl<4;++cpl) for(rpl=0;rpl<4;++rpl)
    for(local=0;local<2;++local) for(odd=0;odd<2;++odd) {
        fixture_t f; bm_cpu_t cpu=protected_cpu(&f,cpl,odd);
        bm_286_arch_state_t a=get(&cpu), before=a;
        bm_286_config_t c=config(&f); bm_286_segment_load_result_t result;
        bm_status_t expected=BM_STATUS_OK; unsigned fault=0, error;
        unsigned selector=(local?4u:8u)+rpl, at=local?0x6000+odd:0x2008+odd;
        a.ldtr.valid=1; a.ldtr.base=0x6000+odd; a.ldtr.limit=15; before=a;
        descriptor(&f,at,0x123456,0x8000,ac); error=selector&0xfffcu;
        if (!(ac&16u) && ((ac&15u)==1 || (ac&15u)==5)) {
            if((ac&15u)==1 && local) fault=13;
            else if(((ac>>5)&3u)<cpl || ((ac>>5)&3u)<rpl) fault=13;
            else if(!(ac&0x80u)) fault=11;
            else if((ac&15u)==5) {fault=13;error=0x3454;}
            else fault=10; /* No current TR; fault belongs to selected task. */
        }
        else if (!(ac&16u) && (ac&15u)==4) {
            /* Retain former gap cases: these gate payloads select beyond LDT. */
            if (((ac>>5)&3u)<cpl || ((ac>>5)&3u)<rpl) fault=13;
            else if (!(ac&0x80u)) fault=11;
            else { fault=13; error=0x3454; }
        }
        else if ((ac&0x18u)!=0x18u || ((ac&4u) ? ((ac>>5)&3u)>cpl :
                 rpl>cpl || ((ac>>5)&3u)!=cpl)) fault=13;
        else if (!(ac&0x80u)) fault=11;
        assert(bm_286_pm_jump(&a,&c,(uint16_t)selector,0x8000, 0x105,&result)==expected);
        assert(result.fault_vector==fault);
        if(fault==10) {
            assert(result.task_context && a.tr.selector==selector && f.locks==1);
            assert(result.fault_error==error); before.tr=a.tr; same(&a,&before);
            cpu.ops.destroy(cpu.context); continue;
        }
        if(expected==BM_STATUS_OK && !fault) {
            assert(result.loaded && a.ip==0x8000 && a.cs.base==0x123456);
            assert(a.cs.selector==((selector&0xfffcu)|cpl) && a.cs.access==(ac|1u));
            assert(a.flags==before.flags && a.cpl==cpl && a.sp==before.sp);
        } else {
            same(&a,&before); assert(!result.loaded && f.locks==0);
            if(fault) assert(result.fault_error==error);
        }
        assert(!f.locked); cpu.ops.destroy(cpu.context);
    }
    {
        fixture_t f; bm_cpu_t cpu=protected_cpu(&f,0,0);
        bm_286_arch_state_t a=get(&cpu); bm_286_config_t c=config(&f);
        bm_286_segment_load_result_t r; unsigned i;
        for(i=0;i<4;++i) {
            assert(bm_286_pm_jump(&a,&c,(uint16_t)i,0, 0x105,&r)==BM_STATUS_OK);
            assert(r.fault_vector==13 && !r.fault_error && !f.calls);
        }
        a.gdtr.limit=14;
        assert(bm_286_pm_jump(&a,&c,8,0, 0x105,&r)==BM_STATUS_OK && r.fault_vector==13 && !f.calls);
        a.gdtr.limit=39; word(&f,0x2008,0x7fff);
        assert(bm_286_pm_jump(&a,&c,8,0x8000, 0x105,&r)==BM_STATUS_OK && r.fault_vector==13 && !r.fault_error);
        assert(!f.locks); f.ram[0x200d]&=0x7f;
        assert(bm_286_pm_jump(&a,&c,8,0x8000, 0x105,&r)==BM_STATUS_OK && r.fault_vector==11 && r.fault_error==8);
        cpu.ops.destroy(cpu.context);
    }
}

static void systems(void)
{
    unsigned cpl,odd,op;
    for(cpl=0;cpl<4;++cpl) for(odd=0;odd<2;++odd) for(op=0;op<8;++op) {
        fixture_t f; bm_cpu_t cpu=protected_cpu(&f,cpl,odd);
        bm_286_arch_state_t a=get(&cpu), after; bm_286_boundary_t b;
        /* 0..3 table stores/loads; 4 SMSW; 5 LMSW; 6 LLDT; 7 CLTS. */
        uint8_t bytes[]={0x0f,1,(uint8_t)((op<4?op:op==4?4:6)*8+6),0,6};
        unsigned n=5; bool privileged=op>=2 && op!=4;
        if(op==6) {bytes[1]=0;bytes[2]=0x16;}
        if(op==7) {bytes[1]=6;n=2;}
        memcpy(f.ram+0x3100,bytes,n); a.msw=0xffff;
        word(&f,0x4600+odd,op==6?32:0x2468);
        word(&f,0x4602+odd,0x4321); word(&f,0x4604+odd,0xab65);
        assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);
        b=step(&cpu);after=get(&cpu);
        if(cpl && privileged) {
            assert(b.vector==13 && after.sp==0x7ff8);
            assert(readword(&f,after.ss.base+after.sp)==0 && after.msw==a.msw);
            assert(f.trace[n].address==a.idtr.base+13*8); /* No operand read. */
        } else {
            assert(b.kind==BM_286_BOUNDARY_INSTRUCTION && after.ip==0x100+n);
            assert(after.flags==a.flags);
            if(op<2) {
                bm_286_table_state_t t=op?a.idtr:a.gdtr;
                assert(readword(&f,0x4600+odd)==t.limit);
                assert(readword(&f,0x4602+odd)==(uint16_t)t.base);
                assert(readword(&f,0x4604+odd)==(0xff00u|(t.base>>16)));
            } else if(op<4) {
                bm_286_table_state_t t=op==3?after.idtr:after.gdtr;
                assert(t.limit==0x2468 && t.base==0x654321);
            } else if(op==4) assert(readword(&f,0x4600+odd)==0xffff);
            else if(op==5) {assert(after.msw==0xfff9);same_cache(&a.cs,&after.cs);}
            else if(op==6) assert(after.ldtr.valid && after.ldtr.selector==32 && after.ldtr.base==0x6000+odd);
            else assert(after.msw==0xfff7);
        }
        cpu.ops.destroy(cpu.context);
    }
}

static void failure_route(fixture_t *f,bm_cpu_t *cpu,unsigned route,unsigned odd)
{
    bm_286_arch_state_t a;
    uint8_t bytes[]={0x0f,1,0x16,0,6}; unsigned n=5;
    *cpu=protected_cpu(f,0,odd);a=get(cpu);
    if(route==0) {bytes[0]=0xea;bytes[1]=0;bytes[2]=2;bytes[3]=8;bytes[4]=0;}
    if(route==1) bytes[2]=6; /* SGDT */
    if(route==2) bytes[2]=0x16; /* LGDT */
    if(route==3) {bytes[1]=0;bytes[2]=0x16;} /* LLDT */
    if(route==4) bytes[2]=0x36; /* LMSW */
    if(route==5) {bytes[0]=0xeb;bytes[1]=0x7f;n=2;a.cs.limit=0x170;}
    memcpy(f->ram+0x3100,bytes,n); word(f,0x4600+odd,32);
    word(f,0x4602+odd,0x4321);word(f,0x4604+odd,0xab65);
    assert(bm_286_set_arch_state(cpu,&a)==BM_STATUS_OK);
}

static void system_edges(void)
{
    unsigned op,ss,range,odd,old,value;
    /* Complete six-byte operands: both endpoints, expand-down and SS override.
     * Keep the delivery stack valid independently of the rejected operand. */
    for(op=0;op<4;++op) for(ss=0;ss<2;++ss) for(range=0;range<4;++range)
    for(odd=0;odd<2;++odd) {
        fixture_t f; bm_cpu_t cpu=protected_cpu(&f,0,odd);
        bm_286_arch_state_t a=get(&cpu), after;
        bm_286_segment_state_t *s=ss?&a.ss:&a.ds;
        bm_286_boundary_t b;
        uint8_t bytes[]={ss?0x36:0x3e,0x0f,1,(uint8_t)(op*8+6),0,6};
        bool allowed=range==1 || range==2;
        s->limit=(uint16_t)(range<2?0x604+range:0x5ff+range-2);
        if(range>=2) s->access|=4;
        a.sp=range<2?0x500:0x8000;
        memcpy(f.ram+0x3100,bytes,sizeof(bytes));
        word(&f,s->base+0x600,0x1234); word(&f,s->base+0x602,0x5678);
        word(&f,s->base+0x604,0xee12);
        assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);
        b=step(&cpu); after=get(&cpu);
        if(allowed) {
            assert(b.kind==BM_286_BOUNDARY_INSTRUCTION && after.ip==0x106);
            if(op>=2) {
                bm_286_table_state_t t=op==2?after.gdtr:after.idtr;
                assert(t.limit==0x1234 && t.base==0x125678);
            }
        } else {
            assert(b.vector==(ss?12:13) && after.sp==a.sp-8);
            assert(readword(&f,after.ss.base+after.sp)==0);
            assert(f.trace[6].address==a.idtr.base+b.vector*8u);
            assert(readword(&f,s->base+0x600)==0x1234);
            assert(after.gdtr.base==a.gdtr.base && after.idtr.base==a.idtr.base);
        }
        cpu.ops.destroy(cpu.context);
    }
    /* Every old/new low MSW combination in PE: PE cannot be cleared; upper
     * operand bits cannot leak, and none of the hidden caches are reloaded. */
    for(old=1;old<16;old+=2) for(value=0;value<16;++value) {
        fixture_t f; bm_cpu_t cpu=protected_cpu(&f,0,0);
        bm_286_arch_state_t a=get(&cpu),after;
        const uint8_t bytes[]={0x0f,1,0xf0};
        a.msw=(uint16_t)(0xfff0|old);a.ax=(uint16_t)(0xa5a0|value);
        memcpy(f.ram+0x3100,bytes,sizeof(bytes));
        assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);
        step(&cpu);after=get(&cpu);assert(after.msw==(0xfff1|value));
        same_cache(&a.cs,&after.cs);same_cache(&a.ss,&after.ss);
        same_cache(&a.ds,&after.ds);same_cache(&a.es,&after.es);
        assert(after.flags==a.flags);cpu.ops.destroy(cpu.context);
    }
    for(value=0;value<9;++value) {
        fixture_t f; bm_cpu_t cpu=protected_cpu(&f,0,0);
        bm_286_arch_state_t a=get(&cpu),after; bm_286_boundary_t b;
        const uint8_t bytes[]={0x0f,0,0xd0};
        a.ax=(uint16_t)(value<4?value:value==4?4:value==5?24:32);
        if(value==6) f.ram[0x2025]=2; /* Absent LDT. */
        if(value==7) a.gdtr.limit=38; /* Last descriptor byte outside GDT. */
        memcpy(f.ram+0x3100,bytes,sizeof(bytes));
        assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);
        b=step(&cpu);after=get(&cpu);
        if(value<4 || value==8) {
            assert(b.kind==BM_286_BOUNDARY_INSTRUCTION);
            assert(after.ldtr.valid==(value==8));
            if(value<4) assert(f.calls==3); /* Null load never reads a table. */
        } else {
            assert(b.vector==(value==6?11:13));
            assert(readword(&f,after.ss.base+after.sp)==(a.ax&0xfffc));
            same_cache(&a.ldtr,&after.ldtr);
        }
        assert(!f.locked && f.locks==f.unlocks);
        assert(f.ram[0x2025]==(value==6?2:0x82));
        cpu.ops.destroy(cpu.context);
    }
}

static void public_transition(void)
{
    fixture_t f; bm_cpu_t cpu=create(&f); bm_286_boundary_t b;
    bm_286_arch_state_t before,after; unsigned calls;
    const uint8_t bytes[]={0xb8,1,0,0x0f,1,0xf0,0x90};
    memcpy(f.ram+0xfff0,bytes,sizeof(bytes));
    assert(bm_286_step(&cpu,&b)==BM_STATUS_OK);
    assert(bm_286_step(&cpu,&b)==BM_STATUS_OK);
    before=get(&cpu);calls=f.calls;assert(before.msw==0xfff1);
    assert(bm_286_step(&cpu,&b)==BM_STATUS_OK && f.calls==calls+1);
    after=get(&cpu); ++before.ip; same(&before,&after);
    uint64_t cycles=99; calls=f.calls;
    assert(bm_286_step_clocked(cpu.context,0,&cycles)==BM_STATUS_UNSUPPORTED && !cycles);
    assert(bm_286_step(&cpu,&b)==BM_STATUS_INVALID_STATE && f.calls==calls);
    cpu.ops.destroy(cpu.context);
}
static void failures(void)
{
    static const bm_status_t errors[]={BM_STATUS_IDLE,BM_STATUS_UNSUPPORTED,
        BM_STATUS_INVALID_ARGUMENT,BM_STATUS_INVALID_STATE,BM_STATUS_DEVICE_ERROR};
    unsigned route,odd,fail,phase,e,total=0;
    for(route=0;route<6;++route) for(odd=0;odd<2;++odd) {
        fixture_t f;bm_cpu_t cpu;unsigned count;
        failure_route(&f,&cpu,route,odd);step(&cpu);count=f.calls;cpu.ops.destroy(cpu.context);
        for(fail=1;fail<=count;++fail) for(phase=0;phase<2;++phase)
        for(e=0;e<sizeof(errors)/sizeof(errors[0]);++e) {
            bm_286_arch_state_t before,after;bm_286_boundary_t b;
            uint8_t expected[65536];unsigned i,t;
            failure_route(&f,&cpu,route,odd);before=get(&cpu);memcpy(expected,f.ram,sizeof(expected));
            f.fail=fail;f.after=phase!=0;f.failure=errors[e];
            assert(bm_286_pm_step_subset(&cpu,&b)==errors[e]);after=get(&cpu);same(&before,&after);
            assert(f.calls==fail && f.effects==fail-1+phase && !f.locked && f.locks==f.unlocks);
            for(t=0;t<f.effects;++t) if(f.trace[t].operation==BM_BUS_WRITE)
                for(i=0;i<f.trace[t].size;++i) expected[(unsigned)(f.trace[t].address+i)&65535u]=
                    (uint8_t)(f.trace[t].value>>(8u*i));
            assert(memcmp(expected,f.ram,sizeof(expected))==0);
            assert(bm_286_pm_step_subset(&cpu,&b)==BM_STATUS_INVALID_STATE && f.calls==fail);
            cpu.ops.destroy(cpu.context);++total;
        }
    }
    printf("protected transition: %u before/after transfer failures\n",total);
}

int main(void)
{
    reset_to_repair();jump_matrix();systems();system_edges();public_transition();failures();
    puts("reset/LMSW/near/far/LLDT/fault/IRET/retry and system/jump matrices passed");
    return 0;
}
