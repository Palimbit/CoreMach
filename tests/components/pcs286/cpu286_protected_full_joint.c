/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 BluMach contributors
 * Full C/D/E/F integration. Fixture adapted from protected_tasks. No ROM,
 * external vectors, physical timing or machine acceptance.
 * Public step/run execute the same programs and every-transfer failures.
 */
#include <blumach/components/cpu_80286.h>
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
    unsigned odd, local, broken, rep_ip;
    unsigned batch, boundary_count;
    bm_286_boundary_t boundaries[160];
} fixture_t;

static unsigned use_run, trace_calls;
static bm_286_boundary_t last_trace;
static void trace_boundary(void *context, const bm_286_boundary_t *b)
{
    fixture_t *f=context;
    last_trace=*b; ++trace_calls;
    assert(f->boundary_count<160); f->boundaries[f->boundary_count++]=*b;
    if(f->batch) {
        bm_286_arch_state_t a;
        assert(bm_286_get_arch_state(f->cpu,&a)==BM_STATUS_OK);
        if(b->has_vector && b->vector==0x21)
            assert(f->cpu->ops.signal(f->cpu->context,BM_286_SIGNAL_INTR,0)==BM_STATUS_OK);
        if(a.tr.selector==72 && a.cpl==3 && a.ip==f->rep_ip && a.cx==2 && !f->program_events) {
            assert(f->cpu->ops.signal(f->cpu->context,BM_286_SIGNAL_NMI,1)==BM_STATUS_OK);
            assert(f->cpu->ops.signal(f->cpu->context,BM_286_SIGNAL_INTR,1)==BM_STATUS_OK);
            assert(f->cpu->ops.signal(f->cpu->context,BM_286_SIGNAL_HOLD,1)==BM_STATUS_OK);
            f->program_events=1;
        }
        /* Retain the boundary trace; recycle only the per-boundary bus log. */
        f->calls=f->effects=0;
    }
}
static bm_status_t api_step(bm_cpu_t *cpu, bm_286_boundary_t *b)
{
    unsigned before=trace_calls; bm_status_t status;
    if(use_run) {
        bm_tick_t consumed=99;
        memset(b,0,sizeof(*b));
        status=cpu->ops.run(cpu->context,1,&consumed);
        assert(consumed==(status==BM_STATUS_OK ? 1U : 0U));
        if(status==BM_STATUS_OK) *b=last_trace;
    } else status=bm_286_step(cpu,b);
    assert(trace_calls-before==(status==BM_STATUS_OK ? 1U : 0U));
    return status;
}

static bm_status_t bus(void *context, bm_bus_transaction_t *t)
{
    fixture_t *f = context;
    unsigned i;
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
    c.trace = trace_boundary; c.trace_context = f;
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
    bm_286_arch_state_t before=get(cpu);
    bm_status_t status=api_step(cpu,&b);
    if(status!=BM_STATUS_OK) {
        bm_286_arch_state_t a=get(cpu);
        fprintf(stderr,"joint step status=%u before TR=%x CS=%x IP=%x SP=%x DS=%x/%u after TR=%x CS=%x IP=%x SS=%x/%u\n",
            (unsigned)status,before.tr.selector,before.cs.selector,before.ip,before.sp,before.ds.selector,before.ds.valid,
            a.tr.selector,a.cs.selector,a.ip,a.ss.selector,a.ss.valid);
    }
    assert(status==BM_STATUS_OK);
    assert(b.timing == BM_286_TIMING_UNKNOWN && b.cpu_cycles == b.bus_wait_cycles);
    return b;
}

/* Small byte emitter: all streams below are authored, not assembled firmware. */
typedef struct stream { uint8_t bytes[256]; unsigned size; } stream_t;
static void emit(stream_t *s,const uint8_t *p,unsigned n)
{
    assert(s->size+n<=sizeof(s->bytes)); memcpy(s->bytes+s->size,p,n); s->size+=n;
}
#define EMIT(s,...) emit(&(s),(const uint8_t[]){__VA_ARGS__},sizeof((const uint8_t[]){__VA_ARGS__}))
static void tss(fixture_t *f,unsigned selector,unsigned base,unsigned ip,unsigned flags,
    unsigned cs,unsigned ss,unsigned ds,unsigned sp,unsigned sp0)
{
    unsigned gdt=0x2000 + f->odd;
    descriptor(f,gdt+selector,base,0x81); word(f,gdt+selector,43);
    word(f,base+2,sp0); word(f,base+4,16);
    const uint16_t words[]={(uint16_t)ip,(uint16_t)flags,0,0,0,0,(uint16_t)sp,0,0,0,
        (uint16_t)ds,(uint16_t)cs,(uint16_t)ss,(uint16_t)ds,56};
    for(unsigned i=0;i<15;++i) word(f,base+14+2*i,words[i]);
}
static bm_cpu_t program(fixture_t *f,unsigned odd,unsigned local,unsigned broken)
{
    bm_cpu_t cpu=create(f,0,odd); bm_286_arch_state_t a=get(&cpu);
    f->odd=odd; f->local=local; f->broken=broken; f->irq_vector=0x21;
    unsigned ucs=local?7:35,uss=local?15:43,uds=local?23:51;
    descriptor(f,a.gdtr.base+24,odd,0x92);
    descriptor(f,a.gdtr.base+32,0x5000,0xfa);
    descriptor(f,a.gdtr.base+40,0x6000 + odd,0xf2);
    descriptor(f,a.gdtr.base+48,odd,0xf2);
    descriptor(f,a.gdtr.base+56,0x8000 + odd,0x82); word(f,a.gdtr.base+56,23);
    descriptor(f,0x8000 + odd,0x5000,0xfa);
    descriptor(f,0x8008 + odd,0x6000 + odd,0xf2);
    descriptor(f,0x8010 + odd,odd,0xf2);
    tss(f,64,0x9000 + odd,0,0,8,16,24,0x8000,0x8000);
    tss(f,72,0xb000 + odd,0x300,0x3202,ucs,uss,uds,0x1800,0x7400);
    tss(f,96,0xc000 + odd,0x400,0x3002,8,16,24,0x6c00,0x6c00);
    tss(f,112,0xd000 + odd,0x700,0x3002,8,16,24,0x6800,0x6800);
    tss(f,120,0xe000 + odd,0x900,0x3002,8,16,24,0x6400,0x6400);
    if(broken==2) word(f,0xb004 + odd,0); /* Force failure while entering #NP. */
    word(f,a.gdtr.base+82,72); f->ram[a.gdtr.base+85]=0xe5;
    word(f,a.gdtr.base+88,0x600); word(f,a.gdtr.base+90,8);
    f->ram[a.gdtr.base+92]=2; f->ram[a.gdtr.base+93]=0xe4;
    word(f,a.idtr.base+13*8+2,96); f->ram[a.idtr.base+13*8+5]=0x85;
    word(f,a.idtr.base+10*8+2,120); f->ram[a.idtr.base+10*8+5]=0x85;
    word(f,a.idtr.base+11*8,0xa00); /* Ordinary repair of a partial new task. */
    word(f,a.idtr.base+2*8+2,112); f->ram[a.idtr.base+2*8+5]=0x85;
    for(unsigned v=0x20;v<=0x22;++v) {
        word(f,a.idtr.base+v*8,v==0x22?0x800:0x500); f->ram[a.idtr.base+v*8+5]=0xe6;
    }
    /* #GP task raises suspended A's IOPL; #TS task fixes B's SS0. Their
     * post-IRET short jumps permit reentry at the saved outgoing IP. */
    const uint8_t gp[]={0x81,0x0e,0x10,0x90,0,0x30,0xff,6,0x32,7,0x83,0xc4,2,0xcf,0xeb,0xf0};
    const uint8_t ts[]={0xc7,6,4,0xb0,16,0,0xff,6,0x36,7,0x83,0xc4,2,0xcf,0xeb,0xf0};
    const uint8_t event[]={0xff,6,0x34,7,0x9a,0,0,112,0,0xcf};
    unsigned descriptor_access=local?0x8015:0x2035;
    const uint8_t call[]={0x55,0x89,0xe5,0x8b,0x46,6,0xa3,0x38,7,
        0x8b,0x46,8,0xa3,0x3a,7,
        0x3d,0xbb,0xaa,0x75,5,0xc6,6,(uint8_t)descriptor_access,(uint8_t)(descriptor_access>>8),0x73,
        0x5d,0xca,4,0};
    /* A's inner call removes descriptor presence while keeping its cached DS.
     * B's subsequent task load faults at DS. The ordinary #NP handler repairs
     * presence and reloads both data caches, as required by PRM B-10. */
    const uint8_t np[]={0x50,0xb8,24,0,0x8e,0xd8,
        0xc6,6,(uint8_t)descriptor_access,(uint8_t)(descriptor_access>>8),0xf3,
        0xb8,(uint8_t)uds,0,0x8e,0xd8,0x8e,0xc0,0x58,0x83,0xc4,2,0xcf};
    const uint8_t task[]={0xff,6,0x30,7,0xcf,0xeb,0xf9};
    const uint8_t finish[]={0xc7,6,0x3c,7,0xde,0xc0,0xf4};
    memcpy(f->ram+0x3400,gp,sizeof(gp)); memcpy(f->ram+0x3500,event,sizeof(event));
    memcpy(f->ram+0x3600,call,sizeof(call)); memcpy(f->ram+0x3700,task,sizeof(task));
    memcpy(f->ram+0x3800,finish,sizeof(finish)); memcpy(f->ram+0x3900,ts,sizeof(ts));
    memcpy(f->ram+0x3a00,np,sizeof(np));
    const uint8_t real[]={0xb8,0,0,0x8e,0xd8,0x8e,0xd0,0xbc,0,0x80,
        0x0f,1,0x16,0,8,0x0f,1,0x1e,6,8,0xb8,1,0,0x0f,1,0xf0,0xea,0,2,8,0};
    code(f,real,sizeof(real)); memcpy(f->ram+0xfff0,(const uint8_t[]){0xea,0,1,0,3},5);
    stream_t kernel={0},user={0},child={0};
    EMIT(kernel,0xb8,16,0,0x8e,0xd0,0xbc,0,0x80,0xb8,24,0,0x8e,0xd8,0x8e,0xc0);
    EMIT(kernel,0xb8,56,0,0x0f,0,0xd0,0xb8,64,0,0x0f,0,0xd8);
    EMIT(kernel,0x68,(uint8_t)uss,0,0x68,0,0x10,0x68,2,2,0x68,(uint8_t)ucs,0,0x68,0,2,0xcf);
    EMIT(user,0xb8,(uint8_t)uds,0,0x8e,0xd8,0x8e,0xc0,0x0f,0,0xc8,0xa3,0x20,7);
    EMIT(user,0x68,0xbb,0xaa,0x68,0xdd,0xcc,0x9a,0,0,91,0);
    EMIT(user,0x9a,0,0,83,0,0xfa,0xcd,0x22);
    /* After ordinary #NP repair, invalidate SS0 before inner CALL. This is
     * a direct #TS; invalidating it before #NP entry would cause #DF instead. */
    if(broken==1) EMIT(child,0xc7,6,4,0xb0,0,0);
    EMIT(child,0x68,0x22,0x11,0x68,0x44,0x33,0x9a,0,0,91,0);
    EMIT(child,0xbe,0,7,0xbf,0x10,7,0xb9,3,0);
    f->rep_ip=0x300 + child.size; EMIT(child,0xf3,0xa4,0xcd,0x20,0xcf);
    memcpy(f->ram+0x3200,kernel.bytes,kernel.size); memcpy(f->ram+0x5200,user.bytes,user.size);
    memcpy(f->ram+0x5300,child.bytes,child.size);
    f->ram[0x700 + odd]=0x42; f->ram[0x701 + odd]=0x43; f->ram[0x702 + odd]=0x44;
    word(f,0x800,127); word(f,0x802,a.gdtr.base); word(f,0x804,0);
    word(f,0x806,0x7ff); word(f,0x808,a.idtr.base); word(f,0x80a,0);
    assert(cpu.ops.reset(cpu.context)==BM_STATUS_OK); f->shutdown_changes=0;
    return cpu;
}
static void signals(fixture_t *f,bm_cpu_t *cpu)
{
    bm_286_arch_state_t a=get(cpu);
    if(a.tr.selector==72 && a.cpl==3 && a.ip==f->rep_ip && a.cx==2 && !f->program_events) {
        assert(cpu->ops.signal(cpu->context,BM_286_SIGNAL_NMI,1)==BM_STATUS_OK);
        assert(cpu->ops.signal(cpu->context,BM_286_SIGNAL_INTR,1)==BM_STATUS_OK);
        assert(cpu->ops.signal(cpu->context,BM_286_SIGNAL_HOLD,1)==BM_STATUS_OK);
        bm_286_boundary_t b; unsigned calls=f->calls;
        assert(api_step(cpu,&b)==BM_STATUS_IDLE && f->calls==calls && f->hlda);
        assert(cpu->ops.signal(cpu->context,BM_286_SIGNAL_HOLD,0)==BM_STATUS_OK);
        f->program_events=1;
    }
    f->calls=f->effects=0;
}
static void after(fixture_t *f,bm_cpu_t *cpu,const bm_286_boundary_t *b)
{
    if(b->has_vector && b->vector==0x21)
        assert(cpu->ops.signal(cpu->context,BM_286_SIGNAL_INTR,0)==BM_STATUS_OK);
    assert(!f->locked && f->locks==f->unlocks);
}
static void replay(fixture_t *f,bm_cpu_t *cpu,unsigned steps)
{
    for(unsigned i=0;i<steps;++i) {signals(f,cpu); bm_286_boundary_t b=step(cpu); after(f,cpu,&b);}
}
static const bm_status_t failures[]={BM_STATUS_IDLE,BM_STATUS_UNSUPPORTED,
    BM_STATUS_INVALID_ARGUMENT,BM_STATUS_INVALID_STATE,BM_STATUS_DEVICE_ERROR};
static void combined_programs(void)
{
    unsigned total=0,gates=0;
    for(unsigned odd=0;odd<2;++odd) for(unsigned local=0;local<2;++local) for(unsigned broken=0;broken<2;++broken) {
        fixture_t f; bm_cpu_t cpu=program(&f,odd,local,broken); bm_286_arch_state_t a;
        unsigned counts[160],steps=0,events=0;
        const unsigned expected[]={11,10,2,0x21,0x20,13,0x22};
        do {
            assert(steps<160); signals(&f,&cpu); bm_286_boundary_t b=step(&cpu); after(&f,&cpu,&b);
            counts[steps++]=f.calls; a=get(&cpu);
            if(b.has_vector) {
                if(events>=7 || b.vector!=expected[events])
                    fprintf(stderr,"joint event %u vector %u at step %u TR=%x CS=%x IP=%x SP=%x\n",events,b.vector,steps,a.tr.selector,a.cs.selector,a.ip,a.sp);
                assert(events<7 && b.vector==expected[events]); ++events;
                if(events==1 && !broken) ++events;
            }
        } while(!a.halted);
        assert(events==7 && a.tr.selector==64 && a.cpl==0 && a.ip==0x807 && a.sp==0x7ff6);
        assert(getword(&f,0x73c + odd)==0xc0de && getword(&f,0x720 + odd)==64);
        assert(getword(&f,0x730 + odd)==3 && getword(&f,0x732 + odd)==1);
        assert(getword(&f,0x734 + odd)==2 && getword(&f,0x736 + odd)==broken);
        assert(getword(&f,0x738 + odd)==0x3344 && getword(&f,0x73a + odd)==0x1122);
        assert(!memcmp(f.ram+0x700 + odd,f.ram+0x710 + odd,3));
        assert(f.acks==2 && !a.nmi_blocked && !a.nmi_pending && !f.shutdown_changes);
        for(unsigned sel=72;sel<=120;sel+=8) if(sel!=80 && sel!=88 && sel!=104)
            assert((f.ram[0x2005 + odd+sel]&31)==1);
        cpu.ops.destroy(cpu.context);
        for(unsigned at=0;at<steps;++at) {
            bm_286_arch_state_t snapshots[512]; cpu=program(&f,odd,local,broken); replay(&f,&cpu,at);
            signals(&f,&cpu); bm_286_arch_state_t entry=get(&cpu); f.cpu=&cpu; f.observed=snapshots;
            step(&cpu); cpu.ops.destroy(cpu.context);
            for(unsigned fail=1;fail<=counts[at];++fail) for(unsigned phase=0;phase<2;++phase)
            for(unsigned e=0;e<sizeof(failures)/sizeof(failures[0]);++e) {
                cpu=program(&f,odd,local,broken); replay(&f,&cpu,at); signals(&f,&cpu);
                uint8_t ram[65536]; memcpy(ram,f.ram,sizeof(ram));
                f.fail=fail; f.after=phase!=0; f.failure=failures[e]; f.cpu=&cpu; f.nmi_at=fail;
                bm_286_boundary_t b;
                assert(api_step(&cpu,&b)==failures[e]); a=get(&cpu);
                bm_286_arch_state_t expected_state=snapshots[fail-1]; expected_state.nmi_pending=1;
                same(&a,&expected_state);
                assert(f.calls==fail && f.effects==fail-1+phase && !f.locked && f.locks==f.unlocks);
                for(unsigned t=0;t<f.effects;++t) if(f.trace[t].operation==BM_BUS_WRITE)
                    for(unsigned i=0;i<f.trace[t].size;++i)
                        ram[(unsigned)(f.trace[t].address+i)&65535]=(uint8_t)(f.trace[t].value>>(8*i));
                assert(!memcmp(ram,f.ram,sizeof(ram)) && !f.shutdown_changes);
                assert(api_step(&cpu,&b)==BM_STATUS_INVALID_STATE);
                assert(bm_286_step(&cpu,&b)==BM_STATUS_INVALID_STATE && f.calls==fail);
                cpu.ops.destroy(cpu.context); ++total;
            }
            if(entry.msw&1) {
                cpu=program(&f,odd,local,broken); replay(&f,&cpu,at); signals(&f,&cpu);
                bm_286_arch_state_t before=get(&cpu); bm_286_boundary_t b;
                uint64_t cycles=99; bm_status_t status;
                unsigned acks=f.acks,locks=f.locks;
                status=bm_286_step_clocked(cpu.context,0,&cycles); assert(!cycles);
                assert(status==BM_STATUS_UNSUPPORTED); a=get(&cpu); same(&a,&before);
                assert(!f.calls && f.acks==acks && f.locks==locks);
                assert(api_step(&cpu,&b)==BM_STATUS_INVALID_STATE);
                cpu.ops.destroy(cpu.context); ++gates;
            }
        }
        printf("full joint: reset/outer IRET/inner CALL/tasks/repair/REP/NMI/IRQ/NT-return/GP/HLT %u boundaries, odd=%u local=%u broken=%u\n",steps,odd,local,broken);
    }
    printf("full joint: %u bus failures with callback NMI, exact effects and no replay; %u strict-clock boundaries\n",total,gates);
}
static bm_cpu_t shutdown_program(fixture_t *f,unsigned odd,unsigned local)
{
    bm_cpu_t cpu=program(f,odd,local,2);
    /* Recovery makes a safe observable stop in its own task; no claim that
     * the instruction interrupted by #DF is restartable. */
    const uint8_t safe[]={0xc7,6,0x3e,7,0xad,0xde,0xf4};
    memcpy(f->ram+0x3700,safe,sizeof(safe));
    assert(cpu.ops.reset(cpu.context)==BM_STATUS_OK); f->shutdown_changes=0;
    for(unsigned i=0;i<160;++i) {
        signals(f,&cpu); bm_286_boundary_t b=step(&cpu); after(f,&cpu,&b);
        if(b.kind==BM_286_BOUNDARY_SHUTDOWN) {
            bm_286_arch_state_t a=get(&cpu);
            assert(a.shutdown && a.tr.selector==72 && a.cpl==3 && !a.ds.valid && a.ip==0x300);
            assert(f->shutdown && f->shutdown_changes==1);
            f->calls=f->effects=0; return cpu;
        }
    }
    assert(false); return cpu;
}
static void partial_task_shutdown(void)
{
    unsigned total=0;
    for(unsigned odd=0;odd<2;++odd) for(unsigned local=0;local<2;++local) {
        fixture_t f; bm_cpu_t cpu=shutdown_program(&f,odd,local); bm_286_boundary_t b;
        assert(api_step(&cpu,&b)==BM_STATUS_IDLE && !f.calls);
        assert(cpu.ops.signal(cpu.context,BM_286_SIGNAL_NMI,1)==BM_STATUS_OK);
        bm_286_arch_state_t snapshots[512]; f.cpu=&cpu; f.observed=snapshots;
        b=step(&cpu); unsigned calls=f.calls; bm_286_arch_state_t a=get(&cpu);
        assert(b.has_vector && b.vector==2 && a.tr.selector==112 && !a.shutdown && a.nmi_blocked);
        assert(!f.shutdown && f.shutdown_changes==2); f.observed=NULL;
        f.calls=f.effects=0; step(&cpu); f.calls=f.effects=0; step(&cpu); a=get(&cpu);
        assert(a.halted && a.ip==0x707 && getword(&f,0x73e + odd)==0xdead);
        assert(cpu.ops.reset(cpu.context)==BM_STATUS_OK); a=get(&cpu);
        assert(!(a.msw&1) && !a.shutdown && !a.halted && !a.tr.valid && !a.nmi_blocked);
        cpu.ops.destroy(cpu.context);
        for(unsigned at=1;at<=calls;++at) for(unsigned phase=0;phase<2;++phase)
        for(unsigned e=0;e<sizeof(failures)/sizeof(failures[0]);++e) {
            cpu=shutdown_program(&f,odd,local);
            uint8_t ram[65536]; memcpy(ram,f.ram,sizeof(ram));
            assert(cpu.ops.signal(cpu.context,BM_286_SIGNAL_NMI,1)==BM_STATUS_OK);
            f.fail=at; f.after=phase!=0; f.failure=failures[e];
            assert(api_step(&cpu,&b)==failures[e]); a=get(&cpu);
            bm_286_arch_state_t expected_state=snapshots[at-1]; expected_state.nmi_pending=1;
            same(&a,&expected_state);
            assert(f.calls==at && f.effects==at-1+phase && f.shutdown && f.shutdown_changes==1);
            for(unsigned t=0;t<f.effects;++t) if(f.trace[t].operation==BM_BUS_WRITE)
                for(unsigned i=0;i<f.trace[t].size;++i)
                    ram[(unsigned)(f.trace[t].address+i)&65535]=(uint8_t)(f.trace[t].value>>(8*i));
            assert(!memcmp(ram,f.ram,sizeof(ram)) && !f.locked && f.locks==f.unlocks);
            assert(api_step(&cpu,&b)==BM_STATUS_INVALID_STATE && f.calls==at);
            cpu.ops.destroy(cpu.context); ++total;
        }
    }
    printf("full joint: 4 reset/partial-task NP/inner-stack failure/DF/shutdown/task-NMI/safe-HLT/reset programs; %u recovery failures\n",total);
}
static void run_budgets(void)
{
    const unsigned budgets[]={1,7,256};
    use_run=0;
    for(unsigned odd=0;odd<2;++odd) for(unsigned local=0;local<2;++local)
    for(unsigned broken=0;broken<2;++broken) {
        fixture_t reference; bm_cpu_t cpu=program(&reference,odd,local,broken);
        while(!get(&cpu).halted) {
            signals(&reference,&cpu); bm_286_boundary_t b=step(&cpu); after(&reference,&cpu,&b);
        }
        bm_286_arch_state_t expected=get(&cpu); cpu.ops.destroy(cpu.context);
        for(unsigned i=0;i<sizeof(budgets)/sizeof(budgets[0]);++i) {
            fixture_t f; cpu=program(&f,odd,local,broken); f.cpu=&cpu; f.batch=1;
            unsigned completed=0,holds=0;
            for(unsigned runs=0;runs<160;++runs) {
                bm_tick_t consumed=99; bm_286_arch_state_t before=get(&cpu);
                unsigned calls=f.calls,traces=f.boundary_count;
                assert(cpu.ops.run(cpu.context,0,&consumed)==BM_STATUS_OK && !consumed);
                bm_286_arch_state_t unchanged=get(&cpu); same(&before,&unchanged);
                assert(f.calls==calls && f.boundary_count==traces);
                assert(cpu.ops.run(cpu.context,budgets[i],NULL)==BM_STATUS_INVALID_ARGUMENT);
                bm_status_t status=cpu.ops.run(cpu.context,budgets[i],&consumed);
                assert(consumed<=budgets[i] && f.boundary_count-traces==consumed);
                completed+=(unsigned)consumed;
                if(status==BM_STATUS_IDLE) {
                    if(f.hlda) {
                        ++holds; assert(get(&cpu).nmi_pending);
                        assert(cpu.ops.signal(cpu.context,BM_286_SIGNAL_HOLD,0)==BM_STATUS_OK);
                    } else {assert(get(&cpu).halted); break;}
                } else assert(status==BM_STATUS_OK && consumed==budgets[i]);
            }
            assert(holds==1 && completed==reference.boundary_count);
            bm_286_arch_state_t actual=get(&cpu); same(&actual,&expected);
            assert(!memcmp(f.ram,reference.ram,sizeof(f.ram)));
            assert(f.acks==reference.acks && f.locks==reference.locks && f.unlocks==reference.unlocks);
            for(unsigned n=0;n<completed;++n) {
                const bm_286_boundary_t *a=&f.boundaries[n],*b=&reference.boundaries[n];
                assert(a->kind==b->kind && a->has_vector==b->has_vector && a->vector==b->vector);
                assert(a->instruction_ip==b->instruction_ip && a->instruction_address==b->instruction_address);
                assert(a->timing==BM_286_TIMING_UNKNOWN && a->cpu_cycles==b->cpu_cycles && a->bus_wait_cycles==b->bus_wait_cycles);
            }
            bm_tick_t consumed=99;
            assert(cpu.ops.run(cpu.context,256,&consumed)==BM_STATUS_IDLE && !consumed && !f.calls);
            assert(cpu.ops.reset(cpu.context)==BM_STATUS_OK);
            actual=get(&cpu); assert(!(actual.msw&1) && !actual.tr.valid && !actual.halted && !actual.nmi_pending);
            cpu.ops.destroy(cpu.context);
        }
    }
    puts("public run: 24 reset-to-HLT programs at budgets 1/7/256 match step state, RAM, boundaries, waits and pins; HOLD/idle/zero budget/reset");
}

static void run_failures_and_isolation(void)
{
    const uint8_t bytes[]={0x90,0x90,0xa3,0,7,0xf4};
    for(unsigned e=0;e<sizeof(failures)/sizeof(failures[0]);++e) for(unsigned phase=0;phase<2;++phase) {
        fixture_t f,other; bm_cpu_t cpu=create(&f,0,0),peer=create(&other,0,0);
        code(&f,bytes,sizeof(bytes)); code(&other,bytes,sizeof(bytes));
        f.cpu=&cpu; f.fail=6; f.nmi_at=6; f.failure=failures[e]; f.after=phase!=0;
        bm_286_arch_state_t before=get(&cpu),isolated=get(&peer); bm_tick_t consumed=99;
        assert(cpu.ops.run(cpu.context,10,&consumed)==failures[e] && consumed==2);
        bm_286_arch_state_t a=get(&cpu); before.ip+=2; before.nmi_pending=1; same(&a,&before);
        assert(f.boundary_count==2 && f.calls==6 && f.effects==5+phase);
        assert(getword(&f,0x4700)==(phase?0x5678:0) && !f.locked && !f.shutdown_changes);
        unsigned calls=f.calls; bm_286_boundary_t b;
        assert(bm_286_step(&cpu,&b)==BM_STATUS_INVALID_STATE);
        assert(cpu.ops.run(cpu.context,10,&consumed)==BM_STATUS_INVALID_STATE && !consumed && f.calls==calls);
        assert(bm_286_set_arch_state(&cpu,&before)==BM_STATUS_INVALID_STATE);
        /* A zero budget is an empty operation, not recovery of a latched stop. */
        assert(cpu.ops.run(cpu.context,0,&consumed)==BM_STATUS_OK && !consumed && f.calls==calls);
        a=get(&peer); same(&a,&isolated); assert(!other.calls && !other.boundary_count);
        assert(peer.ops.run(peer.context,10,&consumed)==BM_STATUS_IDLE && consumed==4);
        assert(get(&peer).halted && getword(&other,0x4700)==0x5678);
        assert(cpu.ops.reset(cpu.context)==BM_STATUS_OK); a=get(&cpu);
        assert(!(a.msw&1) && !a.nmi_pending && !a.tr.valid);
        f.fail=f.nmi_at=f.calls=f.effects=0; f.ram[0xfff0]=0x90;
        assert(cpu.ops.run(cpu.context,1,&consumed)==BM_STATUS_OK && consumed==1);
        assert(get(&cpu).ip==0xfff1);
        cpu.ops.destroy(cpu.context); peer.ops.destroy(peer.context);
    }
    puts("public run: ten before/after host failures retain two completed boundaries, exact effects, NMI and stop; instances isolated; reset resumes real execution");
}

static void acknowledge_failures(void)
{
    for(use_run=0;use_run<2;++use_run) for(unsigned phase=1;phase<=2;++phase)
    for(unsigned e=0;e<sizeof(failures)/sizeof(failures[0]);++e) {
        fixture_t f; bm_cpu_t cpu=create(&f,0,0); bm_286_arch_state_t before=get(&cpu);
        before.flags|=0x200;
        assert(bm_286_set_arch_state(&cpu,&before)==BM_STATUS_OK);
        assert(cpu.ops.signal(cpu.context,BM_286_SIGNAL_INTR,1)==BM_STATUS_OK);
        f.ack_fail=phase; f.failure=failures[e]; bm_286_boundary_t b;
        assert(api_step(&cpu,&b)==failures[e]); bm_286_arch_state_t a=get(&cpu); same(&a,&before);
        assert(f.acks==phase && !f.calls && !f.locked && f.locks==f.unlocks && !f.boundary_count);
        assert(api_step(&cpu,&b)==BM_STATUS_INVALID_STATE && f.acks==phase && !f.calls);
        cpu.ops.destroy(cpu.context);
    }
    puts("public step/run: twenty INTA phase failures keep exact host status, no guest frame/trace, released exclusion and no replay");
}

int main(void)
{
    setvbuf(stdout,NULL,_IONBF,0);
    for(use_run=0;use_run<2;++use_run) {
        printf("public API: %s\n",use_run ? "run(1)" : "step");
        combined_programs(); partial_task_shutdown();
    }
    run_budgets(); run_failures_and_isolation(); acknowledge_failures();
    return 0;
}
