/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Authored scheduler/composition oracles, no firmware or hardware captures. */
#include <blumach/components/at_clock.h>
#include <blumach/platforms/null_host.h>
#include "pit8254_private.h"
#include "failure_injection_host.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct edge { bm_time_point_t when; unsigned channel; int level; } edge_t;
typedef struct fixture {
    bm_engine_t *engine;
    bm_pit8254_t *pit;
    bm_at_clock_link_t *link;
    edge_t edges[4096];
    unsigned count, reentries;
    int inspect;
} fixture_t;
static const bm_clock_rate_t ghz = {1000000000,1};
static const bm_clock_rate_t crystal = {14318180,12};
static unsigned comparisons;
static bm_bus_transaction_t txn(unsigned port, bm_bus_operation_t op, unsigned value)
{
    bm_bus_transaction_t t = {0};
    t.space = BM_ADDRESS_IO; t.operation = op; t.address = port;
    t.value = value; t.size = t.alignment = 1; t.wait_states = 17;
    return t;
}
static void output(void *context, unsigned channel, int level)
{
    fixture_t *f = context;
    assert(f->count < 4096);
    assert(bm_engine_now_exact(f->engine, &f->edges[f->count].when) == BM_STATUS_OK);
    f->edges[f->count].channel = channel; f->edges[f->count++].level = level;
    if (f->inspect && f->link) {
        bm_bus_transaction_t t = txn(0x40, BM_BUS_READ, 0xface);
        assert(bm_at_clock_link_sync(f->link) == BM_STATUS_INVALID_STATE);
        assert(bm_at_clock_link_changed(f->link) == BM_STATUS_INVALID_STATE);
        assert(bm_at_clock_link_reset(f->link) == BM_STATUS_INVALID_STATE);
        assert(bm_at_clock_link_io(f->link, &t) == BM_STATUS_INVALID_STATE && t.value == 0xface);
        t.attributes = BM_BUS_TRANSACTION_DEBUG;
        assert(bm_at_clock_link_io(f->link, &t) == BM_STATUS_OK);
        bm_at_clock_link_destroy(f->link); /* Ignored during either kind of output callback. */
        ++f->reentries;
    }
}
static void start(fixture_t *f, const bm_clock_rate_t *rate)
{
    bm_host_services_t h = bm_null_host_services();
    bm_engine_config_t ec = {1,4,1};
    bm_pit8254_config_t pc = {0x40,output,f};
    memset(f,0,sizeof(*f));
    assert(bm_engine_create_clocked(&h,&ec,&f->engine) == BM_STATUS_OK);
    assert(bm_pit8254_create(&h,&pc,&f->pit) == BM_STATUS_OK);
    assert(bm_pit8254_attach_clock(&h,f->engine,f->pit,rate,&f->link) == BM_STATUS_OK);
}
static void finish(fixture_t *f)
{
    bm_engine_destroy(f->engine);
    bm_at_clock_link_destroy(f->link);
    assert(!f->pit->clock_link);
    bm_pit8254_destroy(f->pit);
}
static void wr(fixture_t *f, unsigned port, unsigned value)
{
    bm_bus_transaction_t t = txn(port,BM_BUS_WRITE,value);
    assert(bm_at_clock_link_io(f->link,&t) == BM_STATUS_OK && t.wait_states == 17);
}
static unsigned rd(fixture_t *f, unsigned port, int debug)
{
    bm_bus_transaction_t t = txn(port,BM_BUS_READ,0xbeef);
    t.attributes = debug ? BM_BUS_TRANSACTION_DEBUG : 0;
    assert(bm_at_clock_link_io(f->link,&t) == BM_STATUS_OK && t.wait_states == 17);
    return (unsigned)t.value;
}
static void program(fixture_t *f, unsigned channel, unsigned mode, unsigned bcd, unsigned n)
{
    wr(f,0x43,(channel<<6)|0x30|(mode<<1)|bcd);
    wr(f,0x40+channel,n&255); wr(f,0x40+channel,n>>8);
}
static void gate(fixture_t *f, unsigned channel, int level)
{
    assert(bm_at_clock_link_sync(f->link) == BM_STATUS_OK);
    assert(bm_pit8254_set_gate(f->pit,channel,level) == BM_STATUS_OK);
    assert(bm_at_clock_link_changed(f->link) == BM_STATUS_OK);
}
static void run(fixture_t *f, uint64_t ns)
{ assert(bm_engine_run_for(f->engine,ns) == BM_STATUS_OK); }
static uint64_t gcd(uint64_t a, uint64_t b)
{ while(b) { uint64_t r=a%b; a=b; b=r; } return a; }
static void exact_crystal(const bm_time_point_t *t, uint64_t pulse)
{
    const uint64_t den=3579545;
    uint64_t num=pulse*UINT64_C(3000000000), rem=num%den, g=gcd(rem,den);
    assert(t->nanoseconds == num/den);
    assert(t->subnanosecond_numerator == rem/g);
    assert(t->subnanosecond_denominator == den/g);
}
static void phase_and_reentry(void)
{
    fixture_t f;
    unsigned i;
    start(&f,&crystal); f.inspect=1;
    run(&f,100); program(&f,0,2,0,4); f.count=0;
    run(&f,4091);
    assert(f.count==2 && f.edges[0].level==0 && f.edges[1].level==1);
    exact_crystal(&f.edges[0].when,4); exact_crystal(&f.edges[1].when,5);
    /* Gate pause and restart at fractional-phase-independent host boundaries. */
    gate(&f,0,0); run(&f,777); gate(&f,0,1); f.count=0;
    run(&f,8500);
    assert(f.count==4);
    for(i=0;i<4;++i) exact_crystal(&f.edges[i].when,9+(i/2)*4+(i%2));
    assert(f.reentries>=7);
    finish(&f);
    start(&f,&ghz); f.inspect=1;
    program(&f,0,2,0,4); run(&f,4); /* OUT low. */
    { unsigned previous=f.reentries;
      gate(&f,0,0); /* Raw gate publishes high while PIT is busy, link is not. */
      assert(f.reentries==previous+1); }
    finish(&f);
}
static void idle_debug_and_errors(void)
{
    fixture_t f;
    bm_pit_exact_device_t before;
    bm_bus_transaction_t t, original;
    uint64_t gap=UINT64_C(1)<<40;
    start(&f,&ghz);
    run(&f,gap); assert(!f.pit->exact.channel[0].clocks);
    before=f.pit->exact; (void)rd(&f,0x40,1);
    assert(!memcmp(&before,&f.pit->exact,sizeof(before)));
    assert(bm_at_clock_link_changed(f.link)==BM_STATUS_INVALID_STATE);
    program(&f,0,0,0,100);
    assert(f.pit->exact.channel[0].clocks==gap);
    run(&f,50); before=f.pit->exact;
    (void)rd(&f,0x40,1); (void)rd(&f,0x40,1);
    assert(!memcmp(&before,&f.pit->exact,sizeof(before)));
    wr(&f,0x43,0); assert(rd(&f,0x40,0)==51); assert(rd(&f,0x40,0)==0);
    run(&f,52); assert(f.count==1);
    run(&f,65536); /* Terminal OUT is idle; CE still counts and wraps. */
    assert(f.pit->exact.channel[0].clocks==gap+101);
    wr(&f,0x43,0); assert(rd(&f,0x40,0)==255); assert(rd(&f,0x40,0)==255);
    run(&f,3);
    t=txn(0x43,BM_BUS_READ,0xface); original=t;
    assert(bm_at_clock_link_io(f.link,&t)==BM_STATUS_UNMAPPED);
    assert(!memcmp(&t,&original,sizeof(t)) && f.pit->exact.channel[0].clocks==gap+65641);
    t=txn(0x43,BM_BUS_WRITE,0xc1); original=t;
    assert(bm_at_clock_link_io(f.link,&t)==BM_STATUS_UNSUPPORTED && !memcmp(&t,&original,sizeof(t)));
    t.attributes=BM_BUS_TRANSACTION_DEBUG;
    assert(bm_at_clock_link_io(f.link,&t)==BM_STATUS_UNSUPPORTED);
    assert(bm_at_clock_link_sync(f.link)==BM_STATUS_OK); /* Register errors are not latched. */
    finish(&f);
}
static void compare_chunks(void)
{
    unsigned mode,bcd,channel,n;
    for(mode=0;mode<6;++mode) for(bcd=0;bcd<2;++bcd)
    for(channel=0;channel<3;++channel) for(n=3;n<=7;++n) {
        fixture_t step,batch;
        unsigned t,i;
        start(&step,&ghz); start(&batch,&ghz);
        program(&step,channel,mode,bcd,n); program(&batch,channel,mode,bcd,n);
        gate(&step,channel,0); gate(&batch,channel,0);
        gate(&step,channel,1); gate(&batch,channel,1);
        for(t=0;t<240;t+=20) {
            for(i=0;i<20;++i) run(&step,1);
            run(&batch,20);
            assert(bm_at_clock_link_sync(step.link)==BM_STATUS_OK);
            assert(bm_at_clock_link_sync(batch.link)==BM_STATUS_OK);
            assert(!memcmp(&step.pit->exact,&batch.pit->exact,sizeof(step.pit->exact)));
            assert(step.count==batch.count);
            for(i=0;i<step.count;++i) {
                assert(step.edges[i].when.nanoseconds==batch.edges[i].when.nanoseconds);
                assert(step.edges[i].channel==batch.edges[i].channel && step.edges[i].level==batch.edges[i].level);
            }
            ++comparisons;
            if(t==40 || t==80) { gate(&step,channel,t==80); gate(&batch,channel,t==80); }
            if(t==120) { wr(&step,0x40+channel,5); wr(&batch,0x40+channel,5); }
            if(t==160) { wr(&step,0x40+channel,0); wr(&batch,0x40+channel,0); }
        }
        finish(&step); finish(&batch);
    }
}
static void reset_lifecycle(void)
{
    fixture_t f;
    unsigned repeat;
    start(&f,&ghz);
    for(repeat=0;repeat<3;++repeat) {
        program(&f,0,2,0,4); run(&f,100+repeat*100);
        assert(bm_at_clock_link_reset(f.link)==BM_STATUS_INVALID_STATE);
        assert(bm_engine_reset(f.engine)==BM_STATUS_OK);
        assert(bm_at_clock_link_reset(f.link)==BM_STATUS_OK);
        f.count=0; run(&f,300); /* Larger new cursor than previous epoch. */
        assert(!f.count);
        assert(bm_at_clock_link_sync(f.link)==BM_STATUS_OK);
        assert(f.pit->exact.channel[0].clocks==300);
    }
    assert(bm_engine_reset(f.engine)==BM_STATUS_OK);
    assert(bm_at_clock_link_sync(f.link)==BM_STATUS_INVALID_STATE);
    (void)rd(&f,0x40,1); /* Observation still available after a latched host error. */
    assert(bm_at_clock_link_reset(f.link)==BM_STATUS_OK);
    program(&f,0,0,0,2); f.count=0; run(&f,4);
    assert(f.count==1 && f.edges[0].when.nanoseconds==3);
    finish(&f);
}
static void errors_and_ownership(void)
{
    fixture_t f;
    bm_host_services_t h=bm_null_host_services();
    failure_injection_host_t allocation;
    bm_host_services_t failing;
    bm_at_clock_link_t *link=(bm_at_clock_link_t *)(uintptr_t)1;
    bm_engine_config_t ec={1,1,1};
    bm_pit8254_config_t pc={0x40,NULL,NULL};
    bm_clock_rate_t invalid={0,1};
    bm_pit8254_t *other;
    memset(&f,0,sizeof(f));
    failure_injection_host_initialize(&allocation); failing=failure_injection_host_services(&allocation);
    assert(bm_engine_create_clocked(&h,&ec,&f.engine)==BM_STATUS_OK);
    assert(bm_pit8254_create(&h,&pc,&f.pit)==BM_STATUS_OK);
    failure_injection_host_fail_on(&allocation,0);
    assert(bm_pit8254_attach_clock(&failing,f.engine,f.pit,&ghz,&link)==BM_STATUS_OUT_OF_MEMORY && !link);
    failure_injection_host_fail_on(&allocation,SIZE_MAX);
    assert(bm_pit8254_attach_clock(&failing,f.engine,f.pit,&invalid,&link)==BM_STATUS_INVALID_ARGUMENT && !link);
    assert(!allocation.outstanding_allocations && !f.pit->clock_link);
    assert(bm_pit8254_attach_clock(&failing,f.engine,f.pit,&ghz,&f.link)==BM_STATUS_OK);
    assert(allocation.outstanding_allocations==1);
    assert(bm_pit8254_attach_clock(&h,f.engine,f.pit,&ghz,&link)==BM_STATUS_INVALID_STATE && !link);
    assert(bm_pit8254_create(&h,&pc,&other)==BM_STATUS_OK);
    assert(bm_pit8254_attach_clock(&failing,f.engine,other,&ghz,&link)==BM_STATUS_CAPACITY_EXCEEDED && !link);
    assert(allocation.outstanding_allocations==1 && !other->clock_link);
    bm_pit8254_destroy(other); run(&f,50); finish(&f);
    assert(!allocation.outstanding_allocations);
    /* Previously advanced devices cannot be attached to an unrelated epoch. */
    assert(bm_engine_create_clocked(&h,&ec,&f.engine)==BM_STATUS_OK);
    assert(bm_pit8254_create(&h,&pc,&f.pit)==BM_STATUS_OK);
    assert(bm_pit8254_advance(f.pit,1)==BM_STATUS_OK);
    assert(bm_pit8254_attach_clock(&h,f.engine,f.pit,&ghz,&link)==BM_STATUS_INVALID_STATE);
    bm_pit8254_reset(f.pit); run(&f,1);
    assert(bm_pit8254_attach_clock(&failing,f.engine,f.pit,&ghz,&link)==BM_STATUS_INVALID_ARGUMENT && !link);
    assert(!allocation.outstanding_allocations);
    bm_engine_destroy(f.engine); bm_pit8254_destroy(f.pit);
    assert(bm_at_clock_link_sync(NULL)==BM_STATUS_INVALID_ARGUMENT);
    assert(bm_at_clock_link_changed(NULL)==BM_STATUS_INVALID_ARGUMENT);
    assert(bm_at_clock_link_reset(NULL)==BM_STATUS_INVALID_ARGUMENT);
    assert(bm_at_clock_link_io(NULL,NULL)==BM_STATUS_INVALID_ARGUMENT);
    bm_at_clock_link_destroy(NULL);
}
static void programmed_attachment(void)
{
    fixture_t f;
    bm_host_services_t h=bm_null_host_services();
    failure_injection_host_t allocation;
    bm_host_services_t failing;
    bm_clock_rate_t slow={1,UINT64_C(10000000000)};
    bm_engine_config_t ec={1,1,1}; bm_pit8254_config_t pc={0x40,output,&f};
    bm_bus_transaction_t t=txn(0x43,BM_BUS_WRITE,0x10);
    memset(&f,0,sizeof(f));
    failure_injection_host_initialize(&allocation); failing=failure_injection_host_services(&allocation);
    assert(bm_engine_create_clocked(&h,&ec,&f.engine)==BM_STATUS_OK);
    assert(bm_pit8254_create(&h,&pc,&f.pit)==BM_STATUS_OK);
    assert(bm_pit8254_io(f.pit,&t)==BM_STATUS_OK);
    t=txn(0x40,BM_BUS_WRITE,2); assert(bm_pit8254_io(f.pit,&t)==BM_STATUS_OK);
    /* Valid period, unrepresentable first deadline. No callback/slot published. */
    assert(bm_pit8254_attach_clock(&failing,f.engine,f.pit,&slow,&f.link)==BM_STATUS_CAPACITY_EXCEEDED);
    assert(!f.link && !allocation.outstanding_allocations && !f.pit->clock_link);
    assert(bm_pit8254_attach_clock(&failing,f.engine,f.pit,&ghz,&f.link)==BM_STATUS_OK);
    run(&f,4); assert(f.count==1 && f.edges[0].when.nanoseconds==3);
    assert(bm_engine_reset(f.engine)==BM_STATUS_OK);
    assert(bm_at_clock_link_reset(f.link)==BM_STATUS_OK);
    f.count=0; run(&f,4); assert(!f.count); /* Clear registered initial deadline. */
    finish(&f); assert(!allocation.outstanding_allocations);
}
static void simultaneous_channels_and_instances(void)
{
    fixture_t a,b;
    bm_host_services_t h=bm_null_host_services();
    bm_engine_config_t ec={1,1,2};
    bm_pit8254_config_t pc={0x40,output,&a};
    unsigned i;
    memset(&a,0,sizeof(a)); memset(&b,0,sizeof(b));
    assert(bm_engine_create_clocked(&h,&ec,&a.engine)==BM_STATUS_OK); b.engine=a.engine;
    assert(bm_pit8254_create(&h,&pc,&a.pit)==BM_STATUS_OK); pc.output_context=&b;
    assert(bm_pit8254_create(&h,&pc,&b.pit)==BM_STATUS_OK);
    assert(bm_pit8254_attach_clock(&h,a.engine,a.pit,&ghz,&a.link)==BM_STATUS_OK);
    assert(bm_pit8254_attach_clock(&h,b.engine,b.pit,&ghz,&b.link)==BM_STATUS_OK);
    for(i=0;i<3;++i) { program(&a,i,2,0,4); program(&b,i,2,0,4); gate(&a,i,1); gate(&b,i,1); }
    a.count=b.count=0; run(&a,5);
    assert(a.count==6 && b.count==6);
    for(i=0;i<6;++i) {
        assert(a.edges[i].channel==i%3 && b.edges[i].channel==i%3);
        assert(a.edges[i].when.nanoseconds==4+i/3 && b.edges[i].when.nanoseconds==4+i/3);
    }
    gate(&a,0,0); a.count=b.count=0; run(&a,4);
    assert(a.count==4 && b.count==6); /* Separate state, shared virtual epoch. */
    bm_engine_destroy(a.engine);
    bm_at_clock_link_destroy(a.link); bm_at_clock_link_destroy(b.link);
    bm_pit8254_destroy(a.pit); bm_pit8254_destroy(b.pit);
}
static void retained_failure(void)
{
    fixture_t f;
    bm_bus_transaction_t t,original;
    bm_pit_exact_device_t snapshot;
    start(&f,&ghz); run(&f,UINT64_MAX-1);
    wr(&f,0x43,0x10); /* Mode zero, LSB count; no output deadline yet. */
    t=txn(0x40,BM_BUS_WRITE,4); original=t;
    assert(bm_at_clock_link_io(f.link,&t)==BM_STATUS_CAPACITY_EXCEEDED);
    assert(!memcmp(&t,&original,sizeof(t)));
    assert(f.pit->exact.channel[0].null_count); /* Accepted CR, arm failed. */
    snapshot=f.pit->exact;
    assert(bm_at_clock_link_sync(f.link)==BM_STATUS_CAPACITY_EXCEEDED);
    assert(bm_at_clock_link_changed(f.link)==BM_STATUS_CAPACITY_EXCEEDED);
    assert(bm_at_clock_link_io(f.link,&t)==BM_STATUS_CAPACITY_EXCEEDED);
    assert(!memcmp(&snapshot,&f.pit->exact,sizeof(snapshot)));
    (void)rd(&f,0x40,1);
    assert(bm_engine_reset(f.engine)==BM_STATUS_OK);
    assert(bm_at_clock_link_reset(f.link)==BM_STATUS_OK);
    program(&f,0,2,0,4); f.count=0;
    /* Test-only corrupt clock injects an advance failure inside source firing. */
    f.pit->exact.channel[1].clocks=UINT64_MAX;
    assert(bm_engine_run_for(f.engine,10)==BM_STATUS_CAPACITY_EXCEEDED);
    assert(!f.count);
    assert(bm_engine_run_for(f.engine,10)==BM_STATUS_CAPACITY_EXCEEDED);
    assert(bm_at_clock_link_sync(f.link)==BM_STATUS_CAPACITY_EXCEEDED && !f.count);
    finish(&f);
}
typedef struct cpu_fixture { fixture_t *f; unsigned steps; } cpu_fixture_t;
static bm_status_t cpu_reset(void *c) { ((cpu_fixture_t *)c)->steps=0; return BM_STATUS_OK; }
static bm_status_t cpu_signal(void *c,uint32_t line,int level)
{ (void)c; (void)line; (void)level; return BM_STATUS_OK; }
static bm_status_t cpu_step(void *c,bm_tick_t start_ns,uint64_t *cycles)
{
    cpu_fixture_t *cpu=c;
    uint64_t cursor=(uint64_t)cpu->steps*3/2;
    unsigned value=rd(cpu->f,0x40,0);
    assert(start_ns==cursor && cpu->f->pit->exact.channel[0].clocks==cursor);
    assert(value==(cursor ? 101-cursor : 0));
    ++cpu->steps; *cycles=1;
    return BM_STATUS_OK;
}
static void cpu_boundary(void)
{
    fixture_t f; cpu_fixture_t c; bm_cpu_t cpu={0};
    bm_clock_rate_t rate={2000000000,3};
    start(&f,&ghz); c=(cpu_fixture_t){&f,0};
    cpu.context=&c; cpu.ops.reset=cpu_reset; cpu.ops.signal=cpu_signal;
    assert(bm_engine_add_clocked_cpu(f.engine,&cpu,cpu_step,&rate,NULL)==BM_STATUS_OK);
    wr(&f,0x43,0x10); wr(&f,0x40,100); run(&f,20);
    assert(c.steps==14);
    { bm_pit_exact_device_t snapshot=f.pit->exact;
      assert(cpu_reset(&c)==BM_STATUS_OK); /* CPU-only reset leaves PIT/epoch alone. */
      assert(!memcmp(&snapshot,&f.pit->exact,sizeof(snapshot)) && bm_engine_now(f.engine)==20); }
    finish(&f);
}
int main(void)
{
    phase_and_reentry(); idle_debug_and_errors(); compare_chunks(); reset_lifecycle();
    errors_and_ownership(); programmed_attachment(); simultaneous_channels_and_instances();
    retained_failure(); cpu_boundary();
    printf("8254 clock: %u chunk/state/edge comparisons; rational phase, idle/debug, CPU boundaries, reset and host failures\n",comparisons);
    return 0;
}
