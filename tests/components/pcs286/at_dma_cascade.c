/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Authored Intel8237 cascade/AT ownership cases; no firmware or hardware traces.
 */
#include <blumach/components/at_dma.h>
#include <blumach/platforms/null_host.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct fixture fixture_t;
typedef struct endpoint { fixture_t *f; unsigned ch; } endpoint_t;
struct fixture {
    bm_at_dma_t *dma;
    bm_at_bus_t *bus;
    endpoint_t ep[8];
    int hrq,hold,dack[8],drop_on_ack;
    unsigned highs[8],lows[8],child_calls,child_effects,fail_at,after;
    bm_status_t error;
    uint8_t ram[4];
};
static bm_at_dma_state_t state(fixture_t *f)
{ bm_at_dma_state_t s;assert(bm_at_dma_state(f->dma,&s)==BM_STATUS_OK);return s; }
static bm_at_dma_channel_state_t channel(fixture_t *f,unsigned ch)
{ bm_at_dma_channel_state_t s;assert(bm_at_dma_channel_state(f->dma,ch,&s)==BM_STATUS_OK);return s; }
static void registers_equal(bm_at_dma_channel_state_t a,bm_at_dma_channel_state_t b)
{
    assert(a.base_address==b.base_address && a.current_address==b.current_address);
    assert(a.base_count==b.base_count && a.current_count==b.current_count);
    assert(a.page==b.page && a.mode==b.mode && a.masked==b.masked && a.terminal_count==b.terminal_count);
    /* requested is an input observation, deliberately allowed to change. */
}
static void hrq(void *p,int level)
{ fixture_t *f=p;assert(f->hrq!=level);f->hrq=level; }
static void hold(void *p,int level) { fixture_t *f=p;f->hold=level; }
static void dack(void *p,int level)
{
    endpoint_t *e=p;fixture_t *f=e->f;assert(e->ch!=4 && f->dack[e->ch]!=level);
    f->dack[e->ch]=level;
    if(level)++f->highs[e->ch];else ++f->lows[e->ch];
    assert(state(f).cascade_active==(level && (channel(f,e->ch).mode>>6)==3));
    if(level && f->drop_on_ack)assert(bm_at_dma_set_dreq(f->dma,e->ch,0)==BM_STATUS_OK);
}
static bm_status_t forbidden_read(void *p,uint16_t *value)
{ (void)p;(void)value;assert(0 && "cascade must not read endpoint data");return BM_STATUS_DEVICE_ERROR; }
static bm_status_t forbidden_write(void *p,uint16_t value)
{ (void)p;(void)value;assert(0 && "cascade must not write endpoint data");return BM_STATUS_DEVICE_ERROR; }
static bm_status_t forbidden_memory(void *p,bm_at_transfer_t *t)
{ (void)p;(void)t;assert(0 && "cascade must not generate memory cycles");return BM_STATUS_DEVICE_ERROR; }
static void forbidden_tc(void *p,int level)
{ (void)p;(void)level;assert(0 && "cascade must not pulse terminal count"); }
static void create(fixture_t *f,int missing_dack)
{
    memset(f,0,sizeof(*f));bm_host_services_t host=bm_null_host_services();bm_at_dma_config_t c={0};
    c.clock=(bm_clock_rate_t){4000000,3};c.memory=forbidden_memory;c.memory_context=f;c.bus_request=hrq;c.bus_context=f;
    for(unsigned ch=0;ch<8;++ch) {
        f->ep[ch]=(endpoint_t){f,ch};
        c.endpoints[ch]=(bm_at_dma_endpoint_t){&f->ep[ch],forbidden_read,forbidden_write,missing_dack?NULL:dack,forbidden_tc};
    }
    assert(bm_at_dma_create(&host,&c,&f->dma)==BM_STATUS_OK);
}
static unsigned port(unsigned u,unsigned r) {return u?0xc0+2*r:r;}
static void wr(fixture_t *f,unsigned p,unsigned value)
{
    bm_bus_transaction_t t={0};t.space=BM_ADDRESS_IO;t.operation=BM_BUS_WRITE;t.address=p;t.size=1;t.value=value;
    assert(bm_at_dma_io(f->dma,&t)==BM_STATUS_OK);
}
static unsigned rd(fixture_t *f,unsigned p)
{
    bm_bus_transaction_t t={0};t.space=BM_ADDRESS_IO;t.operation=BM_BUS_READ;t.address=p;t.size=1;t.attributes=BM_BUS_TRANSACTION_DEBUG;
    assert(bm_at_dma_io(f->dma,&t)==BM_STATUS_OK);return (unsigned)t.value;
}
static void program(fixture_t *f,unsigned ch,unsigned bits,unsigned address,unsigned count)
{
    static const unsigned pages[]={0x87,0x83,0x81,0x82,0,0x8b,0x89,0x8a};
    unsigned u=ch/4,l=ch%4;
    wr(f,port(u,12),0);wr(f,port(u,l*2),address&255);wr(f,port(u,l*2),address>>8);
    wr(f,port(u,l*2+1),count&255);wr(f,port(u,l*2+1),count>>8);
    wr(f,port(u,11),0xc0); /* Intel: begin cascading at local channel0 */
    wr(f,port(u,11),0xc0|bits|l);wr(f,port(u,10),l);wr(f,pages[ch],0xff);
    if(ch<4) {wr(f,0xd6,0xc0);wr(f,0xd4,0);}
}
static void request(fixture_t *f,unsigned ch)
{assert(bm_at_dma_set_dreq(f->dma,ch,1)==BM_STATUS_OK);}
static void grant(fixture_t *f,unsigned ch)
{assert(state(f).pending_channel==(int)ch);assert(bm_at_dma_set_bus_grant(f->dma,1)==BM_STATUS_OK);}
static void idle(fixture_t *f)
{uint64_t clocks=99;assert(bm_at_dma_service(f->dma,&clocks)==BM_STATUS_IDLE && !clocks);}
static void withdraw(fixture_t *f,unsigned ch)
{
    assert(bm_at_dma_set_dreq(f->dma,ch,0)==BM_STATUS_OK);
    assert(!f->hrq && !f->dack[ch] && !state(f).cascade_active && state(f).release_wait);
}
static void release(fixture_t *f)
{assert(bm_at_dma_set_bus_grant(f->dma,0)==BM_STATUS_OK);}
static void destroy(fixture_t *f)
{
    bm_at_dma_destroy(f->dma);assert(!f->hrq);
    for(unsigned ch=0;ch<8;++ch)assert(!f->dack[ch]);
    bm_at_bus_destroy(f->bus);
}

static void controls(void)
{
    unsigned cases=0;
    for(unsigned ch=0;ch<8;++ch)if(ch!=4)for(unsigned bits=0;bits<64;bits+=4)
    for(unsigned cmd=0;cmd<256;++cmd) {
        fixture_t f;create(&f,0);program(&f,ch,bits,cmd*257U,bits?65535:0);wr(&f,port(ch/4,8),cmd);
        bm_at_dma_channel_state_t before[8];for(unsigned i=0;i<8;++i)before[i]=channel(&f,i);
        request(&f,ch);
        if(cmd&4) {assert(!f.hrq);idle(&f);assert(!f.highs[ch]);}
        else {
            grant(&f,ch);
            if(cmd&1) {
                uint64_t clocks=99;assert(bm_at_dma_service(f.dma,&clocks)==BM_STATUS_UNSUPPORTED && !clocks);
                assert(state(&f).stopped && !f.highs[ch]);release(&f);
            } else {
                assert(bm_at_dma_set_eop(f.dma,0,1)==BM_STATUS_OK && bm_at_dma_set_eop(f.dma,1,1)==BM_STATUS_OK);
                idle(&f);assert(f.hrq && f.dack[ch] && f.highs[ch]==1 && state(&f).cascade_active);
                for(unsigned i=0;i<3;++i)idle(&f);
                assert(f.highs[ch]==1 && !f.lows[ch] && !rd(&f,port(ch/4,13)));
                assert(!(rd(&f,8)&15) && !(rd(&f,0xd0)&15));
                bm_at_dma_timing_t t={99,99,99};
                assert(bm_at_dma_channel_timing(f.dma,ch,&t)==BM_STATUS_UNSUPPORTED && !t.transfer_clocks && !t.read_pulse_clocks && !t.write_pulse_clocks);
                assert(bm_at_dma_set_bus_grant(f.dma,0)==BM_STATUS_INVALID_STATE);
                withdraw(&f,ch);idle(&f);assert(f.lows[ch]==1);
                assert(state(&f).priority_first[ch/4]==(cmd&16?(ch+1)%4:0));release(&f);
                assert(state(&f).eop[0] && state(&f).eop[1] && !f.hrq);
            }
        }
        for(unsigned i=0;i<8;++i)registers_equal(before[i],channel(&f,i));
        assert(!f.child_calls);destroy(&f);++cases;
    }
    printf("AT DMA cascade: %u channel/mode/command cases, no data/register/TC/EOP effects, ignored controls, held/duplicate DACK, priority and release\n",cases);
}

static void boundaries(void)
{
    for(unsigned ch=0;ch<8;++ch)if(ch!=4)for(unsigned when=0;when<5;++when) {
        fixture_t f;create(&f,0);program(&f,ch,60,0xffff,0);request(&f,ch);
        if(when==0) {assert(bm_at_dma_set_dreq(f.dma,ch,0)==BM_STATUS_OK && !f.hrq);idle(&f);}
        else {
            grant(&f,ch);
            if(when==1)assert(bm_at_dma_set_dreq(f.dma,ch,0)==BM_STATUS_OK);
            if(when==2)f.drop_on_ack=1;
            idle(&f);
            if(when<=2) {assert(!f.hrq && !state(&f).cascade_active && f.highs[ch]==(when==2));release(&f);}
            else if(when==3) {
                bm_at_dma_reset(f.dma);assert(!f.hrq && !state(&f).cascade_active && state(&f).bus_grant);
                assert(state(&f).dreq&(1U<<ch));release(&f);
            } /* when4: destroy active cascade, must withdraw once */
        }
        assert(channel(&f,ch).current_address==65535 && !channel(&f,ch).current_count);
        destroy(&f);assert(f.lows[ch]==f.highs[ch]);
    }
    /* Late competing higher-priority request cannot replace a granted master.
     * Rotating upper priority then gives lower channel0 the next fresh grant. */
    fixture_t f;create(&f,0);program(&f,7,0,0,0);program(&f,0,0,0,0);
    wr(&f,0xd0,16);wr(&f,8,16);request(&f,7);grant(&f,7);idle(&f);
    request(&f,0);idle(&f);assert(state(&f).pending_channel==7 && !f.highs[0]);
    withdraw(&f,7);assert(state(&f).priority_first[1]==0);idle(&f);assert(!f.highs[0]);
    release(&f);grant(&f,0);idle(&f);withdraw(&f,0);
    assert(state(&f).priority_first[0]==1 && state(&f).priority_first[1]==1);release(&f);destroy(&f);
    /* Withdraw, reassert under stale outer grant: cannot acknowledge twice. */
    create(&f,0);program(&f,1,0,0,0);request(&f,1);grant(&f,1);idle(&f);withdraw(&f,1);
    request(&f,1);assert(!f.hrq);idle(&f);assert(f.highs[1]==1);release(&f);
    grant(&f,1);idle(&f);assert(f.highs[1]==2);withdraw(&f,1);release(&f);destroy(&f);
    /* A retained external EOP must not be lost by the cascade service. It
     * applies to a following ordinary local transfer at its normal boundary. */
    create(&f,0);program(&f,0,0,0,0);program(&f,1,0,0x100,1);wr(&f,11,0x41);
    request(&f,0);grant(&f,0);assert(bm_at_dma_set_eop(f.dma,0,1)==BM_STATUS_OK);idle(&f);
    request(&f,1);idle(&f);assert(!channel(&f,0).terminal_count);withdraw(&f,0);release(&f);
    grant(&f,1);uint64_t clocks=99;assert(bm_at_dma_service(f.dma,&clocks)==BM_STATUS_OK && clocks==4);
    assert(channel(&f,1).terminal_count && !channel(&f,1).current_count && !state(&f).cascade_active && !f.hrq);
    release(&f);destroy(&f); /* no generated-TC callback: EOP alone ended count1 */
    puts("AT DMA cascade: 35 pre-grant/pre-DACK/callback/reset/destroy boundaries, non-preemption, two-level rotation and fresh-grant reacquisition");
    puts("AT DMA cascade: held EOP ignored during delegation and retained for the next ordinary local transfer");
}

static void unsupported(void)
{
    for(unsigned ch=0;ch<8;++ch)if(ch!=4)for(unsigned why=0;why<4;++why) {
        if(why==0 && ch%4==0)continue;
        fixture_t f;create(&f,why==1);program(&f,ch,0,0x1234,0x5678);
        if(why==0)wr(&f,port(ch/4,11),0x40); /* invalid cascade ordering */
        if(why==2)wr(&f,port(ch/4,9),(ch%4)|4); /* software cascade request */
        if(why==3)wr(&f,port(ch/4,8),1); /* mem2mem gate */
        request(&f,ch);grant(&f,ch);uint64_t clocks=99;
        assert(bm_at_dma_service(f.dma,&clocks)==(why==1?BM_STATUS_INVALID_STATE:BM_STATUS_UNSUPPORTED) && !clocks);
        assert(!f.highs[ch] && !f.hrq && state(&f).stopped && channel(&f,ch).current_address==0x1234);
        release(&f);wr(&f,port(ch/4,13),0);assert(bm_at_dma_service(f.dma,&clocks)==BM_STATUS_INVALID_STATE);
        bm_at_dma_reset(f.dma);assert(!state(&f).stopped);destroy(&f);
    }
    /* Intel's documented broken local0 configuration must not silently run
     * an ordinary channel0 transfer just because that channel isn't cascade. */
    fixture_t f;create(&f,0);program(&f,1,0,0,0);wr(&f,11,0x40);wr(&f,10,0);
    request(&f,0);grant(&f,0);uint64_t clocks=9;
    assert(bm_at_dma_service(f.dma,&clocks)==BM_STATUS_UNSUPPORTED && !clocks && !f.highs[0]);destroy(&f);
    puts("AT DMA cascade: unsafe local0 ordering, software requests, mem2mem and missing grant callback stop before effects; reset-only recovery");
}

/* An external actor, advanced explicitly by the test coordinator. Its clock,
 * address and width are deliberately independent of DMA page/address latches. */
static bm_status_t child_memory(void *p,bm_at_transfer_t *t)
{
    fixture_t *f=p;assert(state(f).cascade_active && f->hold);
    assert(t->master==BM_AT_MASTER_ISA && t->requester_clock.cycles_per_second_numerator==7000000 && t->requester_clock.cycles_per_second_denominator==2);
    assert(t->bus.address>=0x923457 && t->bus.address<0x92345b && t->bus.size==1);
    ++f->child_calls;
    if(f->child_calls==f->fail_at && !f->after)return f->error;
    ++f->child_effects;unsigned offset=(unsigned)(t->bus.address-0x923457);
    if(t->bus.operation==BM_BUS_WRITE)f->ram[offset]=(uint8_t)t->bus.value;
    else {assert(t->bus.operation==BM_BUS_READ);t->bus.value=f->ram[offset];}
    t->bus.wait_states=17;
    return f->child_calls==f->fail_at?f->error:BM_STATUS_OK;
}
static bm_at_transfer_t child_transfer(unsigned offset,unsigned value)
{
    bm_at_transfer_t t={0};t.master=BM_AT_MASTER_ISA;t.requester_clock=(bm_clock_rate_t){7000000,2};
    t.bus.space=BM_ADDRESS_MEMORY;t.bus.operation=BM_BUS_WRITE;t.bus.address=0x923457+offset;
    t.bus.size=t.bus.alignment=1;t.bus.value=value;return t;
}
static void external_bus(void)
{
    const bm_status_t errors[]={BM_STATUS_OK,BM_STATUS_DEVICE_ERROR,BM_STATUS_UNMAPPED,BM_STATUS_READ_ONLY,BM_STATUS_CAPACITY_EXCEEDED,BM_STATUS_IDLE};
    unsigned cases=0;
    for(unsigned ch=0;ch<8;++ch)if(ch!=4)for(unsigned err=0;err<6;++err)for(unsigned after=0;after<2;++after) {
        fixture_t f;create(&f,0);bm_host_services_t host=bm_null_host_services();bm_at_bus_config_t bc={0};
        bc.cpu_clock=(bm_clock_rate_t){12000000,1};bc.isa_clock=(bm_clock_rate_t){8000000,1};
        bc.memory=child_memory;bc.io=child_memory;bc.decode_context=&f;bc.hold=hold;bc.hold_context=&f;
        assert(bm_at_bus_create(&host,&bc,&f.bus)==BM_STATUS_OK);
        program(&f,ch,60,0xfffe,0);bm_at_dma_channel_state_t before=channel(&f,ch);request(&f,ch);
        assert(bm_at_bus_set_lock(f.bus,1)==BM_STATUS_OK && bm_at_bus_request(f.bus,BM_AT_MASTER_ISA,1)==BM_STATUS_OK && !f.hold);
        assert(bm_at_bus_hold_ack(f.bus,1)==BM_STATUS_INVALID_STATE);idle(&f);assert(!f.highs[ch]);
        bm_at_transfer_t t=child_transfer(0,0x51);assert(bm_at_bus_access(f.bus,&t)==BM_STATUS_IDLE && !f.child_calls);
        assert(bm_at_bus_set_lock(f.bus,0)==BM_STATUS_OK && bm_at_bus_hold_ack(f.bus,1)==BM_STATUS_OK);grant(&f,ch);idle(&f);
        assert(!f.child_calls); /* notification never recursively advances actor */
        for(unsigned master=BM_AT_MASTER_CPU;master<=BM_AT_MASTER_DMA16;++master) {
            t=child_transfer(0,0x99);t.master=(bm_at_master_t)master;
            assert(bm_at_bus_access(f.bus,&t)==BM_STATUS_IDLE && !f.child_calls);
        }
        t=child_transfer(0,0x51);assert(bm_at_bus_access(f.bus,&t)==BM_STATUS_OK && t.bus.wait_states==17 && f.ram[0]==0x51);
        t=child_transfer(1,0x72);f.fail_at=err?2:0;f.after=after;f.error=errors[err];
        bm_status_t result=bm_at_bus_access(f.bus,&t);assert(result==errors[err]);
        assert(f.ram[0]==0x51 && f.ram[1]==(!err||after?0x72:0) && f.child_effects==1U+(!err||after));
        assert(t.bus.wait_states==(err?0U:17U));
        registers_equal(before,channel(&f,ch));idle(&f);assert(f.child_calls==2 && state(&f).cascade_active);
        /* The DMA has no access status to translate: the outer coordinator
         * handles any child error (including IDLE under a promised grant),
         * stops that actor, and withdraws ownership without TC or replay. */
        withdraw(&f,ch);assert(bm_at_bus_request(f.bus,BM_AT_MASTER_ISA,0)==BM_STATUS_OK && !f.hold);
        t=child_transfer(2,0x93);assert(bm_at_bus_access(f.bus,&t)==BM_STATUS_IDLE && f.child_calls==2);
        assert(bm_at_bus_request(f.bus,BM_AT_MASTER_ISA,1)==BM_STATUS_INVALID_STATE);
        assert(bm_at_bus_hold_ack(f.bus,0)==BM_STATUS_OK);release(&f);
        registers_equal(before,channel(&f,ch));destroy(&f);++cases;
    }
    printf("AT DMA cascade: %u real AT external-master blocks, independent address/clock, LOCK/HLDA, waits and before/after failures retain effects without DMA TC/replay\n",cases);
}

int main(void) {controls();boundaries();unsupported();external_bus();return 0;}
