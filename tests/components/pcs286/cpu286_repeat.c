/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Authored architectural tests, not physical timing or hardware captures.
 */
#include <blumach/components/cpu_80286.h>
#include <blumach/platforms/null_host.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct fixture {
    unsigned locked;
    bm_cpu_t cpu;
    uint8_t *ram;
    bm_bus_transaction_t trace[64];
    unsigned count, fail_at, acknowledgements, traced, hold_at;
    bm_286_boundary_t last_boundary;
} fixture_t;
/* No competing master in this fixture; track the exclusion contract. */
static void lock_changed(void *context, int high)
{
    fixture_t *f = context;
    assert(f->locked != (unsigned) high);
    f->locked = (unsigned) high;
}
static bm_status_t access_bus(void *context, bm_bus_transaction_t *t)
{
    fixture_t *f = context;
    assert(t->space != BM_ADDRESS_IO && t->address + t->size <= 0x1000000);
    assert(!t->wait_states && f->count < 64);
    assert(t->endianness == BM_ENDIAN_LITTLE);
    assert(t->space == (t->operation == BM_BUS_FETCH ? BM_ADDRESS_PROGRAM : BM_ADDRESS_DATA));
    f->trace[f->count++] = *t;
    if (f->hold_at == f->count)
        assert(f->cpu.ops.signal(f->cpu.context, BM_286_SIGNAL_HOLD, 1) == BM_STATUS_OK);
    if (f->count == f->fail_at) return BM_STATUS_DEVICE_ERROR;
    if (t->operation != BM_BUS_WRITE) t->value = 0;
    for (unsigned i = 0; i < t->size; ++i) {
        if (t->operation == BM_BUS_WRITE)
            f->ram[(size_t)t->address + i] = (uint8_t)(t->value >> (8U * i));
        else t->value |= (uint64_t)f->ram[(size_t)t->address + i] << (8U * i);
    }
    t->wait_states = 2;
    return BM_STATUS_OK;
}
static bm_status_t ack(void *context, unsigned phase, uint8_t *v, uint32_t *waits)
{
    fixture_t *f = context;
    assert(phase == (f->acknowledgements & 1U) && !*waits);
    ++f->acknowledgements; *v = 0x30; return BM_STATUS_OK;
}
static void trace_boundary(void *context, const bm_286_boundary_t *b)
{
    fixture_t *f = context;
    f->last_boundary = *b; ++f->traced;
}
static bm_286_arch_state_t state(fixture_t *f)
{
    bm_286_arch_state_t s;
    assert(bm_286_get_arch_state(&f->cpu, &s) == BM_STATUS_OK);
    return s;
}
static void same(const bm_286_arch_state_t *a, const bm_286_arch_state_t *b)
{
#define EQ(field) assert(a->field == b->field)
    EQ(size); EQ(version); EQ(ax); EQ(cx); EQ(dx); EQ(bx); EQ(sp); EQ(bp); EQ(si); EQ(di);
    EQ(ip); EQ(flags); EQ(msw); EQ(cpl); EQ(halted); EQ(shutdown);
    EQ(interrupt_shadow); EQ(nmi_blocked); EQ(nmi_pending); EQ(trap_pending);
    EQ(gdtr.base); EQ(gdtr.limit); EQ(idtr.base); EQ(idtr.limit);
#define SEG(s) EQ(s.selector); EQ(s.base); EQ(s.limit); EQ(s.access); EQ(s.valid)
    SEG(es); SEG(cs); SEG(ss); SEG(ds); SEG(ldtr); SEG(tr);
#undef SEG
#undef EQ
}
static void set(fixture_t *f, const bm_286_arch_state_t *s)
{
    assert(bm_286_set_arch_state(&f->cpu, s) == BM_STATUS_OK);
}
static void word(fixture_t *f, uint32_t address, uint16_t v)
{
    f->ram[address] = (uint8_t)v; f->ram[address + 1] = (uint8_t)(v >> 8);
}
static uint16_t read_word(fixture_t *f, uint32_t address)
{
    return (uint16_t)(f->ram[address] | ((unsigned)f->ram[address + 1] << 8));
}
static bm_286_arch_state_t setup(fixture_t *f, const uint8_t *code, size_t size)
{
    bm_286_arch_state_t s;
    assert(f->cpu.ops.reset(f->cpu.context) == BM_STATUS_OK);
    f->count = f->fail_at = f->acknowledgements = f->traced = f->hold_at = 0;
    s = state(f); s.ip = 0x100;
    s.cs.selector = 0x3000; s.cs.base = 0x30000;
    s.ss.selector = 0x1000; s.ss.base = 0x10000; s.sp = 0x800;
    s.es.selector = 0x2000; s.es.base = 0x20000; s.ax = 0xa55a;
    memcpy(f->ram + 0x30100, code, size);
    f->ram[0x40200] = 0xcf; /* IRET handler */
    for (unsigned i = 0; i < 256; ++i) {
        word(f, i * 4U, 0x200); word(f, i * 4U + 2U, 0x4000);
    }
    return s;
}
static bm_286_boundary_t step(fixture_t *f)
{
    bm_286_boundary_t b;
    unsigned before = f->count;
    unsigned traced = f->traced;
    assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK);
    assert(b.timing == BM_286_TIMING_UNKNOWN && b.bus_wait_cycles == (f->count - before) * 2U);
    assert(b.cpu_cycles == b.bus_wait_cycles);
    assert(f->traced == traced + 1 && f->last_boundary.kind == b.kind);
    assert(f->last_boundary.has_vector == b.has_vector && f->last_boundary.vector == b.vector);
    return b;
}

static bm_286_arch_state_t prepare(fixture_t *f, const uint8_t *code, size_t length, unsigned count)
{
    bm_286_arch_state_t s = setup(f, code, length);
    s.si = 0x500; s.di = 0x600; s.cx = (uint16_t)count; s.ax = 0x5a5a;
    s.flags = 2; return s;
}
static void matrix(fixture_t *f)
{
    const uint8_t ops[] = {0xa4,0xa5,0xa6,0xa7,0xaa,0xab,0xac,0xad,0xae,0xaf};
    for (unsigned k=0; k<sizeof(ops); ++k)
        for (unsigned rep=0xf2; rep<=0xf3; ++rep)
            for (unsigned df=0; df<2; ++df)
                for (unsigned prefix=0; prefix<5; ++prefix)
                    for (unsigned count=0; count<5; ++count)
                        for (unsigned stop=0; stop<5; ++stop) {
                            uint8_t code[4]; unsigned length=0, op=ops[k], kind=op&0xfe;
                            unsigned size=(op&1)+1, executed=0;
                            int compare=kind==0xa6 || kind==0xae;
                            if (prefix) code[length++]=(uint8_t)(0x26+8*(prefix-1));
                            code[length++]=(uint8_t)rep; code[length++]=(uint8_t)op;
                            bm_286_arch_state_t s=prepare(f,code,length,count), a;
                            uint32_t bases[]={s.es.base,s.cs.base,s.ss.base,s.ds.base};
                            uint32_t source=bases[prefix ? prefix-1 : 3];
                            s.flags=(uint16_t)(2 | (df ? 0x400 : 0) | (rep==0xf2 ? 0x40 : 0));
                            if (df) {s.si+=8; s.di+=8;}
                            for (unsigned n=0; n<4; ++n) {
                                uint16_t si=(uint16_t)(s.si+(df ? -(int)(n*size) : (int)(n*size)));
                                uint16_t di=(uint16_t)(s.di+(df ? -(int)(n*size) : (int)(n*size)));
                                unsigned equal=rep==0xf3;
                                if (n==stop) equal=!equal;
                                if (size==1) {
                                    f->ram[source+si]=0x5a;
                                    f->ram[s.es.base+di]=(uint8_t)(equal ? 0x5a : 0x5b);
                                } else {
                                    word(f,source+si,0x5a5a);
                                    word(f,s.es.base+di,(uint16_t)(equal ? 0x5a5a : 0x5b5b));
                                }
                            }
                            if (!count) {s.ds.valid=s.es.valid=s.ss.valid=0;} /* no data check */
                            set(f,&s);
                            do {
                                unsigned before=f->count;
                                bm_286_boundary_t b=step(f); a=state(f);
                                if (count) ++executed;
                                unsigned remaining=count-executed;
                                int more=remaining!=0 && (!compare || executed-1!=stop);
                                assert(b.kind==(more ? BM_286_BOUNDARY_REP_ITERATION : BM_286_BOUNDARY_INSTRUCTION));
                                assert(!b.has_vector && b.instruction_ip==s.ip && a.cx==remaining);
                                assert(a.ip==(more ? s.ip : (uint16_t)(s.ip+length)));
                                if (!count) {assert(f->count==length && a.flags==s.flags);}
                                else {
                                    for (unsigned i=before; i<f->count; ++i)
                                        if (executed>1) assert(f->trace[i].operation!=BM_BUS_FETCH);
                                    if (kind==0xa4 || kind==0xaa) {
                                        uint16_t di=(uint16_t)(s.di+(df ? -(int)((executed-1)*size) : (int)((executed-1)*size)));
                                        assert((read_word(f,s.es.base+di)&(size==1 ? 255U : 65535U))==
                                            (size==1 ? 0x5aU : 0x5a5aU));
                                    }
                                }
                                assert(a.si==(uint16_t)(s.si+((kind==0xa4 || kind==0xa6 || kind==0xac) ?
                                    (df ? -(int)(executed*size) : (int)(executed*size)) : 0)));
                                assert(a.di==(uint16_t)(s.di+(kind!=0xac ?
                                    (df ? -(int)(executed*size) : (int)(executed*size)) : 0)));
                                if (!more) break;
                                assert(executed<4);
                            } while (1);
                            assert(executed==(compare && stop<count ? stop+1 : count));
                        }
}
static void count_space(fixture_t *f)
{
    const uint8_t code[]={0xf3,0xaa};
    bm_286_arch_state_t base=prepare(f,code,sizeof(code),0);
    /* Every initial count: exactly one element or zero, never an unbounded loop. */
    for (unsigned count=0; count<65536; ++count) {
        bm_286_arch_state_t s=base; s.cx=(uint16_t)count; set(f,&s); f->count=0;
        bm_286_boundary_t b=step(f); bm_286_arch_state_t a=state(f);
        assert(a.cx==(count ? count-1 : 0) && a.di==s.di+(count ? 1U : 0U));
        assert(b.kind==(count>1 ? BM_286_BOUNDARY_REP_ITERATION : BM_286_BOUNDARY_INSTRUCTION));
    }
    { /* Full maximum-count run, wrapping DI, no refetch between iterations. */
        bm_286_arch_state_t s=prepare(f,code,sizeof(code),65535); s.di=0xfffe;
        set(f,&s);
        for (unsigned i=0;i<65535;++i) {
            f->count=0; bm_286_boundary_t b=step(f); bm_286_arch_state_t a=state(f);
            assert(a.cx==65534-i && a.di==(uint16_t)(s.di+i+1));
            assert(f->count==(i ? 1U : 3U));
            assert(b.kind==(i==65534 ? BM_286_BOUNDARY_INSTRUCTION : BM_286_BOUNDARY_REP_ITERATION));
        }
    }
}
static void cache_and_import(fixture_t *f)
{
    const uint8_t code[]={0xf3,0xa4,0x90};
    bm_286_arch_state_t s=prepare(f,code,sizeof(code),3), saved, a;
    f->ram[0x500]=1; f->ram[0x501]=2; f->ram[0x502]=3;
    set(f,&s); step(f); saved=state(f);
    f->ram[0x30100]=0x90; f->ram[0x30101]=0xf4;
    step(f); a=state(f);
    assert(a.cx==1 && a.ip==s.ip && f->ram[0x20601]==2); /* retained decode */
    /* Rejected import must not invalidate continuation. */
    bm_286_arch_state_t invalid=a; invalid.version=0;
    assert(bm_286_set_arch_state(&f->cpu,&invalid)==BM_STATUS_INVALID_ARGUMENT);
    step(f); assert(state(f).cx==0 && f->ram[0x20602]==3);
    /* Import resumes from architectural CX/SI/DI, decoding current bytes. */
    set(f,&saved); step(f); a=state(f);
    assert(a.cx==saved.cx && a.si==saved.si && a.ip==s.ip+1); /* NOP, not cached MOVS */
    memcpy(f->ram+0x30100,code,sizeof(code)); set(f,&saved);
    f->ram[0x20600]=0xcc; step(f); step(f);
    assert(f->ram[0x20600]==0xcc && state(f).cx==0); /* no first-element replay */
    /* Reset also invalidates decode. */
    set(f,&s); step(f);
    assert(f->cpu.ops.reset(f->cpu.context)==BM_STATUS_OK);
    a=state(f); f->ram[0xfffff0]=0x90; f->count=0; step(f);
    assert(state(f).ip==a.ip+1 && f->count==1);
}
static void overlap(fixture_t *f)
{
    const uint8_t code[]={0xf3,0xa4};
    for(unsigned df=0;df<2;++df) {
        bm_286_arch_state_t s=prepare(f,code,sizeof(code),4);
        s.es=s.ds; s.si=(uint16_t)(df ? 0x504 : 0x500);
        s.di=(uint16_t)(s.si+(df ? -1 : 1)); s.flags=(uint16_t)(2 | (df ? 0x400 : 0));
        memset(f->ram+0x500,0,5); f->ram[s.si]=0x7b; set(f,&s);
        /* Forward and backward overlap must observe the preceding write,
         * not copy from a snapshot of the entire initial source range. */
        for(unsigned i=0;i<4;++i) step(f);
        for(unsigned i=0;i<5;++i) assert(f->ram[0x500+i]==0x7b);
        assert(!state(f).cx && state(f).ip==s.ip+2);
    }
}
static void interrupts(fixture_t *f)
{
    const uint8_t code[]={0x3e,0xf3,0xa4,0x90};
    for (unsigned event=0;event<3;++event)
        for (unsigned after=1;after<=3;++after) {
            bm_286_arch_state_t s=prepare(f,code,sizeof(code),3), a;
            s.flags=(uint16_t)(0x202 | (event==2 ? 0x100 : 0));
            for(unsigned i=0;i<3;++i) f->ram[0x500+i]=(uint8_t)(0x21+i);
            set(f,&s);
            /* For TF test every boundary; after is only meaningful for INTR/NMI. */
            if(event==2 && after!=1) continue;
            for(unsigned i=0;i<after;++i) step(f);
            a=state(f); uint16_t resume=a.ip, cx=a.cx;
            if(event!=2) assert(f->cpu.ops.signal(f->cpu.context,
                event==0 ? BM_286_SIGNAL_INTR : BM_286_SIGNAL_NMI,1)==BM_STATUS_OK);
            bm_286_boundary_t b=step(f);
            assert(b.has_vector && b.vector==(event==0 ? 0x30 : event==1 ? 2 : 1));
            assert(read_word(f,s.ss.base+s.sp-6)==resume);
            assert(f->acknowledgements==(event==0 ? 2U : 0U));
            if(event!=2) assert(f->cpu.ops.signal(f->cpu.context,
                event==0 ? BM_286_SIGNAL_INTR : BM_286_SIGNAL_NMI,0)==BM_STATUS_OK);
            /* Handler contains IRET. Completed destination bytes are poisoned:
             * restart must use updated SI/DI/CX, not replay the first element. */
            f->ram[0x20600]=0xcc; step(f); a=state(f);
            assert(a.ip==resume && a.cx==cx && a.sp==s.sp);
            while(cx) {
                step(f); --cx; assert(state(f).cx==cx);
                if(event==2) {
                    b=step(f); assert(b.kind==BM_286_BOUNDARY_EXCEPTION && b.vector==1);
                    uint16_t expected_ip=cx ? s.ip : (uint16_t)(s.ip+3);
                    assert(read_word(f,s.ss.base+s.sp-6)==expected_ip);
                    step(f); /* IRET */
                }
            }
            assert(f->ram[0x20600]==0xcc);
        }
    { /* Handler modifies next opcode: interruption must flush cached REP. */
        bm_286_arch_state_t s=prepare(f,code,sizeof(code),3); s.flags=0x202;
        set(f,&s); step(f);
        assert(f->cpu.ops.signal(f->cpu.context,BM_286_SIGNAL_INTR,1)==BM_STATUS_OK);
        step(f); assert(f->cpu.ops.signal(f->cpu.context,BM_286_SIGNAL_INTR,0)==BM_STATUS_OK);
        f->ram[0x30100]=0x90; step(f); step(f);
        assert(state(f).cx==2 && state(f).ip==s.ip+1);
    }
}
static void failures(fixture_t *f)
{
    const uint8_t ops[]={0xa4,0xa5,0xa6,0xa7,0xaa,0xab,0xac,0xad,0xae,0xaf};
    for(unsigned k=0;k<sizeof(ops);++k)
        for(unsigned later=0;later<2;++later) {
            const uint8_t code[]={0xf3,ops[k]};
            unsigned total=0;
            for(unsigned fail=0;fail<=total;++fail) {
                bm_286_arch_state_t s=prepare(f,code,sizeof(code),3), before, a;
                bm_286_boundary_t b; s.si=0x501; s.di=0x601;
                memset(f->ram+0x500,0x5a,16); memset(f->ram+0x20600,0x5a,16);
                set(f,&s); if(later) step(f); before=state(f);
                f->count=f->traced=0; f->fail_at=fail;
                if(!fail) {step(f); total=f->count; continue;}
                assert(bm_286_step(&f->cpu,&b)==BM_STATUS_DEVICE_ERROR);
                a=state(f); same(&a,&before); assert(!f->traced && f->count==fail);
                for(unsigned i=0;i+1<fail;++i)
                    if(f->trace[i].operation==BM_BUS_WRITE)
                        for(unsigned j=0;j<f->trace[i].size;++j)
                            assert(f->ram[f->trace[i].address+j]==(uint8_t)(f->trace[i].value>>(8*j)));
                assert(bm_286_step(&f->cpu,&b)==BM_STATUS_INVALID_STATE && f->count==fail);
            }
        }
    { /* Earlier iterations remain committed when the next word crosses limit. */
        const uint8_t code[]={0xf3,0xab};
        bm_286_arch_state_t s=prepare(f,code,sizeof(code),2), before, a;
        bm_286_boundary_t b; s.di=0xfffd; set(f,&s); step(f); before=state(f);
        assert(before.di==0xffff && before.cx==1);
        assert(bm_286_step(&f->cpu,&b)==BM_STATUS_UNSUPPORTED); a=state(f); same(&a,&before);
        assert(read_word(f,s.es.base+0xfffd)==s.ax);
    }
}
static void prefixes_and_hold(fixture_t *f)
{
    { /* Last repeat/segment prefixes win; ZF is not checked before first compare. */
        const uint8_t code[]={0xf2,0x36,0xf3,0x3e,0xa6};
        bm_286_arch_state_t s=prepare(f,code,sizeof(code),3);
        f->ram[0x500]=0x42; f->ram[0x20600]=0x42; s.flags=2;
        set(f,&s); bm_286_boundary_t b=step(f);
        assert(b.kind==BM_286_BOUNDARY_REP_ITERATION && state(f).cx==2 && (state(f).flags&0x40));
    }
    { /* HOLD mid-element waits until its effects commit, then does no access. */
        const uint8_t code[]={0xf3,0xa5};
        bm_286_arch_state_t s=prepare(f,code,sizeof(code),2), a, held;
        s.si=0x501; s.di=0x601; word(f,s.si,0x1234); word(f,s.si+2,0x5678);
        f->hold_at=3; set(f,&s); step(f); a=state(f); unsigned n=f->count;
        bm_286_boundary_t b; assert(bm_286_step(&f->cpu,&b)==BM_STATUS_IDLE);
        assert(b.kind==BM_286_BOUNDARY_HOLD && f->count==n && a.cx==1);
        held=state(f); same(&a,&held);
        assert(f->cpu.ops.signal(f->cpu.context,BM_286_SIGNAL_HOLD,0)==BM_STATUS_OK);
        step(f); assert(state(f).cx==0 && read_word(f,0x20603)==0x5678);
    }
    for(unsigned shadow=1;shadow<=2;++shadow) {
        const uint8_t code[]={0xf3,0xa4};
        bm_286_arch_state_t s=prepare(f,code,sizeof(code),3);
        s.flags=0x202; s.interrupt_shadow=(uint8_t)shadow;
        set(f,&s);
        assert(f->cpu.ops.signal(f->cpu.context,BM_286_SIGNAL_INTR,1)==BM_STATUS_OK);
        step(f); assert(state(f).cx==2 && state(f).interrupt_shadow==0);
        bm_286_boundary_t b=step(f); assert(b.kind==BM_286_BOUNDARY_INTERRUPT);
        assert(read_word(f,s.ss.base+s.sp-6)==s.ip);
    }
    for(unsigned legal=0;legal<2;++legal) {
        uint8_t code[11]; memset(code,0xf3,sizeof(code)); code[legal ? 9 : 10]=0xaa;
        bm_286_arch_state_t s=prepare(f,code,sizeof(code),1);
        set(f,&s); bm_286_boundary_t b;
        if(legal) {step(f); assert(state(f).ip==s.ip+10);}
        else {assert(bm_286_step(&f->cpu,&b)==BM_STATUS_UNSUPPORTED); assert(f->count==10);}
    }
    { /* REP opcode straddling segment end is refused, not wrapped on fetch. */
        const uint8_t code[]={0x90};
        bm_286_arch_state_t s=prepare(f,code,sizeof(code),1); s.ip=0xffff;
        f->ram[s.cs.base+s.ip]=0xf3; set(f,&s); bm_286_boundary_t b;
        assert(bm_286_step(&f->cpu,&b)==BM_STATUS_UNSUPPORTED && f->count==1);
    }
    { /* Diagnostic run budgets repetitions, not an entire 65535-element block. */
        const uint8_t code[]={0xf3,0xaa};
        bm_286_arch_state_t s=prepare(f,code,sizeof(code),9); bm_tick_t consumed=0;
        set(f,&s); assert(f->cpu.ops.run(f->cpu.context,3,&consumed)==BM_STATUS_OK);
        assert(consumed==3 && state(f).cx==6 && state(f).ip==s.ip);
    }
}
int main(void)
{
    fixture_t f={0}; bm_host_services_t host=bm_null_host_services(); bm_286_config_t config={0};
    f.ram=calloc(0x1000000,1); assert(f.ram);
    config.size=sizeof(config); config.version=BM_286_CONTRACT_VERSION;
    config.access=access_bus; config.access_context=&f; config.interrupt_ack=ack; config.interrupt_context=&f;
    config.bus_lock = lock_changed; config.pin_context = &f;
    config.trace=trace_boundary; config.trace_context=&f;
    assert(bm_286_create(&host,&config,&f.cpu)==BM_STATUS_OK);
    matrix(&f); count_space(&f); cache_and_import(&f); overlap(&f);
    interrupts(&f); failures(&f); prefixes_and_hold(&f);
    { /* Two different decoded continuations must never share private state. */
        fixture_t g={0}; bm_286_config_t other=config;
        g.ram=calloc(0x1000000,1); assert(g.ram);
        other.access_context=&g; other.interrupt_context=&g; other.trace_context=&g;
        assert(bm_286_create(&host,&other,&g.cpu)==BM_STATUS_OK);
        const uint8_t code_f[]={0xf3,0xaa}, code_g[]={0xf2,0xab};
        bm_286_arch_state_t sf=prepare(&f,code_f,sizeof(code_f),3);
        bm_286_arch_state_t sg=prepare(&g,code_g,sizeof(code_g),3);
        sf.ax=0x11; sg.ax=0x3344; set(&f,&sf); set(&g,&sg);
        for(unsigned i=0;i<3;++i) {
            step(&f); step(&g);
            assert(state(&f).di==sf.di+i+1 && state(&g).di==sg.di+2*(i+1));
            assert(f.ram[sf.es.base+sf.di+i]==0x11);
            assert(read_word(&g,sg.es.base+sg.di+2*i)==0x3344);
        }
        g.cpu.ops.destroy(g.cpu.context); free(g.ram);
    }
    f.cpu.ops.destroy(f.cpu.context); free(f.ram); return 0;
}
