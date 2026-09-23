/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Authored string I/O tests, not physical bus timing or chip captures.
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
    unsigned in_bytes, out_bytes, error_after;
    uint8_t output[128];
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
    assert(!t->wait_states && f->count < 64);
    assert(t->attributes == (f->locked ? BM_BUS_TRANSACTION_LOCKED : 0U));
    assert(t->endianness == BM_ENDIAN_LITTLE && t->alignment == t->size);
    assert(t->size == 1 || (t->size == 2 && !(t->address & 1U)));
    f->trace[f->count++] = *t;
    if (f->hold_at == f->count)
        assert(f->cpu.ops.signal(f->cpu.context, BM_286_SIGNAL_HOLD, 1) == BM_STATUS_OK);
    if (f->count == f->fail_at && !f->error_after) return BM_STATUS_DEVICE_ERROR;
    if (t->operation != BM_BUS_WRITE) t->value = 0;
    if (t->space == BM_ADDRESS_IO) {
        assert(t->address + t->size <= 65536U && t->operation != BM_BUS_FETCH);
        for (unsigned i=0;i<t->size;++i) {
            if (t->operation == BM_BUS_WRITE) {
                assert(f->out_bytes < sizeof(f->output));
                f->output[f->out_bytes++]=(uint8_t)(t->value>>(8*i));
            } else {
                t->value |= (uint64_t)(uint8_t)(0x40+f->in_bytes++)<<(8*i);
            }
        }
    } else {
        assert(t->space == (t->operation == BM_BUS_FETCH ? BM_ADDRESS_PROGRAM : BM_ADDRESS_DATA));
        assert(t->address + t->size <= 0x1000000);
        for (unsigned i=0;i<t->size;++i) {
            if (t->operation == BM_BUS_WRITE)
                f->ram[(size_t)t->address+i]=(uint8_t)(t->value>>(8*i));
            else t->value |= (uint64_t)f->ram[(size_t)t->address+i]<<(8*i);
        }
    }
    if (f->count == f->fail_at) return BM_STATUS_DEVICE_ERROR;
    t->wait_states=2; return BM_STATUS_OK;
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
    f->in_bytes=f->out_bytes=f->error_after=0;
    memset(f->output,0,sizeof(f->output));
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
    const uint16_t ports[]={0,0x81,0xfffe,0xffff};
    unsigned cases=0;
    for(unsigned op=0x6c;op<=0x6f;++op)
        for(unsigned rep=0;rep<3;++rep)
            for(unsigned pref=0;pref<5;++pref)
                for(unsigned df=0;df<2;++df)
                    for(unsigned odd=0;odd<2;++odd)
                        for(unsigned p=0;p<4;++p)
                            for(unsigned count=0;count<3;++count) {
                                unsigned output=(op&2)!=0, size=(op&1)+1, length=0;
                                uint8_t code[3];
                                if(pref) code[length++]=(uint8_t)(0x26+8*(pref-1));
                                if(rep) code[length++]=(uint8_t)(rep==1 ? 0xf2 : 0xf3);
                                code[length++]=(uint8_t)op;
                                bm_286_arch_state_t s=prepare(f,code,length,count), expected, a;
                                s.si=(uint16_t)(0x508+odd); s.di=(uint16_t)(0x608+odd);
                                s.dx=ports[p]; s.flags=(uint16_t)(0x8d7 | (df ? 0x400 : 0));
                                uint32_t bases[]={s.es.base,s.cs.base,s.ss.base,s.ds.base};
                                uint32_t base=output ? bases[pref ? pref-1 : 3] : s.es.base;
                                uint16_t offset=output ? s.si : s.di;
                                unsigned iterations=rep ? count : 1;
                                for(unsigned n=0;n<iterations;++n) {
                                    uint16_t at=(uint16_t)(offset+(df ? -(int)(n*size) : (int)(n*size)));
                                    for(unsigned j=0;j<size;++j) f->ram[base+at+j]=(uint8_t)(0x90+n*size+j);
                                }
                                /* Ignore unused segments, even if an INS override selects one. */
                                if(!output) s.ds.valid=s.ss.valid=0;
                                else if(pref!=1) s.es.valid=0;
                                if(rep && !count) s.es.valid=s.ds.valid=s.ss.valid=0;
                                set(f,&s); expected=s;
                                for(unsigned n=0;n<(iterations ? iterations : 1);++n) {
                                    unsigned before=f->count, elements=iterations ? n+1 : 0;
                                    bm_286_boundary_t b=step(f);
                                    if(iterations) {
                                        uint16_t at=(uint16_t)(offset+(df ? -(int)(n*size) : (int)(n*size)));
                                        unsigned mem_parts=size==2 && (at&1) ? 2 : 1;
                                        unsigned io_parts=size==2 && (s.dx&1) ? 2 : 1;
                                        unsigned fetch=n ? 0 : length;
                                        assert(f->count-before==fetch+mem_parts+io_parts);
                                        unsigned first=before+fetch;
                                        /* INS reads all port fragments then writes memory;
                                         * OUTS reads all memory fragments then writes the port. */
                                        for(unsigned j=0;j<mem_parts+io_parts;++j) {
                                            bm_bus_transaction_t *t=&f->trace[first+j];
                                            unsigned in_first=j<(output ? mem_parts : io_parts);
                                            unsigned is_io=output ? !in_first : in_first;
                                            unsigned fragment=in_first ? j : j-(output ? mem_parts : io_parts);
                                            unsigned parts=is_io ? io_parts : mem_parts;
                                            assert(t->space==(is_io ? BM_ADDRESS_IO : BM_ADDRESS_DATA));
                                            assert(t->operation==(in_first ? BM_BUS_READ : BM_BUS_WRITE));
                                            assert(t->size==(parts==2 ? 1U : size));
                                            assert(t->address==(is_io ? (uint16_t)(s.dx+fragment) : base+at+fragment));
                                        }
                                        for(unsigned j=0;j<size;++j) {
                                            if(output) assert(f->output[n*size+j]==(uint8_t)(0x90+n*size+j));
                                            else assert(f->ram[base+at+j]==(uint8_t)(0x40+n*size+j));
                                        }
                                    } else assert(f->count==length);
                                    unsigned more=rep && elements<count;
                                    expected.ip=more ? s.ip : (uint16_t)(s.ip+length);
                                    expected.cx=rep ? (uint16_t)(count-elements) : s.cx;
                                    if(output) expected.si=(uint16_t)(s.si+(df ? -(int)(elements*size) : (int)(elements*size)));
                                    else expected.di=(uint16_t)(s.di+(df ? -(int)(elements*size) : (int)(elements*size)));
                                    a=state(f); same(&a,&expected);
                                    assert(b.kind==(more ? BM_286_BOUNDARY_REP_ITERATION : BM_286_BOUNDARY_INSTRUCTION));
                                    assert(f->in_bytes==(output ? 0 : elements*size));
                                    assert(f->out_bytes==(output ? elements*size : 0));
                                }
                                ++cases;
                            }
    assert(cases==2880);
}
static void failures(fixture_t *f)
{
    for(unsigned op=0x6c;op<=0x6f;++op)
        for(unsigned rep=0;rep<2;++rep)
            for(unsigned odd_mem=0;odd_mem<2;++odd_mem)
                for(unsigned odd_port=0;odd_port<2;++odd_port)
                    for(unsigned later=0;later<2;++later)
                        for(unsigned after=0;after<2;++after) {
                            unsigned total=0;
                            for(unsigned fail=0;fail<=total;++fail) {
                                uint8_t code[]={0xf3,(uint8_t)op};
                                bm_286_arch_state_t s=prepare(f,code+(rep ? 0 : 1),rep ? 2 : 1,3), before, a;
                                bm_286_boundary_t b; s.si=(uint16_t)(0x500+odd_mem);
                                s.di=(uint16_t)(0x600+odd_mem); s.dx=(uint16_t)(0xfffe + odd_port);
                                memset(f->ram+0x500,0x96,16); memset(f->ram+0x20600,0x15,16);
                                set(f,&s);
                                if(later) {
                                    if(!rep) continue;
                                    step(f);
                                }
                                before=state(f); f->count=f->traced=0;
                                unsigned in_before=f->in_bytes, out_before=f->out_bytes;
                                f->fail_at=fail; f->error_after=after;
                                if(!fail) {step(f); total=f->count; continue;}
                                assert(bm_286_step(&f->cpu,&b)==BM_STATUS_DEVICE_ERROR);
                                a=state(f); same(&a,&before);
                                assert(f->count==fail && !f->traced);
                                unsigned in_expected=in_before, out_expected=out_before;
                                for(unsigned i=0;i<fail-(after ? 0U : 1U);++i) {
                                    bm_bus_transaction_t *t=&f->trace[i];
                                    if(t->space==BM_ADDRESS_IO) {
                                        if(t->operation==BM_BUS_READ) in_expected+=t->size;
                                        else out_expected+=t->size;
                                    } else if(t->operation==BM_BUS_WRITE) {
                                        for(unsigned j=0;j<t->size;++j)
                                            assert(f->ram[t->address+j]==(uint8_t)(t->value>>(8*j)));
                                    }
                                }
                                assert(f->in_bytes==in_expected && f->out_bytes==out_expected);
                                for(unsigned i=out_before;i<out_expected;++i) assert(f->output[i]==0x96);
                                assert(bm_286_step(&f->cpu,&b)==BM_STATUS_INVALID_STATE);
                                assert(f->count==fail && f->in_bytes==in_expected && f->out_bytes==out_expected);
                            }
                        }
}
static void events(fixture_t *f)
{
    for(unsigned op=0x6c;op<=0x6f;++op)
        for(unsigned event=0;event<4;++event) {
            const uint8_t code[]={0x3e,0xf3,(uint8_t)op};
            bm_286_arch_state_t s=prepare(f,code,sizeof(code),2), a;
            unsigned size=(op&1)+1, output=(op&2)!=0;
            s.dx=0xffff; s.flags=(uint16_t)(0x202 | (event==2 ? 0x100 : 0));
            for(unsigned i=0;i<4;++i) f->ram[0x500+i]=(uint8_t)(0x80+i);
            set(f,&s);
            /* HOLD is raised inside the element, before it completes. */
            if(event==3) f->hold_at=4;
            step(f); a=state(f); assert(a.cx==1 && a.ip==s.ip);
            if(event==3) {
                unsigned count=f->count; bm_286_boundary_t b;
                assert(bm_286_step(&f->cpu,&b)==BM_STATUS_IDLE && b.kind==BM_286_BOUNDARY_HOLD);
                assert(f->count==count);
                assert(f->cpu.ops.signal(f->cpu.context,BM_286_SIGNAL_HOLD,0)==BM_STATUS_OK);
            } else {
                if(event<2) assert(f->cpu.ops.signal(f->cpu.context,
                    event==0 ? BM_286_SIGNAL_INTR : BM_286_SIGNAL_NMI,1)==BM_STATUS_OK);
                bm_286_boundary_t b=step(f);
                assert(b.has_vector && b.vector==(event==0 ? 0x30 : event==1 ? 2 : 1));
                assert(read_word(f,s.ss.base+s.sp-6)==s.ip);
                if(event<2) assert(f->cpu.ops.signal(f->cpu.context,
                    event==0 ? BM_286_SIGNAL_INTR : BM_286_SIGNAL_NMI,0)==BM_STATUS_OK);
                step(f); /* IRET */
            }
            step(f); a=state(f);
            assert(a.cx==0 && a.ip==s.ip+3);
            assert(f->in_bytes==(output ? 0 : size*2) && f->out_bytes==(output ? size*2 : 0));
            for(unsigned i=0;i<size*2;++i) {
                if(output) assert(f->output[i]==(uint8_t)(0x80+i));
                else assert(f->ram[s.es.base+s.di+i]==(uint8_t)(0x40+i));
            }
            if(event==2) {
                bm_286_boundary_t b=step(f); assert(b.has_vector && b.vector==1);
                assert(read_word(f,s.ss.base+s.sp-6)==s.ip+3);
            }
        }
}
static void limits(fixture_t *f)
{
    for(unsigned op=0x6c;op<=0x6f;++op)
        for(unsigned mode=0;mode<5;++mode) {
            uint8_t code[]={0xf3,(uint8_t)op};
            bm_286_arch_state_t s=prepare(f,code,sizeof(code),2), a;
            bm_286_boundary_t b; unsigned output=(op&2)!=0, size=(op&1)+1;
            if(mode==0) {if(output) s.ds.valid=0; else s.es.valid=0;}
            if(mode==1) {if(output) s.ds.limit=0x100; else s.es.limit=0x100;}
            if(mode==2) s.msw|=1;
            if(mode==3) code[0]=0xf0;
            if(mode==4) {
                s.si=s.di=0xffff;
                if(size==1) { /* A byte at FFFF is legal, then offset wraps. */
                    set(f,&s); step(f); a=state(f);
                    assert((output ? a.si : a.di)==0 && a.cx==1);
                    step(f); assert(state(f).cx==0); continue;
                }
            }
            memcpy(f->ram+0x30100,code,sizeof(code)); set(f,&s);
            assert(bm_286_step(&f->cpu,&b)==BM_STATUS_UNSUPPORTED);
            a=state(f); same(&a,&s);
            assert(!f->in_bytes && !f->out_bytes && !f->traced);
            for(unsigned i=0;i<f->count;++i) assert(f->trace[i].operation==BM_BUS_FETCH);
            unsigned count=f->count;
            assert(bm_286_step(&f->cpu,&b)==BM_STATUS_INVALID_STATE && f->count==count);
        }
    { /* 24-bit host-neutral physical wrap, no CPU-side A20 masking. */
        const uint8_t code[]={0x6d};
        bm_286_arch_state_t s=prepare(f,code,sizeof(code),0);
        s.es.base=0xffffff; s.di=0; s.dx=0xfffe; set(f,&s); step(f);
        assert(f->ram[0xffffff]==0x40 && f->ram[0]==0x41 && state(f).di==2);
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
    matrix(&f); failures(&f); events(&f); limits(&f);
    f.cpu.ops.destroy(f.cpu.context); free(f.ram); return 0;
}
