/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Authored functional refresh/CPU/PIT/61h integration, no firmware vectors. */
#include "refresh.h"
#include "port61.h"
#include <blumach/components/cpu_80286.h>
#include <blumach/platforms/null_host.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct fixture {
    bm_at_bus_t *bus;
    bm_pcs286_refresh_t refresh;
    bm_cpu_t cpu;
    bm_engine_t *engine;
    bm_pit8254_t *pit;
    bm_at_clock_link_t *clock;
    bm_pcs286_port61_t port;
    unsigned holds, fetches, decodes, rises, completions;
    int hold, inspect, cpu_wired, last_out1;
} fixture_t;

static bm_pcs286_refresh_state_t state(fixture_t *f)
{
    bm_pcs286_refresh_state_t s;
    assert(bm_pcs286_refresh_state(&f->refresh,&s)==BM_STATUS_OK);
    return s;
}
static bm_at_bus_arbitration_t arbitration(fixture_t *f)
{
    bm_at_bus_arbitration_t a;
    assert(bm_at_bus_arbitration(f->bus,&a)==BM_STATUS_OK);
    return a;
}
static void hold(void *context,int level)
{
    fixture_t *f=context;
    ++f->holds; f->hold=level;
    if(f->inspect) {
        bm_pcs286_refresh_state_t s=state(f);
        assert(s.pending==level);
        assert(bm_pcs286_refresh_service(&f->refresh)==BM_STATUS_INVALID_STATE);
        assert(bm_pcs286_refresh_pit_input(&f->refresh,0)==BM_STATUS_INVALID_STATE);
        assert(bm_pcs286_refresh_reset(&f->refresh)==BM_STATUS_INVALID_STATE);
    }
    if(f->cpu_wired)
        assert(f->cpu.ops.signal(f->cpu.context,BM_286_SIGNAL_HOLD,level)==BM_STATUS_OK);
}
static void acknowledge(void *context,int level)
{
    fixture_t *f=context;
    assert(bm_at_bus_hold_ack(f->bus,level)==BM_STATUS_OK);
}
static bm_status_t decode(void *context,bm_at_transfer_t *t)
{
    fixture_t *f=context;
    static const uint8_t program[]={0x90,0xe4,0x61,0xf4}; /* NOP; IN AL,61h; HLT */
    ++f->decodes;
    if(t->bus.space==BM_ADDRESS_IO && f->pit)
        return bm_pcs286_port61_io(&f->port,&t->bus);
    if(t->master==BM_AT_MASTER_CPU) {
        assert(t->bus.operation==BM_BUS_FETCH && t->bus.address>=0xfffff0);
        assert(t->bus.address-0xfffff0<sizeof(program));
        ++f->fetches; t->bus.value=program[t->bus.address-0xfffff0];
    } else t->bus.value=0xa5;
    return BM_STATUS_OK;
}
static void start(fixture_t *f)
{
    bm_host_services_t h=bm_null_host_services();
    bm_at_bus_config_t c={0};
    memset(f,0,sizeof(*f));
    c.cpu_clock=(bm_clock_rate_t){12000000,1}; c.isa_clock=(bm_clock_rate_t){8000000,1};
    c.memory=c.io=decode; c.decode_context=f; c.hold=hold; c.hold_context=f;
    assert(bm_at_bus_create(&h,&c,&f->bus)==BM_STATUS_OK);
    assert(bm_pcs286_refresh_initialize(&f->refresh,f->bus)==BM_STATUS_OK);
    assert(!f->holds && !f->decodes);
}
static void finish(fixture_t *f)
{
    f->inspect=0;
    bm_at_bus_destroy(f->bus); /* Pin recipients still alive. */
    if(f->cpu_wired) f->cpu.ops.destroy(f->cpu.context);
    if(f->engine) bm_engine_destroy(f->engine);
    if(f->clock) bm_at_clock_link_destroy(f->clock);
    if(f->pit) bm_pit8254_destroy(f->pit);
}
static void edge(fixture_t *f)
{
    assert(bm_pcs286_refresh_pit_input(&f->refresh,0)==BM_STATUS_OK);
    assert(bm_pcs286_refresh_pit_input(&f->refresh,1)==BM_STATUS_OK);
}
static void complete(fixture_t *f)
{
    assert(bm_pcs286_refresh_service(&f->refresh)==BM_STATUS_IDLE);
    assert(f->hold);
    assert(bm_at_bus_hold_ack(f->bus,1)==BM_STATUS_OK);
    assert(bm_pcs286_refresh_service(&f->refresh)==BM_STATUS_OK);
    assert(!f->hold && !state(f).pending);
    assert(bm_at_bus_hold_ack(f->bus,0)==BM_STATUS_OK);
}
static void latch_lock_and_reset(void)
{
    fixture_t f,g;
    unsigned i;
    start(&f); start(&g);
    assert(!state(&f).refdet && !state(&f).pending);
    assert(bm_pcs286_refresh_service(&f.refresh)==BM_STATUS_IDLE);
    edge(&f);
    for(i=0;i<1000;++i) { edge(&f); assert(state(&f).pending && !state(&f).refdet); }
    assert(!state(&g).pending && !f.holds && !f.decodes);
    assert(bm_at_bus_set_lock(f.bus,1)==BM_STATUS_OK);
    assert(bm_pcs286_refresh_service(&f.refresh)==BM_STATUS_IDLE);
    assert(arbitration(&f).requester==BM_AT_MASTER_REFRESH && !f.hold);
    assert(bm_pcs286_refresh_reset(&f.refresh)==BM_STATUS_INVALID_STATE);
    assert(bm_at_bus_hold_ack(f.bus,1)==BM_STATUS_INVALID_STATE);
    assert(bm_at_bus_set_lock(f.bus,0)==BM_STATUS_OK);
    assert(f.hold && !state(&f).refdet);
    assert(bm_at_bus_hold_ack(f.bus,1)==BM_STATUS_OK);
    f.inspect=1;
    assert(bm_pcs286_refresh_service(&f.refresh)==BM_STATUS_OK);
    assert(state(&f).refdet && !state(&f).pending && f.holds==2);
    assert(bm_pcs286_refresh_service(&f.refresh)==BM_STATUS_IDLE);
    assert(bm_pcs286_refresh_pit_input(&f.refresh,1)==BM_STATUS_OK);
    assert(!state(&f).pending); /* Unchanged high is not another request. */
    edge(&f);
    assert(bm_pcs286_refresh_service(&f.refresh)==BM_STATUS_IDLE); /* stale HLDA */
    assert(state(&f).pending && state(&f).refdet && f.holds==2);
    assert(bm_at_bus_hold_ack(f.bus,0)==BM_STATUS_OK);
    complete(&f);
    assert(!state(&f).refdet && f.holds==4 && !f.decodes);
    edge(&f);
    assert(bm_pcs286_refresh_service(&f.refresh)==BM_STATUS_IDLE);
    f.inspect=0;
    bm_at_bus_reset(f.bus);
    assert(bm_pcs286_refresh_reset(&f.refresh)==BM_STATUS_OK);
    assert(!state(&f).out1 && !state(&f).pending && !state(&f).refdet);
    assert(bm_pcs286_refresh_reset(&f.refresh)==BM_STATUS_OK);
    assert(bm_pcs286_refresh_service(&f.refresh)==BM_STATUS_IDLE);
    finish(&f); finish(&g);
}
static void external_owners(void)
{
    bm_at_master_t owner;
    for(owner=BM_AT_MASTER_DMA8;owner<=BM_AT_MASTER_ISA;owner++) {
        fixture_t f;
        bm_at_transfer_t t={0},before;
        unsigned i;
        start(&f);
        t.master=owner; t.requester_clock=(bm_clock_rate_t){4000000,1};
        t.bus.space=BM_ADDRESS_MEMORY; t.bus.operation=BM_BUS_READ;
        t.bus.size=1; t.bus.value=0xbeef;
        assert(bm_at_bus_request(f.bus,owner,1)==BM_STATUS_OK);
        edge(&f);
        assert(bm_pcs286_refresh_service(&f.refresh)==BM_STATUS_IDLE);
        assert(arbitration(&f).requester==owner && !state(&f).refdet);
        assert(bm_at_bus_hold_ack(f.bus,1)==BM_STATUS_OK);
        for(i=0;i<30;++i) {
            edge(&f);
            assert(bm_pcs286_refresh_service(&f.refresh)==BM_STATUS_IDLE);
            assert(bm_at_bus_access(f.bus,&t)==BM_STATUS_OK && t.bus.value==0xa5);
            assert(arbitration(&f).requester==owner && !state(&f).refdet);
        }
        assert(bm_at_bus_request(f.bus,owner,0)==BM_STATUS_OK);
        assert(bm_pcs286_refresh_service(&f.refresh)==BM_STATUS_IDLE);
        assert(bm_at_bus_hold_ack(f.bus,0)==BM_STATUS_OK);
        assert(bm_pcs286_refresh_service(&f.refresh)==BM_STATUS_IDLE);
        assert(arbitration(&f).requester==BM_AT_MASTER_REFRESH);
        assert(bm_at_bus_request(f.bus,owner,1)==BM_STATUS_UNSUPPORTED);
        assert(bm_at_bus_hold_ack(f.bus,1)==BM_STATUS_OK);
        before=t;
        assert(bm_at_bus_access(f.bus,&t)==BM_STATUS_IDLE);
        assert(!memcmp(&t,&before,sizeof(t)));
        t.master=BM_AT_MASTER_REFRESH;
        for(i=0;i<2;++i) {
            t.bus.attributes=i?BM_BUS_TRANSACTION_DEBUG:0;
            before=t;
            assert(bm_at_bus_access(f.bus,&t)==BM_STATUS_INVALID_ARGUMENT);
            assert(!memcmp(&t,&before,sizeof(t)) && f.decodes==30);
        }
        assert(bm_pcs286_refresh_service(&f.refresh)==BM_STATUS_OK);
        assert(state(&f).refdet && !state(&f).pending && f.decodes==30);
        finish(&f);
    }
}
static bm_status_t sample(void *context,uint8_t *bits)
{
    fixture_t *f=context;
    /* Error circuits remain an explicit synthetic zero fixture; REF DET is real
     * functional coordinator state. No claim of parity/NMI implementation. */
    *bits=(uint8_t)(state(f).refdet<<4);
    return BM_STATUS_OK;
}
static bm_status_t checks(void *context,int ram,int io)
{ (void)context; (void)ram; (void)io; return BM_STATUS_OK; }
static void pit_output(void *context,unsigned channel,int level)
{
    fixture_t *f=context;
    if(channel==1) {
        if(level && !f->last_out1) ++f->rises;
        f->last_out1=level;
        assert(bm_pcs286_refresh_pit_input(&f->refresh,level)==BM_STATUS_OK);
    } else if(channel==2)
        assert(bm_pcs286_port61_pit_input(&f->port,channel,level)==BM_STATUS_OK);
}
static bm_bus_transaction_t io(unsigned address,bm_bus_operation_t op,unsigned value)
{
    bm_bus_transaction_t t={0};
    t.space=BM_ADDRESS_IO; t.operation=op; t.address=address;
    t.value=value; t.size=t.alignment=1;
    return t;
}
static void attach_timer(fixture_t *f)
{
    bm_host_services_t h=bm_null_host_services();
    bm_engine_config_t ec={1,2,1};
    bm_clock_rate_t rate={1193182,1}; /* Authored nominal fixture rate. */
    bm_pit8254_config_t pc={0x40,pit_output,f};
    bm_pcs286_port61_config_t c={0};
    assert(bm_engine_create_clocked(&h,&ec,&f->engine)==BM_STATUS_OK);
    assert(bm_pit8254_create(&h,&pc,&f->pit)==BM_STATUS_OK);
    assert(bm_pit8254_attach_clock(&h,f->engine,f->pit,&rate,&f->clock)==BM_STATUS_OK);
    c.profile=BM_PCS286_PORT61_AT_SIGNALS; c.pit=f->pit; c.clock=f->clock;
    c.status=sample; c.checks=checks; c.board_context=f;
    assert(bm_pcs286_port61_initialize(&f->port,&c)==BM_STATUS_OK);
}
static void program(fixture_t *f)
{
    const unsigned addresses[]={0x43,0x41,0x41},values[]={0x74,18,0};
    unsigned i;
    for(i=0;i<3;++i) {
        bm_bus_transaction_t t=io(addresses[i],BM_BUS_WRITE,values[i]);
        assert(bm_at_clock_link_io(f->clock,&t)==BM_STATUS_OK);
    }
}
static unsigned read61(fixture_t *f,int debug)
{
    bm_bus_transaction_t t=io(0x61,BM_BUS_READ,0xbeef);
    t.attributes=debug?BM_BUS_TRANSACTION_DEBUG:0;
    assert(bm_at_bus_cpu_access(f->bus,&t)==BM_STATUS_OK);
    return (unsigned)t.value;
}
static void timer_and_cpu(void)
{
    fixture_t f;
    bm_host_services_t h=bm_null_host_services();
    bm_286_config_t cc={0}; bm_286_boundary_t b;
    bm_286_arch_state_t arch;
    unsigned i;
    start(&f); attach_timer(&f);
    cc.size=sizeof(cc); cc.version=BM_286_CONTRACT_VERSION;
    cc.access=bm_at_bus_cpu_access; cc.access_context=f.bus;
    cc.hold_ack=acknowledge; cc.pin_context=&f;
    assert(bm_286_create(&h,&cc,&f.cpu)==BM_STATUS_OK); f.cpu_wired=1;
    assert(bm_286_step(&f.cpu,&b)==BM_STATUS_OK && f.fetches==1);
    program(&f); /* Programming mode2 raises OUT1 and requests, not completes. */
    assert(state(&f).pending && !state(&f).refdet);
    for(i=0;i<1000;++i) assert(!(read61(&f,0)&0x10) && !(read61(&f,1)&0x10));
    assert(bm_at_bus_set_lock(f.bus,1)==BM_STATUS_OK);
    assert(bm_engine_run_for(f.engine,500000)==BM_STATUS_OK);
    assert(bm_at_clock_link_sync(f.clock)==BM_STATUS_OK);
    assert(f.rises>30 && state(&f).pending && !state(&f).refdet);
    assert(bm_pcs286_refresh_service(&f.refresh)==BM_STATUS_IDLE);
    assert(!f.hold);
    assert(bm_at_bus_set_lock(f.bus,0)==BM_STATUS_OK);
    assert(bm_286_step(&f.cpu,&b)==BM_STATUS_IDLE && b.kind==BM_286_BOUNDARY_HOLD);
    assert(f.fetches==1 && !state(&f).refdet);
    f.inspect=1;
    assert(bm_pcs286_refresh_service(&f.refresh)==BM_STATUS_OK);
    assert(!arbitration(&f).hlda); /* Real CPU releases HLDA on lowered HOLD. */
    assert(bm_286_step(&f.cpu,&b)==BM_STATUS_OK); /* IN reads the actual port. */
    assert(bm_286_get_arch_state(&f.cpu,&arch)==BM_STATUS_OK);
    assert((arch.ax&0x10)==0x10 && arch.ip==0xfff3);
    assert(bm_286_step(&f.cpu,&b)==BM_STATUS_OK); /* HLT */
    assert(f.cpu.ops.reset(f.cpu.context)==BM_STATUS_OK);
    assert(state(&f).refdet); /* CPU warm reset doesn't reconstruct refresh. */
    f.inspect=0;
    assert(bm_engine_reset(f.engine)==BM_STATUS_OK);
    assert(bm_at_clock_link_reset(f.clock)==BM_STATUS_OK);
    bm_at_bus_reset(f.bus);
    assert(bm_pcs286_refresh_reset(&f.refresh)==BM_STATUS_OK);
    assert(bm_pcs286_port61_reset(&f.port)==BM_STATUS_OK);
    assert(!(read61(&f,1)&0x10) && !state(&f).pending);
    finish(&f);
}
static void periodic(unsigned chunk)
{
    fixture_t f;
    uint64_t elapsed=0;
    start(&f); attach_timer(&f); program(&f);
    complete(&f); ++f.completions;
    while(elapsed<1000000) {
        uint64_t n=1000000-elapsed;
        if(n>chunk) n=chunk;
        assert(bm_engine_run_for(f.engine,n)==BM_STATUS_OK);
        assert(bm_at_clock_link_sync(f.clock)==BM_STATUS_OK);
        elapsed+=n;
        if(state(&f).pending) { complete(&f); ++f.completions; }
    }
    /* 1193 input pulses: one CE load, 66 completed periods of divisor18,
     * plus the programming-induced rise. Independent arithmetic oracle. */
    assert(f.completions==67 && f.rises==67 && f.holds==134);
    assert((read61(&f,0)&0x10)==0x10 && (read61(&f,1)&0x10)==0x10);
    finish(&f);
}
static void invalid_arguments(void)
{
    fixture_t f; bm_pcs286_refresh_t unused={0},before;
    bm_pcs286_refresh_state_t s={1,1,1};
    bm_at_bus_arbitration_t a={BM_AT_MASTER_ISA,1,1,1,1};
    start(&f);
    assert(bm_at_bus_arbitration(NULL,&a)==BM_STATUS_INVALID_ARGUMENT);
    assert(a.requester==BM_AT_MASTER_ISA && a.hlda==1);
    assert(bm_at_bus_arbitration(f.bus,NULL)==BM_STATUS_INVALID_ARGUMENT);
    assert(bm_pcs286_refresh_initialize(NULL,f.bus)==BM_STATUS_INVALID_ARGUMENT);
    assert(bm_pcs286_refresh_initialize(&unused,NULL)==BM_STATUS_INVALID_ARGUMENT);
    before=unused;
    assert(bm_at_bus_set_lock(f.bus,1)==BM_STATUS_OK);
    assert(bm_pcs286_refresh_initialize(&unused,f.bus)==BM_STATUS_INVALID_STATE);
    assert(!memcmp(&before,&unused,sizeof(unused)));
    assert(bm_pcs286_refresh_service(&unused)==BM_STATUS_INVALID_ARGUMENT);
    assert(bm_pcs286_refresh_reset(&unused)==BM_STATUS_INVALID_ARGUMENT);
    assert(bm_pcs286_refresh_pit_input(&f.refresh,2)==BM_STATUS_INVALID_ARGUMENT);
    assert(!state(&f).out1 && !state(&f).pending);
    assert(bm_pcs286_refresh_state(NULL,&s)==BM_STATUS_INVALID_ARGUMENT);
    assert(s.out1==1 && s.pending==1 && s.refdet==1);
    assert(bm_pcs286_refresh_state(&f.refresh,NULL)==BM_STATUS_INVALID_ARGUMENT);
    finish(&f);
}
int main(void)
{
    invalid_arguments(); latch_lock_and_reset(); external_owners(); timer_and_cpu();
    periodic(113); periodic(1000);
    puts("Functional refresh: coalescing, LOCK/HLDA, DMA8/DMA16/ISA ownership, real PIT/CPU/61h, reset and chunk invariance passed");
    return 0;
}
