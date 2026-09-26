/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Authored IBM6280070 signal truth tables; no BIOS or hardware captures. */
#include "port61.h"
#include "pit8254_private.h"
#include "board_io.h"
#include <blumach/platforms/null_host.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct fixture {
    bm_engine_t *engine;
    bm_pit8254_t *pit;
    bm_at_clock_link_t *clock;
    bm_pcs286_port61_t port;
    uint8_t status_bits;
    int ram_enabled, io_enabled, observe;
    unsigned samples, checks, edges, requests, callbacks;
    uint64_t when[4096]; int level[4096];
    bm_status_t sample_failure, check_failure;
    bm_at_pic_t *pic;
} fixture_t;
static unsigned cases;
static bm_bus_transaction_t txn(unsigned address, bm_bus_operation_t operation, unsigned value)
{
    bm_bus_transaction_t t={0};
    t.space=BM_ADDRESS_IO; t.operation=operation; t.address=address; t.value=value;
    t.size=t.alignment=1; t.wait_states=17;
    return t;
}
static bm_status_t sample(void *context,uint8_t *bits)
{
    fixture_t *f=context;
    ++f->samples; *bits=f->status_bits;
    if(f->observe) {
        bm_bus_transaction_t t=txn(0x61,BM_BUS_READ,0xbeef);
        t.attributes=BM_BUS_TRANSACTION_DEBUG;
        assert(bm_pcs286_port61_io(&f->port,&t)==BM_STATUS_INVALID_STATE && t.value==0xbeef);
        assert(bm_pcs286_port61_reset(&f->port)==BM_STATUS_INVALID_STATE);
    }
    return f->sample_failure;
}
static void inspect_callback(fixture_t *f)
{
    bm_pcs286_port61_state_t s;
    bm_bus_transaction_t t=txn(0x61,BM_BUS_WRITE,0);
    if(!f->observe) return;
    assert(bm_pcs286_port61_state(&f->port,&s)==BM_STATUS_OK);
    assert(bm_pcs286_port61_io(&f->port,&t)==BM_STATUS_INVALID_STATE);
    assert(bm_pcs286_port61_reset(&f->port)==BM_STATUS_INVALID_STATE);
    assert(bm_pcs286_port61_pit_input(&f->port,2,!s.out2)==BM_STATUS_INVALID_STATE);
    t.operation=BM_BUS_READ; t.attributes=BM_BUS_TRANSACTION_DEBUG;
    assert(bm_pcs286_port61_io(&f->port,&t)==BM_STATUS_OK);
    ++f->callbacks;
}
static bm_status_t checks(void *context,int ram,int io)
{
    fixture_t *f=context;
    assert((ram==0 || ram==1) && (io==0 || io==1));
    ++f->checks; f->ram_enabled=ram; f->io_enabled=io;
    inspect_callback(f);
    return f->check_failure;
}
static void speaker(void *context,int level)
{
    fixture_t *f=context;
    assert(f->edges<4096);
    f->when[f->edges]=bm_engine_now(f->engine); f->level[f->edges++]=level;
    inspect_callback(f);
}
static void pit_output(void *context,unsigned channel,int level)
{
    fixture_t *f=context;
    if(channel==2) assert(bm_pcs286_port61_pit_input(&f->port,channel,level)==BM_STATUS_OK);
    else if(channel==1) f->requests+=(unsigned)level; /* Requests NEVER synthesize REF DET. */
    else if(f->pic) assert(bm_at_pic_set_irq(f->pic,0,level)==BM_STATUS_OK);
}
static void start(fixture_t *f)
{
    bm_host_services_t h=bm_null_host_services();
    bm_engine_config_t ec={1,2,1}; bm_clock_rate_t rate={1000000000,1};
    bm_pit8254_config_t pc={0x40,pit_output,f};
    bm_pcs286_port61_config_t c={0};
    memset(f,0,sizeof(*f));
    f->ram_enabled=f->io_enabled=1;
    assert(bm_engine_create_clocked(&h,&ec,&f->engine)==BM_STATUS_OK);
    assert(bm_pit8254_create(&h,&pc,&f->pit)==BM_STATUS_OK);
    assert(bm_pit8254_attach_clock(&h,f->engine,f->pit,&rate,&f->clock)==BM_STATUS_OK);
    c.profile=BM_PCS286_PORT61_AT_SIGNALS; c.pit=f->pit; c.clock=f->clock;
    c.status=sample; c.checks=checks; c.board_context=f; c.speaker=speaker; c.speaker_context=f;
    assert(bm_pcs286_port61_initialize(&f->port,&c)==BM_STATUS_OK);
    assert(!f->samples && !f->checks && !f->edges);
}
static void finish(fixture_t *f)
{
    bm_engine_destroy(f->engine); bm_at_clock_link_destroy(f->clock); bm_pit8254_destroy(f->pit);
}
static void reset(fixture_t *f)
{
    assert(bm_engine_reset(f->engine)==BM_STATUS_OK);
    assert(bm_at_clock_link_reset(f->clock)==BM_STATUS_OK);
    f->status_bits=0; f->sample_failure=f->check_failure=BM_STATUS_OK;
    assert(bm_pcs286_port61_reset(&f->port)==BM_STATUS_OK);
}
static void pit_write(fixture_t *f,unsigned port,unsigned value)
{
    bm_bus_transaction_t t=txn(port,BM_BUS_WRITE,value);
    assert(bm_at_clock_link_io(f->clock,&t)==BM_STATUS_OK);
}
static void write61(fixture_t *f,unsigned value)
{
    bm_bus_transaction_t t=txn(0x61,BM_BUS_WRITE,value);
    assert(bm_pcs286_port61_io(&f->port,&t)==BM_STATUS_OK && t.wait_states==17);
}
static unsigned read61(fixture_t *f,int debug)
{
    bm_bus_transaction_t t=txn(0x61,BM_BUS_READ,0xbeef);
    t.attributes=debug?BM_BUS_TRANSACTION_DEBUG:0;
    assert(bm_pcs286_port61_io(&f->port,&t)==BM_STATUS_OK && t.wait_states==17);
    return (unsigned)t.value;
}
static void run(fixture_t *f,uint64_t ns)
{ assert(bm_engine_run_for(f->engine,ns)==BM_STATUS_OK); }

static void signal_matrix(void)
{
    fixture_t f;
    unsigned value,status,out;
    start(&f);
    for(out=0;out<2;++out) for(status=0;status<8;++status) for(value=0;value<256;++value) {
        f.status_bits=(uint8_t)(((status&1)<<4)|((status&6)<<5));
        pit_write(&f,0x43,out?0xb4:0xb0); /* Real PIT creates raw OUT2, no clock pulse. */
        write61(&f,value);
        assert(f.port.state.latch==(value&15));
        assert(f.ram_enabled==!(value&4) && f.io_enabled==!(value&8));
        assert(f.pit->exact.channel[2].gate==!!(value&1));
        assert(f.port.state.speaker_level==(out && (value&2)));
        assert(read61(&f,0)==((value&15)|f.status_bits|(out<<5)));
        assert(read61(&f,1)==((value&15)|f.status_bits|(out<<5)));
        ++cases;
    }
    finish(&f);
}
static void time_and_refresh_boundary(void)
{
    fixture_t f;
    unsigned i,reads;
    bm_pit_exact_device_t before;
    start(&f); f.observe=1;
    pit_write(&f,0x43,0x74); pit_write(&f,0x41,4); pit_write(&f,0x41,0);
    pit_write(&f,0x43,0xb6); pit_write(&f,0x42,4); pit_write(&f,0x42,0);
    write61(&f,3); f.edges=0;
    run(&f,11);
    assert(f.requests==3 && f.edges==5); /* Program edge + reloads at5/9. */
    for(i=0;i<5;++i) { assert(f.when[i]==3+2*i); assert(f.level[i]==(int)(i&1)); }
    assert(!(read61(&f,0)&0x10)); /* Real PIT1 edges are not completed refresh. */
    reads=f.samples;
    for(i=0;i<1000;++i) assert(!(read61(&f,0)&0x10));
    assert(f.samples==reads+1000 && f.requests==3); /* No read-count toggle. */
    f.status_bits=0x10; assert(read61(&f,0)&0x10); /* External REF DET changes. */
    write61(&f,2); /* Gate low forces mode3 OUT high: data alone drives DC. */
    assert(f.port.state.out2 && f.port.state.speaker_level);
    write61(&f,0); assert(!f.port.state.speaker_level && (read61(&f,0)&0x20));
    assert(f.callbacks>10);
    /* Quiet CE progress does not occur through DEBUG. */
    reset(&f); pit_write(&f,0x43,0xb0); pit_write(&f,0x42,100); pit_write(&f,0x42,0);
    write61(&f,1); run(&f,50); before=f.pit->exact;
    (void)read61(&f,1); assert(!memcmp(&before,&f.pit->exact,sizeof(before)));
    (void)read61(&f,0); assert(f.pit->exact.channel[2].clocks==50);
    reset(&f); assert(read61(&f,0)==0 && f.ram_enabled && f.io_enabled);
    finish(&f);
}
static void invalid_transport_and_configuration(void)
{
    fixture_t f;
    bm_pcs286_port61_t untouched,snapshot;
    bm_pcs286_port61_config_t c;
    bm_bus_transaction_t t,old;
    unsigned kind;
    start(&f); run(&f,50); snapshot=f.port;
    for(kind=0;kind<10;++kind) {
        bm_status_t expected=BM_STATUS_INVALID_ARGUMENT;
        t=txn(0x61,BM_BUS_READ,0xbeef);
        switch(kind) {
            case 0:t.space=BM_ADDRESS_MEMORY; expected=BM_STATUS_UNMAPPED;break;
            case 1:t.address=0x62;expected=BM_STATUS_UNMAPPED;break;
            case 2:t.address=0x63;expected=BM_STATUS_UNMAPPED;break;
            case 3:t.address=0x92;expected=BM_STATUS_UNMAPPED;break;
            case 4:t.size=2;expected=BM_STATUS_UNSUPPORTED;break;
            case 5:t.operation=BM_BUS_FETCH;expected=BM_STATUS_UNSUPPORTED;break;
            case 6:t.attributes=0x80000000U;break;
            case 7:t.alignment=2;break;
            case 8:t.address=0x10061;break;
            default:t.operation=BM_BUS_WRITE;t.attributes=BM_BUS_TRANSACTION_DEBUG;expected=BM_STATUS_UNSUPPORTED;break;
        }
        old=t; assert(bm_pcs286_port61_io(&f.port,&t)==expected);
        assert(!memcmp(&t,&old,sizeof(t)) && !memcmp(&snapshot,&f.port,sizeof(snapshot)));
        assert(!f.pit->exact.channel[0].clocks);
    }
    memset(&untouched,0x5a,sizeof(untouched));snapshot=untouched;
    for(kind=0;kind<5;++kind) {
        c=f.port.config;
        switch(kind) {case 0:c.status=NULL;break;case 1:c.checks=NULL;break;
        case 2:c.pit=NULL;break;case 3:c.clock=NULL;break;default:c.profile=0;break;}
        assert(bm_pcs286_port61_initialize(&untouched,&c)==BM_STATUS_INVALID_ARGUMENT);
        assert(!memcmp(&untouched,&snapshot,sizeof(snapshot)));
    }
    assert(bm_pcs286_port61_pit_input(&f.port,1,1)==BM_STATUS_UNSUPPORTED);
    assert(bm_pcs286_port61_pit_input(&f.port,0,1)==BM_STATUS_UNSUPPORTED);
    assert(bm_pcs286_port61_pit_input(&f.port,2,2)==BM_STATUS_INVALID_ARGUMENT);
    assert(bm_pcs286_port61_state(NULL,NULL)==BM_STATUS_INVALID_ARGUMENT);
    assert(bm_pcs286_port61_reset(NULL)==BM_STATUS_INVALID_ARGUMENT);
    finish(&f);
}
static void failures_and_isolation(void)
{
    fixture_t f,other;
    bm_bus_transaction_t t,original;
    unsigned before;
    start(&f);start(&other);
    run(&f,10);f.sample_failure=BM_STATUS_DEVICE_ERROR;
    t=txn(0x61,BM_BUS_READ,0xabcd);original=t;
    assert(bm_pcs286_port61_io(&f.port,&t)==BM_STATUS_DEVICE_ERROR);
    assert(!memcmp(&t,&original,sizeof(t)) && f.pit->exact.channel[0].clocks==10);
    before=f.samples;
    assert(bm_pcs286_port61_io(&f.port,&t)==BM_STATUS_DEVICE_ERROR && f.samples==before);
    f.sample_failure=BM_STATUS_OK; f.status_bits=0xc0;
    assert(read61(&f,1)==0xc0); /* Observation never clears retained failure. */
    assert(read61(&other,0)==0);
    reset(&f);f.status_bits=1;t=original;
    assert(bm_pcs286_port61_io(&f.port,&t)==BM_STATUS_DEVICE_ERROR); /* Bad endpoint bits. */
    reset(&f);f.check_failure=BM_STATUS_UNSUPPORTED;
    t=txn(0x61,BM_BUS_WRITE,3);original=t;
    assert(bm_pcs286_port61_io(&f.port,&t)==BM_STATUS_UNSUPPORTED);
    assert(!memcmp(&t,&original,sizeof(t)) && f.port.state.latch==3);
    assert(f.pit->exact.channel[2].gate && f.checks); /* Accepted prefix retained. */
    before=f.checks;
    assert(bm_pcs286_port61_io(&f.port,&t)==BM_STATUS_UNSUPPORTED && f.checks==before);
    reset(&f);
    /* Idle PIT2 held at gate0 can be synchronized near the engine time limit. */
    pit_write(&f,0x43,0xb0);pit_write(&f,0x42,4);pit_write(&f,0x42,0);
    run(&f,UINT64_MAX-1);t=txn(0x61,BM_BUS_WRITE,1);original=t;
    assert(bm_pcs286_port61_io(&f.port,&t)==BM_STATUS_CAPACITY_EXCEEDED);
    assert(!memcmp(&t,&original,sizeof(t)) && f.port.state.latch==1);
    assert(f.port.state.failure==BM_STATUS_CAPACITY_EXCEEDED);
    reset(&f);assert(read61(&f,0)==0);
    finish(&f);finish(&other);
}
static bm_status_t no_memory(void *context,bm_at_transfer_t *transfer)
{ (void)context;(void)transfer;assert(!"unexpected memory access");return BM_STATUS_DEVICE_ERROR; }
static void at_composition(void)
{
    fixture_t f;
    bm_host_services_t h=bm_null_host_services();
    bm_at_pic_config_t pc={0};bm_at_dma_config_t dc={0};bm_at_bus_config_t bc={0};
    bm_at_dma_t *dma;bm_at_bus_t *bus;bm_pcs286_io_t io;bm_pcs286_io_config_t c={0};
    bm_gc103_memory_t headland;bm_ioc02_legacy_registers_t ioc;
    const unsigned ports[]={0x20,0xa0,0x21,0xa1,0x21,0xa1,0x21,0xa1,0x21,0x43,0x40,0x43,0x42,0x61};
    const unsigned values[]={0x11,0x11,0x20,0x28,4,2,1,1,0xfe,0x14,4,0x94,4,3};
    unsigned i;uint8_t vector=0;
    start(&f);pc.master_base=0x20;pc.slave_base=0xa0;pc.cascade_line=2;
    assert(bm_at_pic_create(&h,&pc,&f.pic)==BM_STATUS_OK);
    dc.memory=no_memory;dc.clock=(bm_clock_rate_t){4,1};
    assert(bm_at_dma_create(&h,&dc,&dma)==BM_STATUS_OK);
    assert(bm_gc103_memory_initialize(&headland,0x100000)==BM_STATUS_OK);
    assert(bm_ioc02_legacy_initialize(&ioc)==BM_STATUS_OK);
    c.profile=BM_PCS286_IO_LEGACY_GC103_AT;c.headland=&headland;c.ioc02=&ioc;c.pic=f.pic;c.dma=dma;
    c.service_clock=(bm_clock_rate_t){8,1};c.timing=BM_PCS286_IO_PROVISIONAL;c.resource_count=2;
    c.resources[0]=(bm_pcs286_io_resource_t){0x40,0x43,1,bm_at_clock_link_io,f.clock,0};
    c.resources[1]=(bm_pcs286_io_resource_t){0x61,0x61,1,bm_pcs286_port61_io,&f.port,0};
    assert(bm_pcs286_io_initialize(&io,&c)==BM_STATUS_OK);
    bc.cpu_clock=(bm_clock_rate_t){12,1};bc.isa_clock=(bm_clock_rate_t){8,1};
    bc.memory=no_memory;bc.io=bm_pcs286_io_access;bc.decode_context=&io;
    assert(bm_at_bus_create(&h,&bc,&bus)==BM_STATUS_OK);
    for(i=0;i<sizeof(ports)/sizeof(ports[0]);++i) {
        bm_bus_transaction_t t=txn(ports[i],BM_BUS_WRITE,values[i]);t.wait_states=0;
        assert(bm_at_bus_cpu_access(bus,&t)==BM_STATUS_OK);
    }
    assert(bm_at_pic_acknowledge(f.pic,0,&vector)==BM_STATUS_OK);
    assert(bm_at_pic_acknowledge(f.pic,1,&vector)==BM_STATUS_OK && vector==0x20);
    {bm_bus_transaction_t t=txn(0x20,BM_BUS_WRITE,0x20);t.wait_states=0;
     assert(bm_at_bus_cpu_access(bus,&t)==BM_STATUS_OK);}
    run(&f,5);
    assert(bm_at_pic_acknowledge(f.pic,0,&vector)==BM_STATUS_OK);
    assert(bm_at_pic_acknowledge(f.pic,1,&vector)==BM_STATUS_OK && vector==0x20);
    f.status_bits=0xd0;
    {bm_bus_transaction_t t=txn(0x61,BM_BUS_READ,0);t.wait_states=0;
     assert(bm_at_bus_cpu_access(bus,&t)==BM_STATUS_OK && t.value==0xf3);
     f.sample_failure=BM_STATUS_DEVICE_ERROR;t.value=0xbeef;
     assert(bm_at_bus_cpu_access(bus,&t)==BM_STATUS_DEVICE_ERROR && t.value==0xbeef);}
    bm_at_bus_destroy(bus);finish(&f);bm_at_dma_destroy(dma);bm_at_pic_destroy(f.pic);
}
int main(void)
{
    signal_matrix();time_and_refresh_boundary();invalid_transport_and_configuration();
    failures_and_isolation();at_composition();
    printf("Port61: %u documented signal combinations; clocked GATE/speaker, external REF DET/PCK/IO CH CK, host failures and AT/PIC integration\n",cases);
    return 0;
}
