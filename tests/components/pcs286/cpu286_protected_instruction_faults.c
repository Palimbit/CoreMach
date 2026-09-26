/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 BluMach contributors
 * Authored PRM-profile fault and LOCK integration cases; fixture adapted from private execution tests. No ROM,
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

enum fault_site { SOURCE, DESTINATION, IOPL };
typedef struct string_case {
    uint8_t opcode, site, si_steps, di_steps, cx_restore;
} string_case_t;
/* Independent handler correction table from Intel LOADALL p14. Entries for
 * SCAS and LODS are the declared PRM-profile interpretations, not captures. */
static const string_case_t cases[] = {
    {0xa4,SOURCE,1,0,1}, {0xa4,DESTINATION,1,1,2},
    {0xa6,DESTINATION,0,1,1}, {0xa6,SOURCE,1,1,2},
    {0xaa,DESTINATION,0,1,2}, {0xac,SOURCE,0,0,0},
    {0xae,DESTINATION,0,1,2}, {0x6c,IOPL,0,1,1},
    {0x6c,DESTINATION,0,1,2}, {0x6e,IOPL,1,0,2}, {0x6e,SOURCE,1,0,2}
};
static void prepare(fixture_t *f, bm_cpu_t *cpu, unsigned which, unsigned wide,
    unsigned odd, unsigned cpl, unsigned df, unsigned rep, unsigned count,
    unsigned reason, unsigned ss, unsigned lock)
{
    const string_case_t *test=&cases[which];
    uint8_t bytes[4]; unsigned length=0;
    *cpu=create(f,cpl,odd); bm_286_arch_state_t a=get(cpu);
    if(lock) bytes[length++]=0xf0;
    if(rep) bytes[length++]=(uint8_t)rep;
    bytes[length++]=(uint8_t)(ss?0x36:reason==2 && test->site==SOURCE?0x2e:0x3e);
    bytes[length++]=(uint8_t)(test->opcode+wide); code(f,bytes,length);
    a.si=0xf000; a.di=0xe000; a.cx=(uint16_t)count; a.dx=0xffff;
    a.flags=(uint16_t)(2|(cpl<<12)|(df?0x400:0)|0x45);
    if(test->site==IOPL) {
        assert(cpl); a.flags&=(uint16_t)~0x3000u;
    } else {
        bm_286_segment_state_t *segment=test->site==DESTINATION?&a.es:ss?&a.ss:reason==2?&a.cs:&a.ds;
        unsigned at=test->site==DESTINATION?a.di:a.si;
        if(reason==0) segment->valid=0;
        if(reason==1) segment->limit=(uint16_t)(at+wide-1);
        if(reason==2) segment->access=(uint8_t)((cpl<<5)|
            (test->site==DESTINATION && (test->opcode==0xa4||test->opcode==0xaa||test->opcode==0x6c)?0x91:0x99));
    }
    assert(bm_286_set_arch_state(cpu,&a)==BM_STATUS_OK);
}
static void fault_matrix(void)
{
    static const unsigned counts[]={1,2,0xffff}; unsigned total=0;
    for(unsigned which=0;which<sizeof(cases)/sizeof(cases[0]);++which)
    for(unsigned wide=0;wide<2;++wide) for(unsigned odd=0;odd<2;++odd)
    for(unsigned cpl=0;cpl<4;++cpl) for(unsigned df=0;df<2;++df)
    for(unsigned repeat=0;repeat<3;++repeat) for(unsigned count=0;count<3;++count)
    for(unsigned reason=0;reason<3;++reason) {
        const string_case_t *test=&cases[which];
        if(test->site==IOPL && (!cpl || reason)) continue;
        /* Ordinary loads cannot create an execute-only ES cache. Read faults
         * there are null/range; source permissions use a CS override instead. */
        if(reason==2 && test->site==DESTINATION && (test->opcode==0xa6 || test->opcode==0xae)) continue;
        fixture_t f; bm_cpu_t cpu;
        unsigned rep=repeat?0xf1+repeat:0;
        prepare(&f,&cpu,which,wide,odd,cpl,df,rep,counts[count],reason,0,0);
        bm_286_arch_state_t a=get(&cpu),b; bm_286_boundary_t boundary;
        bm_status_t status=bm_286_pm_step_subset(&cpu,&boundary);
        if(status!=BM_STATUS_OK) fprintf(stderr,"string which=%u wide=%u odd=%u CPL=%u DF=%u REP=%u CX=%u reason=%u status=%u\n",
            which,wide,odd,cpl,df,repeat,counts[count],reason,(unsigned)status);
        assert(status==BM_STATUS_OK); b=get(&cpu);
        int delta=df?-(int)(wide+1):(int)(wide+1);
        assert(boundary.kind==BM_286_BOUNDARY_EXCEPTION && boundary.has_vector && boundary.vector==13);
        assert(b.si==(uint16_t)(a.si+test->si_steps*delta));
        assert(b.di==(uint16_t)(a.di+test->di_steps*delta));
        assert(b.cx==(uint16_t)(a.cx-(rep?test->cx_restore:0)) && b.ax==a.ax);
        assert(b.sp==a.sp-8 && getword(&f,b.ss.base+b.sp)==0);
        assert(getword(&f,b.ss.base+b.sp+2)==a.ip && getword(&f,b.ss.base+b.sp+6)==a.flags);
        /* Whole element preflight: no current-element memory or I/O effects. */
        assert(f.trace[2+(rep!=0)].address==a.idtr.base+13*8);
        assert(!f.locked && !f.acks); cpu.ops.destroy(cpu.context); ++total;
    }
    for(unsigned which=0;which<sizeof(cases)/sizeof(cases[0]);++which) {
        if(cases[which].site!=SOURCE) continue;
        for(unsigned wide=0;wide<2;++wide) for(unsigned odd=0;odd<2;++odd) {
            fixture_t f; bm_cpu_t cpu;
            prepare(&f,&cpu,which,wide,odd,0,0,0xf3,2,1,1,0);
            bm_286_arch_state_t a=get(&cpu),b; assert(step(&cpu).vector==12); b=get(&cpu);
            assert(getword(&f,b.ss.base+b.sp)==0 && getword(&f,b.ss.base+b.sp+2)==a.ip);
            assert(b.cx==(uint16_t)(a.cx-cases[which].cx_restore));
            cpu.ops.destroy(cpu.context); ++total;
        }
    }
    printf("%u protected string fault snapshots from documented rules/model policy\n",total);
}
static void guest_repair(void)
{
    unsigned total=0;
    for(unsigned which=0;which<sizeof(cases)/sizeof(cases[0]);++which)
    for(unsigned wide=0;wide<2;++wide) for(unsigned df=0;df<2;++df)
    for(unsigned repeat=0;repeat<2;++repeat) for(unsigned odd=0;odd<2;++odd)
    for(unsigned later=0;later<2;++later) {
        const string_case_t *test=&cases[which];
        if(test->site==IOPL) continue; /* Tested separately; same-CPL CPL3 cannot raise IOPL. */
        if(later && (!repeat || df)) continue;
        fixture_t f; bm_cpu_t cpu;
        prepare(&f,&cpu,which,wide,odd,0,df,repeat?0xf3:0,1,1,0,0);
        bm_286_arch_state_t a=get(&cpu),b;
        if(later) {
            a.si=0x600;a.di=0x700;a.cx=2;a.ax=0;
            if(test->site==SOURCE) a.ds.limit=(uint16_t)(a.si+wide);
            else a.es.limit=(uint16_t)(a.di+wide);
            assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);
        }
        /* The real guest repairs the cache and undoes documented adjustments;
         * no fixture mutation or state import after execution starts. */
        uint8_t handler[]={
            0x50,                         /* PUSH AX */
            0xb8,24,0,                    /* MOV AX, good data selector */
            0x8e,(uint8_t)(test->site==SOURCE?0xd8:0xc0),
            0x58,                         /* POP AX */
            0x81,0xee,0,0,                /* SUB SI, signed correction */
            0x81,0xef,0,0,                /* SUB DI, signed correction */
            0x81,0xc1,0,0,                /* ADD CX, correction */
            0x83,0xc4,2,0xcf};            /* Discard #GP error; IRET */
        uint16_t delta=(uint16_t)(df?-(int)(wide+1):(int)(wide+1));
        unsigned si_fix=test->si_steps*delta,di_fix=test->di_steps*delta;
        handler[9]=(uint8_t)si_fix;handler[10]=(uint8_t)(si_fix>>8);
        handler[13]=(uint8_t)di_fix;handler[14]=(uint8_t)(di_fix>>8);
        handler[17]=(uint8_t)(repeat?test->cx_restore:0);
        memcpy(f.ram+0x3400,handler,sizeof(handler));
        if(later) {assert(step(&cpu).kind==BM_286_BOUNDARY_REP_ITERATION);a=get(&cpu);assert(a.cx==1);}
        assert(step(&cpu).vector==13);
        for(unsigned n=0;n<9;++n)step(&cpu);
        b=get(&cpu);assert(b.ip==a.ip && b.si==a.si && b.di==a.di && b.cx==a.cx && b.ax==a.ax && b.flags==a.flags);
        assert(step(&cpu).kind==BM_286_BOUNDARY_INSTRUCTION);b=get(&cpu);
        assert(b.ip==a.ip+2+repeat && b.cx==(repeat?0:1));
        cpu.ops.destroy(cpu.context);++total;
    }
    printf("%u guest-only string repair/IRET/retry programs\n",total);
}
static void locked_segment(fixture_t *f,bm_cpu_t *cpu,unsigned odd,unsigned target,unsigned memory,unsigned selector)
{
    *cpu=create(f,0,odd);bm_286_arch_state_t a=get(cpu);
    const uint8_t bytes[]={0xf0,0x8e,(uint8_t)((target<<3)|(memory?7:0xc0))};
    code(f,bytes,3);a.ax=(uint16_t)selector;word(f,a.ds.base+a.bx,selector);
    assert(bm_286_set_arch_state(cpu,&a)==BM_STATUS_OK);
}
static void lock_forms(void)
{
    static const unsigned selectors[]={0,24,32}; unsigned total=0;
    for(unsigned target=0;target<4;++target) for(unsigned memory=0;memory<2;++memory)
    for(unsigned odd=0;odd<2;++odd) for(unsigned s=0;s<3;++s) {
        if(target==1) continue;
        fixture_t f;bm_cpu_t cpu;locked_segment(&f,&cpu,odd,target,memory,selectors[s]);
        bm_286_arch_state_t a=get(&cpu),b;bm_286_boundary_t boundary=step(&cpu);b=get(&cpu);
        if(s==2 || (target==2 && s==0)) {
            assert(boundary.vector==13 && getword(&f,b.ss.base+b.sp+2)==a.ip);
            assert(getword(&f,b.ss.base+b.sp)==(s==2?32:0));
        } else {
            const bm_286_segment_state_t *seg=target==0?&b.es:target==2?&b.ss:&b.ds;
            assert(!boundary.has_vector && b.ip==a.ip+3 && seg->selector==selectors[s]);
            assert(seg->valid==(s!=0));
            assert(b.interrupt_shadow==(target==2?BM_286_SHADOW_SS_LOAD:BM_286_SHADOW_NONE));
            assert(f.locks==1 && f.unlocks==1);
        }
        assert(!f.locked && f.locks==f.unlocks);
        cpu.ops.destroy(cpu.context);++total;
    }
    /* All documented MOV directions and memory/register XCHG remain ordinary
     * instructions with explicit LOCK, including operand-free AX exchanges. */
    static const uint8_t forms[][5]={
        {0x88,7},{0x89,7},{0x8a,7},{0x8b,7},{0x8c,7},
        {0xc6,7,0x42},{0xc7,7,0x42,0},{0xa0,0,6},{0xa1,0,6},{0xa2,0,6},{0xa3,0,6},
        {0x88,0xc1},{0x89,0xc1},{0x8a,0xc1},{0x8b,0xc1},{0x8c,0xc1},
        {0xc6,0xc1,0x42},{0xc7,0xc1,0x42,0},{0xb0,0x42},{0xb8,0x42,0},
        {0x86,7},{0x87,7},{0x86,0xc1},{0x87,0xc1},{0x90},{0x91}
    };
    for(unsigned k=0;k<sizeof(forms)/sizeof(forms[0]);++k) for(unsigned odd=0;odd<2;++odd) {
        fixture_t f,r;bm_cpu_t cpu=create(&f,0,odd),ref=create(&r,0,odd);
        uint8_t bytes[6]={0xf0};memcpy(bytes+1,forms[k],5);code(&f,bytes,6);code(&r,forms[k],5);
        step(&cpu);step(&ref);bm_286_arch_state_t a=get(&cpu),b=get(&ref);
        --a.ip;same(&a,&b);assert(memcmp(f.ram+0x4000,r.ram+0x4000,0x2000)==0);
        assert(!f.locked && f.locks==f.unlocks);cpu.ops.destroy(cpu.context);ref.ops.destroy(ref.context);++total;
    }
    printf("%u additional LOCK form/selector cases\n",total);
}
static void failure_setup(fixture_t *f,bm_cpu_t *cpu,unsigned route,unsigned odd)
{
    if(route<11) {
        unsigned lock=cases[route].opcode==0xa4 || cases[route].opcode<0x70;
        /* Explicit prefix denial is distinct from INS/OUTS IOPL denial. */
        if(cases[route].site==IOPL) lock=0;
        prepare(f,cpu,route,1,odd,cases[route].site==IOPL?3:0,1,0xf3,1,1,0,lock);
    } else if(route<17) {
        unsigned target=(route-11)/2;target=target==0?0:target+1;
        locked_segment(f,cpu,odd,target,route&1,24);
    } else {
        static const uint8_t ops[][3]={{0x87,7},{0x11,7},{0x19,7},{0xd1,0x17},{0xd1,0x1f}};
        *cpu=create(f,0,odd);bm_286_arch_state_t a=get(cpu);a.ds.access=0x91;
        code(f,ops[route-17],3);assert(bm_286_set_arch_state(cpu,&a)==BM_STATUS_OK);
    }
}
static void failures(void)
{
    static const bm_status_t errors[]={BM_STATUS_IDLE,BM_STATUS_UNSUPPORTED,BM_STATUS_INVALID_ARGUMENT,BM_STATUS_INVALID_STATE,BM_STATUS_DEVICE_ERROR};
    unsigned total=0;
    for(unsigned route=0;route<22;++route) for(unsigned odd=0;odd<2;++odd) {
        fixture_t f;bm_cpu_t cpu;failure_setup(&f,&cpu,route,odd);step(&cpu);unsigned count=f.calls;cpu.ops.destroy(cpu.context);
        for(unsigned fail=1;fail<=count;++fail) for(unsigned phase=0;phase<2;++phase) for(unsigned e=0;e<5;++e) {
            uint8_t ram[65536],ports[65536];failure_setup(&f,&cpu,route,odd);
            bm_286_arch_state_t a=get(&cpu),b;bm_286_boundary_t boundary;
            memcpy(ram,f.ram,sizeof(ram));memcpy(ports,f.ports,sizeof(ports));
            f.fail=fail;f.after=phase!=0;f.failure=errors[e];
            assert(bm_286_pm_step_subset(&cpu,&boundary)==errors[e]);b=get(&cpu);same(&a,&b);
            assert(f.calls==fail && f.effects==fail-1+phase && !f.locked && f.locks==f.unlocks);
            for(unsigned t=0;t<f.effects;++t)if(f.trace[t].operation==BM_BUS_WRITE)
                for(unsigned i=0;i<f.trace[t].size;++i)(f.trace[t].space==BM_ADDRESS_IO?ports:ram)[(unsigned)(f.trace[t].address+i)&65535u]=(uint8_t)(f.trace[t].value>>(8*i));
            assert(memcmp(ram,f.ram,sizeof(ram))==0 && memcmp(ports,f.ports,sizeof(ports))==0);
            assert(bm_286_pm_step_subset(&cpu,&boundary)==BM_STATUS_INVALID_STATE && f.calls==fail);
            cpu.ops.destroy(cpu.context);++total;
        }
    }
    printf("%u instruction-fault/LOCK before-after host failures\n",total);
}
static void fault_edges(void)
{
    /* Invalid imported caches remain host errors, with no fabricated #GP. */
    for(unsigned which=0;which<2;++which) {
        fixture_t f;bm_cpu_t cpu;prepare(&f,&cpu,0,1,0,0,0,0xf3,1,1,0,0);
        bm_286_arch_state_t a=get(&cpu),b;bm_286_boundary_t boundary;
        a.ds.limit=0xffff;a.ds.access=(uint8_t)(which?0x12:0x99);
        /* Invalid hidden presence or execute-only DS, not guest #NP/#GP. */
        assert(bm_286_set_arch_state(&cpu,&a)==BM_STATUS_OK);
        assert(bm_286_pm_step_subset(&cpu,&boundary)==BM_STATUS_INVALID_STATE);
        b=get(&cpu);same(&a,&b);assert(f.calls==3 && !f.locks);cpu.ops.destroy(cpu.context);
    }
    /* NMI raised during delivery survives successful entry and host-stop. */
    for(unsigned failed=0;failed<2;++failed) for(unsigned odd=0;odd<2;++odd) {
        fixture_t f;bm_cpu_t cpu;prepare(&f,&cpu,1,1,odd,0,0,0xf3,1,1,0,1);
        f.cpu=&cpu;f.nmi_at=5; /* First IDT read after four prefix/opcode bytes. */
        bm_286_arch_state_t a=get(&cpu),b;bm_286_boundary_t boundary;
        if(failed) {
            f.fail=5;f.after=true;f.failure=BM_STATUS_DEVICE_ERROR;
            assert(bm_286_pm_step_subset(&cpu,&boundary)==BM_STATUS_DEVICE_ERROR);
            a.nmi_pending=1;b=get(&cpu);same(&a,&b);
        } else {
            assert(step(&cpu).vector==13);b=get(&cpu);
            assert(b.nmi_pending && b.si==a.si+2 && b.di==a.di+2 && b.cx==0xffff);
            assert(step(&cpu).vector==2);
        }
        assert(!f.locked);cpu.ops.destroy(cpu.context);
    }
}
int main(void)
{
    setbuf(stdout,NULL);fault_matrix();guest_repair();lock_forms();failures();fault_edges();
    puts("protected instruction PRM-profile fault checks passed");return 0;
}
