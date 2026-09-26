/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Authored programming/request tests, not DMA transfer or board acceptance.
 */
#include <blumach/components/at_dma.h>
#include <blumach/platforms/null_host.h>
#include "failure_injection_host.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct fixture { int hrq; unsigned edges; } fixture_t;
static void request(void *context, int level)
{
    fixture_t *f=context; assert(f->hrq!=level); f->hrq=level; ++f->edges;
}
static void forbidden_pin(void *context,int level)
{ (void)context; (void)level; assert(!"DACK/TC is not implemented"); }
static bm_status_t forbidden_memory(void *context,bm_at_transfer_t *t)
{ (void)context; (void)t; assert(!"DMA memory transfer is not implemented"); return BM_STATUS_DEVICE_ERROR; }
static bm_status_t forbidden_read(void *context,uint16_t *v)
{ (void)context; (void)v; assert(!"device transfer is not implemented"); return BM_STATUS_DEVICE_ERROR; }
static bm_status_t forbidden_write(void *context,uint16_t v)
{ (void)context; (void)v; assert(!"device transfer is not implemented"); return BM_STATUS_DEVICE_ERROR; }
static bm_at_dma_config_t config(fixture_t *f)
{
    bm_at_dma_config_t c={0}; c.memory=forbidden_memory; c.clock=(bm_clock_rate_t){4000000,1};
    c.bus_request=request; c.bus_context=f;
    for(unsigned i=0;i<8;++i) c.endpoints[i]=(bm_at_dma_endpoint_t){NULL,forbidden_read,forbidden_write,forbidden_pin,forbidden_pin};
    return c;
}
static bm_at_dma_t *create(fixture_t *f)
{
    bm_at_dma_t *d=NULL; bm_host_services_t h=bm_null_host_services(); bm_at_dma_config_t c=config(f);
    memset(f,0,sizeof(*f)); assert(bm_at_dma_create(&h,&c,&d)==BM_STATUS_OK && d); assert(!f->edges); return d;
}
static bm_bus_transaction_t transaction(unsigned port,bm_bus_operation_t op,unsigned value)
{
    bm_bus_transaction_t t={0};t.space=BM_ADDRESS_IO;t.operation=op;t.address=port;t.size=1;t.value=value;return t;
}
static void wr(bm_at_dma_t *d,unsigned port,unsigned value)
{
    bm_bus_transaction_t t=transaction(port,BM_BUS_WRITE,value);t.wait_states=17;
    assert(bm_at_dma_io(d,&t)==BM_STATUS_OK && !t.wait_states);
}
static unsigned rd(bm_at_dma_t *d,unsigned port,int debug)
{
    bm_bus_transaction_t t=transaction(port,BM_BUS_READ,99);t.attributes=debug?BM_BUS_TRANSACTION_DEBUG:0;
    assert(bm_at_dma_io(d,&t)==BM_STATUS_OK && !t.wait_states);return (unsigned)t.value;
}
static unsigned port(unsigned controller,unsigned reg) { return controller?0xc0+2*reg:reg; }
static bm_at_dma_state_t state(bm_at_dma_t *d)
{ bm_at_dma_state_t s;assert(bm_at_dma_state(d,&s)==BM_STATUS_OK);return s; }
static bm_at_dma_channel_state_t channel(bm_at_dma_t *d,unsigned ch)
{ bm_at_dma_channel_state_t s;assert(bm_at_dma_channel_state(d,ch,&s)==BM_STATUS_OK);return s; }
static void word(bm_at_dma_t *d,unsigned unit,unsigned reg,unsigned value)
{ wr(d,port(unit,12),0);wr(d,port(unit,reg),value&255);wr(d,port(unit,reg),value>>8); }

static void words_and_flipflops(void)
{
    fixture_t f;bm_at_dma_t *d=create(&f);unsigned cases=0;
    for(unsigned ch=0;ch<8;++ch) for(unsigned count=0;count<2;++count)
    for(unsigned v=0;v<65536;++v) {
        unsigned u=ch/4,r=2*(ch%4)+count;word(d,u,r,v);
        bm_at_dma_channel_state_t s=channel(d,ch);
        assert((count?s.current_count:s.current_address)==v && (count?s.base_count:s.base_address)==v);
        assert(rd(d,port(u,r),1)==(v&255) && rd(d,port(u,r),1)==(v&255));
        assert(rd(d,port(u,r),0)==(v&255));
        assert(rd(d,port(u,r),1)==(v>>8));
        assert(rd(d,port(u,r),0)==(v>>8)); ++cases;
    }
    word(d,0,0,0x1234); word(d,0,3,0xabcd); word(d,1,0,0x5678);
    assert(rd(d,0,0)==0x34); /* One FF per controller, shared across registers. */
    assert(rd(d,0xc0,0)==0x78); assert(rd(d,3,0)==0xab);
    wr(d,0xc2,0xef); assert(channel(d,4).current_count==0xefff);
    wr(d,0,0x99); assert(channel(d,0).current_address==0x1299);
    assert(rd(d,8,0)==0 && rd(d,13,0)==0 && state(d).byte_high[0]==1);
    wr(d,0,0x77); assert(channel(d,0).current_address==0x7799);
    assert(!f.edges); bm_at_dma_destroy(d);
    printf("AT DMA: %u complete address/count values; shared/independent mixed byte sequencing and DEBUG reads\n",cases);
}

static void programming_and_reset(void)
{
    const unsigned pages[]={0x87,0x83,0x81,0x82,0,0x8b,0x89,0x8a};
    fixture_t f;bm_at_dma_t *d=create(&f);
    for(unsigned ch=0;ch<8;++ch) {
        for(unsigned v=0;v<256;++v) {
            if(ch!=4) {wr(d,pages[ch],v);assert(rd(d,pages[ch],0)==v && channel(d,ch).page==v);}
            wr(d,port(ch/4,11),(v&252)|(ch&3)); assert(channel(d,ch).mode==(v&252));
        }
        word(d,ch/4,2*(ch%4),0x2345+ch);word(d,ch/4,2*(ch%4)+1,0xabcd-ch);
    }
    for(unsigned u=0;u<2;++u) for(unsigned v=0;v<256;++v) {
        wr(d,port(u,8),v); assert(state(d).command[u]==v);
        wr(d,port(u,15),v); assert(state(d).mask[u]==(v&15));
        wr(d,port(u,14),v); assert(!state(d).mask[u]);
        for(unsigned ch=0;ch<4;++ch) {
            wr(d,port(u,10),ch|4|(v&248));assert(channel(d,u*4+ch).masked);
            wr(d,port(u,10),ch|(v&248));assert(!channel(d,u*4+ch).masked);
        }
    }
    bm_at_dma_channel_state_t saved[8];for(unsigned ch=0;ch<8;++ch)saved[ch]=channel(d,ch);
    assert(bm_at_dma_set_dreq(d,2,1)==BM_STATUS_OK);
    wr(d,0xb,0x82);wr(d,9,6);wr(d,0,0x66);saved[0]=channel(d,0);saved[2]=channel(d,2);
    wr(d,13,0xff);assert(state(d).mask[0]==15 && !state(d).software_request[0] && !state(d).command[0]);
    assert(!state(d).byte_high[0] && channel(d,2).requested); /* external DREQ persists */
    bm_at_dma_reset(d);
    bm_at_dma_state_t s=state(d);assert(s.mask[0]==15 && s.mask[1]==15 && s.dreq==4 && !s.bus_request);
    for(unsigned ch=0;ch<8;++ch) {
        bm_at_dma_channel_state_t a=channel(d,ch);
        assert(a.base_address==saved[ch].base_address && a.current_address==saved[ch].current_address);
        assert(a.base_count==saved[ch].base_count && a.current_count==saved[ch].current_count);
        assert(a.mode==saved[ch].mode && a.page==saved[ch].page && a.masked && !a.terminal_count);
    }
    bm_at_dma_destroy(d);
    puts("AT DMA: page bytes, all command/mode/mask writes and reset retention; channel 4 has no page/device endpoint");
}

static void request_matrix(void)
{
    fixture_t f;bm_at_dma_t *d=create(&f);unsigned cases=0;
    for(unsigned u=0;u<2;++u) for(unsigned soft=0;soft<16;++soft)
    for(unsigned lines=0;lines<16;++lines) for(unsigned mask=0;mask<16;++mask)
    for(unsigned disabled=0;disabled<2;++disabled) {
        if(u && (lines&1))continue; /* upper channel zero is wired cascade */
        wr(d,8,4);wr(d,0xd0,4);wr(d,13,0);wr(d,0xda,0);wr(d,8,4);wr(d,0xd0,4);
        for(unsigned ch=0;ch<8;++ch)if(ch!=4)assert(bm_at_dma_set_dreq(d,ch,0)==BM_STATUS_OK);
        for(unsigned local=0;local<4;++local) {
            unsigned ch=u*4+local;
            if(ch!=4)assert(bm_at_dma_set_dreq(d,ch,(lines>>local)&1)==BM_STATUS_OK);
            wr(d,port(u,11),0x80|local);wr(d,port(u,9),local|((soft&(1U<<local))?4:0));
        }
        wr(d,port(u,15),mask);
        if(!u) {wr(d,0xd6,0xc0);wr(d,0xd4,0);wr(d,0xd0,0);}
        wr(d,port(u,8),disabled?4:0);
        /* Raw status is independent of masks/enable; HRQ uses eligibility. */
        assert(rd(d,port(u,8),0)==((soft|lines)<<4));
        unsigned expected=!disabled && (soft || (lines & (15U-mask)));
        assert((unsigned)f.hrq==expected && (unsigned)state(d).bus_request==expected);
        if(!u)assert(rd(d,0xd0,0)==(expected?0x10:0));
        uint64_t cycles=99;assert(bm_at_dma_service(d,&cycles)==BM_STATUS_IDLE && !cycles);
        unsigned edges=f.edges;assert(bm_at_dma_set_bus_grant(d,0)==BM_STATUS_OK && f.edges==edges);
        ++cases;
    }
    bm_at_dma_destroy(d);assert(!f.hrq);
    printf("AT DMA: %u request/mask/disable/software/cascade cases; no grant gives idle without transfers\n",cases);
}

static void rejected_io_and_gate(void)
{
    fixture_t f;bm_at_dma_t *d=create(&f);wr(d,0xc4,0x5a);
    for(unsigned bad=0;bad<10;++bad) {
        bm_bus_transaction_t t=transaction(0xc4,BM_BUS_WRITE,0xa5);
        if(bad==0)t.size=2;
        if(bad==1)t.operation=BM_BUS_FETCH;
        if(bad==2)t.address=0xc5;
        if(bad==3)t.space=BM_ADDRESS_DATA;
        if(bad==4)t.attributes=BM_BUS_TRANSACTION_DEBUG;
        if(bad==5)t.attributes=128;
        if(bad==6)t.address=0x10000;
        if(bad==7)t.alignment=2;
        if(bad==8)t.endianness=(bm_endianness_t)99;
        if(bad==9)assert(bm_at_dma_set_bus_grant(d,1)==BM_STATUS_OK);
        bm_at_dma_state_t before=state(d);bm_bus_transaction_t copy=t;
        assert(bm_at_dma_io(d,&t)!=BM_STATUS_OK && !memcmp(&t,&copy,sizeof(t)));
        bm_at_dma_state_t after=state(d);assert(!memcmp(&before,&after,sizeof(before)));
        assert(bm_at_dma_set_bus_grant(d,0)==BM_STATUS_OK);
    }
    assert(rd(d,0xc4,1)==0);wr(d,0xc4,0xa5);assert(channel(d,5).current_address==0xa55a);
    for(unsigned p=0;p<=65535;++p) {
        int mapped=p<16 || (p>=0xc0 && p<=0xde && !(p&1)) ||
            p==0x87||p==0x83||p==0x81||p==0x82||p==0x8b||p==0x89||p==0x8a;
        if(mapped)continue;
        for(unsigned op=0;op<2;++op) {
            bm_bus_transaction_t t=transaction(p,(bm_bus_operation_t)op,0xa5),before=t;
            assert(bm_at_dma_io(d,&t)==BM_STATUS_UNMAPPED && !memcmp(&t,&before,sizeof(t)));
        }
    }
    for(unsigned u=0;u<2;++u) for(unsigned r=9;r<16;++r) if(r!=13) {
        bm_bus_transaction_t t=transaction(port(u,r),BM_BUS_READ,77);
        assert(bm_at_dma_io(d,&t)==BM_STATUS_UNMAPPED && t.value==77);
    }
    wr(d,0xd6,0xc0);wr(d,0xd4,0);wr(d,0xa,2);
    wr(d,0xb,0x0e); /* illegal transfer type remains explicitly unsupported */
    assert(bm_at_dma_set_dreq(d,2,1)==BM_STATUS_OK && f.hrq);
    assert(bm_at_dma_set_bus_grant(d,1)==BM_STATUS_OK);
    assert(rd(d,8,1)==0x40);uint64_t cycles=99;
    assert(bm_at_dma_service(d,&cycles)==BM_STATUS_UNSUPPORTED && !cycles && !f.hrq && state(d).stopped);
    assert(bm_at_dma_service(d,&cycles)==BM_STATUS_INVALID_STATE && !cycles);
    assert(bm_at_dma_set_bus_grant(d,0)==BM_STATUS_OK);wr(d,13,0);wr(d,0xda,0);
    assert(bm_at_dma_service(d,&cycles)==BM_STATUS_INVALID_STATE && !f.hrq);
    bm_at_dma_reset(d);assert(bm_at_dma_service(d,&cycles)==BM_STATUS_IDLE && !state(d).stopped);
    assert(channel(d,2).requested); /* reset does not lower an external wire */
    unsigned edges=f.edges;
    assert(bm_at_dma_set_dreq(d,2,1)==BM_STATUS_OK && f.edges==edges);
    assert(bm_at_dma_set_bus_grant(d,1)==BM_STATUS_OK);
    bm_at_dma_reset(d);
    assert(state(d).bus_grant && channel(d,2).requested && !f.hrq);
    assert(rd(d,8,1)==0x40); /* inspection remains pure while grant is held */
    bm_bus_transaction_t held=transaction(0,BM_BUS_READ,77),before=held;
    assert(bm_at_dma_io(d,&held)==BM_STATUS_INVALID_STATE && !memcmp(&held,&before,sizeof(held)));
    assert(bm_at_dma_set_bus_grant(d,0)==BM_STATUS_OK);
    assert(bm_at_dma_set_dreq(d,4,1)==BM_STATUS_INVALID_ARGUMENT);
    assert(bm_at_dma_set_dreq(d,8,1)==BM_STATUS_INVALID_ARGUMENT);
    assert(bm_at_dma_set_dreq(d,2,2)==BM_STATUS_INVALID_ARGUMENT);
    assert(bm_at_dma_set_bus_grant(d,-1)==BM_STATUS_INVALID_ARGUMENT);
    bm_at_dma_destroy(d);
    puts("AT DMA: full unmapped port space, write-only reads, atomic invalid/debug/granted I/O; granted-service stop/reset without DACK/TC/memory");
}

static void lifecycle(void)
{
    fixture_t f={0};bm_at_dma_config_t c=config(&f);bm_at_dma_t *d=NULL;
    failure_injection_host_t tracker;failure_injection_host_initialize(&tracker);
    bm_host_services_t h=failure_injection_host_services(&tracker);
    failure_injection_host_fail_on(&tracker,0);
    assert(bm_at_dma_create(&h,&c,&d)==BM_STATUS_OUT_OF_MEMORY && !d && !tracker.outstanding_allocations);
    failure_injection_host_fail_on(&tracker,(size_t)-1);
    assert(bm_at_dma_create(&h,&c,&d)==BM_STATUS_OK && d && tracker.outstanding_allocations==1);
    fixture_t other;bm_at_dma_t *peer=create(&other);wr(d,0,0x42);assert(!channel(peer,0).current_address);
    wr(d,0xd4,1);assert(bm_at_dma_set_dreq(d,5,1)==BM_STATUS_OK && f.hrq && !other.hrq);
    bm_at_dma_destroy(d);assert(!f.hrq && !tracker.outstanding_allocations);bm_at_dma_destroy(peer);
    for(unsigned bad=0;bad<5;++bad) {
        bm_at_dma_config_t invalid=c;bm_host_services_t host=h;d=(bm_at_dma_t*)&f;
        if(bad==0)invalid.memory=NULL;
        if(bad==1)invalid.clock.cycles_per_second_numerator=0;
        if(bad==2)invalid.clock.cycles_per_second_denominator=0;
        if(bad==3)host.release=NULL;
        if(bad==4)invalid.mem2mem_profile=(bm_at_dma_mem2mem_profile_t)2;
        assert(bm_at_dma_create(&host,&invalid,&d)==BM_STATUS_INVALID_ARGUMENT && !d);
    }
    bm_at_dma_destroy(NULL);bm_at_dma_reset(NULL);
    assert(bm_at_dma_io(NULL,NULL)==BM_STATUS_INVALID_ARGUMENT);
    assert(bm_at_dma_service(NULL,NULL)==BM_STATUS_INVALID_ARGUMENT);
    puts("AT DMA: allocation failure/cleanup, invalid construction, instance isolation and HRQ withdrawal on destruction");
}
static void ibm_page_wiring(void)
{
    /* Independent expected port list from IBM 6280070 Type1 sheet15, U124.
     * Covers idle, lower no-local-DACK, all seven normal channels, and refresh
     * superposed on each. A selected register alone never authorizes service. */
    static const struct { uint8_t pins; uint16_t normal, refresh; } cases[] = {
        {0x00,0x8b,0x8f}, {0x10,0x83,0x87},
        {0x11,0x87,0x87}, {0x12,0x83,0x87},
        {0x14,0x81,0x85}, {0x18,0x82,0x86},
        {0x20,0x8b,0x8f}, {0x40,0x89,0x8d}, {0x80,0x8a,0x8e}
    };
    for (unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
        for (unsigned ignored=0;ignored<4;++ignored) {
            uint8_t pins=(uint8_t)((cases[i].pins & 0xddU) |
                ((ignored & 1U) ? 0x02U : 0U) | ((ignored & 2U) ? 0x20U : 0U));
            assert(bm_at_dma_ibm_page_port(pins,0)==cases[i].normal);
            assert(bm_at_dma_ibm_page_port(pins,1)==cases[i].refresh);
            assert(bm_at_dma_ibm_page_port(pins,-1)==cases[i].refresh);
        }
    }
    puts("AT DMA: 72 IBM page routes plus 36 nonzero-refresh checks; internal DACK4, no local DACK, all normal channels, refresh and ignored DACK1/5");
}

int main(void)
{
    ibm_page_wiring();words_and_flipflops();programming_and_reset();request_matrix();rejected_io_and_gate();lifecycle();return 0;
}
