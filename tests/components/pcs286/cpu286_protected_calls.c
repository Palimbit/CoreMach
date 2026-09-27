/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 BluMach contributors
 * Authored ordinary far-transfer tests; fixture adapted from protected_returns. No ROM,
 * external vectors, public PE entry, physical timing or machine acceptance.
 */
#include "execution_286.h"
#include "descriptor_286.h"
#include <blumach/platforms/null_host.h>
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef struct fixture {
    uint8_t ram[131072];
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
        unsigned at = (unsigned)(t->address + i) & 131071u;
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

static bm_286_config_t config(fixture_t *f)
{
    bm_286_config_t c = {0}; c.size = sizeof(c); c.version = BM_286_CONTRACT_VERSION;
    c.access = bus; c.access_context = f; c.bus_lock = pin_lock; c.pin_context = f;
    return c;
}

static bm_cpu_t setup(fixture_t *f, unsigned cpl, unsigned target, unsigned odd,
    unsigned count, bool gate, bool call, bool indirect)
{
    bm_cpu_t cpu = create(f,cpl,odd); bm_286_arch_state_t a = get(&cpu);
    uint8_t bytes[] = {0x36,0x9a,0,2,24,0};
    a.gdtr.limit=63; a.flags=0x402; a.nmi_blocked=1;
    descriptor(f,a.gdtr.base+24,0x5000,0x9a|(target<<5));
    descriptor(f,a.gdtr.base+32,0xa000+odd,0x92|(target<<5));
    a.tr.valid=1; a.tr.selector=56; a.tr.access=0x83;
    a.tr.base=0x9000+odd; a.tr.limit=43;
    descriptor(f,a.gdtr.base+56,0xb000,0x01); /* Deliberately unlike TR cache. */
    for(unsigned level=0;level<3;++level) {
        word(f,a.tr.base+2+level*4,0x800);
        word(f,a.tr.base+4+level*4,32+level);
    }
    word(f,a.gdtr.base+48,0x200); word(f,a.gdtr.base+50,24+target);
    f->ram[a.gdtr.base+52]=(uint8_t)count; f->ram[a.gdtr.base+53]=0xe4;
    word(f,a.gdtr.base+54,0xffff); /* Reserved, not a 386 offset extension. */
    bytes[1]=call?0x9a:0xea; bytes[4]=(uint8_t)(gate?48+cpl:24+cpl);
    if(gate) { bytes[2]=0xad; bytes[3]=0xde; } /* Pointer offset ignored. */
    if(indirect) {
        word(f,a.ss.base+0x600,gate?0xdead:0x200);
        word(f,a.ss.base+0x602,bytes[4]);
        bytes[1]=0xff; bytes[2]=call?0x1e:0x2e; bytes[3]=0; bytes[4]=6;
    }
    code(f,bytes,indirect?5:6);
    f->ram[0x3100+(indirect?5:6)]=0x90;
    f->ram[0x5200]=0xca; word(f,0x5201,2*(count&31u));
    for(unsigned i=0;i<31;++i) word(f,a.ss.base+a.sp+2*i,0xa500+i);
    assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK); return cpu;
}
static bm_status_t transfer(bm_286_arch_state_t *a,fixture_t *f,bool call,
    unsigned selector,bm_286_segment_load_result_t *r)
{
    bm_286_config_t c=config(f);
    return call?bm_286_pm_call(a,&c,(uint16_t)selector,0x200,0x106,r):
                bm_286_pm_jump(a,&c,(uint16_t)selector,0x200, 0x105,r);
}

static void direct_matrix(void)
{
    unsigned total=0;
    for(unsigned ac=0x10;ac<256;++ac) if(ac&16u)
    for(unsigned cpl=0;cpl<4;++cpl) for(unsigned rpl=0;rpl<4;++rpl)
    for(unsigned call=0;call<2;++call) for(unsigned local=0;local<2;++local) {
        fixture_t f; bm_cpu_t cpu=setup(&f,cpl,cpl,ac&1u,0,false,call!=0,false);
        bm_286_arch_state_t a=get(&cpu),before; bm_286_segment_load_result_t r;
        unsigned vector=0, selector=(local?4:24)+rpl,dpl=(ac>>5)&3u;
        a.ldtr.valid=1; a.ldtr.base=0x7001; a.ldtr.limit=7;
        descriptor(&f,local?a.ldtr.base:a.gdtr.base+24,0x5000,ac); before=a;
        if(!(ac&8u) || ((ac&4u)?dpl>cpl:(dpl!=cpl || rpl>cpl))) vector=13;
        else if(!(ac&0x80u)) vector=11;
        assert(transfer(&a,&f,call!=0,selector,&r)==BM_STATUS_OK);
        assert(r.fault_vector==vector && r.loaded==!vector);
        if(vector) { same(&a,&before); assert(r.fault_error==(selector&0xfffcu) && !f.locks); }
        else {
            assert(a.cs.selector==((selector&0xfffcu)|cpl) && a.cpl==cpl && a.ip==0x200);
            assert(a.cs.access==(ac|1u) && a.sp==before.sp-4*call);
            if(call) {
                assert(getword(&f,a.ss.base+a.sp)==0x106);
                assert(getword(&f,a.ss.base+a.sp+2)==before.cs.selector);
            }
            assert(a.flags==before.flags && a.ss.selector==before.ss.selector && a.nmi_blocked);
        }
        cpu.ops.destroy(cpu.context); ++total;
    }
    printf("protected calls: %u direct type/CPL/RPL/table cases\n",total);
}

static void gate_matrices(void)
{
    unsigned total=0;
    for(unsigned ac=0;ac<256;++ac) if(!(ac&16u))
    for(unsigned cpl=0;cpl<4;++cpl) for(unsigned rpl=0;rpl<4;++rpl)
    for(unsigned call=0;call<2;++call) {
        fixture_t f; bm_cpu_t cpu=setup(&f,cpl,cpl,ac&1u,31,true,call!=0,false);
        bm_286_arch_state_t a=get(&cpu),before=a; bm_286_segment_load_result_t r;
        unsigned type=ac&15u,dpl=(ac>>5)&3u,vector=0,error=48; bm_status_t status=BM_STATUS_OK;
        f.ram[a.gdtr.base+53]=(uint8_t)ac;
        if((type!=4 && type!=1 && type!=5) || dpl<cpl || dpl<rpl) vector=13;
        else if(!(ac&0x80u)) vector=11;
        else if(type==5) {vector=13;error=24;}
        else if(type==1) {vector=10;error=0;}
        assert(transfer(&a,&f,call!=0,48+rpl,&r)==status);
        assert(r.fault_vector==vector && r.loaded==(status==BM_STATUS_OK && !vector));
        if(type==1 && vector==10) {
            assert(r.task_context && a.tr.selector==48+rpl && !a.ip && !a.ldtr.valid);
            assert(r.fault_error==0 && f.ram[before.gdtr.base+53]==(ac|2u));
            cpu.ops.destroy(cpu.context); ++total; continue;
        }
        if(!r.loaded) { same(&a,&before); assert(!f.locks); if(vector) assert(r.fault_error==error); }
        else assert(a.cs.selector==24+cpl && a.cpl==cpl && a.sp==before.sp-4*call && a.ip==0x200);
        assert(f.ram[before.gdtr.base+53]==ac); /* No gate A/busy write. */
        cpu.ops.destroy(cpu.context); ++total;
    }
    printf("protected calls: %u gate type/presence/CPL/RPL cases\n",total); total=0;
    for(unsigned ac=0;ac<256;++ac) for(unsigned cpl=0;cpl<4;++cpl)
    for(unsigned call=0;call<2;++call) for(unsigned rpl=0;rpl<4;++rpl) {
        unsigned dpl=(ac>>5)&3u,vector=0;
        fixture_t f; bm_cpu_t cpu=setup(&f,cpl,dpl,ac&1u,0,true,call!=0,false);
        bm_286_arch_state_t a=get(&cpu),before=a; bm_286_segment_load_result_t r;
        f.ram[a.gdtr.base+29]=(uint8_t)ac; word(&f,a.gdtr.base+50,24+rpl);
        if((ac&0x18u)!=0x18u || dpl>cpl || (!call && !(ac&4u) && dpl!=cpl)) vector=13;
        else if(!(ac&0x80u)) vector=11;
        assert(transfer(&a,&f,call!=0,48+cpl,&r)==BM_STATUS_OK);
        assert(r.fault_vector==vector && r.loaded==!vector);
        if(vector) { same(&a,&before); assert(r.fault_error==24 && !f.locks); }
        else {
            bool inner=call && !(ac&4u) && dpl<cpl;
            assert(a.cpl==(inner?dpl:cpl) && a.cs.selector==24+a.cpl && a.ip==0x200);
            assert(a.sp==(inner?0x7f8u:before.sp-4*call));
            assert(a.flags==before.flags && a.ds.selector==before.ds.selector);
        }
        cpu.ops.destroy(cpu.context); ++total;
    }
    printf("protected calls: %u gate target types/privileges/ignored RPL cases\n",total); total=0;
    for(unsigned cpl=1;cpl<4;++cpl) for(unsigned target=0;target<cpl;++target)
    for(unsigned ac=0;ac<256;++ac) for(unsigned rpl=0;rpl<4;++rpl) for(unsigned local=0;local<2;++local) {
        fixture_t f; bm_cpu_t cpu=setup(&f,cpl,target,ac&1u,2,true,true,false);
        bm_286_arch_state_t a=get(&cpu),before; bm_286_segment_load_result_t r;
        unsigned selector=(local?4:32)+rpl,vector=0;
        a.ldtr.valid=1; a.ldtr.base=0x7000; a.ldtr.limit=7;
        descriptor(&f,local?a.ldtr.base:a.gdtr.base+32,0xa000,ac);
        if(ac&4u) word(&f,local?a.ldtr.base:a.gdtr.base+32,0); /* Expand-down room. */
        word(&f,a.tr.base+4+target*4,selector); before=a;
        if(rpl!=target || ((ac>>5)&3u)!=target || (ac&0x1au)!=0x12u) vector=10;
        else if(!(ac&0x80u)) vector=12;
        assert(transfer(&a,&f,true,48+cpl,&r)==BM_STATUS_OK);
        assert(r.fault_vector==vector && r.loaded==!vector);
        if(vector) { same(&a,&before); assert(r.fault_error==(selector&0xfffcu) && !f.locks); }
        else assert(a.ss.selector==selector && a.cpl==target && a.sp==0x7f4 && a.ss.access==(ac|1u));
        cpu.ops.destroy(cpu.context); ++total;
    }
    printf("protected calls: %u inner SS type/privilege/presence/table cases\n",total);
}

static bool range(unsigned at,unsigned length,unsigned limit,bool down)
{
    for(unsigned i=0;i<length;++i) if(at+i>65535 || (down?at+i<=limit:at+i>limit)) return false;
    return true;
}
static void stack_ranges(void)
{
    static const unsigned limits[]={0,1,69,0x7ff,0xfffe,0xffff};
    unsigned total=0;
    for(unsigned route=0;route<3;++route) for(unsigned down=0;down<2;++down)
    for(unsigned li=0;li<sizeof(limits)/sizeof(limits[0]);++li) {
        fixture_t f; bm_cpu_t cpu=setup(&f,3,route?0:3,0,31,route!=0,true,false);
        bm_286_arch_state_t initial=get(&cpu); bm_286_segment_load_result_t r;
        for(unsigned sp=0;sp<65536;++sp) {
            bm_286_arch_state_t a=initial; unsigned length=route==0?4:route==1?70:62;
            unsigned at=route==2?sp:(sp?sp:65536)-length;
            bool fits=range(at,length,limits[li],down!=0);
            if(route==1) {
                word(&f,a.tr.base+2,sp); word(&f,a.gdtr.base+32,limits[li]);
                f.ram[a.gdtr.base+37]=(uint8_t)(0x92+4*down);
            } else { a.sp=(uint16_t)sp; a.ss.limit=(uint16_t)limits[li]; a.ss.access=(uint8_t)(0xf3+4*down); }
            f.calls=f.effects=f.locks=f.unlocks=0;
            /* These destination ranges may alias source descriptors in this
             * sweep. Restore only table/TSS inputs before the next helper. */
            descriptor(&f,a.gdtr.base+24,0x5000,route?0x9a:0xfa);
            if(route==2) { descriptor(&f,a.gdtr.base+32,0xa000,0x92); word(&f,a.tr.base+2,0x800); }
            word(&f,a.gdtr.base+48,0x200); word(&f,a.gdtr.base+50,24+(route?0:3));
            f.ram[a.gdtr.base+52]=31; f.ram[a.gdtr.base+53]=0xe4;
            if(route) word(&f,a.tr.base+4,32);
            assert(transfer(&a,&f,true,route?51:27,&r)==BM_STATUS_OK);
            assert(r.loaded==fits && r.fault_vector==(fits?0:12) && !r.fault_error);
            if(!fits) assert(!f.locks);
            ++total;
        }
        cpu.ops.destroy(cpu.context);
    }
    printf("protected calls: %u exhaustive current/new/source stack bounds\n",total);
}

static void roundtrips(void)
{
    unsigned total=0;
    for(unsigned cpl=1;cpl<4;++cpl) for(unsigned target=0;target<cpl;++target)
    for(unsigned odd=0;odd<2;++odd) for(unsigned count=0;count<256;++count) {
        fixture_t f; bm_cpu_t cpu=setup(&f,cpl,target,odd,count,true,true,(count&1u)!=0);
        bm_286_arch_state_t before=get(&cpu),a; unsigned words=count&31u;
        bm_286_boundary_t b=step(&cpu); a=get(&cpu);
        assert(b.kind==BM_286_BOUNDARY_INSTRUCTION && a.cpl==target && a.ip==0x200 && a.sp==0x800-8-2*words);
        assert(getword(&f,a.ss.base+a.sp)==0x106-(count&1u));
        assert(getword(&f,a.ss.base+a.sp+2)==before.cs.selector);
        for(unsigned i=0;i<words;++i) assert(getword(&f,a.ss.base+a.sp+4+2*i)==0xa500+i);
        assert(getword(&f,a.ss.base+0x7fc)==before.sp && getword(&f,a.ss.base+0x7fe)==before.ss.selector);
        assert(a.flags==before.flags && a.nmi_blocked && a.ds.selector==before.ds.selector);
        assert(getword(&f,a.tr.base+2+4*target)==0x800 && getword(&f,a.tr.base+4+4*target)==32+target);
        f.calls=f.effects=0;
        step(&cpu); a=get(&cpu);
        assert(a.cpl==cpl && a.cs.selector==before.cs.selector && a.ip==0x106-(count&1u));
        assert(a.sp==(uint16_t)(before.sp+2*words) && a.ss.selector==before.ss.selector && a.flags==before.flags);
        step(&cpu); assert(get(&cpu).ip==0x107-(count&1u));
        cpu.ops.destroy(cpu.context); ++total;
    }
    printf("protected calls: %u executed inner CALL/RETF roundtrips, all count/reserved bits\n",total);
}

static void host_failures(void)
{
    const bm_status_t errors[]={BM_STATUS_IDLE,BM_STATUS_UNSUPPORTED,BM_STATUS_INVALID_ARGUMENT,
        BM_STATUS_INVALID_STATE,BM_STATUS_DEVICE_ERROR};
    unsigned total=0;
    for(unsigned route=0;route<8;++route) for(unsigned odd=0;odd<2;++odd) {
        bool call=route<6,gate=route>=2,indirect=(route&1u)!=0;
        unsigned target=route>=4 && route<6?0:3,count=route==4?0:31;
        fixture_t f; bm_cpu_t cpu=setup(&f,3,target,odd,count,gate,call,indirect);
        step(&cpu); unsigned calls=f.calls; cpu.ops.destroy(cpu.context);
        for(unsigned fail=1;fail<=calls;++fail) for(unsigned phase=0;phase<2;++phase)
        for(unsigned e=0;e<sizeof(errors)/sizeof(errors[0]);++e) {
            bm_286_arch_state_t before,after; bm_286_boundary_t b;
            uint8_t expected[131072];
            cpu=setup(&f,3,target,odd,count,gate,call,indirect); before=get(&cpu);
            memcpy(expected,f.ram,sizeof(expected));
            f.fail=fail; f.after=phase!=0; f.failure=errors[e]; f.cpu=&cpu; f.nmi_at=fail;
            assert(bm_286_pm_step_subset(&cpu,&b)==errors[e]);
            after=get(&cpu); before.nmi_pending=1; same(&before,&after);
            assert(f.calls==fail && f.effects==fail-1+phase);
            for(unsigned t=0;t<f.effects;++t) if(f.trace[t].operation==BM_BUS_WRITE)
                for(unsigned i=0;i<f.trace[t].size;++i)
                    expected[(unsigned)(f.trace[t].address+i)&131071u]=(uint8_t)(f.trace[t].value>>(8*i));
            assert(memcmp(expected,f.ram,sizeof(expected))==0 && !f.locked && f.locks==f.unlocks);
            assert(bm_286_pm_step_subset(&cpu,&b)==BM_STATUS_INVALID_STATE && f.calls==fail);
            assert(bm_286_step(&cpu,&b)==BM_STATUS_INVALID_STATE && f.calls==fail);
            cpu.ops.destroy(cpu.context); ++total;
        }
    }
    printf("protected calls: %u before/after every-transfer host failures with NMI and no replay\n",total);
}

static void guest_repair(void)
{
    unsigned total=0;
    for(unsigned cpl=1;cpl<4;++cpl) for(unsigned target=0;target<cpl;++target)
    for(unsigned odd=0;odd<2;++odd) for(unsigned cause=0;cause<4;++cause) {
        fixture_t f; bm_cpu_t cpu=setup(&f,cpl,target,odd,2,true,true,false);
        bm_286_arch_state_t initial=get(&cpu),a; bm_286_boundary_t b;
        uint8_t repair[]={0x36,0xc6,0x06,0x25,0x20,(uint8_t)(0x92|(target<<5)),0x83,0xc4,2,0xcf};
        unsigned vector=12,error=32;
        if(cause==0) f.ram[initial.gdtr.base+37]&=0x7f;
        if(cause==1) {
            word(&f,initial.tr.base+4+4*target,32+cpl); vector=10;
            repair[3]=(uint8_t)(4+4*target); repair[4]=0x90; repair[5]=(uint8_t)(32+target);
        }
        if(cause==2) {
            initial.sp=0xffff; error=0;
            repair[3]=0x34; repair[5]=0; /* The handler removes the need to copy arguments. */
            word(&f,0x5201,0);
        }
        if(cause==3) {
            f.ram[initial.gdtr.base+53]=0x64; vector=11; error=48;
            repair[3]=0x35; repair[5]=0xe4;
        }
        memcpy(f.ram+0x3400,repair,sizeof(repair));
        assert(bm_286_set_arch_state(&cpu,&initial)==BM_STATUS_OK);
        /* Guest owns all repairs after the first fetch. No fixture patches/import. */
        b=step(&cpu); a=get(&cpu);
        assert(b.kind==BM_286_BOUNDARY_EXCEPTION && b.vector==vector && a.cpl==cpl);
        assert(getword(&f,a.ss.base+a.sp)==error && getword(&f,a.ss.base+a.sp+2)==0x100);
        step(&cpu); step(&cpu); step(&cpu); a=get(&cpu);
        assert(a.ip==0x100 && a.sp==initial.sp && a.cpl==cpl);
        step(&cpu); a=get(&cpu); assert(a.ip==0x200 && a.cpl==target);
        step(&cpu); a=get(&cpu);
        assert(a.ip==0x106 && a.cpl==cpl && a.sp==(uint16_t)(initial.sp+(cause==2?0:4)));
        step(&cpu); assert(get(&cpu).ip==0x107);
        cpu.ops.destroy(cpu.context); ++total;
    }
    printf("protected calls: %u guest fault/repair/IRET/retry/inner CALL/outer RETF programs\n",total);
}

static void overlap(void)
{
    unsigned total=0;
    for(unsigned odd=0;odd<2;++odd) for(unsigned count=0;count<32;++count)
    for(unsigned displacement=0;displacement<81;++displacement) {
        fixture_t f; bm_cpu_t cpu=setup(&f,3,0,odd,count,true,true,false);
        bm_286_arch_state_t a=get(&cpu); bm_286_segment_load_result_t r;
        uint8_t expected[256]; unsigned origin=0x8000-128;
        unsigned newsp=0x8000+displacement-40,at=newsp-origin;
        descriptor(&f,a.gdtr.base+32,odd,0x92); word(&f,a.tr.base+2,newsp);
        memcpy(expected,f.ram+origin+odd,sizeof(expected));
#define PUT(v) do { unsigned w=(v); at-=2; expected[at]=(uint8_t)w; expected[at+1]=(uint8_t)(w>>8); } while(0)
        PUT(a.ss.selector); PUT(a.sp);
        for(unsigned n=count;n>0;--n) {
            unsigned src=128+2*(n-1),v=expected[src]|((unsigned)expected[src+1]<<8);
            PUT(v);
        }
        PUT(a.cs.selector); PUT(0x106);
#undef PUT
        assert(transfer(&a,&f,true,51,&r)==BM_STATUS_OK && r.loaded);
        assert(memcmp(expected,f.ram+origin+odd,sizeof(expected))==0);
        assert(a.sp==newsp-8-2*count && a.cpl==0);
        cpu.ops.destroy(cpu.context); ++total;
    }
    printf("protected calls: %u aliased-stack copies with independent byte simulation\n",total);
}

static void priority_and_gaps(void)
{
    unsigned total=0;
    for(unsigned odd=0;odd<2;++odd) for(unsigned cause=0;cause<15;++cause) {
        fixture_t f; bm_cpu_t cpu=setup(&f,3,0,odd,31,true,true,false);
        bm_286_arch_state_t a=get(&cpu),before; bm_286_segment_load_result_t r;
        unsigned vector=13,error=24,selector=51;
        switch(cause) {
        case 0: selector=3; error=0; a.gdtr.limit=0; break;
        case 1: a.gdtr.limit=54; error=48; break;
        case 2: f.ram[a.gdtr.base+53]=0x44; error=48; break; /* Gate DPL before P. */
        case 3: f.ram[a.gdtr.base+53]=0x64; vector=11; error=48; break;
        case 4: word(&f,a.gdtr.base+50,3); error=0; break;
        case 5: word(&f,a.gdtr.base+50,7); error=4; break;
        case 6: f.ram[a.gdtr.base+29]=0x12; break;
        case 7: f.ram[a.gdtr.base+29]=0x1a; vector=11; a.tr.valid=0; break;
        case 8: word(&f,a.tr.base+4,0); vector=10; error=0; break;
        case 9: word(&f,a.tr.base+4,7); vector=10; error=4; break;
        case 10: f.ram[a.gdtr.base+37]=0x32; vector=10; error=32; break;
        case 11: f.ram[a.gdtr.base+37]=0x12; vector=12; error=32; word(&f,a.gdtr.base+24,0); break;
        case 12: word(&f,a.tr.base+2,69); vector=12; error=0; word(&f,a.gdtr.base+24,0); break;
        case 13: a.sp=0xffff; vector=12; error=0; word(&f,a.gdtr.base+24,0); break;
        case 14: word(&f,a.gdtr.base+24,0); error=0; break;
        }
        before=a;
        assert(transfer(&a,&f,true,selector,&r)==BM_STATUS_OK && !r.loaded);
        assert(r.fault_vector==vector && r.fault_error==error && !f.locks);
        same(&a,&before); cpu.ops.destroy(cpu.context); ++total;
    }
    for(unsigned cause=0;cause<6;++cause) {
        fixture_t f; bm_cpu_t cpu=setup(&f,3,0,0,0,true,true,false);
        bm_286_arch_state_t a=get(&cpu),before; bm_286_segment_load_result_t r;
        if(cause==0) a.tr.valid=0;
        if(cause==1) a.tr.limit=12;
        if(cause==2) a.tr.selector=0;
        if(cause==3) a.tr.selector=4;
        if(cause==4) a.tr.access=0x81;
        if(cause==5) a.tr.base=0x1000000;
        before=a;
        if(cause<2) {
            /* E2b: absent TR is #TS; a short TSS with the whole CPL0 slot is usable. */
            assert(transfer(&a,&f,true,51,&r)==BM_STATUS_OK);
            if(cause==0) { assert(r.fault_vector==10 && r.fault_error==56); same(&a,&before); }
            else assert(r.loaded && a.cpl==0 && a.sp==0x7f8);
            cpu.ops.destroy(cpu.context); continue;
        }
        assert(transfer(&a,&f,true,51,&r)==BM_STATUS_INVALID_STATE);
        same(&a,&before); assert(!r.loaded && !r.fault_vector && !f.locks);
        cpu.ops.destroy(cpu.context);
    }
    printf("protected calls: %u competing faults; 2 absent/short TR cases and 4 invalid host cache rejections\n",total);
}

static void indirect_bounds_and_forms(void)
{
    unsigned total=0;
    for(unsigned call=0;call<2;++call) for(unsigned seg=0;seg<2;++seg)
    for(unsigned down=0;down<2;++down) for(unsigned at=0xfffc;at<=0xffff;++at) {
        fixture_t f; bm_cpu_t cpu=setup(&f,3,3,0,0,false,call!=0,true);
        bm_286_arch_state_t a=get(&cpu); bm_286_boundary_t b;
        uint8_t bytes[]={seg?0x36:0x3e,0xff,call?0x1e:0x2e,(uint8_t)at,(uint8_t)(at>>8)};
        bm_286_segment_state_t *s=seg?&a.ss:&a.ds;
        s->access=(uint8_t)(0xf3+4*down); s->limit=down?0x7f00:0xffff;
        code(&f,bytes,sizeof(bytes));
        word(&f,(s->base+at)&131071u,0x200);
        /* Only complete pointers are filled; rejected words need no backing. */
        if(at==0xfffc) word(&f,s->base+at+2,27);
        assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);
        b=step(&cpu);
        if(at==0xfffc) assert(b.kind==BM_286_BOUNDARY_INSTRUCTION && get(&cpu).ip==0x200);
        else {
            assert(b.kind==BM_286_BOUNDARY_EXCEPTION && b.vector==(seg?12:13));
            assert(f.trace[5].address==a.idtr.base+b.vector*8); /* No pointer read. */
        }
        cpu.ops.destroy(cpu.context); ++total;
    }
    for(unsigned call=0;call<2;++call) for(unsigned cpl=0;cpl<4;++cpl) {
        fixture_t f; bm_cpu_t cpu=setup(&f,cpl,cpl,0,0,false,call!=0,false);
        const uint8_t bytes[]={0xff,call?0xd8:0xe8};
        code(&f,bytes,sizeof(bytes)); bm_286_boundary_t b=step(&cpu);
        assert(b.vector==6 && b.kind==BM_286_BOUNDARY_EXCEPTION);
        cpu.ops.destroy(cpu.context);
    }
    for(unsigned route=0;route<4;++route) {
        fixture_t f; bm_cpu_t cpu=setup(&f,3,0,0,31,true,true,route!=0);
        bm_286_arch_state_t a=get(&cpu); a.flags|=0x3000;
        assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);
        bm_286_boundary_t b; code(&f,(const uint8_t[]){0xf0,0x9a,0,2,51,0},6);
        if(route==1) code(&f,(const uint8_t[]){0xf0,0xff,0x1e,0,6},5);
        if(route>=2) { uint64_t cycles=99;
            assert(bm_286_step_clocked(cpu.context,0,&cycles)==BM_STATUS_UNSUPPORTED && !cycles && !f.calls); }
        else assert(bm_286_pm_step_subset(&cpu,&b)==BM_STATUS_UNSUPPORTED && !f.locks);
        unsigned calls=f.calls;
        assert(bm_286_pm_step_subset(&cpu,&b)==BM_STATUS_INVALID_STATE && f.calls==calls);
        cpu.ops.destroy(cpu.context);
    }
    printf("protected calls: %u indirect complete-range cases, register #UD and retained gates\n",total);
}

static void conforming_and_physical(void)
{
    unsigned total=0;
    for(unsigned cpl=0;cpl<4;++cpl) for(unsigned dpl=0;dpl<=cpl;++dpl)
    for(unsigned gate=0;gate<2;++gate) for(unsigned call=0;call<2;++call)
    for(unsigned odd=0;odd<2;++odd) {
        fixture_t f; bm_cpu_t cpu=setup(&f,cpl,dpl,odd,31,gate!=0,call!=0,true);
        bm_286_arch_state_t a=get(&cpu); bm_286_boundary_t b;
        f.ram[a.gdtr.base+29]=(uint8_t)(0x9e|(dpl<<5));
        /* Even at lower CPL, a direct conforming operand RPL=3 is ignored. */
        word(&f,a.ss.base+0x602,gate?48+cpl:27);
        /* Exercise an LDT index-zero gate and a different (GDT) code target. */
        if(gate) {
            a.ldtr.valid=1; a.ldtr.base=0x7000+odd; a.ldtr.limit=7;
            memcpy(f.ram+a.ldtr.base,f.ram+a.gdtr.base+48,8);
            word(&f,a.ss.base+0x602,4+cpl);
        }
        a.tr.valid=0; /* Conforming and same-CPL routes never need TR. */
        f.ram[0x5200]=call?0xcb:0x90;
        assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);
        b=step(&cpu);
        assert(b.kind==BM_286_BOUNDARY_INSTRUCTION && get(&cpu).cpl==cpl && get(&cpu).cs.selector==24+cpl);
        assert(get(&cpu).sp==a.sp-4*call && get(&cpu).flags==a.flags && !get(&cpu).tr.valid);
        b=step(&cpu);
        if(get(&cpu).ip!=(call?0x105:0x201) || get(&cpu).cpl!=cpl)
            printf("conforming return cpl=%u dpl=%u gate=%u call=%u odd=%u ip=%x vector=%u\n",
                cpl,dpl,gate,call,odd,get(&cpu).ip,b.vector);
        assert(get(&cpu).ip==(call?0x105:0x201) && get(&cpu).cpl==cpl);
        if(call) { assert(get(&cpu).sp==a.sp); step(&cpu); assert(get(&cpu).ip==0x106); }
        cpu.ops.destroy(cpu.context); ++total;
    }
    {
        fixture_t f; bm_cpu_t cpu=setup(&f,3,0,1,0,true,true,false);
        bm_286_arch_state_t a=get(&cpu); bm_286_boundary_t b;
        a.tr.base=0xfffffd; /* SP word crosses the 24-bit physical boundary. */
        f.ram[131071]=0x80; f.ram[0]=0; word(&f,1,32);
        descriptor(&f,a.gdtr.base+32,0xffff81,0x92);
        descriptor(&f,a.gdtr.base+24,0xffff00,0x9a); f.ram[0x100]=0x90;
        assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);
        b=step(&cpu);
        assert(b.kind==BM_286_BOUNDARY_INSTRUCTION && get(&cpu).sp==0x78 && get(&cpu).cpl==0);
        assert(f.ram[131071]==a.ss.selector && f.ram[0]==0); /* Split SS push, no host overflow. */
        assert(getword(&f,131069)==a.sp);
        f.calls=0; b=step(&cpu);
        assert(b.instruction_address==0x100 && get(&cpu).ip==0x201);
        cpu.ops.destroy(cpu.context);
    }
    printf("protected calls: %u executed conforming/LDT gate/no-TR routes and physical 24-bit wrap\n",total);
}

int main(void)
{
    setvbuf(stdout,NULL,_IONBF,0);
    direct_matrix(); gate_matrices(); stack_ranges(); roundtrips();
    host_failures(); guest_repair(); overlap(); priority_and_gaps(); indirect_bounds_and_forms();
    conforming_and_physical();
    return 0;
}
