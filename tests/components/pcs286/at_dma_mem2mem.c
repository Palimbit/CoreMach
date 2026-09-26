/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * IBM matched-count functional profile, not hardware/Headland acceptance.
 */
#include <blumach/components/at_dma.h>
#include <blumach/platforms/null_host.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct fixture {
    bm_at_dma_t *dma;
    bm_at_bus_t *bus;
    uint8_t ram[65536];
    int hrq,hold,tc;
    unsigned calls,reads,writes,pulses,fail_at,after,eop_pattern,withdraw_at;
    uint32_t waits[2];
    bm_status_t error;
} fixture_t;
static bm_at_dma_state_t state(fixture_t *f)
{ bm_at_dma_state_t s; assert(bm_at_dma_state(f->dma,&s)==BM_STATUS_OK); return s; }
static bm_at_dma_channel_state_t channel(fixture_t *f,unsigned ch)
{ bm_at_dma_channel_state_t s; assert(bm_at_dma_channel_state(f->dma,ch,&s)==BM_STATUS_OK); return s; }
static void wr(fixture_t *f,unsigned port,unsigned value)
{
    bm_bus_transaction_t t={0};t.space=BM_ADDRESS_IO;t.operation=BM_BUS_WRITE;t.address=port;t.size=1;t.value=value;
    assert(bm_at_dma_io(f->dma,&t)==BM_STATUS_OK);
}
static unsigned peek(fixture_t *f,unsigned port,int debug)
{
    bm_bus_transaction_t t={0};t.space=BM_ADDRESS_IO;t.operation=BM_BUS_READ;t.address=port;t.size=1;
    t.attributes=debug?BM_BUS_TRANSACTION_DEBUG:0;assert(bm_at_dma_io(f->dma,&t)==BM_STATUS_OK);return (unsigned)t.value;
}
static void word(fixture_t *f,unsigned port,unsigned value)
{ wr(f,12,0);wr(f,port,value&255U);wr(f,port,value>>8); }
static void forbidden_pin(void *p,int level) { (void)p;(void)level;assert(!"mem2mem emitted device DACK/source TC"); }
static bm_status_t forbidden_read(void *p,uint16_t *value)
{ (void)p;(void)value;assert(!"mem2mem used device read");return BM_STATUS_DEVICE_ERROR; }
static bm_status_t forbidden_write(void *p,uint16_t value)
{ (void)p;(void)value;assert(!"mem2mem used device write");return BM_STATUS_DEVICE_ERROR; }
static void hrq(void *p,int level)
{
    fixture_t *f=p;assert(f->hrq!=level);f->hrq=level;
    if(f->bus)assert(bm_at_bus_request(f->bus,BM_AT_MASTER_DMA8,level)==BM_STATUS_OK);
}
static void tc(void *p,int level)
{
    fixture_t *f=p;assert(f->tc!=level && state(f).mem2mem_active && !state(f).dack);
    assert(channel(f,0).terminal_count && channel(f,1).terminal_count);
    f->tc=level;if(level)++f->pulses;
}
static void hold(void *p,int level) { ((fixture_t*)p)->hold=level; }
static bm_status_t memory(void *p,bm_at_transfer_t *t)
{
    fixture_t *f=p;unsigned phase=f->calls++&1U;
    assert(state(f).mem2mem_active && !state(f).dack && !state(f).cascade_active);
    assert(t->master==BM_AT_MASTER_DMA8 && t->bus.space==BM_ADDRESS_MEMORY);
    assert(t->bus.operation==(phase?BM_BUS_WRITE:BM_BUS_READ));
    assert(t->bus.address==((uint32_t)channel(f,1).page<<16 | channel(f,phase).current_address));
    assert(t->bus.size==1 && t->bus.alignment==1 && !t->bus.attributes && !t->bus.wait_states);
    assert(t->requester_clock.cycles_per_second_numerator==4000000 && t->requester_clock.cycles_per_second_denominator==3);
    assert(bm_at_dma_set_bus_grant(f->dma,0)==BM_STATUS_INVALID_STATE);
    uint64_t clocks=99;assert(bm_at_dma_service(f->dma,&clocks)==BM_STATUS_INVALID_STATE && !clocks);
    if(phase)assert(peek(f,13,1)==t->bus.value); /* TEMP published after successful read */
    int level=f->eop_pattern==1 || (f->eop_pattern==2 && !phase) || (f->eop_pattern==3 && phase);
    assert(bm_at_dma_set_eop(f->dma,0,level)==BM_STATUS_OK);
    if(f->fail_at==f->calls && !f->after)return f->error;
    if(!phase){++f->reads;t->bus.value=f->ram[(uint16_t)t->bus.address];}
    else {++f->writes;f->ram[(uint16_t)t->bus.address]=(uint8_t)t->bus.value;}
    t->bus.wait_states=f->waits[phase];
    if(f->bus && f->withdraw_at==f->calls)
        assert(bm_at_bus_request(f->bus,BM_AT_MASTER_DMA8,0)==BM_STATUS_OK);
    return f->fail_at==f->calls?f->error:BM_STATUS_OK;
}
static bm_status_t route(void *p,bm_at_transfer_t *t)
{ fixture_t *f=p;return f->bus?bm_at_bus_access(f->bus,t):memory(f,t); }
static void create(fixture_t *f,int enabled)
{
    memset(f,0,sizeof(*f));f->error=BM_STATUS_DEVICE_ERROR;
    bm_host_services_t host=bm_null_host_services();bm_at_dma_config_t c={0};
    c.memory=route;c.memory_context=f;c.bus_request=hrq;c.bus_context=f;c.clock=(bm_clock_rate_t){4000000,3};
    c.mem2mem_profile=enabled?BM_AT_DMA_MEM2MEM_IBM_MATCHED_COUNTS:BM_AT_DMA_MEM2MEM_DISABLED;
    for(unsigned i=0;i<8;++i)c.endpoints[i]=(bm_at_dma_endpoint_t){f,forbidden_read,forbidden_write,forbidden_pin,i==1?tc:forbidden_pin};
    assert(bm_at_dma_create(&host,&c,&f->dma)==BM_STATUS_OK);
}
static void program(fixture_t *f,unsigned source,unsigned destination,unsigned count,unsigned mode,unsigned command)
{
    wr(f,0xd6,0xc0);wr(f,0xd4,0); /* upper internal cascade */
    word(f,0,source);word(f,2,destination);word(f,1,count);word(f,3,count);
    wr(f,11,0x88U|(mode&0x30U));wr(f,11,0x85U|((mode>>2)&0x20U)|(mode&0x10U));
    wr(f,0x87,0x12);wr(f,0x83,0x92);wr(f,8,command|1U);wr(f,9,4);
}
static void grant(fixture_t *f)
{ assert(f->hrq && state(f).pending_channel==0);assert(bm_at_dma_set_bus_grant(f->dma,1)==BM_STATUS_OK); }
static void release(fixture_t *f)
{ assert(!f->hrq);assert(bm_at_dma_set_bus_grant(f->dma,0)==BM_STATUS_OK); }
static void destroy(fixture_t *f)
{
    bm_at_dma_destroy(f->dma);assert(!f->hrq && !f->tc);
    if(f->bus)bm_at_bus_destroy(f->bus);
}
static void succeeded(fixture_t *f)
{
    uint64_t clocks=99;assert(bm_at_dma_service(f->dma,&clocks)==BM_STATUS_OK && clocks==8ULL+f->waits[0]+f->waits[1]);
    bm_at_dma_state_t s=state(f);assert(s.last_pair_read_complete && s.last_pair_write_complete && s.last_pair_completed_clocks==clocks && !s.dack);
}
static unsigned step_address(unsigned address,unsigned decrement)
{ return (address+(decrement?65535U:1U))&65535U; }

static void registers_and_addresses(void)
{
    fixture_t f;unsigned cases=0;
    for(unsigned address=0;address<65536;++address) {
        unsigned mode=((address&1U)?32U:0)|((address&2U)?128U:0),hold_source=(address>>2)&1U;
        create(&f,1);program(&f,address,address^65535U,0,mode,hold_source?2:0);
        wr(&f,0x83,address&255U);f.ram[address]=(uint8_t)(address>>8);grant(&f);succeeded(&f);
        assert(f.ram[address^65535U]==(uint8_t)(address>>8));
        assert(channel(&f,0).current_address==(hold_source?address:step_address(address,mode&32U)));
        assert(channel(&f,1).current_address==step_address(address^65535U,mode&128U));
        assert(channel(&f,0).current_count==65535 && channel(&f,1).current_count==65535);
        assert(peek(&f,8,1)==3 && f.pulses==1 && state(&f).mask[0]==15 && !f.hrq);
        release(&f);assert(peek(&f,13,0)==(address>>8));assert(peek(&f,8,0)==3 && peek(&f,8,0)==0);
        destroy(&f);++cases;
    }
    for(unsigned count=0;count<65536;++count)for(unsigned autoinit=0;autoinit<2;++autoinit) {
        create(&f,1);program(&f,65535,0,count,autoinit?16:0,8);wr(&f,15,0);
        f.ram[65535]=0xa5;grant(&f);succeeded(&f);
        for(unsigned ch=0;ch<2;++ch) {
            bm_at_dma_channel_state_t s=channel(&f,ch);
            assert(s.current_count==(!count && autoinit?count:(count-1U)&65535U));
            assert(s.base_count==count && s.current_address==(!count && autoinit?(ch?0:65535):(ch?1:0)));
            assert(s.masked==(!count && !autoinit) && s.terminal_count==(!count));
        }
        assert(f.pulses==(!count) && state(&f).software_request[0]==(count?1:0));
        assert(state(&f).mem2mem_active==(count!=0));destroy(&f);++cases;
    }
    printf("AT DMA matched mem2mem: %u address/count cases, every count/auto, offset/page, directions/source-hold, TEMP/status/mask, default page87 ignored\n",cases);
}

static void blocks(void)
{
    fixture_t f;const unsigned counts[]={0,1,255,65535};unsigned cases=0;
    for(unsigned n=0;n<4;++n)for(unsigned bits=0;bits<16;++bits) {
        unsigned mode=(bits&1U?32U:0)|(bits&2U?128U:0)|(bits&4U?16U:0),hold_source=bits&8U;
        create(&f,1);program(&f,0xfffe,0x0001,counts[n],mode,16U|(hold_source?2U:0U));
        uint8_t expected[65536];for(unsigned i=0;i<65536;++i)expected[i]=f.ram[i]=(uint8_t)(i^(i>>8));
        unsigned src=0xfffe,dst=1;grant(&f);uint64_t sum=0;
        for(unsigned i=0;i<=counts[n];++i) {
            uint8_t value=expected[src];expected[dst]=value;
            f.waits[0]=i&3U;f.waits[1]=i&7U;succeeded(&f);sum+=state(&f).last_pair_completed_clocks;
            if(!hold_source)src=step_address(src,mode&32U);
            dst=step_address(dst,mode&128U);
            assert(peek(&f,13,1)==value);
            if(i<counts[n])assert(state(&f).mem2mem_active && f.hrq && !f.pulses && !channel(&f,1).terminal_count);
        }
        assert(!memcmp(expected,f.ram,sizeof(expected)) && !f.hrq && f.pulses==1);
        uint64_t length=counts[n]+1U,r4=length%4,r8=length%8;
        assert(sum==8*length+(length/4)*6+r4*(r4-1)/2+(length/8)*28+r8*(r8-1)/2);
        assert(state(&f).priority_first[0]==2 && state(&f).priority_first[1]==0);
        /* Upper fixed priority remains0; lower last transfer was channel1. */
        release(&f);
        if(mode&16U) {
            assert(channel(&f,0).current_address==0xfffe && channel(&f,1).current_address==1);
            assert(channel(&f,0).current_count==counts[n]);
            wr(&f,9,4);grant(&f);succeeded(&f); /* explicit software rearm, not stale grant */
        } else assert(channel(&f,0).current_address==src && channel(&f,1).current_address==dst);
        destroy(&f);++cases;
    }
    printf("AT DMA matched mem2mem: %u complete blocks including sixteen 65536-byte alias/wrap/fill copies, both directions and autorearm\n",cases);
}

static void eop_and_controls(void)
{
    fixture_t f;unsigned cases=0;
    for(unsigned count=0;count<2;++count)for(unsigned auto_init=0;auto_init<2;++auto_init)
    for(unsigned pattern=0;pattern<4;++pattern) {
        create(&f,1);program(&f,0x100,0x200,count,auto_init?16:0,0);f.ram[0x100]=0x56;
        f.eop_pattern=pattern;f.waits[0]=f.waits[1]=UINT32_MAX;grant(&f);uint64_t cycles=99;
        bm_status_t result=bm_at_dma_service(f.dma,&cycles);
        if(pattern>=2) {
            assert(result==BM_STATUS_UNSUPPORTED && !cycles && state(&f).stopped);
            assert(channel(&f,0).current_count==count && channel(&f,1).current_address==0x200 && !f.pulses && !channel(&f,0).terminal_count);
        } else {
            assert(result==BM_STATUS_OK && cycles==8ULL+2ULL*UINT32_MAX);
            int ended=!count || pattern==1;assert((!f.hrq)==ended && f.pulses==(!count));
            for(unsigned ch=0;ch<2;++ch)assert(channel(&f,ch).terminal_count==ended);
            assert(state(&f).last_pair_completed_clocks==cycles);
        }
        assert(state(&f).last_pair_read_complete && state(&f).last_pair_write_complete && f.ram[0x200]==0x56 && peek(&f,13,1)==0x56);
        destroy(&f);++cases;
    }
    for(unsigned command=1;command<256;command+=2) {
        create(&f,1);program(&f,0,1,0,0,command);uint64_t cycles=99;
        if(command&4U)assert(!f.hrq && bm_at_dma_service(f.dma,&cycles)==BM_STATUS_IDLE && !cycles);
        else {
            grant(&f);bm_status_t result=bm_at_dma_service(f.dma,&cycles);
            if(command&32U)assert(result==BM_STATUS_UNSUPPORTED && !cycles && !f.calls);
            else assert(result==BM_STATUS_OK && cycles==8 && f.calls==2);
        }
        destroy(&f);++cases;
    }
    printf("AT DMA matched mem2mem: %u EOP/command cases, coherent early EOP, asymmetric evidence stops, compressed don't-care, extended gate, max waits\n",cases);
}

static void failures_and_gates(void)
{
    const bm_status_t errors[]={BM_STATUS_DEVICE_ERROR,BM_STATUS_UNMAPPED,BM_STATUS_READ_ONLY,
        BM_STATUS_CAPACITY_EXCEEDED,BM_STATUS_UNSUPPORTED,BM_STATUS_OUT_OF_MEMORY,BM_STATUS_IDLE};
    fixture_t f;unsigned cases=0;
    for(unsigned phase=0;phase<2;++phase)for(unsigned after=0;after<2;++after)
    for(unsigned error=0;error<7;++error)for(unsigned eop=0;eop<4;++eop) {
        create(&f,1);program(&f,0x100,0x200,2,0,0);f.ram[0x100]=0x41;f.ram[0x101]=0x52;
        grant(&f);succeeded(&f);f.fail_at=3+phase;f.after=after;f.error=errors[error];f.eop_pattern=eop;
        f.waits[0]=f.waits[1]=UINT32_MAX;uint64_t cycles=99;
        assert(bm_at_dma_service(f.dma,&cycles)==(errors[error]==BM_STATUS_IDLE?BM_STATUS_INVALID_STATE:errors[error]) && !cycles);
        assert(channel(&f,0).current_address==0x101 && channel(&f,1).current_address==0x201);
        assert(channel(&f,0).current_count==1 && channel(&f,1).current_count==1 && !f.pulses && !channel(&f,0).terminal_count);
        assert(f.ram[0x200]==0x41 && f.ram[0x201]==(phase && after?0x52:0));
        assert(peek(&f,13,1)==(phase?0x52:0x41) && state(&f).stopped && !f.hrq && !state(&f).mem2mem_active);
        assert(state(&f).last_pair_read_complete==(int)phase && !state(&f).last_pair_write_complete);
        assert(state(&f).last_pair_completed_clocks==(phase?4ULL+UINT32_MAX:0));
        unsigned calls=f.calls;assert(bm_at_dma_service(f.dma,&cycles)==BM_STATUS_INVALID_STATE && f.calls==calls);
        release(&f);wr(&f,13,0);assert(bm_at_dma_service(f.dma,&cycles)==BM_STATUS_INVALID_STATE && f.calls==calls);
        bm_at_dma_reset(f.dma);assert(!state(&f).stopped && !state(&f).last_pair_completed_clocks);
        destroy(&f);++cases;
    }
    for(unsigned why=0;why<10;++why) {
        create(&f,why!=0);program(&f,0,1,0,0,0);
        if(why==1)word(&f,3,1);
        if(why==2)wr(&f,11,0x95);
        if(why==3)wr(&f,11,0x48);
        if(why==4)wr(&f,11,0x89);
        if(why==5)wr(&f,9,5);
        if(why==6)wr(&f,0xd6,0x80);
        if(why==7)wr(&f,0xd0,1);
        if(why==8){wr(&f,9,0);wr(&f,10,0);assert(bm_at_dma_set_dreq(f.dma,0,1)==BM_STATUS_OK);}
        if(why==9)wr(&f,8,33);
        grant(&f);uint64_t cycles=99;assert(bm_at_dma_service(f.dma,&cycles)==BM_STATUS_UNSUPPORTED && !cycles && !f.calls && state(&f).stopped);
        destroy(&f);
    }
    printf("AT DMA matched mem2mem: %u every-phase prefix failures, reset-only recovery; ten pre-effect profile/configuration gates\n",cases);
}

static void lifecycle_and_bus(void)
{
    fixture_t f;create(&f,1);program(&f,0,1,1,0,16);wr(&f,0xd0,16);wr(&f,10,2);
    assert(bm_at_dma_set_eop(f.dma,1,1)==BM_STATUS_OK);grant(&f);succeeded(&f);
    assert(f.hrq && bm_at_dma_set_bus_grant(f.dma,0)==BM_STATUS_INVALID_STATE);
    /* No programmed I/O while granted; external requests may change. */
    assert(bm_at_dma_set_dreq(f.dma,2,1)==BM_STATUS_OK);assert(state(&f).selected_channel==0);
    succeeded(&f);assert(state(&f).priority_first[0]==2 && state(&f).priority_first[1]==1);
    assert(bm_at_dma_set_dreq(f.dma,0,1)==BM_STATUS_OK && !f.hrq); /* stale HLDA cannot regrant */
    release(&f);assert(state(&f).pending_channel==2);destroy(&f);
    create(&f,1);program(&f,0,1,1,0,0);grant(&f);succeeded(&f);bm_at_dma_reset(f.dma);
    assert(!state(&f).mem2mem_active && !state(&f).last_pair_read_complete && !f.hrq && state(&f).bus_grant);release(&f);destroy(&f);
    for(unsigned fail=0;fail<4;++fail) {
        create(&f,1);bm_host_services_t host=bm_null_host_services();bm_at_bus_config_t bc={0};
        bc.cpu_clock=(bm_clock_rate_t){12000000,1};bc.isa_clock=(bm_clock_rate_t){8000000,1};
        bc.memory=memory;bc.io=memory;bc.decode_context=&f;bc.hold=hold;bc.hold_context=&f;
        assert(bm_at_bus_create(&host,&bc,&f.bus)==BM_STATUS_OK);
        assert(bm_at_bus_set_lock(f.bus,1)==BM_STATUS_OK);program(&f,0xffff,0,0,0,0);
        assert(f.hrq && !f.hold && bm_at_bus_hold_ack(f.bus,1)==BM_STATUS_INVALID_STATE);
        uint64_t cycles=99;assert(bm_at_dma_service(f.dma,&cycles)==BM_STATUS_IDLE && !f.calls);
        assert(bm_at_bus_set_lock(f.bus,0)==BM_STATUS_OK && f.hold);
        assert(bm_at_bus_hold_ack(f.bus,1)==BM_STATUS_OK);grant(&f);
        for(unsigned master=0;master<4;++master)if(master!=BM_AT_MASTER_DMA8) {
            bm_at_transfer_t blocked={0};blocked.master=(bm_at_master_t)master;
            blocked.requester_clock=(bm_clock_rate_t){4000000,3};blocked.bus.space=BM_ADDRESS_MEMORY;
            blocked.bus.operation=BM_BUS_READ;blocked.bus.size=blocked.bus.alignment=1;
            assert(bm_at_bus_access(f.bus,&blocked)==BM_STATUS_IDLE && !f.calls);
        }
        f.ram[65535]=0xa5;f.waits[0]=17;f.waits[1]=UINT32_MAX;f.fail_at=fail<3?fail:0;f.after=1;
        if(fail==3)f.withdraw_at=1; /* bad coordinator withdraws ownership after READ */
        bm_status_t result=bm_at_dma_service(f.dma,&cycles);
        if(fail==3)assert(result==BM_STATUS_INVALID_STATE && !cycles && state(&f).stopped &&
            state(&f).last_pair_read_complete && !state(&f).last_pair_write_complete &&
            state(&f).last_pair_completed_clocks==21 && peek(&f,13,1)==0xa5 && !f.writes);
        else if(fail)assert(result==BM_STATUS_DEVICE_ERROR && !cycles && state(&f).stopped);
        else assert(result==BM_STATUS_OK && cycles==8ULL+17+UINT32_MAX && f.ram[0]==0xa5);
        assert(!f.hrq && !f.hold);assert(bm_at_bus_hold_ack(f.bus,0)==BM_STATUS_OK);release(&f);destroy(&f);
    }
    puts("AT DMA matched mem2mem: non-preemption, two-level rotation, stale HLDA, reset/active destruction and real AT LOCK/grant/waits/failures");
}

static void profile_boundaries(void)
{
    fixture_t f={0};bm_host_services_t host=bm_null_host_services();bm_at_dma_config_t c={0};
    c.memory=route;c.memory_context=&f;c.bus_request=hrq;c.bus_context=&f;c.clock=(bm_clock_rate_t){4000000,3};
    c.mem2mem_profile=BM_AT_DMA_MEM2MEM_IBM_MATCHED_COUNTS;
    assert(bm_at_dma_create(&host,&c,&f.dma)==BM_STATUS_OK);
    /* Opt-in must not change ordinary verify. One single-mode transfer aligns
     * current counts while deliberately leaving unequal base counts. */
    wr(&f,0xd6,0xc0);wr(&f,0xd4,0);word(&f,1,3);word(&f,3,2);wr(&f,11,0x40);wr(&f,10,0);
    assert(bm_at_dma_set_dreq(f.dma,0,1)==BM_STATUS_OK);grant(&f);uint64_t cycles=99;
    assert(bm_at_dma_service(f.dma,&cycles)==BM_STATUS_OK && cycles==4 && !f.calls);
    assert(channel(&f,0).current_count==2 && channel(&f,0).base_count==3);
    assert(!state(&f).mem2mem_active && !state(&f).last_pair_read_complete);
    assert(bm_at_dma_set_dreq(f.dma,0,0)==BM_STATUS_OK);release(&f);
    wr(&f,11,0x98);wr(&f,11,0x95);wr(&f,8,1);wr(&f,9,4);grant(&f);
    assert(bm_at_dma_service(f.dma,&cycles)==BM_STATUS_UNSUPPORTED && !cycles && !f.calls);
    destroy(&f);
    create(&f,1);wr(&f,0xd6,0x88);wr(&f,0xd0,1);wr(&f,0xd2,4);
    assert(f.hrq && state(&f).pending_channel==4);assert(bm_at_dma_set_bus_grant(f.dma,1)==BM_STATUS_OK);
    assert(bm_at_dma_service(f.dma,&cycles)==BM_STATUS_UNSUPPORTED && !cycles && !f.calls);destroy(&f);
    puts("AT DMA matched mem2mem: ordinary verify unchanged under opt-in; unequal autoinit base counts and upper mem2mem explicitly gated");
}
int main(void) {registers_and_addresses();blocks();eop_and_controls();failures_and_gates();lifecycle_and_bus();profile_boundaries();return 0;}
