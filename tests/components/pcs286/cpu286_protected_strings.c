/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 BluMach contributors
 * Authored protected strings, REP and LOCK cases; fixture adapted from private execution tests. No ROM,
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

static const uint8_t string_ops[]={0xa4,0xa5,0xa6,0xa7,0xaa,0xab,0xac,0xad,0xae,0xaf,0x6c,0x6d,0x6e,0x6f};
static void setup(fixture_t *f,bm_cpu_t *cpu,unsigned op,unsigned odd,unsigned cpl,unsigned rep,unsigned df,unsigned lock)
{
    bm_286_arch_state_t a;uint8_t bytes[4];unsigned n=0;
    *cpu=create(f,cpl,odd);a=get(cpu);a.si=0x600;a.di=0x700;a.cx=3;a.dx=0xffff;
    a.flags=(uint16_t)(2|(cpl<<12)|(df?0x400:0));
    if(lock)bytes[n++]=0xf0;
    if(rep)bytes[n++]=(uint8_t)rep;
    bytes[n++]=0x3e;bytes[n++]=(uint8_t)op;code(f,bytes,n);
    for(unsigned i=0;i<16;++i){f->ram[(a.ds.base+0x5f0+i)&65535]=0x78;f->ram[(a.ds.base+0x600+i)&65535]=0x78;}
    for(unsigned i=0;i<32;++i)f->ram[(a.es.base+0x6f0+i)&65535]=0x78;
    f->ports[0xffff]=0x34;f->ports[0]=0x12;
    assert(bm_286_set_arch_state(cpu,&a)==BM_STATUS_OK);
}
static void matrix(void)
{
    unsigned total=0;
    for(unsigned k=0;k<sizeof(string_ops);++k)for(unsigned odd=0;odd<2;++odd)
    for(unsigned cpl=0;cpl<4;++cpl)for(unsigned df=0;df<2;++df)for(unsigned repeat=0;repeat<3;++repeat) {
        unsigned op=string_ops[k],kind=op&0xfe,size=(op&1)+1,rep=repeat?0xf1+repeat:0;
        fixture_t f;bm_cpu_t cpu;setup(&f,&cpu,op,odd,cpl,rep,df,0);bm_286_arch_state_t a=get(&cpu),b;
        unsigned count=rep?3:1,done=0,more;uint16_t flags=a.flags;
        do {
            unsigned before=f.calls;bm_286_boundary_t boundary=step(&cpu);b=get(&cpu);++done;
            more=done<count && (!(kind==0xa6||kind==0xae) || (!!(b.flags&0x40)==(rep==0xf3)));
            assert(boundary.kind==(more?BM_286_BOUNDARY_REP_ITERATION:BM_286_BOUNDARY_INSTRUCTION));
            assert(b.cx==(rep?3-done:3) && b.ip==(more?0x100:0x100+2+(rep!=0)));
            if(done>1)for(unsigned t=before;t<f.calls;++t)assert(f.trace[t].operation!=BM_BUS_FETCH);
            if(kind!=0xa6 && kind!=0xae)assert(b.flags==flags);
            unsigned source=kind==0xa4||kind==0xa6||kind==0xac||kind==0x6e;
            unsigned dest=kind!=0xac && kind!=0x6e;
            assert(b.si==(uint16_t)(a.si+(source?(df?-(int)(done*size):(int)(done*size)):0)));
            assert(b.di==(uint16_t)(a.di+(dest?(df?-(int)(done*size):(int)(done*size)):0)));
            unsigned dst=(a.es.base+(uint16_t)(a.di+(df?-(int)((done-1)*size):(int)((done-1)*size))))&65535;
            if(kind==0xa4)assert((getword(&f,dst)&(size==1?0xff:0xffff))==(size==1?0x78:0x7878));
            if(kind==0xaa)assert((getword(&f,dst)&(size==1?0xff:0xffff))==(size==1?0x78:0x5678));
            if(kind==0xac)assert(b.ax==(size==1?0x5678:0x7878));
            if(kind==0x6c)assert((getword(&f,dst)&(size==1?0xff:0xffff))==(size==1?0x34:0x1234));
            if(kind==0x6e)assert(f.ports[0xffff]==0x78 && (size==1 || f.ports[0]==0x78));
        }while(more);
        assert(!f.locked);cpu.ops.destroy(cpu.context);++total;
    }
    printf("%u string/REP mode-direction-alignment cases\n",total);
}
static void all_counts(void)
{
    fixture_t f;bm_cpu_t cpu;setup(&f,&cpu,0xaa,0,3,0xf3,0,0);bm_286_arch_state_t a=get(&cpu),b;
    for(unsigned count=0;count<65536;++count) {
        a.cx=(uint16_t)count;f.calls=f.effects=0;assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);
        bm_286_boundary_t boundary=step(&cpu);b=get(&cpu);
        assert(b.cx==(count?count-1:0) && b.di==a.di+(count!=0));
        assert(boundary.kind==(count>1?BM_286_BOUNDARY_REP_ITERATION:BM_286_BOUNDARY_INSTRUCTION));
    }
    cpu.ops.destroy(cpu.context);
    for(unsigned k=0;k<sizeof(string_ops);++k) {
        setup(&f,&cpu,string_ops[k],0,3,0xf3,0,0);a=get(&cpu);a.cx=0;a.ds.valid=a.es.valid=0;a.flags=2;
        assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);step(&cpu);b=get(&cpu);assert(f.calls==3 && b.cx==0 && b.si==a.si && b.di==a.di);
        cpu.ops.destroy(cpu.context);
    }
}
static void guest_faults(void)
{
    static const unsigned adjustments[]={5,5,6,6,10,10,0,0,10,10,10,10,9,9};
    for(unsigned k=0;k<sizeof(string_ops);++k)for(unsigned odd=0;odd<2;++odd) {
        fixture_t f;bm_cpu_t cpu;setup(&f,&cpu,string_ops[k],odd,0,0xf3,0,0);bm_286_arch_state_t a=get(&cpu),b;bm_286_boundary_t boundary;
        a.si=a.di=0xffff;assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);
        if(!(string_ops[k]&1)){a.ds.valid=a.es.valid=0;assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);}
        boundary=step(&cpu);b=get(&cpu);assert(boundary.has_vector && boundary.vector==13);
        unsigned adjust=adjustments[k],size=(string_ops[k]&1)+1;
        assert(b.si==(uint16_t)(a.si+((adjust&1)?size:0)));
        assert(b.di==(uint16_t)(a.di+((adjust&2)?size:0)) && b.cx==(uint16_t)(a.cx-(adjust>>2)));
        assert(getword(&f,b.ss.base+b.sp)==0 && getword(&f,b.ss.base+b.sp+2)==a.ip);
        assert(f.trace[3].address==a.idtr.base+13*8);cpu.ops.destroy(cpu.context);
    }
    /* A later denied element preserves previously completed iterations. */
    for(unsigned k=0;k<2;++k) {
        fixture_t f;bm_cpu_t cpu;setup(&f,&cpu,0xa5,0,0,0xf3,0,0);bm_286_arch_state_t a=get(&cpu),b;bm_286_boundary_t boundary;
        a.ds.limit=(uint16_t)(k?0xffff:a.si+1);a.es.limit=(uint16_t)(k?a.di+1:0xffff);
        assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);assert(step(&cpu).kind==BM_286_BOUNDARY_REP_ITERATION);a=get(&cpu);unsigned calls=f.calls;
        boundary=step(&cpu);b=get(&cpu);assert(boundary.has_vector && boundary.vector==13);
        assert(b.si==a.si+2 && b.di==a.di+(k?2:0) && b.cx==(uint16_t)(a.cx-(k?2:1)));
        assert(f.trace[calls].address==a.idtr.base+13*8);
        cpu.ops.destroy(cpu.context);
    }
    for(unsigned op=0x6c;op<=0x6f;++op) {
        fixture_t f;bm_cpu_t cpu;setup(&f,&cpu,op,0,3,0xf3,0,0);bm_286_arch_state_t a=get(&cpu),b;bm_286_boundary_t boundary;
        a.flags=2;assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);
        boundary=step(&cpu);b=get(&cpu);assert(boundary.has_vector && boundary.vector==13);
        assert(b.cx==a.cx-(op<0x6e?1:2));assert(f.trace[3].address==a.idtr.base+13*8);cpu.ops.destroy(cpu.context);
    }
}
static void locks(void)
{
    static const uint8_t ops[]={0xa4,0xa5,0x6c,0x6d,0x6e,0x6f};
    for(unsigned k=0;k<sizeof(ops);++k)for(unsigned odd=0;odd<2;++odd) {
        fixture_t f;bm_cpu_t cpu;setup(&f,&cpu,ops[k],odd,3,0xf3,0,1);bm_286_arch_state_t a=get(&cpu),b;
        step(&cpu);assert(f.locked && f.locks==1 && f.unlocks==0);
        assert(cpu.ops.signal(cpu.context,BM_286_SIGNAL_HOLD,1)==BM_STATUS_OK);
        step(&cpu);assert(f.locked && !f.hlda);step(&cpu);assert(!f.locked && f.locks==1 && f.unlocks==1);
        bm_286_boundary_t boundary;assert(bm_286_pm_step_subset(&cpu,&boundary)==BM_STATUS_IDLE && boundary.kind==BM_286_BOUNDARY_HOLD);
        b=get(&cpu);assert(b.cx==0 && b.ip==a.ip+4);cpu.ops.destroy(cpu.context);
    }
    for(unsigned cpl=0;cpl<4;++cpl)for(unsigned iopl=0;iopl<4;++iopl)for(unsigned odd=0;odd<2;++odd) {
        fixture_t f;bm_cpu_t cpu=create(&f,cpl,odd);bm_286_arch_state_t a=get(&cpu),b;
        const uint8_t bytes[]={0xf0,0x01,7};code(&f,bytes,3);a.flags=(uint16_t)(2|(iopl<<12));word(&f,a.ds.base+a.bx,1);
        assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);bm_286_boundary_t boundary=step(&cpu);b=get(&cpu);
        if(cpl>iopl){assert(boundary.vector==13 && b.ax==a.ax && getword(&f,a.ds.base+a.bx)==1);assert(f.trace[2].address==a.idtr.base+13*8);}
        else {assert(b.ip==a.ip+3 && getword(&f,a.ds.base+a.bx)==0x5679);assert(f.locks==1 && f.unlocks==1);}
        assert(!f.locked);cpu.ops.destroy(cpu.context);
    }
}
static void events(void)
{
    for(unsigned lock=0;lock<2;++lock)for(unsigned odd=0;odd<2;++odd)for(unsigned event=0;event<3;++event) {
        fixture_t f;bm_cpu_t cpu;setup(&f,&cpu,0xa5,odd,0,0xf3,0,lock);bm_286_arch_state_t a=get(&cpu),b;
        a.flags|=event==2?0x100:0x200;f.ram[0x3400]=0xcf;assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);
        assert(step(&cpu).kind==BM_286_BOUNDARY_REP_ITERATION);a=get(&cpu);
        if(event<2)assert(cpu.ops.signal(cpu.context,event?BM_286_SIGNAL_INTR:BM_286_SIGNAL_NMI,1)==BM_STATUS_OK);
        assert(step(&cpu).vector==(event==0?2:event==1?0x20:1));b=get(&cpu);assert(!f.locked);
        assert(getword(&f,b.ss.base+b.sp)==0x100 && b.cx==2 && b.si==a.si && b.di==a.di);
        if(event==1)assert(cpu.ops.signal(cpu.context,BM_286_SIGNAL_INTR,0)==BM_STATUS_OK);
        step(&cpu);assert(get(&cpu).ip==0x100);f.calls=0;assert(step(&cpu).kind==BM_286_BOUNDARY_REP_ITERATION);
        b=get(&cpu);assert(b.cx==1 && f.trace[0].operation==BM_BUS_FETCH);cpu.ops.destroy(cpu.context);
    }
    /* HOLD preserves decoded repetition without a refetch. */
    fixture_t f;bm_cpu_t cpu;setup(&f,&cpu,0xa4,0,0,0xf3,0,0);step(&cpu);
    assert(cpu.ops.signal(cpu.context,BM_286_SIGNAL_HOLD,1)==BM_STATUS_OK);unsigned calls=f.calls;bm_286_boundary_t boundary;
    assert(bm_286_pm_step_subset(&cpu,&boundary)==BM_STATUS_IDLE && f.calls==calls);
    assert(cpu.ops.signal(cpu.context,BM_286_SIGNAL_HOLD,0)==BM_STATUS_OK);step(&cpu);assert(f.trace[calls].operation==BM_BUS_READ);cpu.ops.destroy(cpu.context);
}
static void lifecycle_and_latched_events(void)
{
    for (unsigned action=0;action<5;++action) {
        fixture_t f; bm_cpu_t cpu; setup(&f,&cpu,0xa5,0,0,0xf3,0,1);
        step(&cpu); assert(f.locked);
        bm_286_arch_state_t a=get(&cpu),bad=a; bad.version=0;
        assert(bm_286_set_arch_state(&cpu,&bad)==BM_STATUS_INVALID_ARGUMENT);
        assert(bm_286_pm_step_subset(&cpu,NULL)==BM_STATUS_INVALID_ARGUMENT && f.locked);
        if (action==0) {
            assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK && !f.locked);
            f.calls=0; step(&cpu); assert(f.trace[0].operation==BM_BUS_FETCH);
        } else if (action==1) {
            assert(cpu.ops.reset(cpu.context)==BM_STATUS_OK && !f.locked);
            assert(!(get(&cpu).msw&1));
        } else if (action==2) {
            uint64_t cycles=99;
            assert(bm_286_step_clocked(cpu.context,0,&cycles)==BM_STATUS_UNSUPPORTED);
            assert(!cycles && !f.locked);
        } else if (action==4) {
            bm_286_boundary_t boundary; unsigned calls=f.calls;
            assert(bm_286_step(&cpu,&boundary)==BM_STATUS_OK);
            assert(f.locked && f.trace[calls].operation==BM_BUS_READ);
            bm_tick_t consumed=99;
            assert(cpu.ops.run(cpu.context,1,&consumed)==BM_STATUS_OK && consumed==1);
            assert(!f.locked && !get(&cpu).cx);
        }
        cpu.ops.destroy(cpu.context); assert(!f.locked && f.locks==f.unlocks);
    }
    for (unsigned odd=0;odd<2;++odd) for (unsigned lock=0;lock<2;++lock)
    for (unsigned failed=0;failed<2;++failed) {
        fixture_t f; bm_cpu_t cpu; setup(&f,&cpu,0xa5,odd,0,0xf3,0,lock);
        f.cpu=&cpu; f.nmi_at=3+lock+1; /* First element source read. */
        bm_286_arch_state_t a=get(&cpu),b; bm_286_boundary_t boundary;
        if (failed) { f.fail=f.nmi_at; f.failure=BM_STATUS_DEVICE_ERROR; f.after=true; }
        if (failed) {
            assert(bm_286_pm_step_subset(&cpu,&boundary)==BM_STATUS_DEVICE_ERROR);
            b=get(&cpu); a.nmi_pending=1; same(&a,&b); assert(!f.locked);
        } else {
            assert(step(&cpu).kind==BM_286_BOUNDARY_REP_ITERATION);
            b=get(&cpu); assert(b.nmi_pending && b.cx==2 && b.si==a.si+2 && b.di==a.di+2);
            assert(step(&cpu).vector==2); assert(!f.locked);
        }
        cpu.ops.destroy(cpu.context);
    }
    /* Implicit XCHG exclusion is not subject to explicit LOCK's IOPL check. */
    for (unsigned cpl=0;cpl<4;++cpl) for (unsigned odd=0;odd<2;++odd) {
        fixture_t f; bm_cpu_t cpu=create(&f,cpl,odd); bm_286_arch_state_t a=get(&cpu);
        const uint8_t bytes[]={0x87,7}; code(&f,bytes,2); word(&f,a.ds.base+a.bx,0x1234);
        step(&cpu); assert(get(&cpu).ax==0x1234 && getword(&f,a.ds.base+a.bx)==a.ax);
        assert(f.locks==1 && f.unlocks==1 && !f.locked); cpu.ops.destroy(cpu.context);
    }
}
static void failures(void)
{
    static const bm_status_t errors[]={BM_STATUS_IDLE,BM_STATUS_UNSUPPORTED,BM_STATUS_INVALID_ARGUMENT,BM_STATUS_INVALID_STATE,BM_STATUS_DEVICE_ERROR};unsigned total=0;
    for(unsigned k=0;k<sizeof(string_ops);++k)for(unsigned odd=0;odd<2;++odd)for(unsigned iteration=0;iteration<2;++iteration) {
        unsigned op=string_ops[k],lock=(op==0xa4||op==0xa5||op<0x70);
        fixture_t f;bm_cpu_t cpu;setup(&f,&cpu,op,odd,0,0xf3,0,lock);
        if(iteration){step(&cpu);f.calls=f.effects=0;}step(&cpu);unsigned count=f.calls;cpu.ops.destroy(cpu.context);
        for(unsigned fail=1;fail<=count;++fail)for(unsigned phase=0;phase<2;++phase)for(unsigned e=0;e<5;++e) {
            uint8_t ram[65536],ports[65536];bm_286_arch_state_t a,b;bm_286_boundary_t boundary;
            setup(&f,&cpu,op,odd,0,0xf3,0,lock);if(iteration)step(&cpu);a=get(&cpu);f.calls=f.effects=0;
            memcpy(ram,f.ram,sizeof(ram));memcpy(ports,f.ports,sizeof(ports));f.fail=fail;f.after=phase!=0;f.failure=errors[e];
            assert(bm_286_pm_step_subset(&cpu,&boundary)==errors[e]);b=get(&cpu);same(&a,&b);
            assert(f.calls==fail && f.effects==fail-1+phase && !f.locked && f.locks==f.unlocks);
            for(unsigned t=0;t<f.effects;++t)if(f.trace[t].operation==BM_BUS_WRITE)
                for(unsigned i=0;i<f.trace[t].size;++i)(f.trace[t].space==BM_ADDRESS_IO?ports:ram)[(unsigned)(f.trace[t].address+i)&65535u]=(uint8_t)(f.trace[t].value>>(8*i));
            assert(memcmp(ram,f.ram,sizeof(ram))==0 && memcmp(ports,f.ports,sizeof(ports))==0);
            assert(bm_286_pm_step_subset(&cpu,&boundary)==BM_STATUS_INVALID_STATE && f.calls==fail);
            cpu.ops.destroy(cpu.context);++total;
        }
    }
    printf("%u string/REP before-after transfer failures across first/later elements\n",total);
}
int main(void)
{
    setbuf(stdout,NULL);matrix();all_counts();guest_faults();locks();events();
    lifecycle_and_latched_events();failures();puts("protected strings, REP and LOCK checks passed");return 0;
}
