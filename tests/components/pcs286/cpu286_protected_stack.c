/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 BluMach contributors
 * Authored stack/near-control cases; fixture adapted from private execution tests. No ROM,
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

static uint16_t *reg(bm_286_arch_state_t *a,unsigned r)
{
    switch(r) {case 0:return &a->ax;case 1:return &a->cx;case 2:return &a->dx;
    case 3:return &a->bx;case 4:return &a->sp;case 5:return &a->bp;
    case 6:return &a->si;default:return &a->di;}
}
static void registers(void)
{
    unsigned total=0;
    for(unsigned cpl=0;cpl<4;++cpl) for(unsigned odd=0;odd<2;++odd)
    for(unsigned r=0;r<8;++r) for(unsigned pop=0;pop<2;++pop) {
        fixture_t f;bm_cpu_t cpu=create(&f,cpl,odd);bm_286_arch_state_t a=get(&cpu),e,b;
        uint8_t bytes[]={0x3e,(uint8_t)((pop?0x58:0x50)+r)};
        code(&f,bytes,2);word(&f,a.ss.base+a.sp,0x1234);e=a;e.ip+=2;
        if(pop) {*reg(&e,r)=0x1234;if(r!=4)e.sp+=2;}
        else {e.sp-=2;}
        step(&cpu);b=get(&cpu);same(&e,&b);
        if(!pop) assert(getword(&f,a.ss.base+e.sp)==*reg(&a,r));
        assert(f.trace[2].address==a.ss.base+(pop?a.sp:e.sp));
        cpu.ops.destroy(cpu.context);++total;
    }
    for(unsigned odd=0;odd<2;++odd) for(unsigned value=0;value<256;++value) {
        fixture_t f;bm_cpu_t cpu=create(&f,3,odd);uint8_t bytes[]={0x6a,(uint8_t)value};
        bm_286_arch_state_t a=get(&cpu);code(&f,bytes,2);step(&cpu);
        assert(getword(&f,a.ss.base+a.sp-2)==(uint16_t)(int16_t)(int8_t)value);
        cpu.ops.destroy(cpu.context);++total;
    }
    printf("%u register and immediate stack cases\n",total);
}
static void aggregate(void)
{
    static const uint16_t starts[]={0x8000,0,16,0xfffe};
    unsigned total=0;
    for(unsigned cpl=0;cpl<4;++cpl) for(unsigned odd=0;odd<2;++odd)
    for(unsigned down=0;down<2;++down) for(unsigned k=0;k<4;++k) {
        fixture_t f;bm_cpu_t cpu=create(&f,cpl,odd);bm_286_arch_state_t a=get(&cpu),b,e;
        const uint8_t bytes[]={0x60,0x61};code(&f,bytes,2);
        a.sp=starts[k];a.cx=0x1357;a.dx=0x2468;a.si=0xabcd;a.di=0xdcba;
        if(down) {a.ss.access|=4;a.ss.limit=0;if(k==2) {cpu.ops.destroy(cpu.context);continue;}}
        assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);step(&cpu);b=get(&cpu);
        e=a;e.ip++;e.sp-=16;same(&e,&b);
        for(unsigned i=0;i<8;++i) assert(getword(&f,a.ss.base+(uint16_t)(a.sp-2*(i+1)))==*reg(&a,i));
        word(&f,a.ss.base+(uint16_t)(a.sp-10),0xdead); /* Saved SP ignored. */
        step(&cpu);b=get(&cpu);e=a;e.ip+=2;same(&e,&b);
        cpu.ops.destroy(cpu.context);++total;
    }
    printf("%u PUSHA/POPA round trips\n",total);
}
static void segment_pop(void)
{
    static const uint8_t ops[]={0x07,0x17,0x1f};
    for(unsigned cpl=0;cpl<4;++cpl) for(unsigned odd=0;odd<2;++odd)
    for(unsigned which=0;which<3;++which) for(unsigned null=0;null<2;++null) {
        fixture_t f;bm_cpu_t cpu=create(&f,cpl,odd);bm_286_arch_state_t a=get(&cpu),b;
        uint16_t sel=(uint16_t)(null?cpl:24+cpl);code(&f,ops+which,1);
        word(&f,a.ss.base+a.sp,sel);step(&cpu);b=get(&cpu);
        if(null&&which==1) {assert(b.ip==0x400 && b.sp==a.sp-8);assert(getword(&f,b.ss.base+b.sp)==0);}
        else {
            const bm_286_segment_state_t *seg=which==0?&b.es:which==1?&b.ss:&b.ds;
            assert(b.ip==0x101 && b.sp==a.sp+2 && seg->selector==sel);
            assert(seg->valid==!null);
            if(!null) {assert(seg->base==a.ds.base && (seg->access&1));assert(f.ram[a.gdtr.base+29]&1);}
            if(which==1) assert(b.interrupt_shadow==BM_286_SHADOW_SS_LOAD);
        }
        cpu.ops.destroy(cpu.context);
    }
}
static unsigned condition(unsigned flags,unsigned op)
{
    unsigned c=!!(flags&1),p=!!(flags&4),z=!!(flags&0x40),s=!!(flags&0x80),o=!!(flags&0x800);
    unsigned values[]={o,!o,c,!c,z,!z,c||z,!c&&!z,s,!s,p,!p,s!=o,s==o,z||(s!=o),!z&&(s==o)};
    return values[op];
}
static void branches(void)
{
    unsigned total=0;
    for(unsigned bits=0;bits<32;++bits) for(unsigned op=0;op<16;++op)
    for(unsigned disp=0;disp<256;++disp) {
        fixture_t f;bm_cpu_t cpu=create(&f,3,0);bm_286_arch_state_t a=get(&cpu),b,e;
        uint8_t bytes[]={(uint8_t)(0x70+op),(uint8_t)disp};code(&f,bytes,2);
        a.flags=(uint16_t)(2|(bits&1)|((bits&2)<<1)|((bits&4)<<4)|((bits&8)<<4)|((bits&16)<<7));
        assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);step(&cpu);b=get(&cpu);e=a;
        e.ip=(uint16_t)(0x102+(condition(a.flags,op)?(int8_t)disp:0));same(&e,&b);
        cpu.ops.destroy(cpu.context);++total;
    }
    for(unsigned op=0xe0;op<=0xe3;++op) for(unsigned cx=0;cx<4;++cx) for(unsigned z=0;z<2;++z) {
        fixture_t f;bm_cpu_t cpu=create(&f,0,0);bm_286_arch_state_t a=get(&cpu),b,e;
        uint8_t bytes[]={(uint8_t)op,0xfe};code(&f,bytes,2);a.cx=(uint16_t)cx;a.flags=(uint16_t)(2|(z<<6));
        assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);step(&cpu);b=get(&cpu);e=a;
        if(op!=0xe3)e.cx--;
        e.ip=(op==0xe3?cx==0:e.cx!=0&&(op==0xe2||(op==0xe1)==z))?0x100:0x102;
        same(&e,&b);cpu.ops.destroy(cpu.context);++total;
    }
    printf("%u Jcc/LOOP/JCXZ cases\n",total);
}
static void near_calls(void)
{
    for(unsigned odd=0;odd<2;++odd) for(unsigned kind=0;kind<3;++kind) for(unsigned discard=0;discard<2;++discard) {
        fixture_t f;bm_cpu_t cpu=create(&f,3,odd);bm_286_arch_state_t a=get(&cpu),b;
        uint8_t bytes[]={0xe8,0xfd,0};unsigned size=3;
        if(kind) {bytes[0]=0xff;bytes[1]=(uint8_t)(kind==1?0xd0:0x17);size=2;a.ax=0x200;word(&f,a.ds.base+a.bx,0x200);}
        code(&f,bytes,size);f.ram[0x3200]=(uint8_t)(discard?0xc2:0xc3);word(&f,0x3201,0xfffe);
        assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);step(&cpu);b=get(&cpu);
        assert(b.ip==0x200 && b.sp==a.sp-2 && getword(&f,a.ss.base+b.sp)==0x100+size);
        step(&cpu);b=get(&cpu);assert(b.ip==0x100+size && b.sp==(uint16_t)(a.sp+(discard?0xfffe:0)));
        assert(b.cs.selector==a.cs.selector && b.flags==a.flags);cpu.ops.destroy(cpu.context);
    }
}
/* Every failing route has enough old SS space for a same-CPL fault frame. */
static void fault_setup(fixture_t *f,bm_cpu_t *cpu,unsigned route,unsigned odd)
{
    bm_286_arch_state_t a;uint8_t bytes[]={0x3e,0x61,0,0};unsigned length=2;
    *cpu=create(f,0,odd);a=get(cpu);
    switch(route) {
    case 0:a.sp=0x7ff8;a.ss.limit=0x7fff;break;
    case 1:bytes[1]=0x60;a.ss.access|=4;a.ss.limit=0x7ff0;break;
    case 2:bytes[1]=0x60;a.sp=8;break;
    case 3:a.sp=0xfff8;break;
    case 4:bytes[1]=0xc9;a.bp=0xffff;break;
    case 5:bytes[1]=0xc3;word(f,a.ss.base+a.sp,0x500);a.cs.limit=0x400;break;
    case 6:bytes[1]=0xe8;bytes[2]=0xfc;bytes[3]=3;length=4;a.cs.limit=0x400;break;
    case 7:bytes[1]=0xe2;bytes[2]=0x7f;length=3;a.cx=2;a.cs.limit=0x110;break;
    case 8:bytes[1]=0x8f;bytes[2]=7;length=3;a.ds.access&=(uint8_t)~2u;break;
    case 9:bytes[1]=0x1f;word(f,a.ss.base+a.sp,0x20);break;
    case 10:bytes[1]=0x1f;word(f,a.ss.base+a.sp,24);f->ram[a.gdtr.base+29]&=0x7f;break;
    case 11:bytes[1]=0x17;word(f,a.ss.base+a.sp,24);f->ram[a.gdtr.base+29]&=0x7f;break;
    }
    code(f,bytes,length);assert(bm_286_set_arch_state(cpu,&a)==BM_STATUS_OK);
}
static void faults(void)
{
    for(unsigned route=0;route<12;++route) for(unsigned odd=0;odd<2;++odd) {
        fixture_t f;bm_cpu_t cpu;fault_setup(&f,&cpu,route,odd);
        bm_286_arch_state_t a=get(&cpu),b;bm_286_boundary_t boundary=step(&cpu);b=get(&cpu);
        unsigned vector=route<5||route==11?12:route==10?11:13;
        assert(boundary.vector==vector && b.ip==0x400 && b.sp==(uint16_t)(a.sp-8));
        assert(getword(&f,b.ss.base+b.sp)==(route==9?32:route>=10?24:0));
        assert(getword(&f,b.ss.base+b.sp+2)==0x100 && b.cx==a.cx && b.bp==a.bp && b.ax==a.ax);
        /* Failed destination/target/aggregate checks did not write before IDT lookup. */
        for(unsigned t=0;t<f.calls && f.trace[t].address!=a.idtr.base+vector*8;++t)
            assert(f.trace[t].operation!=BM_BUS_WRITE);
        cpu.ops.destroy(cpu.context);
    }
}
static void failure_setup(fixture_t *f,bm_cpu_t *cpu,unsigned route,unsigned odd)
{
    static const uint8_t bytes[][5]={{0x50},{0x5c},{0x60},{0x61},{0xc9},{0xe8,0xfd,0},
        {0xc3},{0xc2,0xfe,0xff},{0xff,0x17},{0xff,0x27},{0xff,0x37},{0x8f,7},
        {0x1f},{0x07},{0x17},{0x68,0x34,0x12},{0x6a,0xff},{0x8f,0xc4}};
    bm_286_arch_state_t a;
    if(route>=18) {fault_setup(f,cpu,route-18,odd);return;}
    *cpu=create(f,0,odd);a=get(cpu);code(f,bytes[route],5);
    word(f,a.ss.base+a.sp,route>=12&&route<=14?24:0x200);
    word(f,a.ds.base+a.bx,0x200);word(f,a.ss.base+a.bp,0x1234);
}
static void failures(void)
{
    static const bm_status_t errors[]={BM_STATUS_IDLE,BM_STATUS_UNSUPPORTED,
        BM_STATUS_INVALID_ARGUMENT,BM_STATUS_INVALID_STATE,BM_STATUS_DEVICE_ERROR};
    unsigned total=0;
    for(unsigned route=0;route<30;++route) for(unsigned odd=0;odd<2;++odd) {
        fixture_t f;bm_cpu_t cpu;unsigned count;
        failure_setup(&f,&cpu,route,odd);step(&cpu);count=f.calls;cpu.ops.destroy(cpu.context);
        for(unsigned fail=1;fail<=count;++fail) for(unsigned phase=0;phase<2;++phase) for(unsigned e=0;e<5;++e) {
            bm_286_arch_state_t before,after;bm_286_boundary_t b;uint8_t ram[65536];
            failure_setup(&f,&cpu,route,odd);before=get(&cpu);memcpy(ram,f.ram,sizeof(ram));
            f.fail=fail;f.after=phase!=0;f.failure=errors[e];
            assert(bm_286_pm_step_subset(&cpu,&b)==errors[e]);after=get(&cpu);same(&before,&after);
            assert(f.calls==fail && f.effects==fail-1+phase && !f.locked && f.locks==f.unlocks);
            for(unsigned t=0;t<f.effects;++t) if(f.trace[t].operation==BM_BUS_WRITE)
                for(unsigned i=0;i<f.trace[t].size;++i)
                    ram[(unsigned)(f.trace[t].address+i)&65535u]=(uint8_t)(f.trace[t].value>>(8u*i));
            assert(memcmp(ram,f.ram,sizeof(ram))==0);
            assert(bm_286_pm_step_subset(&cpu,&b)==BM_STATUS_INVALID_STATE && f.calls==fail);
            cpu.ops.destroy(cpu.context);++total;
        }
    }
    printf("%u before/after every-transfer host failures\n",total);
}
static void gates(void)
{
    for(unsigned field=0;field<8;++field) if(field==3 || field==5 || field==7) {
        fixture_t f;bm_cpu_t cpu=create(&f,0,0);bm_286_arch_state_t a=get(&cpu),b;bm_286_boundary_t boundary;
        uint8_t bytes[]={0xff,(uint8_t)(field<<3|7)};code(&f,bytes,2);
        if(field==3 || field==5) {
            /* E2a connects these former gaps. The fixture's far pointer is
             * null: preserve the cases as actual #GP(0) delivery checks. */
            boundary=step(&cpu); b=get(&cpu);
            assert(boundary.kind==BM_286_BOUNDARY_EXCEPTION && boundary.vector==13);
            assert(b.sp==a.sp-8 && getword(&f,b.sp)==0 && getword(&f,b.sp+2)==a.ip);
            assert(b.ax==a.ax && b.bx==a.bx && !f.locked);
            cpu.ops.destroy(cpu.context); continue;
        }
        assert(bm_286_pm_step_subset(&cpu,&boundary)==BM_STATUS_UNSUPPORTED);b=get(&cpu);same(&a,&b);
        assert(f.calls==2);cpu.ops.destroy(cpu.context);
    }
}


static void stack_aliases(void)
{
    static const uint8_t push_seg[]={0x06,0x0e,0x16,0x1e};
    for(unsigned cpl=0;cpl<4;++cpl) for(unsigned odd=0;odd<2;++odd)
    for(unsigned which=0;which<4;++which) {
        fixture_t f;bm_cpu_t cpu=create(&f,cpl,odd);bm_286_arch_state_t a=get(&cpu),b;
        const unsigned selectors[]={a.es.selector,a.cs.selector,a.ss.selector,a.ds.selector};
        code(&f,push_seg+which,1);step(&cpu);b=get(&cpu);
        assert(b.sp==a.sp-2 && getword(&f,b.ss.base+b.sp)==selectors[which] && b.flags==a.flags);
        cpu.ops.destroy(cpu.context);
    }
    for(unsigned odd=0;odd<2;++odd) for(unsigned form=0;form<4;++form) {
        fixture_t f;bm_cpu_t cpu=create(&f,0,odd);bm_286_arch_state_t a=get(&cpu),b;
        static const uint8_t ops[][2]={{0xff,0xf4},{0x8f,0xc4},{0xff,0xd4},{0xff,0xe4}};
        code(&f,ops[form],2);word(&f,a.ss.base+a.sp,0xabcd);step(&cpu);b=get(&cpu);
        if(form==0)assert(b.sp==a.sp-2 && getword(&f,b.ss.base+b.sp)==a.sp && b.ip==0x102);
        if(form==1)assert(b.sp==0xabcd && b.ip==0x102);
        if(form==2)assert(b.sp==a.sp-2 && b.ip==a.sp && getword(&f,b.ss.base+b.sp)==0x102);
        if(form==3)assert(b.sp==a.sp && b.ip==a.sp);
        cpu.ops.destroy(cpu.context);
    }
    /* POP [BP] computes EA using old BP; writing onto the popped slot is legal. */
    for(unsigned odd=0;odd<2;++odd) {
        fixture_t f;bm_cpu_t cpu=create(&f,0,odd);bm_286_arch_state_t a=get(&cpu),b;
        const uint8_t bytes[]={0x8f,0x46,0};a.bp=a.sp;code(&f,bytes,3);word(&f,a.ss.base+a.sp,0x1234);
        assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);step(&cpu);b=get(&cpu);
        assert(b.bp==a.bp && b.sp==a.sp+2 && getword(&f,b.ss.base+a.bp)==0x1234);
        cpu.ops.destroy(cpu.context);
    }
}
static void memory_and_edges(void)
{
    for(unsigned odd=0;odd<2;++odd) for(unsigned override=0;override<2;++override) {
        fixture_t f;bm_cpu_t cpu=create(&f,3,odd);bm_286_arch_state_t a=get(&cpu),b;
        /* BP-based source/destination selects SS unless explicitly overridden. */
        const uint8_t push[]={0xff,0x76,0},pop[]={0x8f,0x46,0};
        uint8_t bytes[4]={0x3e,0,0,0};memcpy(bytes+override,push,3);code(&f,bytes,3+override);
        word(&f,a.ss.base+a.bp,0xabcd);word(&f,a.ds.base+a.bp,0x2468);
        step(&cpu);b=get(&cpu);assert(getword(&f,b.ss.base+b.sp)==(override?0x2468:0xabcd));
        memcpy(f.ram+0x3000+b.ip,pop,3);step(&cpu);b=get(&cpu);
        assert(b.sp==a.sp && getword(&f,b.ss.base+b.bp)==(override?0x2468:0xabcd));
        cpu.ops.destroy(cpu.context);
    }
    for(unsigned odd=0;odd<2;++odd) for(unsigned pop=0;pop<2;++pop) {
        fixture_t f;bm_cpu_t cpu=create(&f,0,odd);bm_286_arch_state_t a=get(&cpu),b;
        uint8_t bytes[]={(uint8_t)(pop?0x58:0x50)};code(&f,bytes,1);a.sp=(uint16_t)(pop?0xfffe:0);
        word(&f,a.ss.base+0xfffe,0xabcd);assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);step(&cpu);b=get(&cpu);
        assert(b.sp==(pop?0:0xfffe));assert(pop?b.ax==0xabcd:getword(&f,b.ss.base+b.sp)==a.ax);
        cpu.ops.destroy(cpu.context);
    }
    /* Relative near CALL wraps IP modulo 65536; return retains the next IP. */
    for(unsigned odd=0;odd<2;++odd) {
        fixture_t f;bm_cpu_t cpu=create(&f,0,odd);bm_286_arch_state_t a=get(&cpu),b;
        const uint8_t bytes[]={0xe8,0xfe,0xfe};code(&f,bytes,3);step(&cpu);b=get(&cpu);
        assert(b.ip==1 && getword(&f,b.ss.base+b.sp)==0x103);assert(b.cs.base==a.cs.base);
        cpu.ops.destroy(cpu.context);
    }
    /* Untaken Jcc does not validate its out-of-range destination. */
    for(unsigned op=0;op<16;++op) {
        fixture_t f;bm_cpu_t cpu=create(&f,0,0);bm_286_arch_state_t a=get(&cpu),b;
        uint8_t bytes[]={(uint8_t)(0x70+op),0x7f};unsigned flags;
        for(unsigned bits=0;;++bits) {
            assert(bits<32);flags=2|(bits&1)|((bits&2)<<1)|((bits&4)<<4)|((bits&8)<<4)|((bits&16)<<7);
            if(!condition(flags,op))break;
        }
        a.flags=(uint16_t)flags;a.cs.limit=0x102;code(&f,bytes,2);
        assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);step(&cpu);b=get(&cpu);assert(b.ip==0x102);
        cpu.ops.destroy(cpu.context);
    }
}
static void repair_and_shadow(void)
{
    for(unsigned odd=0;odd<2;++odd) {
        fixture_t f;bm_cpu_t cpu=create(&f,0,odd);bm_286_arch_state_t a=get(&cpu),b;
        const uint8_t popa[]={0x61},handler[]={0xb8,16,0,0x8e,0xd0,0xbc,0xf2,0x7f,0xcf};
        code(&f,popa,1);memcpy(f.ram+0x3400,handler,sizeof(handler));a.sp=0x7ff8;a.ss.limit=0x7fff;
        for(unsigned i=0;i<8;++i)word(&f,a.ss.base+a.sp+2*i,0x1000+i);
        assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);assert(step(&cpu).vector==12);
        for(unsigned i=0;i<4;++i)step(&cpu);
        b=get(&cpu);assert(b.ip==a.ip && b.sp==a.sp && b.ss.limit==0xffff);
        step(&cpu);b=get(&cpu);assert(b.sp==0x8008 && b.ip==0x101);
        for(unsigned i=0;i<8;++i)if(i!=3)assert(*reg(&b,7-i)==0x1000+i);
        cpu.ops.destroy(cpu.context);

        cpu=create(&f,0,odd);a=get(&cpu);
        {const uint8_t program[]={0x17,0x5c,0x90};code(&f,program,sizeof(program));}
        word(&f,a.ss.base+a.sp,24);word(&f,a.ds.base+a.sp+2,0x9000);
        f.cpu=&cpu;f.nmi_at=2;step(&cpu);b=get(&cpu);
        assert(b.nmi_pending && b.interrupt_shadow==BM_286_SHADOW_SS_LOAD && b.sp==a.sp+2);
        assert(b.ss.base==a.ds.base);f.nmi_at=0;
        assert(step(&cpu).kind==BM_286_BOUNDARY_INSTRUCTION);b=get(&cpu);
        assert(b.sp==0x9000 && b.ip==0x102 && b.nmi_pending && !b.interrupt_shadow);
        assert(step(&cpu).vector==2);b=get(&cpu);
        assert(getword(&f,b.ss.base+b.sp)==0x102 && b.nmi_blocked);
        cpu.ops.destroy(cpu.context);
    }
}
int main(void)
{
    setbuf(stdout,NULL);registers();aggregate();segment_pop();branches();near_calls();faults();failures();gates();stack_aliases();memory_and_edges();repair_and_shadow();
    puts("private protected stack and near-control tests passed");return 0;
}
