/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 BluMach contributors
 * Authored same/outer-level return tests; fixture adapted from protected_execution. No ROM,
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

static bm_status_t helper(bm_286_arch_state_t *a, fixture_t *f, bool iret,
    uint16_t discard, bm_286_segment_load_result_t *r)
{
    bm_286_config_t c = config(f);
    return iret ? bm_286_pm_iret(a,&c, 0x105,r) : bm_286_pm_retf(a,&c,discard,r);
}

static bm_cpu_t setup_return(fixture_t *f, unsigned cpl, unsigned target,
    unsigned odd, bool iret, uint16_t discard)
{
    bm_cpu_t cpu = create(f,cpl,odd); bm_286_arch_state_t a = get(&cpu);
    uint8_t bytes[] = {0x36,0xca,(uint8_t)discard,(uint8_t)(discard>>8)};
    a.gdtr.limit = 47;
    a.flags = 0x202; a.nmi_blocked = 1;
    descriptor(f,a.gdtr.base+24,0x5000,0x9a | (target<<5));
    descriptor(f,a.gdtr.base+32,0x6000+odd,0x92 | (target<<5));
    descriptor(f,a.gdtr.base+40,a.es.base,0xf3);
    a.es.selector = 43; a.es.access = 0xf3;
    word(f,a.ss.base+a.sp,0x200); word(f,a.ss.base+a.sp+2,24+target);
    if (iret) {
        bytes[1] = 0xcf; word(f,a.ss.base+a.sp+4,0x202);
        word(f,a.ss.base+a.sp+6,0x800); word(f,a.ss.base+a.sp+8,32+target);
    } else if ((uint32_t)a.sp+8u+discard <= 65536u) {
        word(f,a.ss.base+a.sp+4+discard,0x800);
        word(f,a.ss.base+a.sp+6+discard,32+target);
    }
    code(f,bytes,iret ? 2 : sizeof(bytes)); f->ram[0x5200] = 0x90;
    assert(bm_286_set_arch_state(&cpu,&a) == BM_STATUS_OK); return cpu;
}

static void selector_matrices(void)
{
    unsigned cpl,target,odd,iret,access,local,ss_rpl,total=0;
    for (cpl=0;cpl<4;++cpl) for(target=0;target<4;++target)
    for(iret=0;iret<2;++iret) for(access=0;access<256;++access) for(local=0;local<2;++local) {
        fixture_t f; bm_cpu_t cpu=setup_return(&f,cpl,target,access&1u,iret!=0,4);
        bm_286_arch_state_t a=get(&cpu),before; bm_286_segment_load_result_t r;
        unsigned selector=(local?4u:24u)+target, dpl=(access>>5)&3u, vector=0;
        bool is_code=(access&0x18u)==0x18u;
        a.ldtr.valid=1; a.ldtr.base=0x7000; a.ldtr.limit=15; a.ldtr.access=0x82;
        descriptor(&f,local?a.ldtr.base:a.gdtr.base+24,0x5000,access);
        word(&f,a.ss.base+a.sp+2,selector); before=a;
        if(target<cpl || !is_code || ((access&4u)?dpl>target:dpl!=target)) vector=13;
        else if(!(access&0x80u)) vector=11;
        assert(helper(&a,&f,iret!=0,4,&r)==BM_STATUS_OK && r.fault_vector==vector);
        assert(r.loaded==!vector);
        if(vector) { same(&a,&before); assert(r.fault_error==(selector&0xfffcu)); }
        else {
            assert(a.cpl==target && a.cs.selector==selector && a.cs.access==(access|1u) && a.ip==0x200);
            assert(a.sp==(target>cpl ? (iret?0x800u:0x804u) : (iret?0x8006u:0x8008u)));
            assert(a.nmi_blocked==!iret && a.flags==0x202);
            if(target>cpl) assert(a.ss.selector==32+target && !a.ds.valid && a.ds.selector==0 && a.es.valid);
        }
        assert(!f.locked && f.locks==f.unlocks); cpu.ops.destroy(cpu.context); ++total;
    }
    printf("protected returns: %u CS type/privilege/GDT-LDT cases\n",total); total=0;
    for(cpl=0;cpl<3;++cpl) for(target=cpl+1;target<4;++target)
    for(iret=0;iret<2;++iret) for(access=0;access<256;++access)
    for(ss_rpl=0;ss_rpl<4;++ss_rpl) for(odd=0;odd<2;++odd) {
        fixture_t f; bm_cpu_t cpu=setup_return(&f,cpl,target,odd,iret!=0,4);
        bm_286_arch_state_t a=get(&cpu),before; bm_286_segment_load_result_t r;
        unsigned vector=0;
        f.ram[a.gdtr.base+37]=(uint8_t)access;
        word(&f,a.ss.base+a.sp+(iret?8u:10u),32+ss_rpl); before=a;
        if(ss_rpl!=target || (access&0x1au)!=0x12u || ((access>>5)&3u)!=target) vector=13;
        else if(!(access&0x80u)) vector=12;
        assert(helper(&a,&f,iret!=0,4,&r)==BM_STATUS_OK && r.fault_vector==vector);
        assert(r.loaded==!vector);
        if(vector) { same(&a,&before); assert(r.fault_error==32); }
        else assert(a.cpl==target && a.ss.access==(access|1u) && a.ss.selector==32+target);
        cpu.ops.destroy(cpu.context); ++total;
    }
    printf("protected returns: %u outer SS type/privilege/presence cases\n",total);
}

/* Per-bit oracle uses the executing CPL, before the return changes it. */
static uint16_t restored(uint16_t incoming,uint16_t saved,unsigned cpl)
{
    uint16_t result=2;
    for(unsigned bit=0;bit<16;++bit) {
        unsigned source=saved;
        if(bit==1 || bit==3 || bit==5 || bit==15) continue;
        if((bit==12 || bit==13) && cpl) source=incoming;
        if(bit==9 && cpl>((incoming>>12)&3u)) source=incoming;
        if(source&(1u<<bit)) result|=(uint16_t)(1u<<bit);
    }
    return result;
}

static void outer_flags(void)
{
    unsigned cpl,target,iopl,saved,total=0;
    for(cpl=0;cpl<3;++cpl) for(target=cpl+1;target<4;++target) for(iopl=0;iopl<4;++iopl) {
        fixture_t f; bm_cpu_t cpu=setup_return(&f,cpl,target,cpl&1u,true,0);
        bm_286_arch_state_t before=get(&cpu); bm_286_segment_load_result_t r;
        before.flags=(uint16_t)(2u | (iopl<<12) | ((iopl&1u)<<9));
        for(saved=0;saved<65536;++saved) {
            bm_286_arch_state_t a=before;
            f.calls=f.effects=f.locks=f.unlocks=0;
            word(&f,a.ss.base+a.sp+4,saved);
            assert(helper(&a,&f,true,0,&r)==BM_STATUS_OK && r.loaded);
            assert(a.flags==restored(before.flags,(uint16_t)saved,cpl) && a.cpl==target && !a.nmi_blocked);
            ++total;
        }
        cpu.ops.destroy(cpu.context);
    }
    printf("protected returns: %u outer IRET FLAGS cases\n",total);
}

static void executed_returns(void)
{
    unsigned cpl,target,odd,iret,total=0;
    for(cpl=0;cpl<4;++cpl) for(target=cpl;target<4;++target)
    for(odd=0;odd<2;++odd) for(iret=0;iret<2;++iret) {
        fixture_t f; bm_cpu_t cpu=setup_return(&f,cpl,target,odd,iret!=0,4);
        bm_286_boundary_t b=step(&cpu); bm_286_arch_state_t a=get(&cpu);
        assert(getword(&f,0x8000+odd)==0x200);
        assert(b.kind==BM_286_BOUNDARY_INSTRUCTION && !b.has_vector && a.cpl==target && a.ip==0x200);
        assert(a.flags==0x202 && a.nmi_blocked==!iret);
        b=step(&cpu); assert(b.kind==BM_286_BOUNDARY_INSTRUCTION && get(&cpu).ip==0x201);
        cpu.ops.destroy(cpu.context); ++total;
    }
    printf("protected returns: %u executed same/outer returns and target fetches\n",total);
}

static bool byte_range(unsigned sp,unsigned count,unsigned limit,bool down)
{
    for(unsigned i=0;i<count;++i) {
        unsigned at=sp+i;
        if(at>65535u || (down ? at<=limit : at>limit)) return false;
    }
    return true;
}

static void frame_ranges(void)
{
    const unsigned limits[]={0,2,3,5,7,9,0x8000,0xffff};
    unsigned iret,outer,down,l,sp,total=0;
    for(iret=0;iret<2;++iret) for(outer=0;outer<2;++outer) for(down=0;down<2;++down) {
        fixture_t f; bm_cpu_t cpu=setup_return(&f,0,outer,0,iret!=0,6);
        bm_286_arch_state_t initial=get(&cpu); initial.ss.base=0x10000;
        initial.ss.access=(uint8_t)(0x93u | (down<<2));
        for(l=0;l<sizeof(limits)/sizeof(limits[0]);++l) for(sp=0;sp<65536;++sp) {
            bm_286_arch_state_t a=initial; bm_286_segment_load_result_t r;
            unsigned words[]={0x200,24+outer,0x202,0x800,32+outer,0x800,32+outer};
            bool valid=byte_range(sp+2,2,limits[l],down!=0) &&
                byte_range(sp,iret?(outer?10u:6u):(outer?14u:2u),limits[l],down!=0);
            a.sp=(uint16_t)sp; a.ss.limit=(uint16_t)limits[l];
            for(unsigned w=0;w<7;++w) if(sp+2*w+1<65536)
                word(&f,0x10000+sp+2*w,words[w]);
            f.calls=f.effects=f.locks=f.unlocks=0;
            assert(helper(&a,&f,iret!=0,6,&r)==BM_STATUS_OK);
            assert(r.loaded==valid && r.fault_vector==(valid?0:12) && !r.fault_error);
            if(!valid) assert(!f.locks);
            ++total;
        }
        cpu.ops.destroy(cpu.context);
    }
    printf("protected returns: %u all-SP frame boundary cases\n",total);
}

static void discard_and_new_sp(void)
{
    unsigned outer,discard,total=0;
    for(outer=0;outer<2;++outer) {
        fixture_t f; bm_cpu_t cpu=setup_return(&f,0,outer,0,false,0);
        bm_286_arch_state_t initial=get(&cpu); initial.ss.base=0x10000;
        for(discard=0;discard<65536;++discard) {
            bm_286_arch_state_t a=initial; bm_286_segment_load_result_t r;
            word(&f,0x18000,0x200); word(&f,0x18002,24+outer);
            if(0x8000u+discard+7u<=0xffffu) {
                word(&f,0x18004+discard,0xfff0); word(&f,0x18006+discard,32+outer);
            }
            f.calls=f.effects=f.locks=f.unlocks=0;
            assert(helper(&a,&f,false,(uint16_t)discard,&r)==BM_STATUS_OK);
            if(outer && 0x8000u+discard+7u>0xffffu)
                assert(!r.loaded && r.fault_vector==12 && !r.fault_error);
            else {
                assert(r.loaded && a.sp==(uint16_t)((outer?0xfff0u:0x8004u)+discard));
                assert(a.flags==initial.flags && a.nmi_blocked);
            }
            ++total;
        }
        cpu.ops.destroy(cpu.context);
    }
    for(unsigned iret=0;iret<2;++iret) {
        fixture_t f; bm_cpu_t cpu=setup_return(&f,0,3,0,iret!=0,6);
        bm_286_arch_state_t initial=get(&cpu); bm_286_segment_load_result_t r;
        word(&f,initial.gdtr.base+32,0); /* New stack has just byte zero. */
        for(unsigned sp=0;sp<65536;++sp) {
            bm_286_arch_state_t a=initial;
            word(&f,a.ss.base+a.sp+(iret?6u:10u),sp);
            f.calls=f.effects=f.locks=f.unlocks=0;
            assert(helper(&a,&f,iret!=0,6,&r)==BM_STATUS_OK && r.loaded);
            assert(a.ss.limit==0 && a.sp==(uint16_t)(sp+(iret?0u:6u)));
            ++total;
        }
        cpu.ops.destroy(cpu.context);
    }
    printf("protected returns: %u RETF discard/restored-SP cases\n",total);
}

static void cache_cleanup(void)
{
    unsigned iret,target,access,rpl,table,reg,total=0;
    for(iret=0;iret<2;++iret) for(target=1;target<4;++target)
    for(access=0;access<256;++access) for(rpl=0;rpl<4;++rpl) for(table=0;table<4;++table)
    for(reg=0;reg<2;++reg) {
        unsigned dpl=(access>>5)&3u;
        bool code_type=(access&0x18u)==0x18u, conform=code_type && (access&4u);
        bool data_type=(access&0x18u)==0x10u;
        if(!(access&0x80u) || !(data_type || (code_type && (access&2u))) || (!conform && rpl>dpl)) continue;
        fixture_t f; bm_cpu_t cpu=setup_return(&f,0,target,0,iret!=0,4);
        bm_286_arch_state_t a=get(&cpu); bm_286_segment_state_t ds; bm_286_segment_load_result_t r;
        bool kept=(table==0 || table==2) && (conform || dpl>=target);
        a.gdtr.limit=table==1?47:55;
        a.ldtr.valid=1; a.ldtr.base=0x7000; a.ldtr.limit=table==3?6:7;
        a.ds.selector=(uint16_t)((table>=2?4u:48u)+rpl); a.ds.access=(uint8_t)access;
        ds=a.ds;
        /* Contradictory table bytes must not replace the already loaded
         * DS cache. Only selector bounds and cached rights are consulted. */
        memset(f.ram+(table>=2?0x7000:a.gdtr.base+48),0,8);
        if(reg) { bm_286_segment_state_t temp=a.ds; a.ds=a.es; a.es=temp; }
        assert(helper(&a,&f,iret!=0,4,&r)==BM_STATUS_OK && r.loaded);
        if(reg) { bm_286_segment_state_t temp=a.ds; a.ds=a.es; a.es=temp; }
        if(kept) {
            assert(a.ds.valid && a.ds.selector==ds.selector && a.ds.base==ds.base &&
                a.ds.limit==ds.limit && a.ds.access==ds.access);
        } else assert(!a.ds.valid && !a.ds.selector && !a.ds.access && !a.ds.base && !a.ds.limit);
        assert(a.es.valid && a.es.selector==43);
        for(unsigned t=0;t<f.calls;++t) assert(f.trace[t].address < (table>=2?0x7000:a.gdtr.base+48) ||
            f.trace[t].address >= (table>=2?0x7008:a.gdtr.base+56));
        cpu.ops.destroy(cpu.context); ++total;
    }
    printf("protected returns: %u cached DS/ES cleanup/bounds/no-reload cases\n",total);
}

static void fault_precedence(void)
{
    unsigned iret,cause,total=0;
    for(iret=0;iret<2;++iret) for(cause=0;cause<14;++cause) {
        fixture_t f; bm_cpu_t cpu=setup_return(&f,1,3,0,iret!=0,4);
        bm_286_arch_state_t a=get(&cpu),before; bm_286_segment_load_result_t r;
        unsigned error=32, vector=13;
        switch(cause) {
        case 0: word(&f,a.sp+(iret?8u:10u),3); error=0; break;
        case 1: word(&f,a.sp+(iret?8u:10u),7); error=4; break;
        case 2: a.gdtr.limit=38; break;
        case 3: word(&f,a.sp+(iret?8u:10u),6); a.ldtr.valid=1; a.ldtr.base=0x7000;
                a.ldtr.limit=6; error=4; break;
        case 4: f.ram[a.gdtr.base+37]=0x12; break; /* SS privilege before presence. */
        case 5: f.ram[a.gdtr.base+37]=0x72; vector=12; break;
        case 6: f.ram[a.gdtr.base+29]=0x1a; f.ram[a.gdtr.base+37]=0x72; error=24; break;
        case 7: f.ram[a.gdtr.base+29]=0x7a; f.ram[a.gdtr.base+37]=0; vector=11; error=24; break;
        case 8: word(&f,a.gdtr.base+24,0x100); f.ram[a.gdtr.base+37]=0x72; vector=12; break;
        case 9: word(&f,a.gdtr.base+24,0x100); error=0; break;
        case 10: a.ss.limit=0x8003; word(&f,a.sp+2,24); error=24; break;
        case 11: a.ss.limit=0x8003; word(&f,a.sp+2,0); error=0; break;
        case 12: a.ss.limit=0x8003; word(&f,a.sp+2,3); vector=12; error=0; break;
        case 13: a.ss.limit=0x8002; word(&f,a.sp+2,24); vector=12; error=0; break;
        }
        before=a;
        assert(helper(&a,&f,iret!=0,4,&r)==BM_STATUS_OK && !r.loaded);
        assert(r.fault_vector==vector && r.fault_error==error);
        same(&a,&before); assert(!f.locks && !f.unlocks);
        cpu.ops.destroy(cpu.context); ++total;
    }
    printf("protected returns: %u competing selector/stack/presence/IP faults\n",total);
}

static void guest_repair(void)
{
    unsigned cpl,target,odd,iret,total=0;
    for(cpl=0;cpl<3;++cpl) for(target=cpl+1;target<4;++target)
    for(odd=0;odd<2;++odd) for(iret=0;iret<2;++iret) {
        fixture_t f; bm_cpu_t cpu=setup_return(&f,cpl,target,odd,iret!=0,4);
        bm_286_arch_state_t initial=get(&cpu),a; bm_286_boundary_t b;
        const uint8_t program[]={0x3e,0x8b,0x1e,0,6,0x90};
        uint8_t stack_repair[]={0x36,0xc6,0x06,0x25,0x20,(uint8_t)(0x92u|(target<<5)),0x83,0xc4,2,0xcf};
        const uint8_t data_repair[]={0xb8,43,0,0x8e,0xd8,0x83,0xc4,2,0xcf};
        f.ram[initial.gdtr.base+37]&=0x7f;
        word(&f,initial.idtr.base+13*8+2,24+target);
        memcpy(f.ram+0x3400,stack_repair,sizeof(stack_repair));
        memcpy(f.ram+0x5200,program,sizeof(program)); memcpy(f.ram+0x5400,data_repair,sizeof(data_repair));
        word(&f,initial.es.base+0x600,0xbeef);
        /* No fixture RAM edits or state imports after this first step. */
        b=step(&cpu); a=get(&cpu); assert(b.vector==12 && a.cpl==cpl && a.sp==0x7ff8);
        assert(getword(&f,a.ss.base+a.sp)==32 && getword(&f,a.ss.base+a.sp+2)==0x100);
        step(&cpu); step(&cpu); step(&cpu);
        a=get(&cpu); assert(a.cpl==cpl && a.ip==0x100 && a.sp==0x8000);
        b=step(&cpu); a=get(&cpu);
        assert(b.kind==BM_286_BOUNDARY_INSTRUCTION && a.cpl==target && a.ip==0x200 && !a.ds.valid);
        b=step(&cpu); a=get(&cpu); assert(b.vector==13 && a.cpl==target && a.ip==0x400);
        assert(getword(&f,a.ss.base+a.sp)==0 && getword(&f,a.ss.base+a.sp+2)==0x200);
        step(&cpu); step(&cpu); step(&cpu); step(&cpu);
        assert(get(&cpu).ip==0x200 && get(&cpu).ds.valid);
        step(&cpu); step(&cpu); a=get(&cpu);
        assert(a.cpl==target && a.ip==0x206 && a.bx==0xbeef && a.ss.selector==32+target);
        cpu.ops.destroy(cpu.context); ++total;
    }
    printf("protected returns: %u guest-only SS repair/outer return/DS fault-repair programs\n",total);
}

static void transfer_failures(void)
{
    const bm_status_t errors[]={BM_STATUS_IDLE,BM_STATUS_UNSUPPORTED,BM_STATUS_INVALID_ARGUMENT,
        BM_STATUS_INVALID_STATE,BM_STATUS_DEVICE_ERROR};
    unsigned iret,outer,odd,fail,phase,e,total=0;
    for(iret=0;iret<2;++iret) for(outer=0;outer<2;++outer) for(odd=0;odd<2;++odd) {
        fixture_t f; bm_cpu_t cpu=setup_return(&f,0,outer?3:0,odd,iret!=0,4);
        step(&cpu); unsigned calls=f.calls; cpu.ops.destroy(cpu.context);
        for(fail=1;fail<=calls;++fail) for(phase=0;phase<2;++phase)
        for(e=0;e<sizeof(errors)/sizeof(errors[0]);++e) {
            bm_286_arch_state_t before,after; bm_286_boundary_t b;
            uint8_t expected[131072];
            cpu=setup_return(&f,0,outer?3:0,odd,iret!=0,4); before=get(&cpu);
            memcpy(expected,f.ram,sizeof(expected));
            f.fail=fail; f.after=phase!=0; f.failure=errors[e];
            f.cpu=&cpu; f.nmi_at=fail; /* A new edge must survive every stopped transfer. */
            assert(bm_286_pm_step_subset(&cpu,&b)==errors[e]);
            after=get(&cpu); before.nmi_pending=1; same(&before,&after);
            assert(f.calls==fail && f.effects==fail-1+phase);
            for(unsigned t=0;t<f.effects;++t) if(f.trace[t].operation==BM_BUS_WRITE)
                for(unsigned i=0;i<f.trace[t].size;++i)
                    expected[(unsigned)(f.trace[t].address+i)&131071u]=(uint8_t)(f.trace[t].value>>(8*i));
            assert(memcmp(expected,f.ram,sizeof(expected))==0);
            assert(!f.locked && f.locks==f.unlocks);
            assert(bm_286_pm_step_subset(&cpu,&b)==BM_STATUS_INVALID_STATE && f.calls==fail);
            assert(bm_286_step(&cpu,&b)==BM_STATUS_INVALID_STATE && f.calls==fail);
            cpu.ops.destroy(cpu.context); ++total;
        }
    }
    printf("protected returns: %u before/after host failures with pending NMI, exact effects/no replay\n",total);
}

static void special_contexts(void)
{
    for(unsigned iret=0;iret<2;++iret) {
        fixture_t f; bm_cpu_t cpu=setup_return(&f,0,3,1,iret!=0,4);
        bm_286_arch_state_t a=get(&cpu),before; bm_286_segment_load_result_t r;
        /* LDT index 0 is a usable SS selector, unlike GDT index 0. */
        a.ldtr.valid=1; a.ldtr.base=0x7001; a.ldtr.limit=7;
        descriptor(&f,a.ldtr.base,0x6001,0xf2);
        word(&f,a.ss.base+a.sp+(iret?8u:10u),7);
        assert(helper(&a,&f,iret!=0,4,&r)==BM_STATUS_OK && r.loaded && a.ss.selector==7);
        cpu.ops.destroy(cpu.context);
        cpu=setup_return(&f,0,0,0,iret!=0,4); a=get(&cpu); a.flags|=0x4000; before=a;
        assert(helper(&a,&f,iret!=0,4,&r)==BM_STATUS_OK);
        if(iret) { same(&a,&before); assert(!f.calls && !r.loaded && r.fault_vector==10 && !r.fault_error); }
        else assert(r.loaded && a.flags==before.flags && a.nmi_blocked);
        cpu.ops.destroy(cpu.context);
        /* A restored SP outside the new stack is accepted. Only executing
         * the following PUSH faults; an unusable same-level #DF stack shuts down. */
        cpu=setup_return(&f,0,3,0,iret!=0,4); a=get(&cpu);
        word(&f,a.gdtr.base+32,0);
        word(&f,a.idtr.base+12*8+2,27); word(&f,a.idtr.base+8*8+2,27);
        f.ram[0x5200]=0x50;
        assert(step(&cpu).kind==BM_286_BOUNDARY_INSTRUCTION);
        a=get(&cpu); assert(a.cpl==3 && a.ss.limit==0 && a.sp>0);
        assert(step(&cpu).kind==BM_286_BOUNDARY_SHUTDOWN && get(&cpu).sp==a.sp);
        cpu.ops.destroy(cpu.context);
        /* Logical frame does not wrap; its physical bus addresses may. */
        cpu=setup_return(&f,0,3,0,iret!=0,4); a=get(&cpu);
        a.ss.base=0xffffff; a.sp=0;
        const unsigned values[]={0x200,27,0x202,0x800,35,0x800,35};
        for(unsigned w=0;w<7;++w) for(unsigned b=0;b<2;++b)
            f.ram[(a.ss.base+2*w+b)&131071u]=(uint8_t)(values[w]>>(8*b));
        /* RETF 4 saves SP at +8, SS at +10; avoid sharing IRET's slots. */
        if(!iret) for(unsigned b=0;b<2;++b) {
            f.ram[(a.ss.base+8+b)&131071u]=(uint8_t)(0x800u>>(8*b));
            f.ram[(a.ss.base+10+b)&131071u]=(uint8_t)(35u>>(8*b));
        }
        assert(helper(&a,&f,iret!=0,4,&r)==BM_STATUS_OK && r.loaded && a.cpl==3);
        bool wrapped=false;
        for(unsigned t=0;t<f.calls;++t) if(f.trace[t].address==0) wrapped=true;
        assert(wrapped); cpu.ops.destroy(cpu.context);
    }
    puts("protected returns: LDT SS, NT/RETF distinction, deferred SP fault and physical wrap");
}

int main(void)
{
    selector_matrices(); outer_flags(); executed_returns(); frame_ranges();
    discard_and_new_sp(); cache_cleanup(); transfer_failures(); fault_precedence(); guest_repair();
    special_contexts(); return 0;
}
