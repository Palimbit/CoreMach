/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Authored scheduler/PIC/NMI cases; no firmware or physical captures. */
#include <blumach/components/at_clock.h>
#include <blumach/components/at_pic.h>
#include <blumach/platforms/null_host.h>
#include "rtc_at_private.h"
#include "checks.h"
#include "failure_injection_host.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct edge { bm_time_point_t when; int level; } edge_t;
typedef struct fixture {
    bm_engine_t *engine;
    bm_at_rtc_t *rtc;
    bm_at_clock_link_t *link;
    bm_at_pic_t *pic;
    bm_pcs286_checks_t checks;
    edge_t edges[128];
    unsigned count, masks, reentries;
    int inspect, delivered, nmi, fail_after, fail_mask;
    bm_status_t failure;
} fixture_t;
static bm_bus_transaction_t txn(unsigned port, bm_bus_operation_t op, unsigned value)
{
    bm_bus_transaction_t t = {0};
    t.space = BM_ADDRESS_IO; t.operation = op; t.address = port;
    t.size = t.alignment = 1; t.value = value; t.wait_states = 17; return t;
}
static void inspect(fixture_t *f)
{
    bm_bus_transaction_t t = txn(0x71, BM_BUS_READ, 0xbeef);
    if (!f->inspect || !f->link) return;
    assert(bm_at_clock_link_sync(f->link) == BM_STATUS_INVALID_STATE);
    assert(bm_at_clock_link_changed(f->link) == BM_STATUS_INVALID_STATE);
    assert(bm_at_clock_link_reset(f->link) == BM_STATUS_INVALID_STATE);
    assert(bm_at_clock_link_io(f->link, &t) == BM_STATUS_INVALID_STATE && t.value == 0xbeef);
    t.attributes = BM_BUS_TRANSACTION_DEBUG;
    assert(bm_at_clock_link_io(f->link, &t) == BM_STATUS_OK);
    bm_at_clock_link_destroy(f->link); bm_at_rtc_destroy(f->rtc);
    ++f->reentries;
}
static bm_status_t irq(void *context, int level)
{
    fixture_t *f = context;
    assert(f->count < 128);
    assert(bm_engine_now_exact(f->engine, &f->edges[f->count].when) == BM_STATUS_OK);
    f->edges[f->count++].level = level; inspect(f);
    if (!f->fail_mask && f->failure && !f->fail_after) return f->failure;
    f->delivered = level;
    if (f->pic) assert(bm_at_pic_set_irq(f->pic, 8, level) == BM_STATUS_OK);
    return f->fail_mask ? BM_STATUS_OK : f->failure;
}
static bm_status_t nmi(void *context, int level)
{ ((fixture_t *)context)->nmi = level; return BM_STATUS_OK; }
static bm_status_t mask(void *context, int level)
{
    fixture_t *f = context;
    ++f->masks; inspect(f);
    if (f->fail_mask && f->failure && !f->fail_after) return f->failure;
    if (f->pic) assert(bm_pcs286_checks_mask(&f->checks, level) == BM_STATUS_OK);
    return f->fail_mask ? f->failure : BM_STATUS_OK;
}
static void seed(uint8_t *bytes, unsigned a, unsigned b)
{
    memset(bytes, 0, 128); bytes[6] = bytes[7] = bytes[8] = 1; bytes[9] = 26;
    bytes[1] = bytes[3] = bytes[5] = 0xc0; bytes[10] = (uint8_t)a; bytes[11] = (uint8_t)b;
    bytes[0x32] = 0x19;
}
static void create(fixture_t *f, const bm_host_services_t *h, const uint8_t *bytes, size_t slots)
{
    bm_engine_config_t ec = {1, 4, slots};
    bm_at_rtc_config_t rc = {0};
    memset(f, 0, sizeof(*f));
    assert(bm_engine_create_clocked(h, &ec, &f->engine) == BM_STATUS_OK);
    rc.io_base = 0x70; rc.cmos_size = 128; rc.initial_cmos = bytes;
    rc.initial_cmos_size = bytes ? 128 : 0; rc.battery_valid = bytes != NULL;
    rc.irq = irq; rc.nmi_mask = mask; rc.output_context = f;
    assert(bm_at_rtc_create(h, &rc, &f->rtc) == BM_STATUS_OK);
}
static void start(fixture_t *f, unsigned a, unsigned b)
{
    uint8_t bytes[128]; bm_host_services_t h = bm_null_host_services();
    seed(bytes, a, b); create(f, &h, bytes, 2);
    assert(bm_at_rtc_attach_clock(&h, f->engine, f->rtc, &f->link) == BM_STATUS_OK);
    assert(!f->count && !f->masks);
}
static void finish(fixture_t *f)
{
    bm_engine_destroy(f->engine); bm_at_clock_link_destroy(f->link);
    assert(!f->rtc->clock_link); bm_at_rtc_destroy(f->rtc);
    if (f->pic) bm_at_pic_destroy(f->pic);
}
static void wr_port(fixture_t *f, unsigned port, unsigned value)
{
    bm_bus_transaction_t t = txn(port, BM_BUS_WRITE, value);
    assert(bm_at_clock_link_io(f->link, &t) == BM_STATUS_OK && t.wait_states == 17);
}
static void wr(fixture_t *f, unsigned index, unsigned value)
{ wr_port(f, 0x70, index | 0x80); wr_port(f, 0x71, value); }
static unsigned rd(fixture_t *f, unsigned index)
{
    bm_bus_transaction_t t = txn(0x71, BM_BUS_READ, 0xbeef);
    wr_port(f, 0x70, index | 0x80);
    assert(bm_at_clock_link_io(f->link, &t) == BM_STATUS_OK && t.wait_states == 17);
    return (unsigned)t.value;
}
static void run_to(fixture_t *f, uint64_t ns)
{
    uint64_t now = bm_engine_now(f->engine);
    assert(ns >= now && bm_engine_run_for(f->engine, ns - now) == BM_STATUS_OK);
}
static uint64_t ceil_edge(uint64_t cycle)
{ return (cycle * UINT64_C(1000000000) + 32767) / 32768; }
static void exact_edge(const bm_time_point_t *time, uint64_t cycle)
{
    uint64_t numerator = cycle * UINT64_C(1953125), denominator = 64;
    uint64_t rem = numerator % denominator;
    assert(time->nanoseconds == numerator / denominator);
    if (!rem) denominator = 1;
    else while (!(rem & 1)) { rem /= 2; denominator /= 2; }
    assert(time->subnanosecond_numerator == rem && time->subnanosecond_denominator == denominator);
}
static void calendar_boundaries(void)
{
    fixture_t f; bm_bus_transaction_t t, original;
    start(&f, 0x20, 6); f.inspect = 1; wr(&f, 11, 0x16);
    run_to(&f, ceil_edge(16376) - 1); assert(!f.rtc->state.uip && !f.count);
    run_to(&f, ceil_edge(16376)); assert(f.rtc->state.uip && f.rtc->state.cycles == 16376);
    assert(rd(&f, 10) == 0xa0 && rd(&f, 0) == 0);
    run_to(&f, ceil_edge(16384)); assert(f.rtc->state.updating);
    t = txn(0x71, BM_BUS_READ, 0xbeef); original = t;
    assert(bm_at_clock_link_io(f.link, &t) == BM_STATUS_UNSUPPORTED && !memcmp(&t, &original, sizeof(t)));
    assert(bm_at_clock_link_sync(f.link) == BM_STATUS_OK); /* Register rejection is not sticky. */
    run_to(&f, ceil_edge(16449) - 1); assert(!f.count && f.rtc->state.uip);
    run_to(&f, ceil_edge(16449)); assert(f.count == 1 && f.edges[0].level == 1);
    exact_edge(&f.edges[0].when, 16449);
    assert(rd(&f, 0) == 1 && rd(&f, 12) == 0xb0 && !f.delivered);
    run_to(&f, ceil_edge(49217)); assert(f.count == 3);
    exact_edge(&f.edges[2].when, 49217); assert(rd(&f, 0) == 2);
    assert(f.reentries == f.count); finish(&f);
}
static void periodic_rearm_and_debug(void)
{
    fixture_t f; bm_bus_transaction_t t;
    uint64_t before;
    start(&f, 0x23, 0x86); f.inspect = 1; wr(&f, 11, 0xc6);
    run_to(&f, ceil_edge(4)); assert(f.count == 1); exact_edge(&f.edges[0].when, 4);
    run_to(&f, 200000); assert(f.rtc->state.cycles == 4); /* Sticky PF: idle source. */
    t = txn(0x71, BM_BUS_READ, 0); t.attributes = BM_BUS_TRANSACTION_DEBUG;
    before = f.rtc->state.cycles;
    assert(bm_at_clock_link_io(f.link, &t) == BM_STATUS_OK && t.value == 0xc6);
    assert(f.rtc->state.cycles == before && f.count == 1);
    assert(bm_at_clock_link_changed(f.link) == BM_STATUS_INVALID_STATE);
    assert(rd(&f, 12) == 0xc0 && f.count == 2 && f.rtc->state.cycles == 6);
    run_to(&f, ceil_edge(8)); assert(f.count == 3); exact_edge(&f.edges[2].when, 8);
    run_to(&f, UINT64_C(100000000000)); assert(f.count == 3 && f.rtc->state.cycles == 8);
    assert(rd(&f, 12) == 0xc0 && f.rtc->state.cycles == 3276800);
    run_to(&f, ceil_edge(3276804)); assert(f.count == 5); exact_edge(&f.edges[4].when, 3276804);
    wr(&f, 10, 0x60); assert(rd(&f, 12) == 0xc0); run_to(&f, UINT64_C(200000000000));
    assert(f.count == 6); wr(&f, 10, 0x23); assert(f.rtc->state.divider_phase == 0);
    run_to(&f, ceil_edge(6553604)); assert(f.count == 7); exact_edge(&f.edges[6].when, 6553604);
    assert(f.reentries == f.count); finish(&f);
}
static void stopped_programming_and_errors(void)
{
    fixture_t f; bm_host_services_t h = bm_null_host_services();
    bm_bus_transaction_t t, original; uint64_t cycles;
    create(&f, &h, NULL, 1);
    assert(bm_at_rtc_attach_clock(&h, f.engine, f.rtc, &f.link) == BM_STATUS_OK);
    run_to(&f, UINT64_C(50000000007)); assert(!f.rtc->state.cycles);
    t = txn(0x70, BM_BUS_READ, 0xbeef); original = t;
    assert(bm_at_clock_link_io(f.link, &t) == BM_STATUS_UNMAPPED && !memcmp(&t, &original, sizeof(t)));
    assert(f.rtc->state.cycles == 1638400 && !f.rtc->state.divider_phase);
    wr(&f, 11, 0x86); wr(&f, 6, 1); wr(&f, 7, 1); wr(&f, 8, 1); wr(&f, 9, 26);
    wr(&f, 10, 0x20); wr(&f, 11, 0x16);
    run_to(&f, ceil_edge(1638400 + 16449)); assert(f.count == 1);
    exact_edge(&f.edges[0].when, 1638400 + 16449); assert(rd(&f, 0) == 1);
    t = txn(0x71, BM_BUS_WRITE, 7); wr_port(&f, 0x70, 0x8b);
    assert(bm_at_clock_link_io(f.link, &t) == BM_STATUS_UNSUPPORTED);
    assert(bm_at_clock_link_sync(f.link) == BM_STATUS_OK);
    cycles = f.rtc->state.cycles; t.attributes = BM_BUS_TRANSACTION_DEBUG;
    assert(bm_at_clock_link_io(f.link, &t) == BM_STATUS_UNSUPPORTED && f.rtc->state.cycles == cycles);
    finish(&f);
}
static void resets(void)
{
    fixture_t f; uint64_t preserved;
    start(&f, 0x20, 6); f.inspect = 1; wr(&f, 11, 0x16); wr(&f, 0x32, 0x20);
    run_to(&f, 500100007); /* Fractional oscillator phase, actual update in progress. */
    assert(bm_at_clock_link_sync(f.link) == BM_STATUS_OK);
    preserved = f.rtc->state.cycles; assert(preserved == 16387);
    assert(bm_at_rtc_reset(f.rtc) == BM_STATUS_OK);
    assert(bm_at_clock_link_changed(f.link) == BM_STATUS_OK);
    assert(bm_engine_now(f.engine) == 500100007 && f.rtc->state.updating);
    assert(rd(&f, 11) == 6 && rd(&f, 0x32) == 0x20);
    wr(&f, 11, 0x16); f.count = 0;
    run_to(&f, ceil_edge(16449)); assert(f.count == 1); exact_edge(&f.edges[0].when, 16449);
    assert(rd(&f, 0) == 1); /* Warm reset retained the original fractional phase. */
    for (unsigned i = 0; i < 3; ++i) {
        run_to(&f, bm_engine_now(f.engine) + 170003);
        assert(bm_at_clock_link_reset(f.link) == BM_STATUS_INVALID_STATE);
        assert(bm_at_clock_link_sync(f.link) == BM_STATUS_OK);
        preserved = f.rtc->state.cycles;
        assert(bm_engine_reset(f.engine) == BM_STATUS_OK);
        assert(bm_at_clock_link_reset(f.link) == BM_STATUS_OK);
        assert(f.rtc->state.cycles == preserved && rd(&f, 0x32) == 0x20);
        run_to(&f, 2000000000);
        assert(bm_at_clock_link_sync(f.link) == BM_STATUS_OK);
        assert(f.rtc->state.cycles == preserved + 65536);
    }
    assert(bm_engine_reset(f.engine) == BM_STATUS_OK);
    assert(bm_at_clock_link_sync(f.link) == BM_STATUS_INVALID_STATE); /* Missing link reset. */
    assert(bm_at_clock_link_reset(f.link) == BM_STATUS_OK);
    finish(&f);
}
static void pic_write(fixture_t *f, unsigned port, unsigned value)
{
    bm_bus_transaction_t t = txn(port, BM_BUS_WRITE, value);
    assert(bm_at_pic_io(f->pic, &t) == BM_STATUS_OK);
}
static void pic_and_mask(void)
{
    fixture_t f; bm_host_services_t h = bm_null_host_services();
    bm_at_pic_config_t pc = {0}; bm_at_pic_state_t ps; uint8_t vector = 0xa5, bits;
    start(&f, 0x26, 6); pc.master_base = 0x20; pc.slave_base = 0xa0; pc.cascade_line = 2;
    assert(bm_at_pic_create(&h, &pc, &f.pic) == BM_STATUS_OK);
    assert(bm_pcs286_checks_initialize(&f.checks, nmi, &f) == BM_STATUS_OK);
    pic_write(&f,0x20,0x11); pic_write(&f,0xa0,0x11);
    pic_write(&f,0x21,0x30); pic_write(&f,0xa1,0x70);
    pic_write(&f,0x21,4); pic_write(&f,0xa1,2); pic_write(&f,0x21,1); pic_write(&f,0xa1,1);
    pic_write(&f,0x21,0xfb); pic_write(&f,0xa1,0xfe);
    wr(&f,11,0x46); run_to(&f,ceil_edge(32));
    assert(bm_at_pic_state(f.pic,&ps)==BM_STATUS_OK && ps.intr);
    assert(bm_at_pic_acknowledge(f.pic,0,&vector)==BM_STATUS_OK && vector==0xa5);
    assert(bm_at_pic_acknowledge(f.pic,1,&vector)==BM_STATUS_OK && vector==0x70);
    assert(rd(&f,12)==0xc0 && !f.delivered); pic_write(&f,0xa0,0x20); pic_write(&f,0x20,0x20);
    run_to(&f,ceil_edge(64)); assert(bm_at_pic_state(f.pic,&ps)==BM_STATUS_OK && ps.intr);
    assert(bm_pcs286_checks_memory_sample(&f.checks,1)==BM_STATUS_OK && !f.nmi);
    wr_port(&f,0x70,0x32); assert(f.nmi); wr_port(&f,0x71,0x20); assert(f.nmi);
    wr_port(&f,0x70,0xb2); assert(!f.nmi);
    assert(bm_pcs286_checks_status(&f.checks,&bits)==BM_STATUS_OK && bits==0x80);
    finish(&f);
}
static void failures(void)
{
    fixture_t f; bm_bus_transaction_t t, original;
    for (int after = 0; after <= 1; ++after) {
        unsigned calls;
        start(&f,0x23,0x86); f.inspect=1; wr(&f,11,0xc6);
        f.failure=BM_STATUS_DEVICE_ERROR; f.fail_after=after;
        assert(bm_engine_run_for(f.engine,1000000)==BM_STATUS_DEVICE_ERROR);
        assert(f.count==1 && f.rtc->state.cycles==4 && f.delivered==after);
        exact_edge(&f.edges[0].when,4); calls=f.count;
        assert(bm_engine_run_for(f.engine,1000000)==BM_STATUS_DEVICE_ERROR);
        assert(bm_at_clock_link_sync(f.link)==BM_STATUS_DEVICE_ERROR && f.count==calls);
        t=txn(0x70,BM_BUS_WRITE,0x8c); original=t;
        assert(bm_at_clock_link_io(f.link,&t)==BM_STATUS_DEVICE_ERROR && !memcmp(&t,&original,sizeof(t)));
        t.operation=BM_BUS_READ; t.attributes=BM_BUS_TRANSACTION_DEBUG;
        assert(bm_at_clock_link_io(f.link,&t)==BM_STATUS_OK);
        f.failure=BM_STATUS_OK; assert(bm_engine_reset(f.engine)==BM_STATUS_OK);
        assert(bm_at_clock_link_reset(f.link)==BM_STATUS_OK && !f.rtc->state.failure);
        f.count=0; wr(&f,11,0xc6); run_to(&f,ceil_edge(4)); assert(f.count==1); finish(&f);
        /* Read-C output failure occurs outside source firing; rearm must still
         * retain it, with read result staged and flags actually cleared. */
        start(&f,0x23,0x86); wr(&f,11,0xc6); run_to(&f,ceil_edge(4)); wr_port(&f,0x70,0x8c);
        f.failure=BM_STATUS_DEVICE_ERROR; f.fail_after=after;
        t=txn(0x71,BM_BUS_READ,0xbeef); original=t;
        assert(bm_at_clock_link_io(f.link,&t)==BM_STATUS_DEVICE_ERROR && !memcmp(&t,&original,sizeof(t)));
        assert(!f.rtc->regs[12] && f.delivered==!after);
        assert(bm_at_clock_link_changed(f.link)==BM_STATUS_DEVICE_ERROR);
        calls=f.count; f.failure=BM_STATUS_OK; assert(bm_at_rtc_reset(f.rtc)==BM_STATUS_OK);
        assert(bm_at_clock_link_sync(f.link)==BM_STATUS_DEVICE_ERROR && f.count==calls+1);
        assert(bm_engine_reset(f.engine)==BM_STATUS_OK);
        f.fail_mask=1; f.failure=BM_STATUS_IDLE;
        assert(bm_at_clock_link_reset(f.link)==BM_STATUS_INVALID_STATE);
        assert(bm_at_clock_link_sync(f.link)==BM_STATUS_INVALID_STATE);
        f.failure=BM_STATUS_OK; assert(bm_at_clock_link_reset(f.link)==BM_STATUS_OK); finish(&f);
    }
    /* Invalid calendar is a native advance failure, not a register rejection. */
    start(&f,0x20,6); wr(&f,8,0); assert(bm_engine_run_for(f.engine,600000000)==BM_STATUS_UNSUPPORTED);
    assert(f.rtc->state.cycles==16449 && !f.rtc->state.failure && !f.rtc->regs[12]);
    assert(bm_at_clock_link_sync(f.link)==BM_STATUS_UNSUPPORTED); finish(&f);
    /* Accepted controls survive a host-time deadline overflow. */
    start(&f,0x60,0x86); run_to(&f,UINT64_MAX-1); wr(&f,11,0xc6);
    wr_port(&f,0x70,0x8a); t=txn(0x71,BM_BUS_WRITE,0x23); original=t;
    assert(bm_at_clock_link_io(f.link,&t)==BM_STATUS_CAPACITY_EXCEEDED);
    assert(!memcmp(&t,&original,sizeof(t)) && f.rtc->regs[10]==0x23);
    assert(bm_at_clock_link_sync(f.link)==BM_STATUS_CAPACITY_EXCEEDED); finish(&f);
}
static void attachment(void)
{
    fixture_t f, other; bm_host_services_t h=bm_null_host_services(), failing;
    failure_injection_host_t alloc; bm_at_clock_link_t *link=NULL; uint8_t bytes[128];
    seed(bytes,0x20,6); create(&f,&h,bytes,1); create(&other,&h,bytes,1);
    failure_injection_host_initialize(&alloc); failing=failure_injection_host_services(&alloc);
    failure_injection_host_fail_on(&alloc,0);
    assert(bm_at_rtc_attach_clock(&failing,f.engine,f.rtc,&link)==BM_STATUS_OUT_OF_MEMORY && !link);
    failure_injection_host_fail_on(&alloc,SIZE_MAX);
    assert(bm_at_rtc_attach_clock(&failing,NULL,f.rtc,&link)==BM_STATUS_INVALID_ARGUMENT && !link);
    f.rtc->busy=1; assert(bm_at_rtc_attach_clock(&h,f.engine,f.rtc,&link)==BM_STATUS_INVALID_STATE);
    f.rtc->busy=0; assert(bm_at_rtc_attach_clock(&failing,f.engine,f.rtc,&f.link)==BM_STATUS_OK);
    assert(bm_at_rtc_attach_clock(&h,f.engine,f.rtc,&link)==BM_STATUS_INVALID_STATE && !link);
    assert(bm_at_rtc_attach_clock(&failing,f.engine,other.rtc,&link)==BM_STATUS_CAPACITY_EXCEEDED && !link);
    assert(alloc.outstanding_allocations==1 && !other.rtc->clock_link);
    assert(bm_at_rtc_advance(other.rtc,1)==BM_STATUS_OK);
    assert(bm_at_rtc_attach_clock(&h,other.engine,other.rtc,&link)==BM_STATUS_INVALID_STATE);
    finish(&f); assert(!alloc.outstanding_allocations); finish(&other);
    create(&f,&h,bytes,1); run_to(&f,1);
    assert(bm_at_rtc_attach_clock(&failing,f.engine,f.rtc,&link)==BM_STATUS_INVALID_ARGUMENT && !link);
    assert(!alloc.outstanding_allocations && !f.rtc->clock_link); finish(&f);
}
static void partitioned_and_shared_pit(void)
{
    fixture_t a,b; bm_host_services_t h=bm_null_host_services();
    bm_pit8254_t *pit; bm_at_clock_link_t *pit_link; bm_pit8254_config_t pc={0x40,NULL,NULL};
    bm_clock_rate_t rate={1000000000,1}; bm_bus_transaction_t t; unsigned random=37;
    uint8_t x[128],y[128]; uint64_t at=0;
    start(&a,0x26,6); start(&b,0x26,6); wr(&a,11,0x76); wr(&b,11,0x76);
    assert(bm_pit8254_create(&h,&pc,&pit)==BM_STATUS_OK);
    assert(bm_pit8254_attach_clock(&h,a.engine,pit,&rate,&pit_link)==BM_STATUS_OK);
    t=txn(0x43,BM_BUS_WRITE,0x30); assert(bm_at_clock_link_io(pit_link,&t)==BM_STATUS_OK);
    t=txn(0x40,BM_BUS_WRITE,0xff); assert(bm_at_clock_link_io(pit_link,&t)==BM_STATUS_OK);
    assert(bm_at_clock_link_io(pit_link,&t)==BM_STATUS_OK);
    run_to(&a,3000000000);
    while(at<3000000000) {
        random=random*1664525U+1013904223U; at+=1+random%10000000;
        if(at>3000000000) at=3000000000;
        run_to(&b,at);
    }
    assert(bm_at_clock_link_sync(a.link)==BM_STATUS_OK && bm_at_clock_link_sync(b.link)==BM_STATUS_OK);
    assert(bm_at_rtc_export_cmos(a.rtc,x,128)==BM_STATUS_OK);
    assert(bm_at_rtc_export_cmos(b.rtc,y,128)==BM_STATUS_OK && !memcmp(x,y,128));
    assert(a.count==b.count && a.rtc->state.cycles==98304 && b.rtc->state.cycles==98304);
    for(unsigned i=0;i<a.count;++i) {
        assert(a.edges[i].level==b.edges[i].level);
        assert(a.edges[i].when.nanoseconds==b.edges[i].when.nanoseconds);
        assert(a.edges[i].when.subnanosecond_numerator==b.edges[i].when.subnanosecond_numerator);
        assert(a.edges[i].when.subnanosecond_denominator==b.edges[i].when.subnanosecond_denominator);
    }
    assert(bm_at_clock_link_sync(pit_link)==BM_STATUS_OK);
    bm_engine_destroy(a.engine); bm_at_clock_link_destroy(pit_link); bm_pit8254_destroy(pit);
    bm_at_clock_link_destroy(a.link); bm_at_rtc_destroy(a.rtc); finish(&b);
}
typedef struct cpu_fixture { fixture_t *f; unsigned steps; } cpu_fixture_t;
static bm_status_t cpu_reset(void *context)
{ ((cpu_fixture_t *)context)->steps=0; return BM_STATUS_OK; }
static bm_status_t cpu_signal(void *context,uint32_t line,int level)
{ (void)context; (void)line; (void)level; return BM_STATUS_OK; }
static bm_status_t cpu_step(void *context,bm_tick_t start,uint64_t *cycles)
{
    cpu_fixture_t *cpu=context;
    uint64_t expected=(uint64_t)cpu->steps/2;
    /* now_exact is the committed engine cursor, not an in-flight CPU dispatch.
     * The link must use the source cursor at this exact instruction boundary. */
    assert(start==(uint64_t)cpu->steps*UINT64_C(1000000000)/65536);
    wr_port(cpu->f,0x70,0x8c);
    assert(cpu->f->rtc->state.cycles==expected);
    assert(rd(cpu->f,12)==(expected && expected%4==0 && !(cpu->steps&1) ? 0xc0 : 0));
    ++cpu->steps; *cycles=1; return BM_STATUS_OK;
}
static void cpu_boundaries(void)
{
    fixture_t f; cpu_fixture_t c; bm_cpu_t cpu={0}; bm_clock_rate_t rate={65536,1};
    start(&f,0x23,0x86); wr(&f,11,0xc6); c=(cpu_fixture_t){&f,0};
    cpu.context=&c; cpu.ops.reset=cpu_reset; cpu.ops.signal=cpu_signal;
    assert(bm_engine_add_clocked_cpu(f.engine,&cpu,cpu_step,&rate,NULL)==BM_STATUS_OK);
    run_to(&f,1000000); assert(c.steps==66 && f.count==16);
    { uint64_t cycles=f.rtc->state.cycles; unsigned phase=f.rtc->state.divider_phase;
      assert(cpu_reset(&c)==BM_STATUS_OK); assert(f.rtc->state.cycles==cycles && f.rtc->state.divider_phase==phase); }
    finish(&f);
}
int main(void)
{
    calendar_boundaries(); periodic_rearm_and_debug(); stopped_programming_and_errors(); resets();
    pic_and_mask(); failures(); attachment(); partitioned_and_shared_pit(); cpu_boundaries();
    puts("RTC clock: rational deadlines, lazy sync/read-C rearm, warm/full reset, PIC/NMI, failures, partitioning/shared PIT and CPU boundaries passed");
    return 0;
}
