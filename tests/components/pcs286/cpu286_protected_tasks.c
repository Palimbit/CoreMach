/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 BluMach contributors
 * Task switches. Fixture adapted from protected_inner. No ROM,
 * external vectors, public PE entry, physical timing or machine acceptance.
 */
#include "execution_286.h"
#include "task_286.h"
#include <blumach/platforms/null_host.h>
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef struct fixture {
    uint8_t ram[65536];
    bm_bus_transaction_t trace[512];
    unsigned calls, effects, fail, acks, locks, unlocks, hlda, shutdown;
    bool after, locked;
    bm_status_t failure;
    bm_cpu_t *cpu;
    bm_286_arch_state_t *observed;
    unsigned nmi_at, ack_fail, shutdown_changes, program_events; uint8_t irq_vector;
    unsigned mutate_at; uint8_t mutate_value;
    bool chain;
} fixture_t;

static bm_status_t bus(void *context, bm_bus_transaction_t *t)
{
    fixture_t *f = context;
    unsigned i;
    if(f->chain && f->calls==512) f->calls=0; /* Bounded trace of a long diagnostic chain. */
    assert(f->calls < 512 && t->address <= 0xffffffu);
    assert(t->size == 1 || (t->size == 2 && !(t->address & 1u)));
    assert(t->alignment == t->size && !t->wait_states);
    assert(t->endianness == BM_ENDIAN_LITTLE);
    assert(t->space == (t->operation == BM_BUS_FETCH ? BM_ADDRESS_PROGRAM : BM_ADDRESS_DATA));
    assert(t->attributes == (f->locked ? BM_BUS_TRANSACTION_LOCKED : 0u));
    if(f->observed) assert(bm_286_get_arch_state(f->cpu,&f->observed[f->calls])==BM_STATUS_OK);
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
    if(f->chain && t->operation==BM_BUS_WRITE && t->address>=0xc000 &&
        t->address<0xd000 && !(t->address&63u)) {
        unsigned next=128+8*((unsigned)(t->address-0xc000)/64+1);
        f->ram[0x1052]=(uint8_t)next; f->ram[0x1053]=(uint8_t)(next>>8);
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
    f->ram[at&65535u] = (uint8_t)value; f->ram[(at + 1)&65535u] = (uint8_t)(value >> 8);
}
static uint16_t getword(fixture_t *f, unsigned at)
{
    return (uint16_t)(f->ram[at&65535u] | ((uint16_t)f->ram[(at + 1)&65535u] << 8));
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
    return c;
}
static bm_cpu_t setup(fixture_t *f,unsigned cpl,unsigned odd)
{
    bm_cpu_t cpu=create(f,0,odd); bm_286_arch_state_t a=get(&cpu);
    a.gdtr.limit=95; a.flags=0x7202;
    descriptor(f,a.gdtr.base+32,0x5000,0x9a|(cpl<<5));
    descriptor(f,a.gdtr.base+40,0x6000 + odd,0x92|(cpl<<5));
    descriptor(f,a.gdtr.base+48,0x7000 + odd,0x92|(cpl<<5));
    descriptor(f,a.gdtr.base+56,0x8000 + odd,0x82); word(f,a.gdtr.base+56,7);
    descriptor(f,a.gdtr.base+64,0x9000 + odd,0x83); word(f,a.gdtr.base+64,43);
    descriptor(f,a.gdtr.base+72,0xa000 + odd,0x81); word(f,a.gdtr.base+72,43);
    a.tr.selector=64; a.tr.base=0x9000 + odd; a.tr.limit=43; a.tr.access=0x83; a.tr.valid=1;
    for(unsigned i=0;i<22;++i) word(f,a.tr.base+2*i,0x1100 + i);
    const uint16_t values[]={0x200,0x3202,0x101,0x202,0x303,0x404,0x1000,0x606,0x707,0x808,
        (uint16_t)(48+cpl),(uint16_t)(32+cpl),(uint16_t)(40+cpl),(uint16_t)(48+cpl),56};
    for(unsigned i=0;i<15;++i) word(f,0xa00e + odd+2*i,values[i]);
    word(f,a.tr.base,72); word(f,0xa000 + odd,0xabcd);
    f->ram[0x5200]=0xcf; code(f,(const uint8_t[]){0x90},1);
    assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK); return cpu;
}
static bm_status_t run(bm_286_arch_state_t *a,fixture_t *f,unsigned kind,bool direct,
    bm_286_task_result_t *r)
{
    bm_286_task_request_t q={0}; bm_286_config_t c=config(f);
    q.kind=(bm_286_task_kind_t)kind; q.direct=direct; q.selector=72; q.return_ip=0x105;
    return bm_286_pm_switch_task(a,&c,&q,r);
}
static void transitions(void)
{
    unsigned total=0;
    for(unsigned kind=0;kind<3;++kind) for(unsigned cpl=0;cpl<4;++cpl)
    for(unsigned odd=0;odd<2;++odd) {
        fixture_t f; bm_cpu_t cpu=setup(&f,cpl,odd); bm_286_arch_state_t a=get(&cpu),before=a;
        bm_286_task_result_t r;
        if(kind==2) f.ram[a.gdtr.base+77]=0x83;
        assert(run(&a,&f,kind,true,&r)==BM_STATUS_OK && r.phase==BM_286_TASK_COMPLETE && !r.fault_vector);
        same(&a,&before); /* Mechanism never publishes a candidate on its own. */
        bm_286_arch_state_t *n=&r.candidate;
        assert(n->tr.selector==72 && n->tr.base==0xa000 + odd && n->tr.access==0x83);
        assert(n->cs.selector==32+cpl && n->cs.base==0x5000 && n->cpl==cpl && n->ip==0x200);
        assert(n->sp==0x1000 && n->ss.base==0x6000 + odd && n->ss.valid && n->ldtr.valid);
        assert(n->ax==0x101 && n->cx==0x202 && n->dx==0x303 && n->bx==0x404 && n->di==0x808);
        assert(n->flags==(kind==0?0x7202:0x3202) && n->msw==(a.msw|8));
        assert(getword(&f,a.tr.base+14)==0x105 && getword(&f,a.tr.base+16)==(kind==2?0x3202:0x7202));
        assert(getword(&f,a.tr.base+18)==a.ax && getword(&f,a.tr.base+40)==a.ds.selector);
        assert(getword(&f,a.tr.base+42)==0x1115 && getword(&f,a.tr.base+2)==0x1101);
        assert(getword(&f,0xa000 + odd)==(kind==0?64:0xabcd));
        assert(f.ram[a.gdtr.base+69]==(kind==0?0x83:0x81) && f.ram[a.gdtr.base+77]==0x83);
        assert(!f.locked && f.locks==f.unlocks); cpu.ops.destroy(cpu.context); ++total;
    }
    printf("tasks: %u full context/busy/backlink/NT transitions\n",total);
}
static void selection(void)
{
    unsigned total=0;
    for(unsigned kind=0;kind<3;++kind) for(unsigned ac=0;ac<256;++ac)
    for(unsigned cpl=0;cpl<4;++cpl) for(unsigned rpl=0;rpl<4;++rpl) for(unsigned direct=0;direct<2;++direct) {
        fixture_t f; bm_cpu_t cpu=setup(&f,0,ac&1u); bm_286_arch_state_t a=get(&cpu),before;
        bm_286_task_request_t q={0}; bm_286_task_result_t r; bm_286_config_t c=config(&f);
        a.cpl=(uint8_t)cpl; before=a; q.kind=(bm_286_task_kind_t)kind;
        q.direct=direct!=0; q.selector=(uint16_t)(72+rpl); q.return_ip=0x105;
        word(&f,a.tr.base,72+rpl); f.ram[a.gdtr.base+77]=(uint8_t)ac;
        unsigned vector=0;
        if(kind!=2 && direct && (((ac>>5)&3u)<cpl || ((ac>>5)&3u)<rpl)) vector=13;
        else if((ac&0x1fu)!=(kind==2?3u:1u)) vector=kind==2?10:13;
        else if(!(ac&0x80u)) vector=11;
        assert(bm_286_pm_switch_task(&a,&c,&q,&r)==BM_STATUS_OK && r.fault_vector==vector);
        same(&a,&before);
        if(vector) assert(r.phase==BM_286_TASK_OLD && r.fault_error==72 && !f.locks);
        else assert(r.phase==BM_286_TASK_COMPLETE && r.candidate.tr.selector==72+rpl);
        cpu.ops.destroy(cpu.context); ++total;
    }
    printf("tasks: %u target type/privilege/return-availability cases\n",total);
}
static void segment_faults(void)
{
    unsigned total=0;
    for(unsigned role=0;role<5;++role) for(unsigned ac=0;ac<256;++ac)
    for(unsigned cpl=0;cpl<4;++cpl) for(unsigned rpl=0;rpl<(role==2?1u:4u);++rpl)
    for(unsigned ext=0;ext<2;++ext) {
        fixture_t f; bm_cpu_t cpu=setup(&f,cpl,ac&1u); bm_286_arch_state_t a=get(&cpu);
        bm_286_task_request_t q={0}; bm_286_task_result_t r; bm_286_config_t c=config(&f);
        const unsigned index[]={56,40,32,48,88},slot[]={42,38,36,40,34};
        unsigned selector=index[role]+(role==2?cpl:rpl),dpl=(ac>>5)&3u;
        q.kind=BM_286_TASK_CALL; q.selector=72; q.return_ip=0x105; q.external=ext!=0;
        word(&f,0xa000 + (ac&1u)+slot[role],selector);
        descriptor(&f,a.gdtr.base+index[role],role==0?0x8000:role==2?0x5000:0x6000,ac);
        if(role==0) word(&f,a.gdtr.base+index[role],7);
        bool type=role==0?(ac&0x1fu)==2u:role==1?(ac&0x1au)==0x12u:
            role==2?(ac&0x18u)==0x18u:(ac&0x10u) && (!(ac&8u)||(ac&2u));
        bool priv=role==0 || (role==1?(rpl==cpl && dpl==cpl):
            role==2?((ac&4u)?dpl<=cpl:dpl==cpl):
            ((ac&0x1cu)==0x1cu || (dpl>=cpl && dpl>=rpl)));
        unsigned vector=!type||!priv?10:!(ac&0x80u)?(role==0?10:role==1?12:11):0;
        assert(bm_286_pm_switch_task(&a,&c,&q,&r)==BM_STATUS_OK && r.fault_vector==vector);
        assert(r.candidate.tr.selector==72 && r.candidate.ax==0x101 && r.candidate.ip==0x200);
        if(vector) {
            const bm_286_task_phase_t phases[]={BM_286_TASK_REGISTERS,BM_286_TASK_LDT,
                BM_286_TASK_STACK,BM_286_TASK_CODE,BM_286_TASK_DATA};
            assert(r.phase==phases[role] && r.fault_error==(index[role]|ext));
            bm_286_segment_state_t *bad=role==0?&r.candidate.ldtr:role==1?&r.candidate.ss:
                role==2?&r.candidate.cs:role==3?&r.candidate.ds:&r.candidate.es;
            assert(!bad->valid && bad->selector==selector);
        } else assert(r.phase==BM_286_TASK_COMPLETE);
        cpu.ops.destroy(cpu.context); ++total;
    }
    printf("tasks: %u incoming segment faults with context/phase/EXT checks\n",total);
}
static void limits_and_flags(void)
{
    for(unsigned area=0;area<2;++area) {
        fixture_t f; bm_cpu_t cpu=setup(&f,0,0); bm_286_arch_state_t initial=get(&cpu);
        for(unsigned limit=0;limit<65536;++limit) {
            bm_286_arch_state_t a=initial; bm_286_task_result_t r;
            f.calls=f.effects=f.locks=f.unlocks=0; f.ram[a.gdtr.base+77]=0x81;
            if(area) a.tr.limit=(uint16_t)limit; else word(&f,a.gdtr.base+72,limit);
            assert(run(&a,&f,0,false,&r)==BM_STATUS_OK);
            assert(r.fault_vector==(limit<(area?41u:43u)?10:0));
            if(r.fault_vector) assert(r.fault_error==72 && r.phase==BM_286_TASK_SELECTED && r.candidate.tr.selector==72);
            else assert(r.phase==BM_286_TASK_COMPLETE);
        }
        cpu.ops.destroy(cpu.context);
    }
    for(unsigned kind=0;kind<3;++kind) {
        fixture_t f; bm_cpu_t cpu=setup(&f,0,0); bm_286_arch_state_t initial=get(&cpu);
        for(unsigned flags=0;flags<65536;++flags) {
            bm_286_arch_state_t a=initial; bm_286_task_result_t r;
            f.calls=f.effects=f.locks=f.unlocks=0;
            f.ram[a.gdtr.base+69]=0x83; f.ram[a.gdtr.base+77]=(uint8_t)(kind==2?0x83:0x81);
            word(&f,0xa010,flags); a.flags=(uint16_t)((flags&0x7fd5u)|2u);
            assert(run(&a,&f,kind,false,&r)==BM_STATUS_OK && r.phase==BM_286_TASK_COMPLETE);
            unsigned expected=(flags&0x7fd5u)|2u;
            assert(getword(&f,a.tr.base+16)==(kind==2?(expected&~0x4000u):expected));
            if(kind==0) expected|=0x4000u; else if(kind==1) expected&=~0x4000u;
            assert(r.candidate.flags==expected && (r.candidate.msw&8u));
        }
        cpu.ops.destroy(cpu.context);
    }
    printf("tasks: 131072 outgoing/incoming limits and 196608 complete FLAGS cases\n");
}
static const bm_status_t failures[]={BM_STATUS_IDLE,BM_STATUS_UNSUPPORTED,
    BM_STATUS_INVALID_ARGUMENT,BM_STATUS_INVALID_STATE,BM_STATUS_DEVICE_ERROR};
static void host_failures(void)
{
    unsigned total=0;
    for(unsigned kind=0;kind<3;++kind) for(unsigned odd=0;odd<2;++odd) for(unsigned error=0;error<2;++error) {
        fixture_t f; bm_cpu_t cpu=setup(&f,0,odd); bm_286_arch_state_t a=get(&cpu); bm_286_task_result_t r;
        bm_286_task_request_t q={0}; q.kind=(bm_286_task_kind_t)kind; q.selector=72;
        q.return_ip=0x105; q.has_error=error!=0; q.error_code=0xabcd;
        bm_286_config_t c=config(&f); if(kind==2) f.ram[a.gdtr.base+77]=0x83;
        assert(bm_286_pm_switch_task(&a,&c,&q,&r)==BM_STATUS_OK); unsigned calls=f.calls;
        cpu.ops.destroy(cpu.context);
        for(unsigned at=1;at<=calls;++at) for(unsigned phase=0;phase<2;++phase)
        for(unsigned e=0;e<sizeof(failures)/sizeof(failures[0]);++e) {
            cpu=setup(&f,0,odd); a=get(&cpu); bm_286_arch_state_t before=a;
            c=config(&f); if(kind==2) f.ram[a.gdtr.base+77]=0x83;
            uint8_t expected[65536]; memcpy(expected,f.ram,sizeof(expected));
            f.fail=at; f.after=phase!=0; f.failure=failures[e];
            assert(bm_286_pm_switch_task(&a,&c,&q,&r)==failures[e]); same(&a,&before);
            assert(f.calls==at && f.effects==at-1+phase && r.waits==3u*(at-1));
            for(unsigned t=0;t<f.effects;++t) if(f.trace[t].operation==BM_BUS_WRITE)
                for(unsigned i=0;i<f.trace[t].size;++i)
                    expected[(unsigned)(f.trace[t].address+i)&65535u]=(uint8_t)(f.trace[t].value>>(8*i));
            assert(!memcmp(expected,f.ram,sizeof(expected)) && !f.locked && f.locks==f.unlocks);
            cpu.ops.destroy(cpu.context); ++total;
        }
    }
    printf("tasks: %u before/after mechanism transfer failures and exact memory effects\n",total);
}

static void executed_roundtrips(void)
{
    unsigned total=0;
    for(unsigned route=0;route<4;++route) for(unsigned odd=0;odd<2;++odd) {
        fixture_t f; bm_cpu_t cpu=setup(&f,3,odd); bm_286_arch_state_t a=get(&cpu);
        a.flags=0x3202; word(&f,a.tr.base+42,56); /* Static LDTR used when old task resumes. */
        word(&f,a.gdtr.base+80,0xffff); word(&f,a.gdtr.base+82,72); f.ram[a.gdtr.base+85]=0xe5;
        const uint8_t call[]={0x3e,0x9a,0xad,0xde,72,0},gate[]={0x36,0x9a,0xad,0xde,80,0};
        code(&f,route==0?call:route==1?gate:(const uint8_t[]){0xcd,0x20},route<2?6:2);
        word(&f,a.idtr.base+0x20*8+2,72); f.ram[a.idtr.base+0x20*8+5]=0xe5;
        if(route==3) assert(cpu.ops.signal(cpu.context,BM_286_SIGNAL_INTR,1)==BM_STATUS_OK);
        assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);
        bm_286_boundary_t b=step(&cpu); bm_286_arch_state_t n=get(&cpu);
        assert(n.tr.selector==72 && n.cpl==3 && n.ip==0x200 && n.flags==0x7202);
        assert(b.has_vector==(route>=2) && f.acks==(route==3?2u:0u));
        if(route==3) assert(cpu.ops.signal(cpu.context,BM_286_SIGNAL_INTR,0)==BM_STATUS_OK);
        f.calls=f.effects=0; b=step(&cpu); n=get(&cpu);
        assert(n.tr.selector==64 && n.cpl==0 && n.ip==(route<2?0x106:route==2?0x102:0x100));
        assert(n.flags==0x3202 && n.ax==a.ax && n.sp==a.sp && !n.nmi_blocked);
        assert(getword(&f,0xa00e + odd)==0x201 && getword(&f,0xa010 + odd)==0x3202);
        assert(f.ram[a.gdtr.base+69]==0x83 && f.ram[a.gdtr.base+77]==0x81);
        assert(!f.locked && f.locks==f.unlocks); cpu.ops.destroy(cpu.context); ++total;
    }
    printf("tasks: %u executed direct/gate/software/INTA task + NT-IRET roundtrips\n",total);
}
/* C repairs B's saved selectors/descriptor/IP, then returns to B; B returns
 * to A. Fixture writes are finished before the first executed instruction. */
static bm_cpu_t repair_setup(fixture_t *f,unsigned cause,unsigned odd)
{
    bm_cpu_t cpu=setup(f,3,odd); bm_286_arch_state_t a=get(&cpu); a.flags=0x3202;
    word(f,a.tr.base+42,56);
    descriptor(f,a.gdtr.base+88,0xb000 + odd,0x81); word(f,a.gdtr.base+88,43);
    const uint16_t values[]={0x400,0x3202,0,0,0,0,0x7800,0,0,0,24,8,16,24,56};
    for(unsigned i=0;i<15;++i) word(f,0xb00e + odd+2*i,values[i]);
    for(unsigned v=8;v<=13;++v) {
        word(f,a.idtr.base+v*8+2,88); f->ram[a.idtr.base+v*8+5]=0x85;
    }
    uint8_t handler[]={0xc7,6,0x26,0x60,43,0,0x83,0xc4,2,0xcf};
    if(cause==0) word(f,0xa026 + odd,0);
    if(cause==1) {
        word(f,0xa02a + odd,80); handler[2]=0x2a; handler[4]=56;
    }
    if(cause==2) {
        word(f,0xa00e + odd,0xffff); word(f,a.gdtr.base+32,0xfffe);
        handler[2]=0x0e; handler[4]=0; handler[5]=2;
    }
    if(cause==3 || cause==4) {
        unsigned offset=cause==3?37:53; f->ram[a.gdtr.base+offset]&=0x7f;
        const uint8_t bytefix[]={0xc6,6,(uint8_t)(0x20 + offset),0xe0,(uint8_t)(cause==3?0xfa:0xf2),0x90,0x83,0xc4,2,0xcf};
        /* Descriptor offset is 2000+offset, so DS offset E000+offset. */
        memcpy(handler,bytefix,sizeof(handler)); handler[2]=(uint8_t)offset;
    }
    memcpy(f->ram+0x3400,handler,sizeof(handler));
    code(f,(const uint8_t[]){0x3e,0x9a,0,0,72,0,0x90},7);
    assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK); return cpu;
}
static void guest_repairs(void)
{
    const unsigned vectors[]={10,10,13,11,11},errors[]={0,80,0,32,48};
    for(unsigned cause=0;cause<5;++cause) for(unsigned odd=0;odd<2;++odd) {
        fixture_t f; bm_cpu_t cpu=repair_setup(&f,cause,odd); bm_286_arch_state_t initial=get(&cpu),a;
        bm_286_boundary_t b=step(&cpu); a=get(&cpu);
        assert(b.kind==BM_286_BOUNDARY_EXCEPTION && b.vector==vectors[cause] && a.tr.selector==88);
        assert(a.sp==0x77fe && getword(&f,a.ss.base+a.sp)==errors[cause]);
        assert(getword(&f,0xb000 + odd)==72 && getword(&f,0xa000 + odd)==64);
        assert(getword(&f,0xa00e + odd)==(cause==2?0xffff:0x200));
        for(unsigned i=0;i<(cause>=3?4u:3u);++i) {f.calls=f.effects=0; step(&cpu);}
        a=get(&cpu); assert(a.tr.selector==72 && a.ip==0x200 && a.cpl==3 && (a.flags&0x4000));
        f.calls=f.effects=0; step(&cpu); a=get(&cpu);
        assert(a.tr.selector==64 && a.ip==0x106 && a.cpl==0 && a.ax==initial.ax && a.sp==initial.sp);
        assert(f.ram[a.gdtr.base+77]==0x81 && f.ram[a.gdtr.base+93]==0x81);
        cpu.ops.destroy(cpu.context);
    }
    printf("tasks: 10 new-task #TS/#NP/#GP guest repair and nested task-return programs\n");
}
static void executed_host_failures(void)
{
    unsigned total=0;
    for(unsigned cause=0;cause<5;++cause) for(unsigned odd=0;odd<2;++odd) {
        fixture_t f; bm_cpu_t cpu=repair_setup(&f,cause,odd); bm_286_arch_state_t snapshots[512];
        f.cpu=&cpu; f.observed=snapshots; step(&cpu); unsigned calls=f.calls;
        cpu.ops.destroy(cpu.context);
        for(unsigned at=1;at<=calls;++at) for(unsigned phase=0;phase<2;++phase)
        for(unsigned e=0;e<sizeof(failures)/sizeof(failures[0]);++e) {
            cpu=repair_setup(&f,cause,odd); bm_286_boundary_t b; bm_286_arch_state_t expected=snapshots[at-1],a;
            uint8_t ram[65536]; memcpy(ram,f.ram,sizeof(ram));
            f.cpu=&cpu; f.nmi_at=at; expected.nmi_pending=1;
            f.fail=at; f.after=phase!=0; f.failure=failures[e];
            assert(bm_286_pm_step_subset(&cpu,&b)==failures[e]); a=get(&cpu); same(&a,&expected);
            assert(f.calls==at && f.effects==at-1+phase && !f.shutdown_changes);
            for(unsigned t=0;t<f.effects;++t) if(f.trace[t].operation==BM_BUS_WRITE)
                for(unsigned i=0;i<f.trace[t].size;++i)
                    ram[(unsigned)(f.trace[t].address+i)&65535u]=(uint8_t)(f.trace[t].value>>(8*i));
            assert(!memcmp(ram,f.ram,sizeof(ram)) && !f.locked && f.locks==f.unlocks);
            assert(bm_286_pm_step_subset(&cpu,&b)==BM_STATUS_INVALID_STATE);
            assert(bm_286_step(&cpu,&b)==BM_STATUS_INVALID_STATE && f.calls==at);
            cpu.ops.destroy(cpu.context); ++total;
        }
    }
    printf("tasks: %u executed failures across old/new task context, callback NMI and no replay\n",total);
}
static void selector_edges(void)
{
    unsigned total=0;
    for(unsigned role=0;role<5;++role) for(unsigned selector=0;selector<104;++selector) {
        fixture_t f; bm_cpu_t cpu=setup(&f,0,selector&1); bm_286_arch_state_t a=get(&cpu);
        const unsigned slot[]={42,38,36,40,34}; bm_286_task_result_t r;
        word(&f,0xa000 + (selector&1)+slot[role],selector);
        /* CS.RPL sets the incoming CPL before SS is checked. Isolate null CS
         * from an earlier SS privilege fault in these four cases. */
        if(role==2 && selector<4) {
            word(&f,0xa026 + (selector&1),40+selector);
            f.ram[a.gdtr.base+45]=(uint8_t)(0x92|(selector<<5));
        }
        assert(run(&a,&f,0,false,&r)==BM_STATUS_OK);
        if(selector<4) {
            if(role==0 || role>=3) {
                assert(!r.fault_vector && r.phase==BM_286_TASK_COMPLETE);
                const bm_286_segment_state_t *s=role==0?&r.candidate.ldtr:role==3?&r.candidate.ds:&r.candidate.es;
                assert(!s->valid && s->selector==selector);
            } else assert(r.fault_vector==10 && r.fault_error==0);
        } else if(selector>=96 || (selector&4)) assert(r.fault_vector==10);
        cpu.ops.destroy(cpu.context); ++total;
    }
    /* All incoming segment selectors can use the newly loaded LDT. */
    for(unsigned odd=0;odd<2;++odd) {
        fixture_t f; bm_cpu_t cpu=setup(&f,3,odd); bm_286_arch_state_t a=get(&cpu); bm_286_task_result_t r;
        word(&f,a.gdtr.base+56,31);
        descriptor(&f,0x8000 + odd,0x5000,0xfa);
        descriptor(&f,0x8008 + odd,0x6000 + odd,0xf2);
        descriptor(&f,0x8010 + odd,0x7000 + odd,0xf2);
        word(&f,0xa024 + odd,7); word(&f,0xa026 + odd,15);
        word(&f,0xa028 + odd,23); word(&f,0xa022 + odd,23);
        assert(run(&a,&f,0,false,&r)==BM_STATUS_OK && r.phase==BM_286_TASK_COMPLETE);
        assert(r.candidate.cs.selector==7 && r.candidate.cs.base==0x5000);
        assert(r.candidate.ss.selector==15 && r.candidate.ds.selector==23);
        assert(f.ram[0x8005 + odd]==0xfb && f.ram[0x800d + odd]==0xf3 && f.ram[0x8015 + odd]==0xf3);
        cpu.ops.destroy(cpu.context);
    }
    /* Fresh byte under LOCK can revoke availability/presence after lookup. */
    for(unsigned kind=0;kind<3;++kind) for(unsigned ac=0;ac<256;++ac) {
        fixture_t f; bm_cpu_t cpu=setup(&f,0,0); bm_286_arch_state_t a=get(&cpu); bm_286_task_result_t r;
        if(kind==2) f.ram[a.gdtr.base+77]=0x83;
        assert(run(&a,&f,kind,false,&r)==BM_STATUS_OK);
        unsigned at=0;
        for(unsigned i=0;i<f.calls;++i) if(f.trace[i].attributes && f.trace[i].operation==BM_BUS_READ) {at=i+1; break;}
        assert(at); cpu.ops.destroy(cpu.context); cpu=setup(&f,0,0); a=get(&cpu);
        if(kind==2) f.ram[a.gdtr.base+77]=0x83;
        f.mutate_at=at; f.mutate_value=(uint8_t)ac;
        assert(run(&a,&f,kind,false,&r)==BM_STATUS_OK);
        unsigned vector=(ac&31)!=(kind==2?3u:1u)?(kind==2?10:13):!(ac&128)?11:0;
        assert(r.fault_vector==vector && !f.locked && f.locks==f.unlocks);
        if(vector) assert(r.phase==BM_286_TASK_OLD && f.ram[a.gdtr.base+77]==ac);
        else assert(r.phase==BM_286_TASK_COMPLETE);
        cpu.ops.destroy(cpu.context);
    }
    printf("tasks: %u selector edges, 2 local-table contexts and 768 locked-byte races\n",total);
}
/* Entire authored program starts at the architectural reset vector. A first
 * CALL faults in B, C repairs its SS and returns via B to A. B subsequently
 * resumes after each saved IP: IRET -> JMP A -> IRET -> IRET -> IRET. */
static bm_cpu_t reset_program(fixture_t *f,unsigned odd)
{
    bm_cpu_t cpu=repair_setup(f,0,odd); bm_286_arch_state_t a=get(&cpu);
    const uint8_t real[]={
        0xb8,0,0,0x8e,0xd8,0x8e,0xd0,0xbc,0,0x80,
        0x0f,1,0x16,0,8,0x0f,1,0x1e,6,8,
        0xb8,1,0,0x0f,1,0xf0,0xeb,0,0xea,0,2,8,0
    };
    const uint8_t supervisor[]={
        0xb8,16,0,0x8e,0xd0,0xbc,0,0x80,0xb8,24,0,0x8e,0xd8,0x8e,0xc0,
        0xb8,56,0,0x0f,0,0xd0,0xb8,64,0,0x0f,0,0xd8,
        0x68,2,0x32,0x9d,
        0x3e,0x9a,0xff,0xff,72,0,0xea,0xff,0xff,80,0,0xcd,0x20,
        0xc7,6,0,7,0xde,0xc0,0xf4
    };
    const uint8_t child[]={0xcf,0xea,0xff,0xff,64,0,0xcf,0xcf,0xcf};
    /* Both direct TSSs callable from CPL3; gate target DPL is ignored. */
    f->ram[a.gdtr.base+69]=0xe1; f->ram[a.gdtr.base+77]=0xe1;
    word(f,a.gdtr.base+80,0xffff); word(f,a.gdtr.base+82,72); f->ram[a.gdtr.base+85]=0xe5;
    for(unsigned v=0;v<3;++v) {
        unsigned vector=v==0?0x20:v==1?2:0x21;
        word(f,a.idtr.base+vector*8+2,72); f->ram[a.idtr.base+vector*8+5]=0xe5;
    }
    f->irq_vector=0x21;
    memcpy(f->ram+0xfff0,(const uint8_t[]){0xea,0,1,0,3},5);
    memcpy(f->ram+0x3100,real,sizeof(real)); memcpy(f->ram+0x3200,supervisor,sizeof(supervisor));
    memcpy(f->ram+0x5200,child,sizeof(child));
    word(f,0x800,95); word(f,0x802,a.gdtr.base); word(f,0x804,0);
    word(f,0x806,0x7ff); word(f,0x808,a.idtr.base); word(f,0x80a,0);
    assert(cpu.ops.reset(cpu.context)==BM_STATUS_OK); f->shutdown_changes=0;
    return cpu;
}
static void program_signals(fixture_t *f,bm_cpu_t *cpu)
{
    bm_286_arch_state_t a=get(cpu);
    if(a.tr.selector==64 && a.ip==0x22c && !f->program_events) {
        assert(cpu->ops.signal(cpu->context,BM_286_SIGNAL_NMI,1)==BM_STATUS_OK);
        f->program_events=1;
    } else if(a.tr.selector==64 && a.ip==0x22c && f->program_events==1 && !a.nmi_blocked) {
        /* Task gates restore IF from the TSS. Request this same handler again
         * only after NMI returns: an IRQ to its busy TSS must fault. */
        assert(cpu->ops.signal(cpu->context,BM_286_SIGNAL_INTR,1)==BM_STATUS_OK);
        f->program_events=2;
    }
    f->calls=f->effects=0;
}
static void program_boundary(fixture_t *f,bm_cpu_t *cpu,const bm_286_boundary_t *b)
{
    if(b->has_vector && b->vector==0x21)
        assert(cpu->ops.signal(cpu->context,BM_286_SIGNAL_INTR,0)==BM_STATUS_OK);
    assert(!f->locked && f->locks==f->unlocks);
}
static void reset_program_audit(void)
{
    unsigned total=0;
    for(unsigned odd=0;odd<2;++odd) {
        fixture_t f; bm_cpu_t cpu=reset_program(&f,odd); bm_286_arch_state_t a;
        unsigned counts[100],boundaries=0,events=0; const unsigned expected[]={10,0x20,2,0x21};
        do {
            assert(boundaries<100); program_signals(&f,&cpu); bm_286_boundary_t b=step(&cpu);
            counts[boundaries++]=f.calls; program_boundary(&f,&cpu,&b);
            if(b.has_vector) {
                if(events>=4 || b.vector!=expected[events]) {
                    a=get(&cpu);
                    fprintf(stderr,"reset task event %u: vector=%u boundary=%u TR=%x IP=%x SP=%x\n",
                        events,b.vector,boundaries,a.tr.selector,a.ip,a.sp);
                }
                assert(events<4 && b.vector==expected[events]); ++events;
            }
            a=get(&cpu);
        } while(!a.halted);
        assert(events==4 && a.cpl==0 && a.tr.selector==64 && a.ip==0x233 && a.sp==0x8000);
        assert(getword(&f,0x4700 + odd)==0xc0de && !a.nmi_blocked && !a.trap_pending && f.acks==2);
        assert(f.ram[0x2045 + odd]==0xe3 && f.ram[0x204d + odd]==0xe1 && f.ram[0x205d + odd]==0x81);
        assert(getword(&f,0xa00e + odd)==0x209 && !(getword(&f,0xa010 + odd)&0x4000));
        cpu.ops.destroy(cpu.context);
        for(unsigned at=0;at<boundaries;++at) {
            bm_286_arch_state_t snapshots[512]; cpu=reset_program(&f,odd);
            for(unsigned i=0;i<at;++i) {program_signals(&f,&cpu); bm_286_boundary_t b=step(&cpu); program_boundary(&f,&cpu,&b);}
            program_signals(&f,&cpu); f.cpu=&cpu; f.observed=snapshots; step(&cpu); cpu.ops.destroy(cpu.context);
            for(unsigned fail=1;fail<=counts[at];++fail) for(unsigned phase=0;phase<2;++phase)
            for(unsigned e=0;e<sizeof(failures)/sizeof(failures[0]);++e) {
                bm_286_boundary_t b; uint8_t ram[65536]; cpu=reset_program(&f,odd);
                for(unsigned i=0;i<at;++i) {program_signals(&f,&cpu); b=step(&cpu); program_boundary(&f,&cpu,&b);}
                program_signals(&f,&cpu); memcpy(ram,f.ram,sizeof(ram));
                bm_286_arch_state_t entry=get(&cpu);
                f.fail=fail; f.after=phase!=0; f.failure=failures[e];
                assert(bm_286_pm_step_subset(&cpu,&b)==failures[e]); a=get(&cpu);
                bm_286_arch_state_t before=snapshots[fail-1];
                /* Acceptance of NMI is rolled back on host failure, never lost. */
                if(entry.nmi_pending && !entry.nmi_blocked) before.nmi_pending=1;
                same(&a,&before);
                assert(f.calls==fail && f.effects==fail-1+phase && !f.locked && f.locks==f.unlocks);
                for(unsigned t=0;t<f.effects;++t) if(f.trace[t].operation==BM_BUS_WRITE)
                    for(unsigned i=0;i<f.trace[t].size;++i)
                        ram[(unsigned)(f.trace[t].address+i)&65535u]=(uint8_t)(f.trace[t].value>>(8*i));
                assert(!memcmp(ram,f.ram,sizeof(ram)));
                assert(bm_286_pm_step_subset(&cpu,&b)==BM_STATUS_INVALID_STATE);
                assert(bm_286_step(&cpu,&b)==BM_STATUS_INVALID_STATE && f.calls==fail);
                cpu.ops.destroy(cpu.context); ++total;
            }
        }
        printf("tasks: reset/LTR/CALL/#TS repair/NT-IRET/JMP/INT/NMI/IRQ/HLT, %u boundaries, alignment %u\n",boundaries,odd);
    }
    printf("tasks: %u complete reset-program transport failures\n",total);
}
static void task_stack_and_wrap(void)
{
    const unsigned limits[]={0,0xfffe,0xffff};
    for(unsigned down=0;down<2;++down) for(unsigned l=0;l<3;++l) {
        fixture_t f; bm_cpu_t cpu=setup(&f,0,0); bm_286_arch_state_t a=get(&cpu);
        bm_286_config_t c=config(&f); bm_286_task_request_t q={0}; bm_286_task_result_t r;
        q.kind=BM_286_TASK_CALL; q.selector=72; q.return_ip=0x105; q.has_error=true; q.error_code=0xace1;
        f.ram[a.gdtr.base+45]=(uint8_t)(down?0x96:0x92); word(&f,a.gdtr.base+40,limits[l]);
        /* Keep the stack away from GDT/TSS despite a complete SP sweep. */
        for(unsigned sp=0;sp<65536;++sp) {
            f.calls=f.effects=f.locks=f.unlocks=0; f.ram[a.gdtr.base+77]=0x81;
            word(&f,0xa01a,sp);
            unsigned top=sp?sp:65536;
            bool fits=top>=2 && (down?top-2>limits[l]:top-1<=limits[l]);
            /* Avoid allowing a successful push to overwrite fixture tables:
             * check rejected ranges exhaustively, accepted ranges below. */
            if(fits) continue;
            assert(bm_286_pm_switch_task(&a,&c,&q,&r)==BM_STATUS_OK);
            assert(r.fault_vector==12 && !r.fault_error && r.phase==BM_286_TASK_DATA);
        }
        cpu.ops.destroy(cpu.context);
    }
    for(unsigned down=0;down<2;++down) for(unsigned odd=0;odd<2;++odd) for(unsigned error=0;error<2;++error) {
        fixture_t f; bm_cpu_t cpu=setup(&f,0,odd); bm_286_arch_state_t a=get(&cpu);
        bm_286_config_t c=config(&f); bm_286_task_request_t q={0}; bm_286_task_result_t r;
        q.kind=BM_286_TASK_CALL; q.selector=72; q.has_error=error!=0; q.error_code=0xace1;
        f.ram[a.gdtr.base+45]=(uint8_t)(down?0x96:0x92); word(&f,a.gdtr.base+40,down?0xfffd:0xffff);
        word(&f,0xa01a + odd,0);
        assert(bm_286_pm_switch_task(&a,&c,&q,&r)==BM_STATUS_OK && r.phase==BM_286_TASK_COMPLETE);
        assert(r.candidate.sp==(error?0xfffe:0));
        if(error) assert(getword(&f,0x6000 + odd+0xfffe)==0xace1);
        cpu.ops.destroy(cpu.context);
    }
    for(unsigned part=0;part<3;++part) {
        fixture_t f; bm_cpu_t cpu=setup(&f,0,0); bm_286_arch_state_t a=get(&cpu);
        bm_286_config_t c=config(&f); bm_286_task_request_t q={0}; bm_286_task_result_t r;
        q.kind=BM_286_TASK_CALL; q.selector=72; q.return_ip=0x105;
        if(part<2) {
            unsigned source=part?0xa000:0x9000;
            for(unsigned i=0;i<44;++i) f.ram[(0xfff1 + i)&65535]=f.ram[source+i];
            if(part) {descriptor(&f,a.gdtr.base+72,0xfffff1,0x81); word(&f,a.gdtr.base+72,43);}
            else a.tr.base=0xfffff1;
        } else {
            descriptor(&f,a.gdtr.base+40,0xffffff,0x92); word(&f,0xa01a,2);
            q.has_error=true; q.error_code=0xace1;
        }
        assert(bm_286_pm_switch_task(&a,&c,&q,&r)==BM_STATUS_OK && r.phase==BM_286_TASK_COMPLETE);
        bool edge=false;
        for(unsigned i=0;i+1<f.calls;++i)
            if(f.trace[i].address==0xffffff && f.trace[i+1].address==0) edge=true;
        assert(edge); cpu.ops.destroy(cpu.context);
    }
    /* Distinct descriptors can alias the same physical TSS: saves precede
     * incoming reads, as explicitly specified by this functional bus profile. */
    fixture_t f; bm_cpu_t cpu=setup(&f,0,0); bm_286_arch_state_t a=get(&cpu); bm_286_task_result_t r;
    word(&f,a.tr.base+42,56); descriptor(&f,a.gdtr.base+72,a.tr.base,0x81); word(&f,a.gdtr.base+72,43);
    assert(run(&a,&f,0,false,&r)==BM_STATUS_OK && r.phase==BM_286_TASK_COMPLETE);
    assert(r.candidate.ip==0x105 && r.candidate.ax==a.ax && r.candidate.sp==a.sp);
    cpu.ops.destroy(cpu.context);
    puts("tasks: error-stack rejection sweeps, SP=0 success, 24-bit TSS/stack wrap and alias order");
}
static bm_cpu_t string_task(fixture_t *f,unsigned odd,unsigned nested)
{
    bm_cpu_t cpu=nested?repair_setup(f,0,odd):setup(f,0,odd); bm_286_arch_state_t a=get(&cpu);
    a.flags=0x3202; a.si=0x100; a.di=0x700; a.cx=2; a.ds.limit=0;
    word(f,a.tr.base+42,56); word(f,a.idtr.base+13*8+2,72); f->ram[a.idtr.base+13*8+5]=0x85;
    code(f,(const uint8_t[]){0xf3,0xa4},2); f->ram[0x4100 + odd]=0x42;
    /* Handler edits the suspended task, not its own SI/CX. Restoring DS on
     * task return reloads its full GDT limit. */
    const uint8_t repair[]={0x83,0x2e,0x1e,0x20,1,0x83,6,0x14,0x20,1,0x83,0xc4,2,0xcf};
    memcpy(f->ram+0x5200,repair,sizeof(repair));
    assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK); return cpu;
}
static void string_task_faults(void)
{
    unsigned total=0;
    for(unsigned odd=0;odd<2;++odd) {
        fixture_t f; bm_cpu_t cpu=string_task(&f,odd,0); bm_286_arch_state_t snapshots[512];
        f.cpu=&cpu; f.observed=snapshots; bm_286_boundary_t b=step(&cpu); unsigned calls=f.calls;
        bm_286_arch_state_t a=get(&cpu);
        assert(b.vector==13 && a.tr.selector==72 && a.si==0x707 && a.di==0x808 && a.cx==0x202);
        assert(getword(&f,0x901e + odd)==0x101 && getword(&f,0x9020 + odd)==0x700 && getword(&f,0x9014 + odd)==1);
        f.observed=NULL;
        for(unsigned i=0;i<4;++i) {f.calls=f.effects=0; step(&cpu);}
        a=get(&cpu); assert(a.tr.selector==64 && a.ip==0x100 && a.si==0x100 && a.di==0x700 && a.cx==2);
        f.calls=f.effects=0; assert(step(&cpu).kind==BM_286_BOUNDARY_REP_ITERATION);
        assert(f.ram[0x4700 + odd]==0x42); cpu.ops.destroy(cpu.context);
        for(unsigned at=1;at<=calls;++at) for(unsigned phase=0;phase<2;++phase)
        for(unsigned e=0;e<sizeof(failures)/sizeof(failures[0]);++e) {
            cpu=string_task(&f,odd,0); uint8_t ram[65536]; memcpy(ram,f.ram,sizeof(ram));
            f.fail=at; f.after=phase!=0; f.failure=failures[e];
            assert(bm_286_pm_step_subset(&cpu,&b)==failures[e]); a=get(&cpu); same(&a,&snapshots[at-1]);
            for(unsigned t=0;t<f.effects;++t) if(f.trace[t].operation==BM_BUS_WRITE)
                for(unsigned i=0;i<f.trace[t].size;++i)
                    ram[(unsigned)(f.trace[t].address+i)&65535u]=(uint8_t)(f.trace[t].value>>(8*i));
            assert(!memcmp(ram,f.ram,sizeof(ram)) && !f.locked && f.locks==f.unlocks);
            assert(bm_286_pm_step_subset(&cpu,&b)==BM_STATUS_INVALID_STATE && f.calls==at);
            cpu.ops.destroy(cpu.context); ++total;
        }
        /* A post-switch fault must not apply the outgoing correction a second
         * time when C saves B's partial context. */
        cpu=string_task(&f,odd,1); b=step(&cpu); a=get(&cpu);
        assert(b.vector==10 && a.tr.selector==88);
        assert(getword(&f,0x901e + odd)==0x101 && getword(&f,0x9014 + odd)==1);
        assert(getword(&f,0xa01e + odd)==0x707 && getword(&f,0xa014 + odd)==0x202);
        cpu.ops.destroy(cpu.context);
    }
    printf("tasks: string fault task-save/guest repair/retry and %u transport failures\n",total);
}
static void task_events(void)
{
    for(unsigned origin=0;origin<2;++origin) for(unsigned vector=0;vector<256;++vector) {
        fixture_t f; bm_cpu_t cpu=setup(&f,0,vector&1); bm_286_arch_state_t a=get(&cpu);
        a.flags=0x3202; word(&f,a.idtr.base+vector*8+2,72); f.ram[a.idtr.base+vector*8+5]=0x85;
        code(&f,(const uint8_t[]){0xcd,(uint8_t)vector},2); f.irq_vector=(uint8_t)vector;
        if(origin) assert(cpu.ops.signal(cpu.context,BM_286_SIGNAL_INTR,1)==BM_STATUS_OK);
        assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);
        bm_286_boundary_t b=step(&cpu); a=get(&cpu);
        assert(b.has_vector && b.vector==vector && a.tr.selector==72 && a.sp==0x1000);
        assert(b.kind==(origin?BM_286_BOUNDARY_INTERRUPT:BM_286_BOUNDARY_INSTRUCTION));
        assert(f.acks==(origin?2u:0u) && getword(&f,0x900e + (vector&1))==(origin?0x100:0x102));
        assert(!a.nmi_blocked && !f.locked && f.locks==f.unlocks);
        cpu.ops.destroy(cpu.context);
    }
    for(unsigned ss=0;ss<2;++ss) {
        fixture_t f; bm_cpu_t cpu=setup(&f,0,0); bm_286_arch_state_t a=get(&cpu); bm_286_boundary_t b;
        a.flags=0x3002; a.ax=16;
        code(&f,ss?(const uint8_t[]){0x8e,0xd0,0x90}:(const uint8_t[]){0xfb,0x90},ss?3:2);
        if(ss) a.flags|=0x200;
        word(&f,a.idtr.base+0x20*8+2,72); f.ram[a.idtr.base+0x20*8+5]=0x85;
        assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK); step(&cpu);
        assert(get(&cpu).interrupt_shadow);
        assert(cpu.ops.signal(cpu.context,BM_286_SIGNAL_INTR,1)==BM_STATUS_OK);
        assert(cpu.ops.signal(cpu.context,BM_286_SIGNAL_HOLD,1)==BM_STATUS_OK);
        f.calls=f.effects=0;
        assert(bm_286_pm_step_subset(&cpu,&b)==BM_STATUS_IDLE && !f.calls && !f.acks && f.hlda);
        assert(cpu.ops.signal(cpu.context,BM_286_SIGNAL_HOLD,0)==BM_STATUS_OK);
        step(&cpu); assert(!get(&cpu).interrupt_shadow && !f.acks && !f.hlda);
        f.calls=f.effects=0; b=step(&cpu); a=get(&cpu);
        assert(b.kind==BM_286_BOUNDARY_INTERRUPT && a.tr.selector==72 && f.acks==2);
        cpu.ops.destroy(cpu.context);
    }
    for(unsigned phase=1;phase<=2;++phase) for(unsigned e=0;e<sizeof(failures)/sizeof(failures[0]);++e) {
        fixture_t f; bm_cpu_t cpu=setup(&f,0,0); bm_286_arch_state_t a=get(&cpu),before=a; bm_286_boundary_t b;
        word(&f,a.idtr.base+0x20*8+2,72); f.ram[a.idtr.base+0x20*8+5]=0x85;
        assert(cpu.ops.signal(cpu.context,BM_286_SIGNAL_INTR,1)==BM_STATUS_OK);
        f.ack_fail=phase; f.failure=failures[e];
        assert(bm_286_pm_step_subset(&cpu,&b)==failures[e]); a=get(&cpu); same(&a,&before);
        assert(f.acks==phase && !f.calls && !f.locked && f.locks==f.unlocks);
        assert(bm_286_pm_step_subset(&cpu,&b)==BM_STATUS_INVALID_STATE && f.acks==phase);
        cpu.ops.destroy(cpu.context);
    }
    for(unsigned oldtf=0;oldtf<2;++oldtf) for(unsigned newtf=0;newtf<2;++newtf) {
        fixture_t f; bm_cpu_t cpu=setup(&f,0,0); bm_286_arch_state_t a=get(&cpu);
        a.flags=(uint16_t)(0x3202|(oldtf<<8)); word(&f,0xa010,0x3202|(newtf<<8));
        code(&f,(const uint8_t[]){0x9a,0,0,72,0},5); f.ram[0x5200]=0x90;
        assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK); step(&cpu); a=get(&cpu);
        assert(!a.trap_pending && a.tr.selector==72 && a.ip==0x200);
        f.calls=f.effects=0; step(&cpu); a=get(&cpu);
        assert(a.trap_pending==newtf && a.ip==0x201);
        if(newtf) {f.calls=f.effects=0; bm_286_boundary_t b=step(&cpu); assert(b.has_vector && b.vector==1);}
        cpu.ops.destroy(cpu.context);
    }
    for(unsigned bad=0;bad<2;++bad) for(unsigned recovery=0;recovery<2;++recovery) {
        fixture_t f; bm_cpu_t cpu=setup(&f,0,0); bm_286_arch_state_t a=get(&cpu);
        bm_286_config_t c=config(&f); c.shutdown=shutdown_pin;
        bm_286_pm_delivery_state_t state={0}; bm_286_pm_delivery_result_t r;
        bm_286_pm_request_t q={0}; q.source=BM_286_PM_EXCEPTION; q.vector=13; q.restart_ip=0x100;
        f.ram[a.idtr.base+13*8+5]=0; /* #GP handler fails -> #DF. */
        word(&f,a.idtr.base+8*8+2,bad?8:72); f.ram[a.idtr.base+8*8+5]=0x85;
        assert(bm_286_pm_deliver(&a,&c,&state,&q,&r)==BM_STATUS_OK && !state.stopped);
        if(!bad) {
            assert(r.entered && r.vector==8 && r.attempts==2 && a.tr.selector==72);
            assert(a.sp==0xffe && getword(&f,0x6ffe)==0 && getword(&f,0xa000)==64 && !a.shutdown);
        } else {
            assert(r.shutdown && a.shutdown && f.shutdown==1);
            q.source=BM_286_PM_BOUNDARY; a.nmi_pending=1;
            word(&f,a.idtr.base+2*8+2,recovery?72:8); f.ram[a.idtr.base+2*8+5]=0x85;
            f.calls=f.effects=0;
            assert(bm_286_pm_deliver(&a,&c,&state,&q,&r)==BM_STATUS_OK && !state.stopped);
            assert(a.nmi_blocked && !a.nmi_pending);
            if(recovery) assert(r.entered && !a.shutdown && !f.shutdown && a.tr.selector==72);
            else assert(r.shutdown && a.shutdown && f.shutdown && a.tr.selector==64);
        }
        cpu.ops.destroy(cpu.context);
    }
    /* Task selection changes exception context. Repeated invalid tasks must
     * exhaust a host diagnostic bound, never masquerade as guest shutdown.
     * The synthetic bus updates the #TS gate on each incoming backlink write. */
    fixture_t f; bm_cpu_t cpu=setup(&f,0,0); bm_286_arch_state_t a=get(&cpu);
    a.gdtr.limit=1023; word(&f,0xa026,0);
    for(unsigned i=0;i<33;++i) {
        descriptor(&f,a.gdtr.base+128+8*i,0xc000 + 64*i,0x81); word(&f,a.gdtr.base+128+8*i,43);
        const uint16_t values[]={0x400,0x3202,0,0,0,0,0x1000,0,0,0,24,8,0,24,56};
        for(unsigned j=0;j<15;++j) word(&f,0xc00e + 64*i+2*j,values[j]);
    }
    word(&f,a.idtr.base+10*8+2,128); f.ram[a.idtr.base+10*8+5]=0x85;
    word(&f,a.idtr.base+0x20*8+2,72); f.ram[a.idtr.base+0x20*8+5]=0x85;
    bm_286_config_t c=config(&f); bm_286_pm_delivery_state_t state={0}; bm_286_pm_delivery_result_t r;
    bm_286_pm_request_t q={0}; q.source=BM_286_PM_SOFTWARE; q.vector=0x20; q.next_ip=0x102;
    f.chain=true;
    assert(bm_286_pm_deliver(&a,&c,&state,&q,&r)==BM_STATUS_UNSUPPORTED);
    assert(state.stopped && r.attempts==32 && !r.entered && !r.shutdown && !a.shutdown);
    assert(a.tr.selector==368 && a.ip==0x400 && !f.locked && f.locks==f.unlocks);
    for(unsigned i=1;i<32;++i) assert(r.vectors[i]==10);
    unsigned calls=f.calls;
    assert(bm_286_pm_deliver(&a,&c,&state,&q,&r)==BM_STATUS_INVALID_STATE && f.calls==calls);
    cpu.ops.destroy(cpu.context);
    puts("tasks: 512 software/INTA vectors, SS/STI/HOLD, INTA failures, TF handoff, task #DF/shutdown/NMI recovery and 32-context host bound");
}
int main(void)
{
    setvbuf(stdout,NULL,_IONBF,0); transitions(); selection(); segment_faults();
    limits_and_flags(); host_failures(); executed_roundtrips();
    guest_repairs(); executed_host_failures(); selector_edges(); reset_program_audit();
    task_stack_and_wrap(); task_events(); string_task_faults();
    return 0;
}
