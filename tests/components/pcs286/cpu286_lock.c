/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Authored architectural tests, not physical timing or hardware captures.
 */
#include <blumach/components/cpu_80286.h>
#include <blumach/platforms/null_host.h>
#include <blumach/components/at_bus.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct fixture {
    bm_cpu_t cpu;
    uint8_t *ram;
    bm_bus_transaction_t trace[64];
    unsigned count, fail_at, acknowledgements, traced, hold_at;
    bm_at_bus_t *bus;
    unsigned lock_edges, locked, hold, request, error_after, dma_calls;
    unsigned fail_ack, release_count;
    uint16_t release_ip;
    bm_286_boundary_t last_boundary;
} fixture_t;

static void hold_changed(void *context, int asserted)
{
    fixture_t *f=context; f->hold=(unsigned)asserted;
    assert(f->cpu.ops.signal(f->cpu.context,BM_286_SIGNAL_HOLD,asserted)==BM_STATUS_OK);
}
static void hold_ack(void *context, int asserted)
{
    fixture_t *f=context;
    assert(bm_at_bus_hold_ack(f->bus,asserted)==BM_STATUS_OK);
}
static void lock_changed(void *context, int asserted)
{
    fixture_t *f=context; bm_286_arch_state_t s;
    assert(f->locked!=(unsigned)asserted);
    f->locked=(unsigned)asserted; ++f->lock_edges;
    assert(bm_at_bus_set_lock(f->bus,asserted)==BM_STATUS_OK);
    assert(bm_286_get_arch_state(&f->cpu,&s)==BM_STATUS_OK);
    if(!asserted) { f->release_ip=s.ip; f->release_count=f->count; }
}
static bm_at_transfer_t dma_transfer(void)
{
    bm_at_transfer_t t={0}; t.master=BM_AT_MASTER_DMA16;
    t.requester_clock=(bm_clock_rate_t){4000000,1};
    t.bus.space=BM_ADDRESS_MEMORY; t.bus.operation=BM_BUS_READ;
    t.bus.address=0x500; t.bus.size=1; t.bus.alignment=1;
    t.bus.endianness=BM_ENDIAN_LITTLE; return t;
}
static bm_status_t access_bus(void *context, bm_at_transfer_t *at)
{
    fixture_t *f=context; bm_bus_transaction_t *t=&at->bus;
    if(at->master!=BM_AT_MASTER_CPU) { ++f->dma_calls; t->value=f->ram[t->address]; return BM_STATUS_OK; }
    assert(t->space!=BM_ADDRESS_IO && t->address+t->size<=0x1000000);
    assert(!t->wait_states && f->count<64 && t->alignment==t->size);
    assert(t->endianness==BM_ENDIAN_LITTLE);
    assert(t->attributes==(f->locked ? BM_BUS_TRANSACTION_LOCKED : 0U));
    if(t->operation==BM_BUS_FETCH) assert(!f->locked);
    f->trace[f->count++]=*t;
    if(f->locked && f->request) {
        bm_at_transfer_t dma=dma_transfer();
        assert(bm_at_bus_request(f->bus,BM_AT_MASTER_DMA16,1)==BM_STATUS_OK);
        assert(!f->hold);
        assert(bm_at_bus_access(f->bus,&dma)==BM_STATUS_IDLE);
        assert(!f->dma_calls); /* Actual AT arbiter excludes the competing master. */
    }
    if(f->hold_at==f->count) {
        assert(f->cpu.ops.signal(f->cpu.context,BM_286_SIGNAL_INTR,1)==BM_STATUS_OK);
    }
    if(f->count==f->fail_at && !f->error_after) return BM_STATUS_DEVICE_ERROR;
    if(t->operation!=BM_BUS_WRITE) t->value=0;
    for(unsigned i=0;i<t->size;++i) {
        if(t->operation==BM_BUS_WRITE) f->ram[(size_t)t->address+i]=(uint8_t)(t->value>>(8*i));
        else t->value|=(uint64_t)f->ram[(size_t)t->address+i]<<(8*i);
    }
    if(f->count==f->fail_at) return BM_STATUS_DEVICE_ERROR;
    t->wait_states=2; return BM_STATUS_OK;
}
static bm_status_t ack(void *context, unsigned phase, uint8_t *v, uint32_t *waits)
{
    fixture_t *f = context;
    assert(phase == (f->acknowledgements & 1U) && !*waits);
    assert(f->locked);
    ++f->acknowledgements;
    if(f->request) {
        bm_at_transfer_t dma=dma_transfer();
        assert(bm_at_bus_request(f->bus,BM_AT_MASTER_DMA16,1)==BM_STATUS_OK);
        assert(!f->hold && bm_at_bus_access(f->bus,&dma)==BM_STATUS_IDLE);
    }
    if(f->fail_ack==f->acknowledgements) return BM_STATUS_DEVICE_ERROR;
    *v = phase ? 0x30 : 0x77; return BM_STATUS_OK;
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
    bm_at_bus_reset(f->bus);
    assert(f->cpu.ops.reset(f->cpu.context) == BM_STATUS_OK);
    assert(!f->locked);
    f->lock_edges=f->request=f->error_after=f->dma_calls=0;
    f->fail_ack=f->release_count=0;
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

static uint16_t *reg(bm_286_arch_state_t *s,unsigned n)
{
    switch(n) {
    case 0:return &s->ax; case 1:return &s->cx; case 2:return &s->dx; case 3:return &s->bx;
    case 4:return &s->sp; case 5:return &s->bp; case 6:return &s->si; default:return &s->di;
    }
}
static void exchanged(fixture_t *f)
{
    unsigned cases=0;
    for(unsigned width=1;width<=2;++width)
        for(unsigned r=0;r<8;++r)
            for(unsigned pref=0;pref<5;++pref)
                for(unsigned odd=0;odd<2;++odd)
                    for(unsigned explicit_lock=0;explicit_lock<2;++explicit_lock) {
                        uint8_t code[6]; unsigned n=0;
                        if(explicit_lock) code[n++]=0xf0;
                        if(pref) code[n++]=(uint8_t)(0x26+8*(pref-1));
                        code[n++]=(uint8_t)(width==1 ? 0x86 : 0x87);
                        code[n++]=(uint8_t)(6+8*r); code[n++]=(uint8_t)odd; code[n++]=5;
                        bm_286_arch_state_t s=prepare(f,code,n,7), a, expected;
                        *reg(&s,width==1 ? r&3 : r)=0x1234; s.flags=0x8d7;
                        uint32_t bases[]={s.es.base,s.cs.base,s.ss.base,s.ds.base};
                        uint32_t address=bases[pref ? pref-1 : 3]+0x500+odd;
                        word(f,address,0xabcd); set(f,&s); f->request=1;
                        bm_286_boundary_t b=step(f);
                        a=state(f); expected=s; expected.ip=(uint16_t)(s.ip+n);
                        if(width==2) *reg(&expected,r)=0xabcd;
                        else {
                            uint16_t *v=reg(&expected,r&3);
                            *v=(uint16_t)(r&4 ? ((*v&255)|0xcd00) : ((*v&0xff00)|0xcd));
                        }
                        same(&a,&expected);
                        assert((read_word(f,address)&(width==1 ? 255U : 65535U))==
                               (width==2 ? 0x1234U : r&4 ? 0x12U : 0x34U));
                        assert(b.kind==BM_286_BOUNDARY_INSTRUCTION && f->lock_edges==2 && !f->locked);
                        assert(f->release_ip==expected.ip && f->hold && !f->dma_calls);
                        unsigned count=f->count;
                        bm_at_transfer_t dma=dma_transfer();
                        assert(bm_at_bus_access(f->bus,&dma)==BM_STATUS_IDLE);
                        assert(bm_286_step(&f->cpu,&b)==BM_STATUS_IDLE && b.kind==BM_286_BOUNDARY_HOLD);
                        assert(f->count==count && bm_at_bus_access(f->bus,&dma)==BM_STATUS_OK);
                        assert(f->dma_calls==1);
                        assert(bm_at_bus_request(f->bus,BM_AT_MASTER_DMA16,0)==BM_STATUS_OK);
                        ++cases;
                    }
    assert(cases==320);
}
static void rmw_equivalence(fixture_t *f)
{
    /* Existing arithmetic oracles validate math; here compare locked and
     * unlocked forms while independently checking lock windows and transfers. */
    for(unsigned form=0;form<43;++form)
        for(unsigned odd=0;odd<2;++odd)
            for(unsigned pref=0;pref<5;++pref) {
                uint8_t body[8]; unsigned n=0, width, group=0;
                if(pref) body[n++]=(uint8_t)(0x26+8*(pref-1));
                if(form<14) {width=1+(form&1); body[n++]=(uint8_t)((form/2)*8+(width-1));}
                else if(form<35) {
                    unsigned v=form-14; group=v/3; width=v%3 ? 2 : 1;
                    body[n++]=(uint8_t)(v%3==0 ? 0x80 : v%3==1 ? 0x81 : 0x83);
                } else {
                    unsigned v=form-35; width=1+(v&1);
                    group=v/2<2 ? 2+v/2 : v/2-2;
                    body[n++]=(uint8_t)((v/2<2 ? 0xf6 : 0xfe)+(width-1));
                }
                body[n++]=(uint8_t)(6+8*group); body[n++]=(uint8_t)odd; body[n++]=5;
                if(form>=14 && form<35) {body[n++]=0x93; if((form-14)%3==1) body[n++]=0x71;}
                bm_286_arch_state_t result={0}; uint16_t memory=0;
                for(unsigned locked=0;locked<2;++locked) {
                    uint8_t code[9]; unsigned length=0; if(locked) code[length++]=0xf0;
                    memcpy(code+length,body,n); length+=n;
                    bm_286_arch_state_t s=prepare(f,code,length,3), a;
                    s.ax=0x1234; s.flags=0x8d7;
                    uint32_t bases[]={s.es.base,s.cs.base,s.ss.base,s.ds.base};
                    uint32_t address=bases[pref ? pref-1 : 3]+0x500+odd;
                    word(f,address,0x93e7); set(f,&s); step(f); a=state(f);
                    assert(f->lock_edges==(locked ? 2U : 0U) && !f->locked);
                    for(unsigned i=0;i<f->count;++i)
                        assert(f->trace[i].attributes==((locked && f->trace[i].operation!=BM_BUS_FETCH) ? BM_BUS_TRANSACTION_LOCKED : 0U));
                    if(!locked) {result=a; memory=read_word(f,address);}
                    else {--a.ip; same(&a,&result); assert(read_word(f,address)==memory);}
                }
            }
}
static void failures(fixture_t *f)
{
    const uint8_t codes[][7]={
        {0x87,6,1,5}, {0xf0,0x81,6,1,5,7,0}, {0xf0,0xf7,0x16,1,5},
        {0xf0,0xc7,6,1,5,0x34,0x12}, {0xf0,0xa1,1,5}, {0xf0,0xa3,1,5},
        {0xf0,0x8e,0x16,1,5}, {0xf0,0x8c,0x16,1,5},
        {0xf0,0xc1,0x26,1,5,17}, {0xf0,0xd3,0x1e,1,5},
        {0xf0,0xc1,0x26,1,5,0}
    };
    const unsigned lengths[]={4,7,5,7,4,4,5,5,6,5,6};
    for(unsigned k=0;k<sizeof(lengths)/sizeof(lengths[0]);++k)
        for(unsigned after=0;after<2;++after) {
            unsigned total=0;
            for(unsigned fail=0;fail<=total;++fail) {
                uint8_t code[7]={0}; memcpy(code,codes[k],lengths[k]);
                unsigned length=lengths[k];
                bm_286_arch_state_t s=prepare(f,code,length,3), a; bm_286_boundary_t b;
                word(f,0x501,0xabcd); set(f,&s); f->fail_at=fail; f->error_after=after; f->request=1;
                if(!fail) {step(f); total=f->count; continue;}
                assert(bm_286_step(&f->cpu,&b)==BM_STATUS_DEVICE_ERROR);
                a=state(f); same(&a,&s); assert(!f->locked && !f->traced && f->count==fail);
                assert(f->lock_edges==(fail>length ? 2U : 0U));
                if(f->lock_edges) assert(f->release_ip==s.ip);
                for(unsigned i=0;i<fail-(after ? 0U : 1U);++i) {
                    bm_bus_transaction_t *t=&f->trace[i];
                    if(t->operation==BM_BUS_WRITE)
                        for(unsigned j=0;j<t->size;++j) assert(f->ram[t->address+j]==(uint8_t)(t->value>>(8*j)));
                }
                assert(bm_286_step(&f->cpu,&b)==BM_STATUS_INVALID_STATE && f->count==fail);
                assert(!f->locked && !(f->lock_edges&1));
            }
        }
}
static void memory_equivalence(fixture_t *f, const uint8_t *body, unsigned n,
                               unsigned pref, unsigned odd, unsigned count)
{
    bm_286_arch_state_t result={0}; uint16_t memory=0;
    for(unsigned locked=0;locked<2;++locked) {
        uint8_t code[10]; unsigned length=0;
        if(locked) code[length++]=0xf0;
        if(pref) code[length++]=(uint8_t)(0x26+8*(pref-1));
        memcpy(code+length,body,n); length+=n;
        bm_286_arch_state_t s=prepare(f,code,length,count), a;
        s.ax=0x1234; s.flags=0x8d7;
        uint32_t bases[]={s.es.base,s.cs.base,s.ss.base,s.ds.base};
        uint32_t address=bases[pref ? pref-1 : 3]+0x500+odd;
        word(f,address,0x93e7); set(f,&s); f->request=locked;
        step(f); a=state(f);
        assert(f->lock_edges==(locked ? 2U : 0U) && !f->locked);
        assert(!f->dma_calls);
        if(locked) assert(f->release_ip==a.ip && f->hold);
        for(unsigned i=0;i<f->count;++i)
            assert(f->trace[i].attributes==((locked && f->trace[i].operation!=BM_BUS_FETCH) ? BM_BUS_TRANSACTION_LOCKED : 0U));
        if(!locked) {result=a; memory=read_word(f,address);}
        else {--a.ip; same(&a,&result); assert(read_word(f,address)==memory);}
    }
}
static void moves_and_shifts(fixture_t *f)
{
    /* MOV direction, byte/word aliases, segment registers and moffs/immediates.
     * Compare the existing data-transfer oracle's implementation without LOCK;
     * independently assert memory-only writes are not turned into RMW reads. */
    for(unsigned pref=0;pref<5;++pref)
        for(unsigned odd=0;odd<2;++odd) {
            for(unsigned opcode=0x88;opcode<=0x8e;++opcode) {
                if(opcode==0x8d) continue;
                for(unsigned r=0;r<(opcode==0x8c || opcode==0x8e ? 4U : 8U);++r) {
                    if(opcode==0x8e && r==1) continue;
                    uint8_t body[]={(uint8_t)opcode,(uint8_t)(6+8*r),(uint8_t)odd,5};
                    memory_equivalence(f,body,sizeof(body),pref,odd,3);
                    if(opcode==0x88 || opcode==0x89 || opcode==0x8c)
                        for(unsigned i=0;i<f->count;++i)
                            assert(f->trace[i].operation!=BM_BUS_READ);
                }
            }
            for(unsigned opcode=0xa0;opcode<=0xa3;++opcode) {
                uint8_t body[]={(uint8_t)opcode,(uint8_t)odd,5};
                memory_equivalence(f,body,sizeof(body),pref,odd,3);
                for(unsigned i=0;i<f->count;++i)
                    assert(f->trace[i].operation!=((opcode&2) ? BM_BUS_READ : BM_BUS_WRITE));
            }
            for(unsigned width=1;width<=2;++width) {
                uint8_t body[]={(uint8_t)(0xc5+width),6,(uint8_t)odd,5,0x34,0x12};
                memory_equivalence(f,body,4+width,pref,odd,3);
                assert((read_word(f,0x500+odd+(pref==1 ? 0x20000 : pref==2 ? 0x30000 : pref==3 ? 0x10000 : 0)) &
                        (width==1 ? 255U : 65535U))==(width==1 ? 0x34U : 0x1234U));
                for(unsigned i=0;i<f->count;++i) assert(f->trace[i].operation!=BM_BUS_READ);
            }
        }
    const uint8_t opcodes[]={0xc0,0xc1,0xd0,0xd1,0xd2,0xd3};
    for(unsigned form=0;form<6;++form)
        for(unsigned group=0;group<8;++group) {
            if(group==6) continue;
            for(unsigned count=0;count<256;++count)
                for(unsigned odd=0;odd<2;++odd) {
                    uint8_t body[]={opcodes[form],(uint8_t)(6+8*group),(uint8_t)odd,5,(uint8_t)count};
                    memory_equivalence(f,body,form<2 ? 5U : 4U, count%5,odd,count);
                    if((form<2 || form>=4) && !(count&31))
                        for(unsigned i=0;i<f->count;++i) assert(f->trace[i].operation!=BM_BUS_WRITE);
                }
        }
}
static void rejection_and_interrupt(fixture_t *f)
{
    const uint8_t rejected[][6]={
        {0xf0,0x87,0xc0}, {0xf0,0x03,6,0,5}, {0xf0,0x81,0x3e,0,5,1},
        {0xf0,0xf7,6,0,5,1}, {0xf0,0xff,0x36,0,5},
        {0xf0,0xf3,0xa4}, {0xf3,0xf0,0xa4}, {0xf0,0x90},
        {0xf0,0xd1,0xe0}, {0xf0,0xc1,0x36,0,5,1},
        {0xf0,0x89,0xc0}, {0xf0,0xc7,0xc0,1,0},
        {0xf0,0x8e,0x0e,0,5}, {0xf0,0x8c,0x26,0,5},
        {0xf0,0xc7,0x0e,0,5,1}
    };
    for(unsigned k=0;k<sizeof(rejected)/sizeof(rejected[0]);++k) {
        bm_286_arch_state_t s=prepare(f,rejected[k],sizeof(rejected[k]),3), a;
        bm_286_boundary_t b; set(f,&s);
        assert(bm_286_step(&f->cpu,&b)==BM_STATUS_UNSUPPORTED);
        a=state(f); same(&a,&s); assert(!f->lock_edges);
        for(unsigned i=0;i<f->count;++i) assert(f->trace[i].operation==BM_BUS_FETCH);
    }
    {
        const uint8_t codes[][7]={
            {0xf0,0x87,6,0xff,0xff}, {0xf0,0xa1,0xff,0xff},
            {0xf0,0xa3,0xff,0xff}, {0xf0,0xc7,6,0xff,0xff,1,0},
            {0xf0,0xd1,0x26,0xff,0xff}
        };
        for(unsigned k=0;k<sizeof(codes)/sizeof(codes[0]);++k)
            for(unsigned invalid=0;invalid<2;++invalid) {
                bm_286_arch_state_t s=prepare(f,codes[k],sizeof(codes[k]),1), a;
                bm_286_boundary_t b;
                if(invalid) s.ds.valid=0;
                set(f,&s);
                assert(bm_286_step(&f->cpu,&b)==BM_STATUS_UNSUPPORTED && !f->lock_edges);
                a=state(f); same(&a,&s);
                for(unsigned i=0;i<f->count;++i) assert(f->trace[i].operation==BM_BUS_FETCH);
            }
    }
    { /* IRQ from the first operand transfer waits for commit/release. */
        const uint8_t codes[][5]={
            {0xf0,0x87,6,1,5}, {0xf0,0x89,6,1,5},
            {0xf0,0x8b,6,1,5}, {0xf0,0xd3,0x26,1,5}
        };
        for(unsigned k=0;k<sizeof(codes)/sizeof(codes[0]);++k) {
            bm_286_arch_state_t s=prepare(f,codes[k],sizeof(codes[k]),1);
            s.flags=0x202; word(f,0x501,0x4567); set(f,&s); f->hold_at=6;
            step(f); assert(!f->acknowledgements && !f->locked && f->lock_edges==2);
            bm_286_boundary_t b=step(f);
            assert(b.has_vector && b.vector==0x30 && f->acknowledgements==2);
            assert(read_word(f,s.ss.base+s.sp-6)==s.ip+sizeof(codes[k]));
        }
    }
}
static void interrupt_lock(fixture_t *f)
{
    const uint8_t code[]={0x90};
    for(unsigned odd=0;odd<2;++odd) {
        bm_286_arch_state_t s=prepare(f,code,sizeof(code),1);
        s.sp=(uint16_t)(s.sp+odd); s.flags=0x202; set(f,&s); f->request=1;
        assert(f->cpu.ops.signal(f->cpu.context,BM_286_SIGNAL_INTR,1)==BM_STATUS_OK);
        bm_286_boundary_t b=step(f);
        assert(b.vector==0x30 && f->acknowledgements==2 && f->lock_edges==2 && !f->locked);
        assert(f->release_count==1+odd && f->release_ip==s.ip);
        for(unsigned i=0;i<f->count;++i) {
            assert(f->trace[i].operation!=BM_BUS_FETCH);
            assert(f->trace[i].attributes==(i<1+odd ? BM_BUS_TRANSACTION_LOCKED : 0U));
        }
        assert(read_word(f,s.ss.base+s.sp-2)==s.flags);
        assert(f->hold && !f->dma_calls);
        bm_at_transfer_t dma=dma_transfer();
        assert(bm_at_bus_access(f->bus,&dma)==BM_STATUS_IDLE);
        assert(bm_286_step(&f->cpu,&b)==BM_STATUS_IDLE && b.kind==BM_286_BOUNDARY_HOLD);
        assert(bm_at_bus_access(f->bus,&dma)==BM_STATUS_OK && f->dma_calls==1);
        assert(bm_at_bus_request(f->bus,BM_AT_MASTER_DMA16,0)==BM_STATUS_OK);
        unsigned total=f->count;
        for(unsigned after=0;after<2;++after)
            for(unsigned fail=1;fail<=total+2;++fail) {
                s=prepare(f,code,sizeof(code),1); s.sp=(uint16_t)(s.sp+odd);
                s.flags=0x202; set(f,&s); f->request=1; f->error_after=after;
                if(fail<=2) f->fail_ack=fail; else f->fail_at=fail-2;
                assert(f->cpu.ops.signal(f->cpu.context,BM_286_SIGNAL_INTR,1)==BM_STATUS_OK);
                assert(bm_286_step(&f->cpu,&b)==BM_STATUS_DEVICE_ERROR);
                bm_286_arch_state_t a=state(f); same(&a,&s);
                assert(!f->locked && f->lock_edges==2 && !f->traced);
                assert(f->acknowledgements==(fail<=2 ? fail : 2U));
                unsigned count=f->count, acks=f->acknowledgements;
                assert(bm_286_step(&f->cpu,&b)==BM_STATUS_INVALID_STATE);
                assert(f->count==count && f->acknowledgements==acks);
                for(unsigned i=0;i<f->count;++i)
                    if((!f->fail_at || i+1<f->fail_at || after) && f->trace[i].operation==BM_BUS_WRITE)
                        for(unsigned j=0;j<f->trace[i].size;++j)
                            assert(f->ram[f->trace[i].address+j]==(uint8_t)(f->trace[i].value>>(8*j)));
            }
    }
    for(unsigned event=0;event<3;++event) {
        const uint8_t op[]={0xcc};
        bm_286_arch_state_t s=prepare(f,op,sizeof(op),1);
        if(event==0) s.nmi_pending=1;
        if(event==1) s.trap_pending=1;
        set(f,&s); step(f); assert(!f->lock_edges && !f->acknowledgements);
    }
    for(unsigned invalid=0;invalid<2;++invalid) {
        bm_286_arch_state_t s=prepare(f,code,sizeof(code),1); bm_286_boundary_t b;
        s.flags=0x202; if(invalid) s.ss.valid=0; else s.idtr.limit=0;
        set(f,&s); assert(f->cpu.ops.signal(f->cpu.context,BM_286_SIGNAL_INTR,1)==BM_STATUS_OK);
        assert(bm_286_step(&f->cpu,&b)==BM_STATUS_UNSUPPORTED);
        assert(f->acknowledgements==2 && !f->count && !f->locked && f->lock_edges==2);
    }
}
int main(void)
{
    fixture_t f={0}; bm_host_services_t host=bm_null_host_services();
    bm_at_bus_config_t bus={0}; bm_286_config_t cpu={0};
    f.ram=calloc(0x1000000,1); assert(f.ram);
    bus.cpu_clock=(bm_clock_rate_t){12000000,1}; bus.isa_clock=(bm_clock_rate_t){8000000,1};
    bus.memory=access_bus; bus.io=access_bus; bus.decode_context=&f;
    bus.hold=hold_changed; bus.hold_context=&f;
    assert(bm_at_bus_create(&host,&bus,&f.bus)==BM_STATUS_OK);
    cpu.size=sizeof(cpu); cpu.version=BM_286_CONTRACT_VERSION;
    cpu.access=bm_at_bus_cpu_access; cpu.access_context=f.bus;
    cpu.bus_lock=lock_changed; cpu.hold_ack=hold_ack; cpu.pin_context=&f;
    cpu.interrupt_ack=ack; cpu.interrupt_context=&f; cpu.trace=trace_boundary; cpu.trace_context=&f;
    assert(bm_286_create(&host,&cpu,&f.cpu)==BM_STATUS_OK);
    exchanged(&f); rmw_equivalence(&f); moves_and_shifts(&f);
    failures(&f); rejection_and_interrupt(&f);
    interrupt_lock(&f);
    /* Missing exclusion adapter must refuse before touching the PIC or RAM. */
    f.cpu.ops.destroy(f.cpu.context); cpu.bus_lock=NULL;
    assert(bm_286_create(&host,&cpu,&f.cpu)==BM_STATUS_OK);
    {
        const uint8_t code[]={0x90}; bm_286_boundary_t b;
        bm_286_arch_state_t s=prepare(&f,code,sizeof(code),1); s.flags=0x202; set(&f,&s);
        assert(f.cpu.ops.signal(f.cpu.context,BM_286_SIGNAL_INTR,1)==BM_STATUS_OK);
        assert(bm_286_step(&f.cpu,&b)==BM_STATUS_UNSUPPORTED);
        assert(!f.count && !f.acknowledgements && !f.lock_edges);
    }
    bm_at_bus_reset(f.bus); assert(f.cpu.ops.reset(f.cpu.context)==BM_STATUS_OK);
    bm_at_bus_destroy(f.bus); f.cpu.ops.destroy(f.cpu.context); free(f.ram); return 0;
}
