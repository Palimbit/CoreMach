/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 BluMach contributors
 * LTR/inner events. Fixture adapted from protected_joint. No ROM,
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
    uint8_t ram[65536];
    bm_bus_transaction_t trace[256];
    unsigned calls, effects, fail, acks, locks, unlocks, hlda, shutdown;
    bool after, locked;
    bm_status_t failure;
    bm_cpu_t *cpu;
    unsigned nmi_at, ack_fail, shutdown_changes, program_events; uint8_t irq_vector;
    unsigned mutate_at; uint8_t mutate_value;
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
    if (f->calls == f->mutate_at) f->ram[t->address & 65535u]=f->mutate_value;
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

static void pin_lock(void *context, int asserted)
{
    fixture_t *f = context;
    assert(f->locked != (asserted != 0)); f->locked = asserted != 0;
    if (asserted) ++f->locks; else ++f->unlocks;
}
static void hlda(void *context, int asserted) { ((fixture_t *)context)->hlda = (unsigned)asserted; }
static void shutdown_pin(void *context, int asserted)
{
    fixture_t *f = context; f->shutdown = (unsigned)asserted; ++f->shutdown_changes;
}
static bm_status_t ack(void *context, unsigned phase, uint8_t *vector, uint32_t *waits)
{
    fixture_t *f = context;
    assert(f->locked && phase == (f->acks & 1u)); ++f->acks;
    if (f->acks == f->ack_fail) return f->failure;
    *vector = f->irq_vector; *waits = 5;
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
    memset(f, 0, sizeof(*f)); f->irq_vector = 0x20;
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
    bm_286_config_t c={0}; c.size=sizeof(c); c.version=BM_286_CONTRACT_VERSION;
    c.access=bus; c.access_context=f; c.bus_lock=pin_lock; c.pin_context=f;
    c.interrupt_ack=ack; c.interrupt_context=f; c.shutdown=shutdown_pin;
    return c;
}
static bm_cpu_t setup(fixture_t *f,unsigned cpl,unsigned target,unsigned odd)
{
    bm_cpu_t cpu=create(f,cpl,odd); bm_286_arch_state_t a=get(&cpu);
    a.gdtr.limit=63; a.flags=0x3202;
    descriptor(f,a.gdtr.base+24,0x5000,0x9a|(target<<5));
    descriptor(f,a.gdtr.base+32,0xa000+odd,0x92|(target<<5));
    descriptor(f,a.gdtr.base+48,0x9000+odd,0x81); word(f,a.gdtr.base+48,43);
    a.tr.selector=48; a.tr.base=0x9000+odd; a.tr.limit=43; a.tr.access=0x83; a.tr.valid=1;
    for(unsigned level=0;level<3;++level) {
        word(f,a.tr.base+2+4*level,0x800); word(f,a.tr.base+4+4*level,32+level);
    }
    word(f,a.idtr.base+0x20*8,0x200); word(f,a.idtr.base+0x20*8+2,24+target);
    f->ram[a.idtr.base+0x20*8+5]=0xe6; f->ram[0x5200]=0xcf;
    code(f,(const uint8_t[]){0xcd,0x20,0x90},3);
    assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK); return cpu;
}
static bm_status_t enter(bm_286_arch_state_t *a,fixture_t *f,bool ext,bool error,
    bm_286_segment_load_result_t *r)
{
    bm_286_config_t c=config(f); bm_286_pm_event_t e={0};
    e.vector=0x20; e.return_ip=0x102; e.external=ext; e.has_error=error; e.error_code=0x1234;
    return bm_286_pm_enter_event(a,&c,&e,r);
}

static void ltr_matrices(void)
{
    unsigned total=0;
    for(unsigned ac=0;ac<256;++ac) for(unsigned cpl=0;cpl<4;++cpl)
    for(unsigned rpl=0;rpl<4;++rpl) for(unsigned local=0;local<2;++local) for(unsigned odd=0;odd<2;++odd) {
        fixture_t f; bm_cpu_t cpu=setup(&f,cpl,cpl,odd);
        bm_286_arch_state_t a=get(&cpu),before; bm_286_segment_load_result_t r;
        bm_286_config_t c=config(&f); unsigned selector=48+rpl+4*local,vector=0,error=selector&0xfffcu;
        a.flags=0x7702; a.msw=0xffff; a.tr.selector=56;
        descriptor(&f,a.gdtr.base+56,0xb000,0x83); f.ram[a.gdtr.base+53]=(uint8_t)ac;
        before=a;
        if(cpl) { vector=13; error=0; }
        else if(local || (ac&0x1fu)!=1) vector=13;
        else if(!(ac&0x80u)) vector=11;
        assert(bm_286_pm_ltr(&a,&c,(uint16_t)selector,&r)==BM_STATUS_OK);
        assert(r.fault_vector==vector && r.loaded==!vector);
        if(vector) { same(&a,&before); assert(r.fault_error==error && !f.locks); }
        else {
            assert(a.tr.selector==selector && a.tr.base==0x9000+odd && a.tr.limit==43 && a.tr.access==(ac|2u));
            assert(f.ram[a.gdtr.base+53]==(ac|2u) && f.ram[a.gdtr.base+61]==0x83);
            before.tr=a.tr; same(&a,&before); /* Includes unchanged MSW.TS/NT/LDTR. */
            assert(f.locks==1 && f.unlocks==1);
        }
        cpu.ops.destroy(cpu.context); ++total;
    }
    printf("inner: %u LTR type/presence/CPL/RPL/TI/alignment cases\n",total);
    {
        fixture_t f; bm_cpu_t cpu=setup(&f,0,0,0); bm_286_arch_state_t initial=get(&cpu);
        bm_286_config_t c=config(&f); bm_286_segment_load_result_t r;
        for(unsigned limit=0;limit<65536;++limit) {
            bm_286_arch_state_t a=initial; f.calls=f.effects=f.locks=f.unlocks=0;
            word(&f,a.gdtr.base+48,limit); f.ram[a.gdtr.base+53]=0x81;
            assert(bm_286_pm_ltr(&a,&c,51,&r)==BM_STATUS_OK && r.loaded && a.tr.limit==limit);
            assert(f.calls==6); /* Descriptor + locked byte RMW; no TSS-content access. */
        }
        cpu.ops.destroy(cpu.context);
    }
    for(unsigned odd=0;odd<2;++odd) for(unsigned ac=0;ac<256;++ac) {
        fixture_t f; bm_cpu_t cpu=setup(&f,0,0,odd); bm_286_arch_state_t a=get(&cpu),before=a;
        bm_286_config_t c=config(&f); bm_286_segment_load_result_t r;
        f.mutate_at=odd?9:5; f.mutate_value=(uint8_t)ac;
        unsigned vector=(ac&0x1fu)!=1?13:!(ac&0x80u)?11:0;
        assert(bm_286_pm_ltr(&a,&c,48,&r)==BM_STATUS_OK && r.fault_vector==vector && r.loaded==!vector);
        assert(f.locks==1 && f.unlocks==1 && !f.locked);
        if(vector) { same(&a,&before); assert(f.calls==f.mutate_at && r.fault_error==48); }
        else assert(a.tr.access==(ac|2u) && f.ram[a.gdtr.base+53]==(ac|2u));
        cpu.ops.destroy(cpu.context);
    }
    printf("inner: 65536 LTR limits and 512 locked fresh-byte availability cases\n");
}

static void ltr_instruction(void)
{
    for(unsigned cpl=0;cpl<4;++cpl) for(unsigned odd=0;odd<2;++odd) for(unsigned memory=0;memory<2;++memory) {
        fixture_t f; bm_cpu_t cpu=setup(&f,cpl,cpl,odd); bm_286_arch_state_t a=get(&cpu),b;
        const uint8_t reg[]={0x0f,0,0xd8},mem[]={0x0f,0,0x1e,0,6};
        a.ax=51; word(&f,a.ds.base+0x600,51); code(&f,memory?mem:reg,memory?5:3);
        if(cpl) a.ds.valid=0; /* CPL fault precedes a bad memory operand. */
        assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);
        bm_286_boundary_t event=step(&cpu); b=get(&cpu);
        if(cpl) { assert(event.vector==13 && b.sp==a.sp-8 && getword(&f,b.ss.base+b.sp)==0); }
        else assert(b.ip==a.ip+(memory?5:3) && b.tr.selector==51 && b.tr.valid && f.ram[a.gdtr.base+53]==0x83);
        cpu.ops.destroy(cpu.context);
    }
    for(unsigned cause=0;cause<7;++cause) {
        fixture_t f; bm_cpu_t cpu=setup(&f,0,0,0); bm_286_arch_state_t a=get(&cpu),before;
        bm_286_config_t c=config(&f); bm_286_segment_load_result_t r; unsigned selector=48,vector=13,error=48;
        if(cause<4) {selector=cause;error=0;}
        if(cause==4) a.gdtr.limit=54;
        if(cause==5) {a.msw=0;vector=6;error=0;}
        if(cause==6) {a.gdtr.base=0xffffcb; /* access byte wraps to address 0 */
            for(unsigned i=0;i<8;++i) f.ram[(0xfffffbu+i)&65535u]=f.ram[0x2030+i];
            vector=0;}
        before=a;
        assert(bm_286_pm_ltr(&a,&c,(uint16_t)selector,&r)==BM_STATUS_OK && r.fault_vector==vector);
        if(vector) {same(&a,&before);assert(r.fault_error==error && !f.calls);}
        else assert(r.loaded && f.ram[0]==0x83);
        cpu.ops.destroy(cpu.context);
    }
}

static void entry_matrices(void)
{
    unsigned total=0;
    for(unsigned cpl=1;cpl<4;++cpl) for(unsigned target=0;target<cpl;++target)
    for(unsigned ac=0;ac<256;++ac) for(unsigned rpl=0;rpl<4;++rpl)
    for(unsigned ext=0;ext<2;++ext) for(unsigned local=0;local<2;++local) {
        fixture_t f; bm_cpu_t cpu=setup(&f,cpl,target,ac&1u);
        bm_286_arch_state_t a=get(&cpu),before; bm_286_segment_load_result_t r;
        unsigned selector=(local?4:32)+rpl,vector=0;
        a.ldtr.valid=1; a.ldtr.base=0x7000; a.ldtr.limit=7;
        descriptor(&f,local?a.ldtr.base:a.gdtr.base+32,0xa000,ac);
        if(ac&4u) word(&f,local?a.ldtr.base:a.gdtr.base+32,0);
        word(&f,a.tr.base+4+4*target,selector); before=a;
        if(rpl!=target || ((ac>>5)&3u)!=target || (ac&0x1au)!=0x12u) vector=10;
        else if(!(ac&0x80u)) vector=12;
        assert(enter(&a,&f,ext!=0,true,&r)==BM_STATUS_OK && r.fault_vector==vector && r.loaded==!vector);
        if(vector) {same(&a,&before);assert(r.fault_error==((selector&0xfffcu)|ext) && !f.locks);}
        else {
            assert(a.cpl==target && a.ss.selector==selector && a.sp==0x7f4 && a.cs.selector==24+target);
            assert(getword(&f,a.ss.base+a.sp)==0x1234 && getword(&f,a.ss.base+a.sp+2)==0x102);
            assert(getword(&f,a.ss.base+a.sp+4)==before.cs.selector && getword(&f,a.ss.base+a.sp+6)==before.flags);
            assert(getword(&f,a.ss.base+a.sp+8)==before.sp && getword(&f,a.ss.base+a.sp+10)==before.ss.selector);
        }
        cpu.ops.destroy(cpu.context); ++total;
    }
    printf("inner: %u SS type/privilege/LDT/EXT/error-frame cases\n",total); total=0;
    for(unsigned ac=0;ac<256;++ac) for(unsigned cpl=0;cpl<4;++cpl)
    for(unsigned rpl=0;rpl<4;++rpl) for(unsigned ext=0;ext<2;++ext) {
        unsigned dpl=(ac>>5)&3u,vector=0;
        fixture_t f; bm_cpu_t cpu=setup(&f,cpl,dpl,ac&1u); bm_286_arch_state_t a=get(&cpu),before=a;
        bm_286_segment_load_result_t r;
        f.ram[a.gdtr.base+29]=(uint8_t)ac; word(&f,a.idtr.base+0x20*8+2,24+rpl);
        if((ac&0x18u)!=0x18u || dpl>cpl) vector=13;
        else if(!(ac&0x80u)) vector=11;
        assert(enter(&a,&f,ext!=0,false,&r)==BM_STATUS_OK && r.fault_vector==vector && r.loaded==!vector);
        if(vector) {same(&a,&before);assert(r.fault_error==(24|ext));}
        else assert(a.cpl==((ac&4u)?cpl:dpl) && a.cs.selector==24+a.cpl);
        cpu.ops.destroy(cpu.context); ++total;
    }
    printf("inner: %u target code type/CPL/RPL/EXT cases\n",total);
}

static void slot_limits(void)
{
    unsigned total=0;
    for(unsigned target=0;target<3;++target) for(unsigned route=0;route<2;++route) {
        fixture_t f; bm_cpu_t cpu=setup(&f,3,target,0); bm_286_arch_state_t initial=get(&cpu);
        bm_286_config_t c=config(&f); bm_286_segment_load_result_t r;
        word(&f,initial.gdtr.base+56,0x200); word(&f,initial.gdtr.base+58,24+target);
        f.ram[initial.gdtr.base+61]=0xe4;
        for(unsigned limit=0;limit<65536;++limit) {
            bm_286_arch_state_t a=initial; a.tr.limit=(uint16_t)limit;
            f.calls=f.effects=f.locks=f.unlocks=0;
            bm_status_t status=route?bm_286_pm_call(&a,&c,59,0,0x102,&r):enter(&a,&f,true,true,&r);
            bool fits=limit>=5+4*target;
            assert(status==BM_STATUS_OK && r.loaded==fits && r.fault_vector==(fits?0:10));
            if(!fits) assert(r.fault_error==(route?48:49) && !f.locks);
            ++total;
        }
        cpu.ops.destroy(cpu.context);
    }
    printf("inner: %u cached TSS slot-limit cases shared with CALL\n",total);
}

static void frame_ranges(void)
{
    const unsigned limits[]={0,9,11,0x7ff,0xfffe,0xffff}; unsigned total=0;
    for(unsigned down=0;down<2;++down) for(unsigned err=0;err<2;++err)
    for(unsigned li=0;li<sizeof(limits)/sizeof(limits[0]);++li) {
        fixture_t f; bm_cpu_t cpu=setup(&f,3,0,0); bm_286_arch_state_t initial=get(&cpu);
        bm_286_segment_load_result_t r;
        for(unsigned sp=0;sp<65536;++sp) {
            bm_286_arch_state_t a=initial; unsigned frame=10+2*err,top=sp?sp:65536;
            bool fits=top>=frame;
            if(fits) for(unsigned at=top-frame;at<top;++at) if(down?at<=limits[li]:at>limits[li]) {fits=false;break;}
            f.calls=f.effects=f.locks=f.unlocks=0;
            /* Large target ranges may alias tables: restore them per case. */
            descriptor(&f,a.gdtr.base+24,0x5000,0x9a);
            descriptor(&f,a.gdtr.base+32,0xa000,0x92+4*down); word(&f,a.gdtr.base+32,limits[li]);
            word(&f,a.tr.base+2,sp); word(&f,a.tr.base+4,32);
            word(&f,a.idtr.base+0x20*8,0x200); word(&f,a.idtr.base+0x20*8+2,24); f.ram[a.idtr.base+0x20*8+5]=0xe6;
            /* Old stack has no room at all; successful inner entry need not use it. */
            a.sp=1; a.ss.limit=0;
            assert(enter(&a,&f,true,err!=0,&r)==BM_STATUS_OK && r.loaded==fits);
            assert(r.fault_vector==(fits?0:12) && !r.fault_error);
            if(!fits) assert(!f.locks);
            ++total;
        }
        cpu.ops.destroy(cpu.context);
    }
    printf("inner: %u full 10/12-byte frame limits with unusable old stack\n",total);
}

static void precedence_and_wrap(void)
{
    for(unsigned cause=0;cause<13;++cause) for(unsigned ext=0;ext<2;++ext) {
        fixture_t f; bm_cpu_t cpu=setup(&f,3,0,0); bm_286_arch_state_t a=get(&cpu),before;
        bm_286_segment_load_result_t r; unsigned vector=10,error=32;
        /* Every rejection also competes with an invalid destination IP. */
        word(&f,a.gdtr.base+24,0x100);
        switch(cause) {
        case 0: a.tr.valid=0; a.tr.selector=0; error=0; break;
        case 1: a.tr.limit=4; error=48; break;
        case 2: word(&f,a.tr.base+4,0); error=0; break;
        case 3: word(&f,a.tr.base+4,4); error=4; break;
        case 4: a.gdtr.limit=38; break;
        case 5: f.ram[a.gdtr.base+37]=0x90; break;
        case 6: f.ram[a.gdtr.base+37]=0x12; vector=12; break;
        case 7: word(&f,a.tr.base+2,11); vector=12; error=0; break;
        case 8: vector=13; error=0; break;
        case 9: a.tr.limit=0; f.ram[a.gdtr.base+29]=0x1a; vector=11; error=24; break;
        case 10: a.tr.limit=0; f.ram[a.gdtr.base+29]=0x10; vector=13; error=24; break;
        case 11: a.tr.limit=0; f.ram[a.idtr.base+0x20*8+5]=0x66; vector=11; error=0x102; break;
        default: a.tr.limit=0; f.ram[a.idtr.base+0x20*8+5]=0x61; vector=13; error=0x102; break;
        }
        before=a;
        assert(enter(&a,&f,ext!=0,true,&r)==BM_STATUS_OK && r.fault_vector==vector && !r.loaded);
        /* Range/IP errors have no selector information, including EXT. */
        assert(r.fault_error==(error?error|ext:(cause==7||cause==8?0:ext)));
        same(&a,&before); assert(!f.locks);
        cpu.ops.destroy(cpu.context);
    }
    {
        fixture_t f; bm_cpu_t cpu=setup(&f,3,0,0); bm_286_arch_state_t a=get(&cpu);
        bm_286_segment_load_result_t r;
        a.tr.base=0xfffffc; word(&f,0xfffe,12); word(&f,0,32);
        descriptor(&f,a.gdtr.base+32,0xfffffb,0x92);
        assert(enter(&a,&f,false,true,&r)==BM_STATUS_OK && r.loaded && !a.sp);
        assert(a.ss.base==0xfffffb && f.ram[0xffff]==0x0b && f.ram[0]==0);
        /* CS word crosses the physical 24-bit boundary, not a segment limit. */
        bool high=false,low=false;
        for(unsigned i=0;i<f.calls;++i) {
            if(f.trace[i].operation==BM_BUS_WRITE && f.trace[i].address==0xffffff) high=true;
            if(f.trace[i].operation==BM_BUS_WRITE && !f.trace[i].address) low=true;
        }
        assert(high && low); cpu.ops.destroy(cpu.context);
    }
}

static void flags_and_origins(void)
{
    unsigned total=0;
    for(unsigned odd=0;odd<2;++odd) for(unsigned trap=0;trap<2;++trap) {
        fixture_t f; bm_cpu_t cpu=setup(&f,3,0,odd); bm_286_arch_state_t initial=get(&cpu);
        for(unsigned flags=0;flags<65536;++flags) {
            bm_286_arch_state_t a=initial; bm_286_segment_load_result_t r;
            a.flags=(uint16_t)((flags&0x7fd5u)|2u);
            f.calls=f.effects=0; f.ram[a.idtr.base+0x20*8+5]=(uint8_t)(0xe6+trap);
            assert(enter(&a,&f,true,false,&r)==BM_STATUS_OK && r.loaded);
            assert(a.flags==(uint16_t)(((flags&0x7fd5u)|2u)&~(0x4100u|(trap?0:0x200u))));
            assert(getword(&f,a.ss.base+a.sp+4)==(uint16_t)((flags&0x7fd5u)|2u));
            ++total;
        }
        cpu.ops.destroy(cpu.context);
    }
    printf("inner: %u FLAGS images across trap/interrupt gates and alignment\n",total);
    total=0;
    for(unsigned vector=0;vector<256;++vector) for(unsigned irq=0;irq<2;++irq)
    for(unsigned odd=0;odd<2;++odd) {
        fixture_t f; bm_cpu_t cpu=setup(&f,3,0,odd); bm_286_arch_state_t a=get(&cpu),after;
        bm_286_boundary_t b;
        word(&f,a.idtr.base+vector*8,0x200); word(&f,a.idtr.base+vector*8+2,24);
        f.ram[a.idtr.base+vector*8+5]=0xe6;
        code(&f,(const uint8_t[]){0xcd,(uint8_t)vector,0x90},3); f.irq_vector=(uint8_t)vector;
        if(irq) assert(cpu.ops.signal(cpu.context,BM_286_SIGNAL_INTR,1)==BM_STATUS_OK);
        b=step(&cpu); after=get(&cpu);
        assert(b.has_vector && b.vector==vector && b.kind==(irq?BM_286_BOUNDARY_INTERRUPT:BM_286_BOUNDARY_INSTRUCTION));
        assert(after.cpl==0 && after.sp==0x7f6 && after.ss.selector==32 && f.acks==2*irq);
        assert(getword(&f,after.ss.base+after.sp)==(irq?0x100:0x102));
        if(irq) {
            unsigned first=0;
            assert(cpu.ops.signal(cpu.context,BM_286_SIGNAL_INTR,0)==BM_STATUS_OK);
            for(unsigned t=0;t<f.calls;++t) if(f.trace[t].operation==BM_BUS_WRITE && f.trace[t].address>=0xa000) {
                if(first<(odd?2u:1u)) assert(f.trace[t].attributes==BM_BUS_TRANSACTION_LOCKED);
                else assert(!f.trace[t].attributes);
                ++first;
            }
            assert(first==(odd?10u:5u));
        }
        f.calls=f.effects=0; step(&cpu); after=get(&cpu);
        a.ip=(uint16_t)(irq?0x100:0x102); same(&a,&after);
        assert(!f.locked && f.locks==f.unlocks); cpu.ops.destroy(cpu.context); ++total;
    }
    printf("inner: %u software/INTA vectors, first-word LOCK and outer IRET roundtrips\n",total);
}

/* Actual decoder/delivery routes. Imported caches isolate entry semantics;
 * reset_program below establishes its own TR entirely with guest instructions. */
static bm_cpu_t route(fixture_t *f,unsigned which,unsigned odd)
{
    bm_cpu_t cpu=setup(f,which<2?0:3,0,odd); bm_286_arch_state_t a=get(&cpu);
    if(which<2) {
        a.ax=48; word(f,a.ds.base+0x600,48);
        code(f,which?(const uint8_t[]){0x0f,0,0x1e,0,6}:(const uint8_t[]){0x0f,0,0xd8},which?5:3);
    } else {
        for(unsigned v=0;v<256;++v) {
            word(f,a.idtr.base+v*8,0x200); word(f,a.idtr.base+v*8+2,24);
            f->ram[a.idtr.base+v*8+5]=0xe6;
        }
        if(which==3 || which==6 || which==8) {
            code(f,(const uint8_t[]){0x3e,0x8b,7},3); a.ds.valid=0;
        }
        if(which==4 || which==7) assert(cpu.ops.signal(cpu.context,BM_286_SIGNAL_INTR,1)==BM_STATUS_OK);
        if(which==5) a.nmi_pending=1;
        if(which==9) a.trap_pending=1;
        if(which==6 || which==7 || which==8) {
            /* Broken requested stack, but a separate CPL1 stack can enter #DF. */
            word(f,a.tr.base+4,0);
            descriptor(f,a.gdtr.base+40,0x5000,0xba);
            descriptor(f,a.gdtr.base+56,0xb000+odd,0xb2); word(f,a.tr.base+8,57);
            word(f,a.idtr.base+8*8+2,41);
            if(which==8) f->ram[a.idtr.base+8*8+5]=0x66;
        }
    }
    assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK); return cpu;
}

static void route_results(void)
{
    const unsigned vectors[]={0,0,0x20,13,0x20,2,8,8,0,1};
    for(unsigned which=0;which<10;++which) for(unsigned odd=0;odd<2;++odd) {
        fixture_t f; bm_cpu_t cpu=route(&f,which,odd); bm_286_arch_state_t before=get(&cpu),a;
        bm_286_boundary_t b=step(&cpu); a=get(&cpu);
        if(which<2) assert(a.tr.selector==48 && a.ip==0x100+(which?5:3));
        else if(which==8) {
            assert(b.kind==BM_286_BOUNDARY_SHUTDOWN && a.shutdown && a.sp==before.sp && a.cpl==3);
            assert(f.shutdown && f.shutdown_changes==1);
        } else {
            bool error=which==3 || which==6 || which==7;
            assert(b.has_vector && b.vector==vectors[which] && a.cpl==(which==6||which==7?1:0));
            assert(a.sp==0x800-(error?12:10));
            assert(getword(&f,a.ss.base+a.sp+(error?2:0))==(which==2?0x102:0x100));
            if(error) assert(!getword(&f,a.ss.base+a.sp));
        }
        assert(!f.locked && f.locks==f.unlocks); cpu.ops.destroy(cpu.context);
    }
}

static void shadows_and_shutdown(void)
{
    for(unsigned odd=0;odd<2;++odd) for(unsigned ss=0;ss<2;++ss) {
        fixture_t f; bm_cpu_t cpu=setup(&f,3,0,odd); bm_286_arch_state_t a=get(&cpu); bm_286_boundary_t b;
        a.ax=19; if(!ss) a.flags&=(uint16_t)~0x200u;
        code(&f,ss?(const uint8_t[]){0x8e,0xd0,0x90,0x90}:(const uint8_t[]){0xfb,0x90,0x90},ss?4:3);
        assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK); step(&cpu);
        assert(get(&cpu).interrupt_shadow);
        assert(cpu.ops.signal(cpu.context,BM_286_SIGNAL_INTR,1)==BM_STATUS_OK);
        assert(cpu.ops.signal(cpu.context,BM_286_SIGNAL_HOLD,1)==BM_STATUS_OK);
        f.calls=f.effects=0; assert(bm_286_pm_step_subset(&cpu,&b)==BM_STATUS_IDLE && !f.calls && !f.acks && f.hlda);
        assert(cpu.ops.signal(cpu.context,BM_286_SIGNAL_HOLD,0)==BM_STATUS_OK);
        step(&cpu); assert(!get(&cpu).interrupt_shadow && !f.acks && !f.hlda);
        f.calls=f.effects=0; b=step(&cpu); a=get(&cpu);
        assert(b.kind==BM_286_BOUNDARY_INTERRUPT && a.cpl==0 && a.sp==0x7f6 && f.acks==2);
        assert(getword(&f,a.ss.base+a.sp)==(ss?0x103:0x102)); cpu.ops.destroy(cpu.context);
    }
    for(unsigned odd=0;odd<2;++odd) for(unsigned broken=0;broken<2;++broken) {
        fixture_t f; bm_cpu_t cpu=route(&f,8,odd); bm_286_arch_state_t a=get(&cpu); bm_286_boundary_t b;
        if(!broken) word(&f,a.idtr.base+2*8+2,41);
        assert(step(&cpu).kind==BM_286_BOUNDARY_SHUTDOWN);
        assert(cpu.ops.signal(cpu.context,BM_286_SIGNAL_NMI,1)==BM_STATUS_OK);
        f.calls=f.effects=0; b=step(&cpu); a=get(&cpu);
        if(broken) assert(a.shutdown && a.nmi_blocked && b.kind==BM_286_BOUNDARY_SHUTDOWN && f.shutdown_changes==1);
        else {
            assert(!a.shutdown && a.nmi_blocked && a.cpl==1 && b.vector==2 && f.shutdown_changes==2);
            f.calls=f.effects=0; step(&cpu); a=get(&cpu);
            assert(a.cpl==3 && a.ip==0x100 && a.sp==0x8000 && !a.nmi_blocked);
        }
        assert(cpu.ops.reset(cpu.context)==BM_STATUS_OK); a=get(&cpu);
        assert(!a.tr.valid && !a.shutdown && !a.nmi_blocked && !(a.msw&1u)); cpu.ops.destroy(cpu.context);
    }
}

static void failed_memory(fixture_t *f,uint8_t expected[65536])
{
    for(unsigned t=0;t<f->effects;++t) if(f->trace[t].operation==BM_BUS_WRITE)
        for(unsigned i=0;i<f->trace[t].size;++i)
            expected[(unsigned)(f->trace[t].address+i)&65535u]=(uint8_t)(f->trace[t].value>>(8*i));
    assert(!memcmp(expected,f->ram,65536));
    assert(!f->locked && f->locks==f->unlocks);
}
static const bm_status_t failures[]={BM_STATUS_IDLE,BM_STATUS_UNSUPPORTED,
    BM_STATUS_INVALID_ARGUMENT,BM_STATUS_INVALID_STATE,BM_STATUS_DEVICE_ERROR};

static void host_failures(void)
{
    unsigned total=0,nmis=0;
    for(unsigned which=0;which<10;++which) for(unsigned odd=0;odd<2;++odd) {
        fixture_t f; bm_cpu_t cpu=route(&f,which,odd); step(&cpu); unsigned calls=f.calls;
        cpu.ops.destroy(cpu.context);
        for(unsigned at=1;at<=calls;++at) for(unsigned phase=0;phase<2;++phase)
        for(unsigned e=0;e<sizeof(failures)/sizeof(failures[0]);++e) {
            cpu=route(&f,which,odd); bm_286_arch_state_t before=get(&cpu),after; bm_286_boundary_t b;
            uint8_t expected[65536]; memcpy(expected,f.ram,sizeof(expected));
            f.fail=at; f.after=phase!=0; f.failure=failures[e];
            /* Incoming callback NMI must survive even a partial busy/A/frame write. */
            f.cpu=&cpu; f.nmi_at=at; before.nmi_pending=1;
            assert(bm_286_pm_step_subset(&cpu,&b)==failures[e]); after=get(&cpu); same(&before,&after);
            assert(f.calls==at && f.effects==at-1+phase && !f.shutdown_changes); failed_memory(&f,expected);
            unsigned acks=f.acks;
            assert(bm_286_pm_step_subset(&cpu,&b)==BM_STATUS_INVALID_STATE);
            assert(bm_286_step(&cpu,&b)==BM_STATUS_INVALID_STATE && f.calls==at && f.acks==acks);
            cpu.ops.destroy(cpu.context); ++total;
        }
        for(unsigned at=1;at<=calls;++at) {
            cpu=route(&f,which,odd); f.cpu=&cpu; f.nmi_at=at; step(&cpu);
            assert(get(&cpu).nmi_pending && !f.locked && f.locks==f.unlocks);
            cpu.ops.destroy(cpu.context); ++nmis;
        }
    }
    for(unsigned phase=1;phase<=2;++phase) for(unsigned e=0;e<sizeof(failures)/sizeof(failures[0]);++e) {
        fixture_t f; bm_cpu_t cpu=route(&f,4,0); bm_286_arch_state_t before=get(&cpu),after; bm_286_boundary_t b;
        f.ack_fail=phase; f.failure=failures[e];
        assert(bm_286_pm_step_subset(&cpu,&b)==failures[e]); after=get(&cpu); same(&before,&after);
        assert(!f.calls && f.acks==phase && !f.locked && f.locks==f.unlocks);
        assert(bm_286_pm_step_subset(&cpu,&b)==BM_STATUS_INVALID_STATE && f.acks==phase);
        cpu.ops.destroy(cpu.context); ++total;
    }
    printf("inner: %u transport failures with NMI/exact effects/no replay, %u successful callback edges\n",total,nmis);
}

/* Entirely authored RAM program, including both repair handlers. */
static bm_cpu_t reset_program(fixture_t *f,unsigned odd)
{
    bm_cpu_t cpu=create(f,0,odd); bm_286_arch_state_t a=get(&cpu);
    const uint8_t reset_jump[]={0xea,0,1,0,3};
    const uint8_t real[]={
        0xb8,0,0,0x8e,0xd8,0x8e,0xd0,0xbc,0,0x80,
        0x0f,1,0x16,0,8,0x0f,1,0x1e,6,8,
        0xb8,1,0,0x0f,1,0xf0,0xeb,0,0xea,0,2,8,0
    };
    const uint8_t supervisor[]={
        0xb8,16,0,0x8e,0xd0,0xbc,0,0x80,0xb8,24,0,0x8e,0xd8,0x8e,0xc0,
        0xb8,48,0,0x0f,0,0xd8,0x0f,0,0xcb,0x89,0x1e,2,7,
        0x68,43,0,0x68,0,0x10,0x68,2,0x32,0x68,35,0,0x68,0,2,0xcf
    };
    const uint8_t user[]={
        0xb8,67,0,0x8e,0xd8,0x8e,0xc0,0x0f,0,0xc8,0xa3,0,7,
        0xcd,0x20,0x90,0x68,0xaa,0xbb,0x68,0xcc,0xdd,0x9a,0,0,59,0,
        0xb8,0,0,0x8e,0xd8,0xa1,0,6,0x68,2,0x33,0x9d,0x90,0xcd,0x21
    };
    /* DS base=odd, so GDTR+53 is reached through offset2035 for both layouts. */
    const uint8_t np[]={0xc6,6,0x35,0x20,0x81,0x83,0xc4,2,0xcf};
    const uint8_t ts[]={0xc7,6,4,0x90,16,0,0x83,0xc4,2,0xcf};
    const uint8_t gp[]={0xb8,67,0,0x8e,0xd8,0x83,0xc4,2,0xcf};
    const uint8_t tf[]={0x36,0x81,0x26,0xfa,0x7f,0xff,0xfe,0xcf};
    const uint8_t finish[]={0xc7,6,4,7,0xde,0xc0,0xf4};
    descriptor(f,a.gdtr.base+24,odd,0x92);
    descriptor(f,a.gdtr.base+32,0x5000,0xfa);
    descriptor(f,a.gdtr.base+40,0x6000+odd,0xf2);
    descriptor(f,a.gdtr.base+48,0x9000+odd,0x01); word(f,a.gdtr.base+48,43);
    word(f,a.gdtr.base+56,0x560); word(f,a.gdtr.base+58,8);
    f->ram[a.gdtr.base+60]=2; f->ram[a.gdtr.base+61]=0xe4;
    descriptor(f,a.gdtr.base+64,odd,0xf2);
    word(f,0x9002+odd,0x8000); word(f,0x9004+odd,0);
    word(f,a.idtr.base+10*8,0x400); word(f,a.idtr.base+10*8+2,35);
    word(f,a.idtr.base+13*8,0x520); word(f,a.idtr.base+1*8,0x540);
    word(f,a.idtr.base+2*8,0x500); word(f,a.idtr.base+0x20*8,0x500);
    word(f,a.idtr.base+0x21*8,0x580);
    f->ram[a.idtr.base+0x20*8+5]=f->ram[a.idtr.base+0x21*8+5]=0xe6;
    memcpy(f->ram+0xfff0,reset_jump,sizeof(reset_jump));
    memcpy(f->ram+0x3100,real,sizeof(real)); memcpy(f->ram+0x3200,supervisor,sizeof(supervisor));
    memcpy(f->ram+0x5200,user,sizeof(user)); memcpy(f->ram+0x3400,np,sizeof(np));
    memcpy(f->ram+0x5400,ts,sizeof(ts)); memcpy(f->ram+0x3520,gp,sizeof(gp));
    memcpy(f->ram+0x3540,tf,sizeof(tf)); memcpy(f->ram+0x3580,finish,sizeof(finish));
    f->ram[0x3500]=0xcf; memcpy(f->ram+0x3560,(const uint8_t[]){0xca,4,0},3);
    word(f,0x800,71); word(f,0x802,a.gdtr.base); word(f,0x804,0);
    word(f,0x806,0x7ff); word(f,0x808,a.idtr.base); word(f,0x80a,0);
    word(f,0x600+odd,0xbeef);
    assert(cpu.ops.reset(cpu.context)==BM_STATUS_OK); f->shutdown_changes=0;
    return cpu;
}
static void program_signals(fixture_t *f,bm_cpu_t *cpu)
{
    bm_286_arch_state_t a=get(cpu);
    if(a.cpl==3 && a.ip==0x20f && !f->program_events) {
        assert(cpu->ops.signal(cpu->context,BM_286_SIGNAL_NMI,1)==BM_STATUS_OK);
        assert(cpu->ops.signal(cpu->context,BM_286_SIGNAL_INTR,1)==BM_STATUS_OK);
        f->program_events=1;
    }
    f->calls=f->effects=0;
}
static void program_boundary(fixture_t *f,bm_cpu_t *cpu,const bm_286_boundary_t *b)
{
    if(b->kind==BM_286_BOUNDARY_INTERRUPT && b->vector==0x20)
        assert(cpu->ops.signal(cpu->context,BM_286_SIGNAL_INTR,0)==BM_STATUS_OK);
    assert(!f->locked && f->locks==f->unlocks);
}
static void reset_program_audit(void)
{
    unsigned total=0;
    for(unsigned odd=0;odd<2;++odd) {
        fixture_t f; bm_cpu_t cpu=reset_program(&f,odd); bm_286_arch_state_t a;
        unsigned counts[100],boundaries=0,events=0;
        const unsigned expected[]={11,10,0x20,2,0x20,13,1,0x21};
        do {
            bm_286_boundary_t b; assert(boundaries<100);
            program_signals(&f,&cpu); b=step(&cpu); counts[boundaries++]=f.calls;
            program_boundary(&f,&cpu,&b);
            if(b.has_vector) {
                assert(events<sizeof(expected)/sizeof(expected[0]) && b.vector==expected[events]); ++events;
            }
            a=get(&cpu);
        } while(!a.halted);
        assert(events==8 && a.cpl==0 && a.ip==0x587 && a.sp==0x7ff6);
        assert(a.tr.valid && a.tr.selector==48 && a.tr.base==0x9000+odd && a.tr.access==0x83);
        assert(getword(&f,0x700+odd)==48 && getword(&f,0x702+odd)==48 && getword(&f,0x704+odd)==0xc0de);
        assert(a.ax==0xbeef && !a.trap_pending && !a.nmi_blocked && f.acks==2 && !f.shutdown_changes);
        cpu.ops.destroy(cpu.context);
        for(unsigned at=0;at<boundaries;++at) for(unsigned fail=1;fail<=counts[at];++fail)
        for(unsigned phase=0;phase<2;++phase) for(unsigned e=0;e<sizeof(failures)/sizeof(failures[0]);++e) {
            bm_286_arch_state_t before,after; bm_286_boundary_t b; uint8_t expected_ram[65536];
            cpu=reset_program(&f,odd);
            for(unsigned i=0;i<at;++i) {
                program_signals(&f,&cpu); b=step(&cpu); program_boundary(&f,&cpu,&b);
            }
            program_signals(&f,&cpu); before=get(&cpu); memcpy(expected_ram,f.ram,sizeof(expected_ram));
            f.fail=fail; f.after=phase!=0; f.failure=failures[e];
            assert(bm_286_pm_step_subset(&cpu,&b)==failures[e]); after=get(&cpu); same(&before,&after);
            assert(f.calls==fail && f.effects==fail-1+phase && !f.shutdown_changes); failed_memory(&f,expected_ram);
            unsigned acks=f.acks;
            assert(bm_286_pm_step_subset(&cpu,&b)==BM_STATUS_INVALID_STATE);
            assert(bm_286_step(&cpu,&b)==BM_STATUS_INVALID_STATE && f.calls==fail && f.acks==acks);
            cpu.ops.destroy(cpu.context); ++total;
        }
        printf("inner: reset/LTR guest repair/outer IRET/inner SS repair/IRQ/NMI/CALL/GP/TF/HLT, %u boundaries, alignment %u\n",boundaries,odd);
    }
    printf("inner: %u whole-program boundary transport failures\n",total);
}

int main(void)
{
    setvbuf(stdout,NULL,_IONBF,0);
    ltr_matrices(); ltr_instruction(); entry_matrices(); slot_limits(); frame_ranges();
    precedence_and_wrap(); flags_and_origins(); route_results(); shadows_and_shutdown(); host_failures();
    reset_program_audit();
    return 0;
}
