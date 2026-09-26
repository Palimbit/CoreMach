/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Authored AT check/NMI behavior and portable CPU roundtrip, no firmware. */
#include "checks.h"
#include "refresh.h"
#include "port61.h"
#include <blumach/systems/pcs286_memory.h>
#include <blumach/platforms/null_host.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct fixture {
    bm_pcs286_checks_t checks;
    unsigned calls, high, low;
    int delivered, inspect, fail_after;
    bm_status_t failure;
    bm_cpu_t cpu;
    bm_at_bus_t *bus;
    bm_pcs286_memory_t *memory;
    bm_pcs286_refresh_t refresh;
    bm_pcs286_port61_t port;
    bm_engine_t *engine;
    bm_pit8254_t *pit;
    bm_at_rtc_t *rtc;
    bm_at_clock_link_t *clock;
    int bad_parity, memory_failure;
} fixture_t;

static bm_pcs286_checks_state_t state(fixture_t *f)
{
    bm_pcs286_checks_state_t s;
    assert(bm_pcs286_checks_state(&f->checks,&s)==BM_STATUS_OK);
    return s;
}
static unsigned status(fixture_t *f)
{
    uint8_t bits=0;
    assert(bm_pcs286_checks_status(&f->checks,&bits)==BM_STATUS_OK);
    return bits;
}
static bm_status_t output(void *context,int level)
{
    fixture_t *f=context;
    ++f->calls;
    if(f->inspect) {
        assert(state(f).nmi==level);
        (void)status(f);
        assert(bm_pcs286_checks_mask(&f->checks,1)==BM_STATUS_INVALID_STATE);
        assert(bm_pcs286_checks_enable(&f->checks,0,0)==BM_STATUS_INVALID_STATE);
        assert(bm_pcs286_checks_memory_sample(&f->checks,1)==BM_STATUS_INVALID_STATE);
        assert(bm_pcs286_checks_io_input(&f->checks,1)==BM_STATUS_INVALID_STATE);
        assert(bm_pcs286_checks_reset(&f->checks)==BM_STATUS_INVALID_STATE);
    }
    if(f->failure!=BM_STATUS_OK && !f->fail_after) return f->failure;
    f->delivered=level;
    if(level) ++f->high; else ++f->low;
    if(f->cpu.context)
        assert(f->cpu.ops.signal(f->cpu.context,BM_286_SIGNAL_NMI,level)==BM_STATUS_OK);
    return f->failure;
}
static void start(fixture_t *f)
{
    memset(f,0,sizeof(*f));
    assert(bm_pcs286_checks_initialize(&f->checks,output,f)==BM_STATUS_OK);
    assert(!f->calls && !status(f) && state(f).masked);
}
static void truth_tables(void)
{
    unsigned ram,io,mask,bad,active,cases=0;
    for(ram=0;ram<2;++ram) for(io=0;io<2;++io) for(mask=0;mask<2;++mask)
    for(bad=0;bad<2;++bad) for(active=0;active<2;++active) {
        fixture_t f;
        unsigned bits=(ram && bad?0x80:0)|(active?0x40:0);
        start(&f); f.inspect=1;
        assert(bm_pcs286_checks_enable(&f.checks,(int)ram,(int)io)==BM_STATUS_OK);
        assert(bm_pcs286_checks_mask(&f.checks,(int)mask)==BM_STATUS_OK);
        assert(bm_pcs286_checks_memory_sample(&f.checks,(int)bad)==BM_STATUS_OK);
        assert(bm_pcs286_checks_io_input(&f.checks,(int)active)==BM_STATUS_OK);
        assert(status(&f)==bits);
        assert(state(&f).nmi==(!mask && ((ram && bad)||(io && active))));
        assert(f.calls==(unsigned)state(&f).nmi); /* OR doesn't create extra edges. */
        assert(bm_pcs286_checks_memory_sample(&f.checks,0)==BM_STATUS_OK);
        assert(bm_pcs286_checks_io_input(&f.checks,0)==BM_STATUS_OK);
        assert(status(&f) == (uint8_t)((ram && bad ? 0x80 : 0) |
            (io && active ? 0x40 : 0)));
        assert(bm_pcs286_checks_mask(&f.checks,1)==BM_STATUS_OK);
        assert(status(&f) == (uint8_t)((ram && bad ? 0x80 : 0) |
            (io && active ? 0x40 : 0)));
        assert(!state(&f).nmi);
        assert(bm_pcs286_checks_enable(&f.checks,0,0)==BM_STATUS_OK);
        assert(!status(&f));
        assert(bm_pcs286_checks_enable(&f.checks,1,1)==BM_STATUS_OK);
        assert(bm_pcs286_checks_mask(&f.checks,0)==BM_STATUS_OK);
        assert(!status(&f) && !state(&f).nmi);
        ++cases;
    }
    assert(cases==32);
}
static void held_io_and_independent_sources(void)
{
    fixture_t f,other;
    unsigned calls,i;
    start(&f); start(&other); f.inspect=1;
    assert(bm_pcs286_checks_mask(&f.checks,0)==BM_STATUS_OK);
    assert(bm_pcs286_checks_memory_sample(&f.checks,1)==BM_STATUS_OK);
    assert(bm_pcs286_checks_io_input(&f.checks,1)==BM_STATUS_OK);
    assert(status(&f)==0xc0 && f.high==1 && !status(&other));
    assert(bm_pcs286_checks_enable(&f.checks,0,1)==BM_STATUS_OK);
    assert(status(&f)==0x40 && f.high==1 && !f.low);
    assert(bm_pcs286_checks_enable(&f.checks,0,0)==BM_STATUS_OK);
    assert(status(&f)==0x40 && !state(&f).nmi && f.low==1);
    assert(!state(&f).io_latched && state(&f).io_active);
    calls=f.calls;
    for(i=0;i<1000;++i) {
        assert(status(&f)==0x40);
        assert(bm_pcs286_checks_enable(&f.checks,0,0)==BM_STATUS_OK);
        assert(bm_pcs286_checks_io_input(&f.checks,1)==BM_STATUS_OK);
    }
    assert(f.calls==calls); /* Held disabled error remains visible, not pulsing. */
    assert(bm_pcs286_checks_enable(&f.checks,1,1)==BM_STATUS_OK);
    assert(state(&f).io_latched && f.high==2);
    assert(bm_pcs286_checks_io_input(&f.checks,0)==BM_STATUS_OK);
    assert(status(&f)==0x40 && f.high==2);
    assert(bm_pcs286_checks_enable(&f.checks,1,0)==BM_STATUS_OK);
    assert(!status(&f) && f.low==2);
    assert(bm_pcs286_checks_io_input(&f.checks,1)==BM_STATUS_OK);
    assert(status(&f)==0x40 && !state(&f).nmi);
    assert(bm_pcs286_checks_reset(&f.checks)==BM_STATUS_OK);
    assert(state(&f).masked && state(&f).io_latched && state(&f).io_active);
    assert(bm_pcs286_checks_mask(&f.checks,0)==BM_STATUS_OK && f.high==3);
}
static void failures(void)
{
    int after,falling;
    for(after=0;after<2;++after) for(falling=0;falling<2;++falling) {
        fixture_t f; unsigned calls;
        start(&f); f.inspect=1;
        assert(bm_pcs286_checks_mask(&f.checks,0)==BM_STATUS_OK);
        if(falling) assert(bm_pcs286_checks_memory_sample(&f.checks,1)==BM_STATUS_OK);
        f.fail_after=after; f.failure=BM_STATUS_DEVICE_ERROR;
        if(falling) assert(bm_pcs286_checks_enable(&f.checks,0,1)==BM_STATUS_DEVICE_ERROR);
        else assert(bm_pcs286_checks_memory_sample(&f.checks,1)==BM_STATUS_DEVICE_ERROR);
        assert(!!state(&f).nmi == !falling &&
            status(&f) == (uint8_t)(falling ? 0 : 0x80));
        assert(f.delivered == (after ? (int)!falling : (int)falling));
        assert(state(&f).failure==BM_STATUS_DEVICE_ERROR);
        calls=f.calls;
        assert(bm_pcs286_checks_mask(&f.checks,1)==BM_STATUS_DEVICE_ERROR);
        assert(bm_pcs286_checks_memory_sample(&f.checks,0)==BM_STATUS_DEVICE_ERROR);
        assert(bm_pcs286_checks_io_input(&f.checks,0)==BM_STATUS_DEVICE_ERROR);
        assert(bm_pcs286_checks_enable(&f.checks,1,1)==BM_STATUS_DEVICE_ERROR);
        assert(f.calls==calls); /* No automatic retry/rollback. */
        f.failure=BM_STATUS_OK;
        assert(bm_pcs286_checks_reset(&f.checks)==BM_STATUS_OK);
        assert(f.calls==calls+1 && !f.delivered && !status(&f));
        assert(state(&f).masked && state(&f).failure==BM_STATUS_OK);
    }
    {
        fixture_t f;
        start(&f); f.failure=BM_STATUS_IDLE;
        assert(bm_pcs286_checks_mask(&f.checks,0)==BM_STATUS_OK);
        assert(bm_pcs286_checks_memory_sample(&f.checks,1)==BM_STATUS_INVALID_STATE);
        assert(bm_pcs286_checks_reset(&f.checks)==BM_STATUS_INVALID_STATE);
        assert(state(&f).failure==BM_STATUS_INVALID_STATE);
        f.failure=BM_STATUS_OK;
        assert(bm_pcs286_checks_reset(&f.checks)==BM_STATUS_OK);
    }
}
static bm_bus_transaction_t txn(unsigned address,bm_bus_operation_t op,unsigned value)
{
    bm_bus_transaction_t t={0};
    t.space=BM_ADDRESS_MEMORY; t.operation=op; t.address=address;
    t.value=value; t.size=t.alignment=1;
    return t;
}
static bm_status_t memory(void *context,bm_at_transfer_t *t)
{
    fixture_t *f=context;
    bm_status_t s;
    int debug=!!(t->bus.attributes&BM_BUS_TRANSACTION_DEBUG);
    if(t->bus.address>=0xfe0000)
        return bm_pcs286_memory_access(f->memory,BM_PCS286_MEMORY_ROM,
            (uint32_t)(t->bus.address-0xfe0000),&t->bus);
    if(!debug && f->memory_failure && t->bus.address==0x700)
        return BM_STATUS_DEVICE_ERROR;
    s=bm_pcs286_memory_access(f->memory,BM_PCS286_MEMORY_RAM,(uint32_t)t->bus.address,&t->bus);
    if(s==BM_STATUS_OK && !debug && t->bus.operation!=BM_BUS_WRITE)
        s=bm_pcs286_checks_memory_sample(&f->checks,f->bad_parity && t->bus.address==0x700);
    return s;
}
static bm_status_t sample(void *context,uint8_t *bits)
{
    fixture_t *f=context; bm_pcs286_refresh_state_t s;
    bm_status_t result=bm_pcs286_checks_status(&f->checks,bits);
    if(result!=BM_STATUS_OK) return result;
    assert(bm_pcs286_refresh_state(&f->refresh,&s)==BM_STATUS_OK);
    *bits|=(uint8_t)(s.refdet<<4);
    return BM_STATUS_OK;
}
static bm_status_t enables(void *context,int ram,int io)
{ return bm_pcs286_checks_enable(&((fixture_t *)context)->checks,ram,io); }
static bm_status_t io(void *context,bm_at_transfer_t *t)
{
    fixture_t *f=context;
    if(t->bus.address==0x70 || t->bus.address==0x71)
        return bm_at_rtc_io(f->rtc,&t->bus);
    return bm_pcs286_port61_io(&f->port,&t->bus);
}
static void hold(void *context,int level)
{
    fixture_t *f=context;
    assert(f->cpu.ops.signal(f->cpu.context,BM_286_SIGNAL_HOLD,level)==BM_STATUS_OK);
}
static void hlda(void *context,int level)
{ assert(bm_at_bus_hold_ack(((fixture_t *)context)->bus,level)==BM_STATUS_OK); }
static void pit_output(void *context,unsigned channel,int level)
{
    fixture_t *f=context;
    if(channel==1) assert(bm_pcs286_refresh_pit_input(&f->refresh,level)==BM_STATUS_OK);
    else if(channel==2) assert(bm_pcs286_port61_pit_input(&f->port,channel,level)==BM_STATUS_OK);
}
static void load(fixture_t *f,unsigned address,const uint8_t *data,size_t size)
{
    for(size_t i=0;i<size;++i) {
        bm_bus_transaction_t t=txn(address+(unsigned)i,BM_BUS_WRITE,data[i]);
        assert(bm_pcs286_memory_access(f->memory,BM_PCS286_MEMORY_RAM,(uint32_t)t.address,&t)==BM_STATUS_OK);
    }
}
static bm_286_arch_state_t cpu_state(fixture_t *f)
{
    bm_286_arch_state_t s;
    assert(bm_286_get_arch_state(&f->cpu,&s)==BM_STATUS_OK);
    return s;
}
static bm_286_boundary_t step(fixture_t *f)
{
    bm_286_boundary_t b;
    assert(bm_286_step(&f->cpu,&b)==BM_STATUS_OK);
    return b;
}
static unsigned peek(fixture_t *f,unsigned address)
{
    bm_bus_transaction_t t=txn(address,BM_BUS_READ,0);
    t.attributes=BM_BUS_TRANSACTION_DEBUG;
    assert(bm_at_bus_cpu_access(f->bus,&t)==BM_STATUS_OK);
    return (unsigned)t.value;
}
static bm_status_t rtc_irq(void *context,int level)
{
    (void)context; (void)level; /* This NMI-only fixture has no PIC consumer. */
    return BM_STATUS_OK;
}
static bm_status_t rtc_mask(void *context,int level)
{
    fixture_t *f=context;
    return bm_pcs286_checks_mask(&f->checks,level);
}
static void mask_port(fixture_t *f,int masked)
{
    bm_bus_transaction_t t=txn(0x70,BM_BUS_WRITE,masked ? 0x80 : 0);
    t.space=BM_ADDRESS_IO;
    assert(bm_at_bus_cpu_access(f->bus,&t)==BM_STATUS_OK);
}
static void board_start(fixture_t *f)
{
    bm_host_services_t h=bm_null_host_services();
    bm_at_bus_config_t bc={0}; bm_286_config_t cc={0};
    bm_pcs286_firmware_t fw={0}; bm_engine_config_t ec={1,2,1};
    bm_pit8254_config_t pc={0x40,pit_output,f};
    bm_clock_rate_t rate={1193182,1}; bm_pcs286_port61_config_t port={0};
    bm_at_rtc_config_t rc={0};
    uint8_t *rom=calloc(BM_PCS286_FIRMWARE_BYTES,1);
    const uint8_t trampoline[]={0xea,0,1,0,0};
    const uint8_t program[]={
        0xfa,0xb8,0,0x20,0x8e,0xd0,0xbc,0,0x10, /* CLI; SS=2000; SP=1000 */
        0xc7,6,8,0,0,3,0xc7,6,10,0,0,0, /* vector2 -> 0000:0300 */
        0xb0,0,0xe6,0x70,0xf4,0x90,0xf4 /* unmask; HLT; NOP; HLT */
    };
    const uint8_t handler[]={
        0x50,0xe4,0x61,0xa2,0,6, /* PUSH AX; IN AL,61h; MOV [0600],AL */
        0xff,6,2,6, /* INC word [0602] */
        0xb0,0x0c,0xe6,0x61,0xb0,0,0xe6,0x61, /* clear then reenable */
        0x58,0xcf /* POP AX; IRET */
    };
    assert(rom); start(f);
    memcpy(rom+BM_PCS286_FIRMWARE_BYTES-16,trampoline,sizeof(trampoline));
    fw.image[0].data=rom; fw.image[0].size=BM_PCS286_FIRMWARE_BYTES;
    assert(bm_pcs286_memory_create(&h,0x100000,&fw,&f->memory)==BM_STATUS_OK); free(rom);
    bc.cpu_clock=(bm_clock_rate_t){12000000,1}; bc.isa_clock=(bm_clock_rate_t){8000000,1};
    bc.memory=memory; bc.io=io; bc.decode_context=f; bc.hold=hold; bc.hold_context=f;
    assert(bm_at_bus_create(&h,&bc,&f->bus)==BM_STATUS_OK);
    assert(bm_pcs286_refresh_initialize(&f->refresh,f->bus)==BM_STATUS_OK);
    cc.size=sizeof(cc); cc.version=BM_286_CONTRACT_VERSION;
    cc.access=bm_at_bus_cpu_access; cc.access_context=f->bus; cc.hold_ack=hlda; cc.pin_context=f;
    assert(bm_286_create(&h,&cc,&f->cpu)==BM_STATUS_OK);
    assert(bm_engine_create_clocked(&h,&ec,&f->engine)==BM_STATUS_OK);
    assert(bm_pit8254_create(&h,&pc,&f->pit)==BM_STATUS_OK);
    assert(bm_pit8254_attach_clock(&h,f->engine,f->pit,&rate,&f->clock)==BM_STATUS_OK);
    port.profile=BM_PCS286_PORT61_AT_SIGNALS; port.pit=f->pit; port.clock=f->clock;
    port.status=sample; port.checks=enables; port.board_context=f;
    assert(bm_pcs286_port61_initialize(&f->port,&port)==BM_STATUS_OK);
    rc.io_base=0x70; rc.cmos_size=128; rc.irq=rtc_irq; rc.nmi_mask=rtc_mask; rc.output_context=f;
    assert(bm_at_rtc_create(&h,&rc,&f->rtc)==BM_STATUS_OK);
    load(f,0x100,program,sizeof(program)); load(f,0x300,handler,sizeof(handler));
    for(unsigned i=0;!cpu_state(f).halted;++i) { assert(i<30); step(f); }
    assert(!(cpu_state(f).flags&0x200) && !state(f).masked);
}
static void board_finish(fixture_t *f)
{
    f->failure=BM_STATUS_OK;
    assert(bm_pcs286_checks_reset(&f->checks)==BM_STATUS_OK);
    assert(bm_at_rtc_reset(f->rtc)==BM_STATUS_OK);
    bm_at_bus_destroy(f->bus); f->cpu.ops.destroy(f->cpu.context);
    bm_engine_destroy(f->engine); bm_at_clock_link_destroy(f->clock);
    bm_pit8254_destroy(f->pit); bm_pcs286_memory_destroy(f->memory);
    bm_at_rtc_destroy(f->rtc);
}
static void nmi_roundtrip(void)
{
    fixture_t f;
    bm_bus_transaction_t t; bm_286_boundary_t b;
    uint16_t resume;
    board_start(&f); f.inspect=1; resume=cpu_state(&f).ip;
    t=txn(0x43,BM_BUS_WRITE,0x74); t.space=BM_ADDRESS_IO;
    assert(bm_at_clock_link_io(f.clock,&t)==BM_STATUS_OK); /* OUT1 -> pending refresh. */
    assert(bm_pcs286_refresh_service(&f.refresh)==BM_STATUS_IDLE);
    assert(bm_286_step(&f.cpu,&b)==BM_STATUS_IDLE && b.kind==BM_286_BOUNDARY_HOLD);
    assert(bm_pcs286_refresh_service(&f.refresh)==BM_STATUS_OK);
    mask_port(&f,1);
    f.bad_parity=1;
    assert(peek(&f,0x700)==0 && !status(&f)); /* DEBUG never samples parity. */
    f.memory_failure=1; t=txn(0x700,BM_BUS_READ,0xbeef);
    assert(bm_at_bus_cpu_access(f.bus,&t)==BM_STATUS_DEVICE_ERROR && t.value==0xbeef);
    assert(!status(&f) && !cpu_state(&f).nmi_pending); /* Host != guest parity. */
    f.memory_failure=0;
    assert(bm_at_bus_cpu_access(f.bus,&t)==BM_STATUS_OK && status(&f)==0x80);
    assert(!cpu_state(&f).nmi_pending && !state(&f).nmi);
    mask_port(&f,0); assert(cpu_state(&f).nmi_pending);
    mask_port(&f,1);
    assert(cpu_state(&f).nmi_pending); /* A mask cannot retract an accepted edge. */
    b=step(&f); assert(b.kind==BM_286_BOUNDARY_INTERRUPT && b.vector==2);
    assert(cpu_state(&f).nmi_blocked && cpu_state(&f).sp==0x0ffa);
    assert(peek(&f,0x20ffa) == (uint8_t)(resume & 255U));
    for(unsigned i=0;cpu_state(&f).nmi_blocked;++i) { assert(i<20); step(&f); }
    assert(cpu_state(&f).ip==resume && cpu_state(&f).sp==0x1000);
    assert(peek(&f,0x600)==0x90 && peek(&f,0x602)==1); /* RAM fault + real REF DET. */
    assert(!status(&f) && !state(&f).nmi);
    mask_port(&f,0);
    assert(bm_pcs286_checks_io_input(&f.checks,1)==BM_STATUS_OK);
    assert(bm_pcs286_checks_io_input(&f.checks,0)==BM_STATUS_OK);
    b=step(&f); assert(b.vector==2 && b.kind==BM_286_BOUNDARY_INTERRUPT);
    for(unsigned i=0;cpu_state(&f).nmi_blocked;++i) { assert(i<20); step(&f); }
    assert(peek(&f,0x600)==0x50 && peek(&f,0x602)==2 && !status(&f));
    step(&f); step(&f); /* NOP; HLT: no retrigger from reads/old latches. */
    assert(bm_286_step(&f.cpu,&b)==BM_STATUS_IDLE && b.kind==BM_286_BOUNDARY_HALT);
    assert(peek(&f,0x602)==2);
    /* Warm CPU reset does not erase the board fault; owner re-presents level. */
    assert(bm_pcs286_checks_memory_sample(&f.checks,1)==BM_STATUS_OK);
    assert(f.cpu.ops.reset(f.cpu.context)==BM_STATUS_OK);
    assert(status(&f)==0x80 && state(&f).nmi);
    assert(f.cpu.ops.signal(f.cpu.context,BM_286_SIGNAL_NMI,state(&f).nmi)==BM_STATUS_OK);
    assert(cpu_state(&f).nmi_pending);
    /* Accepted port-latch/check changes survive a failing NMI sink. The port
     * propagates the host failure and does not retry or invent a guest fault. */
    {
        bm_bus_transaction_t before;
        bm_pcs286_port61_state_t ps;
        unsigned calls;
        f.failure=BM_STATUS_DEVICE_ERROR; f.fail_after=1;
        t=txn(0x61,BM_BUS_WRITE,0x0c); t.space=BM_ADDRESS_IO; before=t;
        assert(bm_at_bus_cpu_access(f.bus,&t)==BM_STATUS_DEVICE_ERROR);
        assert(!memcmp(&before,&t,sizeof(t)) && !status(&f) && !state(&f).nmi);
        assert(bm_pcs286_port61_state(&f.port,&ps)==BM_STATUS_OK);
        assert(ps.latch==0x0c && ps.failure==BM_STATUS_DEVICE_ERROR);
        calls=f.calls;
        assert(bm_at_bus_cpu_access(f.bus,&t)==BM_STATUS_DEVICE_ERROR && f.calls==calls);
        t.operation=BM_BUS_READ; t.attributes=BM_BUS_TRANSACTION_DEBUG;
        assert(bm_at_bus_cpu_access(f.bus,&t)==BM_STATUS_OK && t.value==0x1c);
        assert(cpu_state(&f).ip==0xfff0 && cpu_state(&f).nmi_pending);
    }
    board_finish(&f);
}
static void invalid_arguments(void)
{
    fixture_t f;
    bm_pcs286_checks_t unused={0}; uint8_t bits=0xa5;
    start(&f);
    assert(bm_pcs286_checks_initialize(NULL,output,&f)==BM_STATUS_INVALID_ARGUMENT);
    assert(bm_pcs286_checks_initialize(&unused,NULL,&f)==BM_STATUS_INVALID_ARGUMENT);
    assert(!unused.output);
    assert(bm_pcs286_checks_enable(&unused,1,1)==BM_STATUS_INVALID_ARGUMENT);
    assert(bm_pcs286_checks_enable(&f.checks,2,1)==BM_STATUS_INVALID_ARGUMENT);
    assert(bm_pcs286_checks_enable(&f.checks,1,-1)==BM_STATUS_INVALID_ARGUMENT);
    assert(bm_pcs286_checks_memory_sample(&f.checks,2)==BM_STATUS_INVALID_ARGUMENT);
    assert(bm_pcs286_checks_io_input(&f.checks,-1)==BM_STATUS_INVALID_ARGUMENT);
    assert(bm_pcs286_checks_mask(&f.checks,2)==BM_STATUS_INVALID_ARGUMENT);
    assert(bm_pcs286_checks_status(&unused,&bits)==BM_STATUS_INVALID_ARGUMENT && bits==0xa5);
    assert(bm_pcs286_checks_status(&f.checks,NULL)==BM_STATUS_INVALID_ARGUMENT);
    assert(bm_pcs286_checks_state(&f.checks,NULL)==BM_STATUS_INVALID_ARGUMENT);
    assert(bm_pcs286_checks_reset(NULL)==BM_STATUS_INVALID_ARGUMENT);
    assert(!status(&f) && state(&f).masked && !f.calls);
}
int main(void)
{
    invalid_arguments(); truth_tables(); held_io_and_independent_sources(); failures(); nmi_roundtrip();
    puts("AT checks/NMI:32 truth combinations,held IO,mask/capture independence,failures,real CPU vector2/IRET and port61 refresh composition passed");
    return 0;
}
