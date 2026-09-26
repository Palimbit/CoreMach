/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 BluMach contributors
 * Authored IOPL/FLAGS cases; fixture adapted from private execution tests. No ROM,
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

static uint16_t popf_expected(uint16_t old,unsigned saved,unsigned cpl)
{
    unsigned bit;uint16_t result=2;
    for(bit=0;bit<15;++bit) {
        unsigned source=saved;
        if(bit==1 || bit==3 || bit==5) continue;
        if((bit==12 || bit==13) && cpl) source=old;
        if(bit==9 && cpl>((old>>12)&3u)) source=old;
        if(source&(1u<<bit)) result|=(uint16_t)(1u<<bit);
    }
    return result;
}
static void popf_matrix(void)
{
    unsigned cpl,iopl,old_if,saved,total=0;
    for(cpl=0;cpl<4;++cpl) for(iopl=0;iopl<4;++iopl) for(old_if=0;old_if<2;++old_if) {
        fixture_t f;bm_cpu_t cpu=create(&f,cpl,iopl&1u);
        bm_286_arch_state_t a=get(&cpu),after,expected;
        const uint8_t bytes[]={0x3e,0x9d}; /* SS cannot be overridden. */
        code(&f,bytes,sizeof(bytes));a.flags=(uint16_t)(0x4cd7|(iopl<<12)|(old_if<<9));
        for(saved=0;saved<65536;++saved) {
            word(&f,a.ss.base+a.sp,saved);f.calls=f.effects=0;
            assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);
            assert(step(&cpu).kind==BM_286_BOUNDARY_INSTRUCTION);after=get(&cpu);
            expected=a;expected.ip+=2;expected.sp+=2;
            expected.flags=popf_expected(a.flags,saved,cpl);same(&expected,&after);
            assert(f.trace[2].address==a.ss.base+a.sp && !f.locks);++total;
        }
        cpu.ops.destroy(cpu.context);
    }
    printf("POPF: %u saved-FLAGS/CPL/IOPL/old-IF cases\n",total);
}

static void privilege_matrix(void)
{
    static const uint8_t ops[]={0xfa,0xfb,0xf4,0xe4,0xe5,0xe6,0xe7,0xec,0xed,0xee,0xef};
    unsigned cpl,iopl,initial_if,op,odd;
    for(cpl=0;cpl<4;++cpl) for(iopl=0;iopl<4;++iopl) for(initial_if=0;initial_if<2;++initial_if)
    for(op=0;op<sizeof(ops);++op) for(odd=0;odd<2;++odd) {
        fixture_t f;bm_cpu_t cpu=create(&f,cpl,odd);
        bm_286_arch_state_t a=get(&cpu),after,expected;bm_286_boundary_t b;
        uint8_t bytes[]={0x3e,ops[op],(uint8_t)(0x80+odd)};
        unsigned n=op>=3 && op<=6?3:2,port=op<=6?0x80+odd:0xfffe + odd;
        bool denied=op==2?cpl!=0:cpl>iopl;
        a.flags=(uint16_t)(0x4cd7|(iopl<<12)|(initial_if<<9));a.dx=(uint16_t)(0xfffe + odd);
        f.ports[port]=0x34;f.ports[(port+1)&65535]=0x12;
        code(&f,bytes,n);assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);
        b=step(&cpu);after=get(&cpu);
        if(denied) {
            assert(b.vector==13 && after.ip==0x400 && after.sp==a.sp-8);
            assert(getword(&f,after.ss.base+after.sp)==0);
            assert(getword(&f,after.ss.base+after.sp+2)==0x100);
            assert(getword(&f,after.ss.base+after.sp+6)==a.flags);
            assert(after.ax==a.ax && !after.halted);
            assert(f.trace[n].address==a.idtr.base+13*8);
            for(unsigned i=0;i<f.calls;++i) assert(f.trace[i].space!=BM_ADDRESS_IO);
        } else {
            expected=a;expected.ip+=(uint16_t)n;
            if(op==0) expected.flags&=0xfdff;
            if(op==1) {expected.flags|=0x200;expected.interrupt_shadow=BM_286_SHADOW_INTR_ONLY;}
            if(op==2) expected.halted=1;
            if(op>=3) {
                bool output=(ops[op]&2)!=0,wide=(ops[op]&1)!=0;
                if(!output) expected.ax=wide?0x1234:0x5634;
                else {assert(f.ports[port]==0x78);if(wide) assert(f.ports[(port+1)&65535]==0x56);}
                assert(f.trace[n].space==BM_ADDRESS_IO && f.trace[n].address==port);
                assert(f.calls==n+(wide&&odd?2u:1u));
            }
            same(&expected,&after);assert(!f.locks);
        }
        cpu.ops.destroy(cpu.context);
    }
    puts("704 CLI/STI/HLT/scalar-I/O privilege cases passed");
}

static void stack_and_flags(void)
{
    unsigned cpl,odd,which;
    static const uint8_t ops[]={0x9c,0x9e,0x9f,0xf5,0xf8,0xf9,0xfc,0xfd};
    for(cpl=0;cpl<4;++cpl) for(odd=0;odd<2;++odd) for(which=0;which<sizeof(ops);++which) {
        fixture_t f;bm_cpu_t cpu=create(&f,cpl,odd);
        bm_286_arch_state_t a=get(&cpu),after,expected;
        a.flags=0x7ed7;a.ax=0x285a;code(&f,ops+which,1);
        assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);step(&cpu);after=get(&cpu);expected=a;expected.ip++;
        switch(which) {
        case 0:expected.sp-=2;assert(getword(&f,a.ss.base+expected.sp)==a.flags);break;
        case 1:expected.flags=0x7e02;break;
        case 2:expected.ax=0xd75a;break;
        case 3:case 4:expected.flags&=0xfffe;break;
        case 5:expected.flags|=1;break;
        case 6:expected.flags&=0xfbff;break;
        default:expected.flags|=0x400;break;
        }
        same(&expected,&after);cpu.ops.destroy(cpu.context);
    }
    for(odd=0;odd<2;++odd) for(which=0;which<6;++which) {
        fixture_t f;bm_cpu_t cpu=create(&f,0,odd);
        bm_286_arch_state_t a=get(&cpu),after;bm_286_boundary_t b;
        uint8_t op=which<4?0x9d:0x9c;
        a.sp=(uint16_t)(which==0?0x7fff:which==1?0xffff:which==2?0x8000:which==3?0xfffe:0);
        if(which==0) a.ss.limit=0x7fff;
        if(which==2 || which==5) {a.ss.access|=4;a.ss.limit=0x7fff;}
        if(which<4) word(&f,(a.ss.base+a.sp)&65535,2);
        code(&f,&op,1);assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);b=step(&cpu);after=get(&cpu);
        if(which<2) {
            assert(b.vector==12 && after.sp==(uint16_t)(a.sp-8));
            assert(f.trace[1].address==a.idtr.base+12*8);
            assert(getword(&f,a.ss.base+after.sp)==0);
        } else {
            assert(b.kind==BM_286_BOUNDARY_INSTRUCTION);
            assert(after.sp==(uint16_t)(a.sp+(op==0x9d?2:-2)));
            assert(f.trace[1].address==((a.ss.base+(op==0x9d?a.sp:0xfffe))&0xffffffu));
        }
        cpu.ops.destroy(cpu.context);
    }
}

static void events(void)
{
    fixture_t f;bm_cpu_t cpu;bm_286_arch_state_t a;bm_286_boundary_t b;
    const uint8_t sti[]={0xfb,0x90,0x90},popf[]={0x9d,0x90,0x90},hlt[]={0xf4,0x90};
    cpu=create(&f,0,0);code(&f,sti,sizeof(sti));
    assert(cpu.ops.signal(cpu.context,BM_286_SIGNAL_INTR,1)==BM_STATUS_OK);
    step(&cpu);step(&cpu);assert(get(&cpu).ip==0x102 && !f.acks);
    b=step(&cpu);assert(b.vector==0x20 && f.acks==2);
    assert(getword(&f,get(&cpu).sp)==0x102);cpu.ops.destroy(cpu.context);
    /* STI shadow blocks INTR only: NMI and sampled TF remain deliverable. */
    for(unsigned trap=0;trap<2;++trap) {
        cpu=create(&f,0,0);a=get(&cpu);if(trap) a.flags|=0x100;
        code(&f,sti,sizeof(sti));assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);
        step(&cpu);if(!trap) assert(cpu.ops.signal(cpu.context,BM_286_SIGNAL_NMI,1)==BM_STATUS_OK);
        b=step(&cpu);assert(b.vector==(trap?1:2) && !f.acks);
        cpu.ops.destroy(cpu.context);
    }
    /* POPF changes IF without an STI shadow; newly set TF waits an instruction. */
    for(unsigned trap=0;trap<2;++trap) {
        cpu=create(&f,0,0);code(&f,popf,sizeof(popf));word(&f,0x8000,trap?0x102:0x202);
        if(!trap) assert(cpu.ops.signal(cpu.context,BM_286_SIGNAL_INTR,1)==BM_STATUS_OK);
        step(&cpu);a=get(&cpu);assert(!a.interrupt_shadow && !a.trap_pending);
        if(trap) {step(&cpu);assert(get(&cpu).trap_pending);}
        b=step(&cpu);assert(b.vector==(trap?1:0x20));cpu.ops.destroy(cpu.context);
    }
    cpu=create(&f,0,0);a=get(&cpu);a.flags=0x102;
    code(&f,popf,sizeof(popf));word(&f,0x8000,2);
    assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);step(&cpu);
    assert(get(&cpu).flags==2 && get(&cpu).trap_pending); /* Incoming TF still samples POPF. */
    assert(step(&cpu).vector==1);cpu.ops.destroy(cpu.context);
    for(unsigned nmi=0;nmi<2;++nmi) {
        cpu=create(&f,0,0);a=get(&cpu);a.flags=nmi?2:0x202;code(&f,hlt,sizeof(hlt));
        assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);step(&cpu);assert(get(&cpu).halted);
        {unsigned calls=f.calls;assert(bm_286_pm_step_subset(&cpu,&b)==BM_STATUS_IDLE);
         assert(b.kind==BM_286_BOUNDARY_HALT && f.calls==calls);}
        assert(cpu.ops.signal(cpu.context,nmi?BM_286_SIGNAL_NMI:BM_286_SIGNAL_INTR,1)==BM_STATUS_OK);
        b=step(&cpu);assert(b.vector==(nmi?2:0x20) && !get(&cpu).halted);
        assert(getword(&f,get(&cpu).sp)==0x101);cpu.ops.destroy(cpu.context);
    }
    /* NMI latched during I/O must survive accumulator commit. */
    cpu=create(&f,0,0);{const uint8_t in[]={0xec};code(&f,in,1);}
    a=get(&cpu);a.dx=0x80;assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);
    f.cpu=&cpu;f.nmi_at=2;f.ports[0x80]=0xab;step(&cpu);a=get(&cpu);
    assert(a.ax==0x56ab && a.nmi_pending);f.nmi_at=0;assert(step(&cpu).vector==2);
    cpu.ops.destroy(cpu.context);
}

static void failure_setup(fixture_t *f,bm_cpu_t *cpu,unsigned route,unsigned odd)
{
    static const uint8_t ops[]={0x9c,0x9d,0x9e,0x9f,0xf5,0xf8,0xf9,0xfc,0xfd,
        0xfa,0xfb,0xf4,0xe4,0xe5,0xe6,0xe7,0xec,0xed,0xee,0xef,0xfa,0xfb,0xf4,0x9d};
    bm_286_arch_state_t a;uint8_t bytes[]={0x3e,ops[route],0xff};
    *cpu=create(f,route>=20&&route<=22?3:0,odd);a=get(cpu);
    a.dx=0xffff;a.flags=2;code(f,bytes,route>=12&&route<=15?3:2);word(f,a.ss.base+a.sp,0x7fd7);
    f->ports[0xffff]=0x34;f->ports[0]=0x12;
    f->ports[0xff]=0x34;f->ports[0x100]=0x12;
    if(route==23) {a.sp=0x7fff;a.ss.limit=0x7fff;}
    assert(bm_286_set_arch_state(cpu,&a)==BM_STATUS_OK);
}
static void failures(void)
{
    static const bm_status_t errors[]={BM_STATUS_IDLE,BM_STATUS_UNSUPPORTED,
        BM_STATUS_INVALID_ARGUMENT,BM_STATUS_INVALID_STATE,BM_STATUS_DEVICE_ERROR};
    unsigned route,odd,fail,phase,e,total=0;
    for(route=0;route<24;++route) for(odd=0;odd<2;++odd) {
        fixture_t f;bm_cpu_t cpu;unsigned count;
        failure_setup(&f,&cpu,route,odd);step(&cpu);count=f.calls;cpu.ops.destroy(cpu.context);
        for(fail=1;fail<=count;++fail) for(phase=0;phase<2;++phase) for(e=0;e<5;++e) {
            bm_286_arch_state_t before,after;bm_286_boundary_t b;uint8_t ram[65536],ports[65536];
            failure_setup(&f,&cpu,route,odd);before=get(&cpu);
            memcpy(ram,f.ram,sizeof(ram));memcpy(ports,f.ports,sizeof(ports));
            f.fail=fail;f.after=phase!=0;f.failure=errors[e];
            assert(bm_286_pm_step_subset(&cpu,&b)==errors[e]);after=get(&cpu);same(&before,&after);
            assert(f.calls==fail && f.effects==fail-1+phase && !f.locked && f.locks==f.unlocks);
            for(unsigned t=0;t<f.effects;++t) if(f.trace[t].operation==BM_BUS_WRITE)
                for(unsigned i=0;i<f.trace[t].size;++i)
                    (f.trace[t].space==BM_ADDRESS_IO?ports:ram)[(unsigned)(f.trace[t].address+i)&65535u]=
                        (uint8_t)(f.trace[t].value>>(8u*i));
            assert(memcmp(ram,f.ram,sizeof(ram))==0 && memcmp(ports,f.ports,sizeof(ports))==0);
            assert(bm_286_pm_step_subset(&cpu,&b)==BM_STATUS_INVALID_STATE && f.calls==fail);
            cpu.ops.destroy(cpu.context);++total;
        }
    }
    printf("IOPL/FLAGS: %u before/after transfer failures\n",total);
}

static void repair_stack(void)
{
    for(unsigned odd=0;odd<2;++odd) {
        fixture_t f;bm_cpu_t cpu=create(&f,0,odd);bm_286_arch_state_t a=get(&cpu);
        const uint8_t pop[]={0x9d},handler[]={0xb8,16,0,0x8e,0xd0,0xbc,0xf9,0x7f,0xcf};
        a.sp=0x7fff;a.ss.limit=0x7fff;
        code(&f,pop,1);memcpy(f.ram+0x3400,handler,sizeof(handler));word(&f,a.ss.base+a.sp,0x202);
        assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);
        assert(step(&cpu).vector==12);
        for(unsigned i=0;i<4;++i) step(&cpu);
        a=get(&cpu);assert(a.ip==0x100 && a.sp==0x7fff && a.ss.limit==0xffff);
        step(&cpu);a=get(&cpu);assert(a.ip==0x101 && a.sp==0x8001 && a.flags==0x202);
        assert(!a.interrupt_shadow);cpu.ops.destroy(cpu.context);
    }
}
int main(void)
{
    setvbuf(stdout,NULL,_IONBF,0);popf_matrix();privilege_matrix();stack_and_flags();events();repair_stack();failures();
    puts("private IOPL, FLAGS, scalar I/O, HLT and event checks passed");return 0;
}
