/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Authored Intel8237/IBM AT functional oracles; no firmware or hardware vectors.
 */
#include <blumach/components/at_dma.h>
#include <blumach/platforms/null_host.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct fixture fixture_t;
static unsigned fixture_command; /* rerun unchanged functional oracles with extended write */
typedef struct endpoint { fixture_t *f; unsigned ch; } endpoint_t;
struct fixture {
    bm_at_dma_t *dma;
    bm_at_bus_t *bus;
    endpoint_t ep[8];
    int hrq, hold, dack[8], tc[8], drop_at, fail_at, after, split;
    int eop_at, eop_unit, eop_pulse, profile_waits;
    bm_status_t failure;
    unsigned calls, device_reads, device_writes, memory_reads, memory_writes;
    unsigned high[8], low[8], pulses[8], chosen, events;
    char trace[64];
    uint16_t device_value, delivered;
    uint8_t written[2];
    uint32_t waits;
    bm_at_transfer_t last;
    bm_at_dma_timing_t timing;
};
static bm_at_dma_state_t state(fixture_t *f)
{ bm_at_dma_state_t s; assert(bm_at_dma_state(f->dma,&s)==BM_STATUS_OK); return s; }
static bm_at_dma_channel_state_t channel(fixture_t *f,unsigned ch)
{ bm_at_dma_channel_state_t s; assert(bm_at_dma_channel_state(f->dma,ch,&s)==BM_STATUS_OK); return s; }
static void same_channel(const bm_at_dma_channel_state_t *a,const bm_at_dma_channel_state_t *b)
{
    /* Struct assignment/return need not preserve padding bytes, notably under
     * Release optimization. Compare every observable field, not the padding. */
    assert(a->base_address==b->base_address && a->current_address==b->current_address);
    assert(a->base_count==b->base_count && a->current_count==b->current_count);
    assert(a->page==b->page && a->mode==b->mode && a->masked==b->masked);
    assert(a->requested==b->requested && a->terminal_count==b->terminal_count);
}
static void same_state(const bm_at_dma_state_t *a,const bm_at_dma_state_t *b)
{
    for(unsigned u=0;u<2;++u) {
        assert(a->command[u]==b->command[u] && a->mask[u]==b->mask[u]);
        assert(a->software_request[u]==b->software_request[u] && a->byte_high[u]==b->byte_high[u]);
        assert(a->priority_first[u]==b->priority_first[u] && a->eop[u]==b->eop[u]);
    }
    assert(a->dreq==b->dreq && a->bus_request==b->bus_request && a->bus_grant==b->bus_grant);
    assert(a->stopped==b->stopped && a->pending_channel==b->pending_channel && a->selected_channel==b->selected_channel);
    assert(a->dack==b->dack && a->release_wait==b->release_wait);
    assert(a->cascade_active==b->cascade_active);
    assert(a->mem2mem_active==b->mem2mem_active);
    assert(a->last_pair_read_complete==b->last_pair_read_complete && a->last_pair_write_complete==b->last_pair_write_complete);
    assert(a->last_pair_completed_clocks==b->last_pair_completed_clocks);
}
static void event(fixture_t *f,char e)
{ if(f->events<sizeof(f->trace))f->trace[f->events]=e; ++f->events; }
static void drive_eop(fixture_t *f,int at)
{
    if(f->eop_at!=at)return;
    assert(bm_at_dma_set_eop(f->dma,(unsigned)f->eop_unit,1)==BM_STATUS_OK);
    if(f->eop_pulse)assert(bm_at_dma_set_eop(f->dma,(unsigned)f->eop_unit,0)==BM_STATUS_OK);
}
static void hrq(void *p,int level)
{ fixture_t *f=p; assert(f->hrq!=level); f->hrq=level; event(f,level?'H':'h'); }
static void hold(void *p,int level) { fixture_t *f=p; f->hold=level; }
static void dack(void *p,int level)
{
    endpoint_t *e=p; fixture_t *f=e->f; assert(e->ch!=4 && f->dack[e->ch]!=level);
    f->dack[e->ch]=level;
    if(level) { ++f->high[e->ch]; f->chosen=e->ch; } else ++f->low[e->ch];
    event(f,level?'D':'d');
    drive_eop(f,level?1:6);
    if(level && f->drop_at==1)assert(bm_at_dma_set_dreq(f->dma,e->ch,0)==BM_STATUS_OK);
}
static void tc(void *p,int level)
{
    endpoint_t *e=p; fixture_t *f=e->f; assert(e->ch!=4 && f->tc[e->ch]!=level && f->dack[e->ch]);
    f->tc[e->ch]=level; if(level) {++f->pulses[e->ch];assert(channel(f,e->ch).terminal_count);}
    event(f,level?'T':'t');
    if(level)drive_eop(f,7);
}
static int begin(fixture_t *f,char kind)
{ ++f->calls; event(f,kind);drive_eop(f,2*(int)f->calls);return f->fail_at==(int)f->calls && !f->after; }
static bm_status_t end(fixture_t *f)
{
    drive_eop(f,2*(int)f->calls+1);
    if(f->drop_at==2)assert(bm_at_dma_set_dreq(f->dma,f->chosen,0)==BM_STATUS_OK);
    return f->fail_at==(int)f->calls ? f->failure : BM_STATUS_OK;
}
static bm_status_t device_read(void *p,uint16_t *value)
{
    endpoint_t *e=p; fixture_t *f=e->f; assert(f->dack[e->ch]);
    if(begin(f,'R'))return f->failure;
    ++f->device_reads; *value=f->device_value; return end(f);
}
static bm_status_t device_write(void *p,uint16_t value)
{
    endpoint_t *e=p; fixture_t *f=e->f; assert(f->dack[e->ch]);
    if(begin(f,'W'))return f->failure;
    ++f->device_writes; f->delivered=value; return end(f);
}
static uint8_t memory_byte(uint32_t address)
{ return (uint8_t)((address*37U)^(address>>8)^0xa5U); }
static bm_status_t memory(void *p,bm_at_transfer_t *t)
{
    fixture_t *f=p; unsigned ch=f->chosen; assert(f->dack[ch]);
    assert(t->master==(ch<4?BM_AT_MASTER_DMA8:BM_AT_MASTER_DMA16));
    assert(t->requester_clock.cycles_per_second_numerator==4000000 && t->requester_clock.cycles_per_second_denominator==3);
    assert(t->bus.space==BM_ADDRESS_MEMORY && !t->bus.attributes && !t->bus.wait_states);
    assert(t->bus.size==(ch<4?1U:2U) && t->bus.alignment==t->bus.size && t->bus.endianness==BM_ENDIAN_LITTLE);
    assert(t->bus.address<=0xffffff && !(t->bus.address%(ch<4?1U:2U)));
    if(f->profile_waits) {
        bm_at_dma_state_t before=state(f);
        assert(bm_at_dma_channel_timing(f->dma,ch,&f->timing)==BM_STATUS_OK);
        bm_at_dma_state_t after=state(f);same_state(&before,&after);
        /* Authored adapter requires a nominal three-clock memory-read or
         * two-clock memory-write window. This is synthetic board policy,
         * not a claim about Intel AC margins or the PCS286 memory controller. */
        f->waits=t->bus.operation==BM_BUS_READ?3-f->timing.read_pulse_clocks:2-f->timing.write_pulse_clocks;
    }
    f->last=*t;
    if(f->split) {
        assert(t->bus.size==2);uint16_t value=0;
        for(unsigned byte=0;byte<2;++byte) {
            if(begin(f,t->bus.operation==BM_BUS_READ?'r':'w'))return f->failure;
            if(t->bus.operation==BM_BUS_READ) {
                ++f->memory_reads;value|=(uint16_t)memory_byte((uint32_t)t->bus.address+byte)<<(byte*8);
            } else {++f->memory_writes;f->written[byte]=(uint8_t)(t->bus.value>>(byte*8));}
            bm_status_t status=end(f);if(status!=BM_STATUS_OK)return status;
        }
        if(t->bus.operation==BM_BUS_READ)t->bus.value=value;
        t->bus.wait_states=f->waits;return BM_STATUS_OK;
    }
    if(begin(f,t->bus.operation==BM_BUS_READ?'r':'w'))return f->failure;
    if(t->bus.operation==BM_BUS_READ) {
        ++f->memory_reads; t->bus.value=memory_byte((uint32_t)t->bus.address);
        if(t->bus.size==2)t->bus.value|=(uint16_t)memory_byte((uint32_t)t->bus.address+1)<<8;
    } else {assert(t->bus.operation==BM_BUS_WRITE); ++f->memory_writes;}
    t->bus.wait_states=f->waits; return end(f);
}
static bm_status_t access(void *p,bm_at_transfer_t *t)
{ fixture_t *f=p; return f->bus?bm_at_bus_access(f->bus,t):memory(p,t); }
static void create(fixture_t *f)
{
    memset(f,0,sizeof(*f)); f->failure=BM_STATUS_DEVICE_ERROR; f->device_value=0xfedc;
    bm_host_services_t h=bm_null_host_services(); bm_at_dma_config_t c={0};
    c.clock=(bm_clock_rate_t){4000000,3}; c.memory=access; c.memory_context=f; c.bus_request=hrq; c.bus_context=f;
    for(unsigned ch=0;ch<8;++ch) {f->ep[ch]=(endpoint_t){f,ch};c.endpoints[ch]=(bm_at_dma_endpoint_t){&f->ep[ch],device_read,device_write,dack,tc};}
    assert(bm_at_dma_create(&h,&c,&f->dma)==BM_STATUS_OK);
    assert(state(f).selected_channel==-1 && state(f).pending_channel==-1);
}
static unsigned port(unsigned u,unsigned r) { return u?0xc0+2*r:r; }
static void write_port(fixture_t *f,unsigned p,unsigned value)
{
    bm_bus_transaction_t t={0};t.space=BM_ADDRESS_IO;t.operation=BM_BUS_WRITE;t.address=p;t.size=1;t.value=value;
    assert(bm_at_dma_io(f->dma,&t)==BM_STATUS_OK);
}
static unsigned read_port(fixture_t *f,unsigned p,int debug)
{
    bm_bus_transaction_t t={0};t.space=BM_ADDRESS_IO;t.operation=BM_BUS_READ;t.address=p;t.size=1;t.attributes=debug?BM_BUS_TRANSACTION_DEBUG:0;
    assert(bm_at_dma_io(f->dma,&t)==BM_STATUS_OK); return (unsigned)t.value;
}
static void program(fixture_t *f,unsigned ch,unsigned mode,unsigned address,unsigned count,unsigned page)
{
    static const unsigned pages[]={0x87,0x83,0x81,0x82,0,0x8b,0x89,0x8a};
    unsigned u=ch/4,local=ch%4;
    write_port(f,port(u,12),0);
    write_port(f,port(u,2*local),address&255); write_port(f,port(u,2*local),address>>8);
    write_port(f,port(u,2*local+1),count&255); write_port(f,port(u,2*local+1),count>>8);
    write_port(f,port(u,11),mode|local);write_port(f,port(u,10),local);
    if(ch!=4)write_port(f,pages[ch],page);
    if(ch<4) {write_port(f,0xd6,0xc0);write_port(f,0xd4,0);}
    if(fixture_command)write_port(f,port(u,8),fixture_command);
}
static void request(fixture_t *f,unsigned ch)
{ assert(bm_at_dma_set_dreq(f->dma,ch,1)==BM_STATUS_OK && f->hrq); }
static void grant(fixture_t *f,unsigned ch)
{
    assert(state(f).pending_channel==(int)ch);
    assert(bm_at_dma_set_bus_grant(f->dma,1)==BM_STATUS_OK && state(f).selected_channel==(int)ch);
}
static uint64_t step(fixture_t *f)
{ uint64_t clocks=99; assert(bm_at_dma_service(f->dma,&clocks)==BM_STATUS_OK); return clocks; }
static void release(fixture_t *f)
{ assert(!f->hrq && state(f).release_wait);assert(bm_at_dma_set_bus_grant(f->dma,0)==BM_STATUS_OK); }
static void destroy(fixture_t *f)
{
    bm_at_dma_destroy(f->dma); assert(!f->hrq);
    for(unsigned ch=0;ch<8;++ch)assert(!f->dack[ch] && !f->tc[ch]);
    bm_at_bus_destroy(f->bus);
}

static void word_and_page_matrix(void)
{
    fixture_t f;create(&f);unsigned cases=0;
    /* Every programmed address in each direction/width, including both page
     * low-bit values on word channels; independent linear/modulo oracle. */
    for(unsigned width=0;width<2;++width)for(unsigned dec=0;dec<2;++dec)
    for(unsigned a=0;a<65536;++a) {
        unsigned ch=width?5:2,page=(a&1)?0xff:0xfe,type=(a&2)?4:8;
        bm_at_dma_reset(f.dma);assert(bm_at_dma_set_dreq(f.dma,ch,0)==BM_STATUS_OK);
        program(&f,ch,0x40|(dec?0x20:0)|type,a,1,page);request(&f,ch);grant(&f,ch);
        f.waits=a==0xffff?UINT32_MAX:7;f.calls=f.events=0;
        assert(step(&f)==4ULL+f.waits);
        uint32_t expected=width?((page/2)*131072U+a*2U):(page*65536U+a);
        assert(f.last.bus.address==expected && f.last.bus.size==(width?2U:1U));
        if(type==4)assert(f.last.bus.value==(width?0xfedcU:0xdcU));
        else assert(f.delivered==(uint16_t)(memory_byte(expected)|(width?(uint16_t)memory_byte(expected+1)<<8:0)));
        bm_at_dma_channel_state_t s=channel(&f,ch);
        assert(s.current_address==(uint16_t)(dec?(a+65535U):(a+1U)) && !s.current_count && s.base_address==a && s.base_count==1 && s.page==page);
        assert(!s.terminal_count && !s.masked && f.calls==2);
        assert(!memcmp(f.trace,type==4?"DRwdh":"DrWdh",5));
        uint64_t clocks=99;assert(bm_at_dma_service(f.dma,&clocks)==BM_STATUS_IDLE && !clocks && f.calls==2);
        release(&f);assert(f.hrq);++cases;
    }
    /* Every latch value on every usable channel, no carry into page. */
    for(unsigned ch=0;ch<8;++ch)if(ch!=4)for(unsigned page=0;page<256;++page) {
        bm_at_dma_reset(f.dma);
        for(unsigned i=0;i<8;++i)if(i!=4)assert(bm_at_dma_set_dreq(f.dma,i,0)==BM_STATUS_OK);
        program(&f,ch,0x44,0xffff,0,page);request(&f,ch);grant(&f,ch);f.waits=0;assert(step(&f)==4);
        assert(f.last.bus.address==(ch<4?page*65536U+65535U:(page/2)*131072U+131070U));
        assert(!channel(&f,ch).current_address && channel(&f,ch).page==page);release(&f);++cases;
    }
    destroy(&f);printf("AT DMA: %u address/direction/width/page cases, values, maximum waits and fresh-grant single release\n",cases);
}

static void counts_and_autoinit(void)
{
    fixture_t f;create(&f);unsigned cases=0;
    /* One verify transaction for every initial counter and autoinit choice,
     * across byte/word: endpoint callbacks must remain absent. */
    for(unsigned width=0;width<2;++width)for(unsigned aut=0;aut<2;++aut)
    for(unsigned count=0;count<65536;++count) {
        unsigned ch=width?7:0;bm_at_dma_reset(f.dma);
        for(unsigned i=0;i<8;++i)if(i!=4)assert(bm_at_dma_set_dreq(f.dma,i,0)==BM_STATUS_OK);
        program(&f,ch,0x40|(aut?0x10:0),0xffff,count,0x35);request(&f,ch);grant(&f,ch);
        unsigned pulses=f.pulses[ch];f.calls=0;assert(step(&f)==4 && !f.calls);
        bm_at_dma_channel_state_t s=channel(&f,ch);
        assert(s.current_count==(count?count-1:(aut?0:65535)) && s.current_address==(!count&&aut?65535:0));
        assert(s.masked==(!count&&!aut) && s.terminal_count==!count && f.pulses[ch]==pulses+!count);
        assert((read_port(&f,port(ch/4,8),1)&15)==(!count?1U<<(ch%4):0));
        release(&f);unsigned raw=read_port(&f,port(ch/4,8),0);
        assert((raw&15)==(!count?1U<<(ch%4):0) && !(read_port(&f,port(ch/4,8),0)&15));++cases;
    }
    /* Count FFFF means 65536 transfers, terminal only at the last. */
    bm_at_dma_reset(f.dma);assert(bm_at_dma_set_dreq(f.dma,7,0)==BM_STATUS_OK);
    program(&f,3,0x80,0xff80,65535,0xab);request(&f,3);grant(&f,3);
    uint64_t clocks=0;
    for(unsigned i=0;i<65536;++i) {
        uint16_t a=(uint16_t)(0xff80+i);uint64_t expected=3+(i==0 || !(a&255));
        uint64_t actual=step(&f);clocks+=actual;assert(actual==expected);
        assert(!channel(&f,3).terminal_count || i==65535);
        if(i!=65535)assert(f.hrq && f.dack[3]);
    }
    assert(clocks==3ULL*65536+257 && channel(&f,3).current_count==65535 && f.high[3]==1 && f.low[3]==1);
    release(&f);destroy(&f);printf("AT DMA: %u count/TC/autoinit cases plus complete 65536-transfer block\n",cases);
}

static void bursts_and_priority(void)
{
    for(unsigned ch=0;ch<8;++ch)if(ch!=4)for(unsigned kind=0;kind<3;++kind)
    for(unsigned drop=1;drop<=2;++drop)for(unsigned aut=0;aut<2;++aut) {
        fixture_t f;create(&f);program(&f,ch,(kind<<6)|4|(aut?16:0),0xff,2,0x23);
        request(&f,ch);grant(&f,ch);f.drop_at=(int)drop;
        assert(step(&f)==4);assert(channel(&f,ch).current_count==1);
        if(kind==2) {
            assert(f.hrq && f.dack[ch]); assert(step(&f)==4);assert(step(&f)==3);
            assert(f.pulses[ch]==1 && channel(&f,ch).current_count==(aut?2:65535));
        } else {
            assert(!f.hrq && !f.dack[ch] && !f.pulses[ch]);release(&f);f.drop_at=0;
            request(&f,ch);grant(&f,ch);assert(step(&f)==4);
            if(kind==1) {release(&f);grant(&f,ch);}
            assert(step(&f)==(kind==1?4:3));assert(f.pulses[ch]==1);
        }
        assert(!f.hrq);release(&f);destroy(&f);
    }
    /* Two levels of priority, service is not preempted by later requests. */
    fixture_t f;create(&f);
    for(unsigned ch=0;ch<8;++ch)if(ch!=4)program(&f,ch,0x40,0,9,0);
    write_port(&f,8,0x10|fixture_command);write_port(&f,0xd0,0x10|fixture_command);
    for(unsigned ch=0;ch<8;++ch)if(ch!=4)request(&f,ch);
    const unsigned order[]={0,5,6,7,1,5,6,7,2,5,6,7,3,5,6,7};
    for(unsigned i=0;i<sizeof(order)/sizeof(order[0]);++i) {grant(&f,order[i]);assert(step(&f)==4);release(&f);}
    destroy(&f);
    create(&f);program(&f,7,0x84,0xfe,3,0);program(&f,0,0x40,0,0,0);
    request(&f,7);grant(&f,7);assert(step(&f)==4);request(&f,0);
    assert(state(&f).pending_channel==7 && step(&f)==3 && step(&f)==4 && step(&f)==3);
    release(&f);grant(&f,0);assert(step(&f)==4);release(&f);destroy(&f);
    /* Software block requests bypass masks and clear only at terminal count. */
    for(unsigned ch=0;ch<8;++ch)if(ch!=4) {
        create(&f);program(&f,ch,0x80,0,1,0);write_port(&f,port(ch/4,10),(ch%4)|4);
        write_port(&f,port(ch/4,9),(ch%4)|4);grant(&f,ch);assert(step(&f)==4 && step(&f)==3);
        assert(!(state(&f).software_request[ch/4]&(1U<<(ch%4))));release(&f);assert(!f.hrq);destroy(&f);
    }
    puts("AT DMA: demand/single/block DREQ changes at DACK/data, resume, auto-init, two-level rotation and non-preemption, software block");
}

static void failures(void)
{
    unsigned cases=0;
    const bm_status_t errors[]={BM_STATUS_DEVICE_ERROR,BM_STATUS_UNMAPPED,BM_STATUS_READ_ONLY,BM_STATUS_CAPACITY_EXCEEDED,BM_STATUS_IDLE};
    for(unsigned ch=0;ch<8;++ch)if(ch!=4)for(unsigned type=4;type<=8;type+=4)
    for(unsigned endpoint=1;endpoint<=2;++endpoint)for(unsigned after=0;after<2;++after)
    for(unsigned error=0;error<sizeof(errors)/sizeof(errors[0]);++error) {
        fixture_t f;create(&f);program(&f,ch,0x80|type|0x10,0xff,1,0x45);
        request(&f,ch);grant(&f,ch);assert(step(&f)==4); /* completed prefix retained */
        bm_at_dma_channel_state_t before=channel(&f,ch);
        unsigned dr=f.device_reads,dw=f.device_writes,mr=f.memory_reads,mw=f.memory_writes;
        f.calls=f.events=0;f.fail_at=(int)endpoint;f.after=(int)after;f.failure=errors[error];f.drop_at=2;
        uint64_t cycles=77;bm_status_t expected=f.failure==BM_STATUS_IDLE?BM_STATUS_INVALID_STATE:f.failure;
        assert(bm_at_dma_service(f.dma,&cycles)==expected && !cycles && state(&f).stopped && !f.hrq && !f.dack[ch]);
        bm_at_dma_channel_state_t now=channel(&f,ch);
        assert(now.current_address==before.current_address && now.current_count==before.current_count && !now.terminal_count && !f.pulses[ch]);
        assert(f.calls==endpoint);
        unsigned first=endpoint==2||after,second=endpoint==2&&after;
        assert(f.device_reads==dr+(type==4?first:0) && f.memory_writes==mw+(type==4?second:0));
        assert(f.memory_reads==mr+(type==8?first:0) && f.device_writes==dw+(type==8?second:0));
        assert(bm_at_dma_service(f.dma,&cycles)==BM_STATUS_INVALID_STATE && f.calls==endpoint);
        bm_at_dma_timing_t timing;bm_at_dma_state_t stopped=state(&f);
        assert(bm_at_dma_channel_timing(f.dma,ch,&timing)==BM_STATUS_OK);
        assert(timing.transfer_clocks==3 && timing.read_pulse_clocks==2 && timing.write_pulse_clocks==(fixture_command?2U:1U));
        bm_at_dma_state_t inspected=state(&f);same_state(&stopped,&inspected);
        release(&f);write_port(&f,13,0);write_port(&f,0xda,0);
        assert(bm_at_dma_service(f.dma,&cycles)==BM_STATUS_INVALID_STATE && !f.hrq);
        bm_at_dma_reset(f.dma);assert(!state(&f).stopped);destroy(&f);++cases;
    }
    printf("AT DMA: %u before/after endpoint errors retain exact effects and completed prefix, no count/TC/replay; reset recovery\n",cases);
}

static void gates_and_bus(void)
{
    fixture_t f;
    for(unsigned bad=0;bad<8;++bad) {
        create(&f);program(&f,2,0x44,0,0,0);
        if(bad==0)write_port(&f,8,1); /* mem2mem still unsupported */
        if(bad==1)write_port(&f,8,0x29); /* compressed does not enable mem2mem */
        if(bad==2)write_port(&f,8,0x21); /* extended write does not enable mem2mem */
        if(bad==3)write_port(&f,0xb,0xc2);
        if(bad==4)write_port(&f,0xb,0x4e);
        if(bad==5)write_port(&f,9,6);
        if(bad==6)write_port(&f,0xd6,0x40);
        if(bad==7)write_port(&f,0xd2,4); /* software request on cascade */
        request(&f,2);grant(&f,2);uint64_t cycles=1;
        assert(bm_at_dma_service(f.dma,&cycles)==BM_STATUS_UNSUPPORTED && !cycles && !f.calls && !f.high[2]);destroy(&f);
    }
    /* Selection disappears before DACK: no phantom transfer. */
    create(&f);program(&f,2,0x44,0,0,0);request(&f,2);grant(&f,2);
    assert(bm_at_dma_set_dreq(f.dma,2,0)==BM_STATUS_OK);uint64_t cycles=9;
    assert(bm_at_dma_service(f.dma,&cycles)==BM_STATUS_IDLE && !cycles && !f.calls && !f.high[2]);release(&f);destroy(&f);
    /* Real AT ownership adapter. Board performs claim/HLDA sequencing at CPU
     * boundaries; DMA callback cannot manufacture a grant through LOCK. */
    for(unsigned width=0;width<2;++width) {
        create(&f);bm_host_services_t h=bm_null_host_services();
        bm_at_bus_config_t bc={0};bc.cpu_clock=(bm_clock_rate_t){12000000,1};bc.isa_clock=(bm_clock_rate_t){8000000,1};
        bc.memory=memory;bc.io=memory;bc.decode_context=&f;bc.hold=hold;bc.hold_context=&f;
        assert(bm_at_bus_create(&h,&bc,&f.bus)==BM_STATUS_OK);
        unsigned ch=width?6:1;bm_at_master_t master=width?BM_AT_MASTER_DMA16:BM_AT_MASTER_DMA8;
        program(&f,ch,0x44,0xffff,1,0xff);request(&f,ch);
        assert(bm_at_bus_set_lock(f.bus,1)==BM_STATUS_OK && bm_at_bus_request(f.bus,master,1)==BM_STATUS_OK && !f.hold);
        assert(bm_at_bus_hold_ack(f.bus,1)==BM_STATUS_INVALID_STATE);
        assert(bm_at_dma_service(f.dma,&cycles)==BM_STATUS_IDLE && !cycles && !f.calls);
        assert(bm_at_bus_set_lock(f.bus,0)==BM_STATUS_OK && f.hold && bm_at_bus_hold_ack(f.bus,1)==BM_STATUS_OK);
        grant(&f,ch);f.waits=11;assert(step(&f)==15 && f.calls==2);
        assert(bm_at_bus_request(f.bus,master,0)==BM_STATUS_OK && !f.hold);
        assert(bm_at_bus_request(f.bus,master,1)==BM_STATUS_INVALID_STATE); /* old HLDA */
        assert(bm_at_bus_hold_ack(f.bus,0)==BM_STATUS_OK);release(&f);
        assert(bm_at_bus_request(f.bus,master,1)==BM_STATUS_OK && bm_at_bus_hold_ack(f.bus,1)==BM_STATUS_OK);
        grant(&f,ch);assert(step(&f)==15 && f.pulses[ch]==1);
        assert(bm_at_bus_request(f.bus,master,0)==BM_STATUS_OK && bm_at_bus_hold_ack(f.bus,0)==BM_STATUS_OK);release(&f);destroy(&f);
    }
    puts("AT DMA: unsupported modes stop before effects; pre-DACK withdrawal; real AT LOCK/HOLD/HLDA byte/word transfers with waits");
}

static void partial_words_and_lifecycle(void)
{
    unsigned cases=0;
    for(unsigned ch=5;ch<=7;++ch)for(unsigned type=4;type<=8;type+=4)
    for(unsigned fail=1;fail<=3;++fail)for(unsigned after=0;after<2;++after) {
        fixture_t f;create(&f);f.split=1;
        program(&f,ch,0x80|type,0xffff,0,0xff);request(&f,ch);grant(&f,ch);
        f.fail_at=(int)fail;f.after=(int)after;
        uint64_t cycles=77;assert(bm_at_dma_service(f.dma,&cycles)==BM_STATUS_DEVICE_ERROR && !cycles);
        assert(channel(&f,ch).current_address==65535 && !channel(&f,ch).current_count && !f.pulses[ch]);
        unsigned completed=fail-1+after;
        if(type==4) {
            assert(f.device_reads==(completed>0) && f.memory_writes==(completed?completed-1:0));
            assert(f.written[0]==(completed>1?0xdc:0) && f.written[1]==(completed>2?0xfe:0));
        } else assert(f.memory_reads==(completed<2?completed:2) && f.device_writes==(completed==3));
        assert(f.calls==fail && state(&f).stopped && !f.hrq && !f.dack[ch]);destroy(&f);++cases;
    }
    fixture_t f,peer;create(&f);create(&peer);
    program(&f,2,0x84,0,2,0);request(&f,2);grant(&f,2);assert(step(&f)==4 && f.dack[2]);
    assert(bm_at_dma_set_bus_grant(f.dma,0)==BM_STATUS_INVALID_STATE && state(&f).bus_grant);
    bm_at_dma_reset(f.dma);assert(!f.hrq && !f.dack[2] && state(&f).bus_grant && channel(&f,2).current_address==1);
    assert(channel(&f,2).current_count==1 && state(&f).dreq==4 && !state(&peer).dreq);
    assert(bm_at_dma_set_bus_grant(f.dma,0)==BM_STATUS_OK);program(&f,2,0x84,0,2,0);grant(&f,2);assert(step(&f)==4);
    destroy(&f);assert(!state(&peer).stopped && !state(&peer).bus_request);destroy(&peer);
    /* Missing direction callbacks fail before even DACK; verify needs neither. */
    for(unsigned type=0;type<=8;type+=4) {
        memset(&f,0,sizeof(f));bm_host_services_t h=bm_null_host_services();bm_at_dma_config_t c={0};
        c.memory=memory;c.memory_context=&f;c.clock=(bm_clock_rate_t){4000000,3};c.bus_request=hrq;c.bus_context=&f;
        assert(bm_at_dma_create(&h,&c,&f.dma)==BM_STATUS_OK);program(&f,5,0x40|type,0,0,0);request(&f,5);grant(&f,5);
        uint64_t cycles=99;
        if(type)assert(bm_at_dma_service(f.dma,&cycles)==BM_STATUS_INVALID_STATE && !cycles && state(&f).stopped);
        else assert(step(&f)==4 && channel(&f,5).terminal_count);
        assert(!f.calls);destroy(&f);
    }
    printf("AT DMA: %u split-word endpoint failures, exact retained bytes, active reset/destroy and isolation, missing callbacks\n",cases);
}

static void eop_counts(void)
{
    fixture_t f;create(&f);unsigned cases=0;
    for(unsigned width=0;width<2;++width)for(unsigned aut=0;aut<2;++aut)
    for(unsigned count=0;count<65536;++count) {
        unsigned ch=width?5:0;
        bm_at_dma_reset(f.dma);assert(bm_at_dma_set_bus_grant(f.dma,0)==BM_STATUS_OK);
        for(unsigned u=0;u<2;++u)assert(bm_at_dma_set_eop(f.dma,u,0)==BM_STATUS_OK);
        for(unsigned c=0;c<8;++c)if(c!=4)assert(bm_at_dma_set_dreq(f.dma,c,0)==BM_STATUS_OK);
        program(&f,ch,0xa0|(aut?16:0),0,count,0x87); /* decrement, verify, block */
        write_port(&f,port(ch/4,9),(ch%4)|4); /* cleared by EOP, even before actual TC */
        grant(&f,ch);f.eop_at=1;f.eop_unit=(int)(ch/4);f.calls=0;
        unsigned pulses=f.pulses[ch];assert(step(&f)==4 && !f.calls);
        bm_at_dma_channel_state_t s=channel(&f,ch);
        assert(s.terminal_count && s.masked==!aut && s.current_count==(aut?count:(uint16_t)(count-1)));
        assert(s.current_address==(aut?0:65535) && s.base_address==0 && s.base_count==count && s.page==0x87);
        assert(f.pulses[ch]==pulses+(count==0) && !f.hrq && !f.dack[ch]);
        assert(!state(&f).software_request[ch/4] && state(&f).eop[ch/4] && !(read_port(&f,port(1-ch/4,8),1)&15));
        assert((read_port(&f,port(ch/4,8),1)&15)==(1U<<(ch%4)));
        release(&f);assert((read_port(&f,port(ch/4,8),0)&15)==(1U<<(ch%4)));
        assert(!(read_port(&f,port(ch/4,8),0)&15) && state(&f).eop[ch/4] && !f.hrq);
        ++cases;
    }
    destroy(&f);printf("AT DMA EOP: %u full counter/width/autoinit cases; status versus generated TC, software clear, no forced FFFF\n",cases);
}

static void eop_boundaries(void)
{
    unsigned cases=0;
    for(unsigned ch=0;ch<8;++ch)if(ch!=4)for(unsigned kind=0;kind<3;++kind)
    for(unsigned type=4;type<=8;type+=4)for(unsigned at=0;at<=5;++at)
    for(unsigned aut=0;aut<2;++aut)for(unsigned pulse=0;pulse<2;++pulse) {
        fixture_t f;create(&f);f.eop_unit=(int)(ch/4);f.eop_at=(int)at;f.eop_pulse=(int)pulse;
        program(&f,ch,(kind<<6)|type|(aut?16:0),0xffff,3,0xff);
        if(!at)drive_eop(&f,0); /* held input or pulse while still idle */
        request(&f,ch);grant(&f,ch);f.waits=9;assert(step(&f)==13 && f.calls==2);
        bm_at_dma_channel_state_t s=channel(&f,ch);
        int accepted=!pulse;
        assert(s.terminal_count==accepted && !f.pulses[ch] && s.masked==(accepted&&!aut));
        assert(s.current_count==(accepted&&aut?3:2) && s.current_address==(accepted&&aut?65535:0));
        assert(f.hrq==(!accepted && kind!=1));
        if(f.hrq) {assert(bm_at_dma_set_eop(f.dma,ch/4,1)==BM_STATUS_OK);f.eop_at=-1;assert(step(&f)==13);assert(channel(&f,ch).terminal_count);}
        release(&f);destroy(&f);++cases;
    }
    /* Opposite-controller EOP does not end this transfer; both can be held.
     * Cascade channel4 address/count/status must never be changed by lower EOP. */
    for(unsigned ch=0;ch<8;++ch)if(ch!=4) {
        fixture_t f;create(&f);program(&f,ch,0x84,0,7,0);request(&f,ch);grant(&f,ch);
        assert(bm_at_dma_set_eop(f.dma,1-ch/4,1)==BM_STATUS_OK);assert(step(&f)==4 && f.hrq && !channel(&f,ch).terminal_count);
        bm_at_dma_channel_state_t cascade=channel(&f,4);
        assert(bm_at_dma_set_eop(f.dma,ch/4,1)==BM_STATUS_OK);assert(step(&f)==3 && !f.hrq && channel(&f,ch).terminal_count);
        bm_at_dma_channel_state_t now=channel(&f,4);
        assert(now.current_address==cascade.current_address && now.current_count==cascade.current_count && !now.terminal_count && !f.pulses[4]);
        release(&f);destroy(&f);
    }
    /* EOP driven by DACK falling occurs after this unit's sample. Held level
     * ends next grant's unit; it cannot retroactively convert a single release. */
    fixture_t f;create(&f);program(&f,6,0x44,0,2,0);request(&f,6);grant(&f,6);f.eop_unit=1;f.eop_at=6;
    assert(step(&f)==4 && !channel(&f,6).terminal_count && state(&f).eop[1]);release(&f);
    grant(&f,6);assert(step(&f)==4 && channel(&f,6).terminal_count && !f.pulses[6]);release(&f);destroy(&f);
    /* A TC callback input edge is likewise after the sample; genuine TC still
     * generates one pulse, and held EOP can terminate the following autoinit. */
    create(&f);program(&f,1,0x94,0,0,0);request(&f,1);grant(&f,1);f.eop_unit=0;f.eop_at=7;
    assert(step(&f)==4 && f.pulses[1]==1);release(&f);program(&f,1,0x94,0,7,0);grant(&f,1);
    assert(step(&f)==4 && f.pulses[1]==1 && channel(&f,1).current_count==7);release(&f);destroy(&f);
    printf("AT DMA EOP: %u mode/direction/channel/edge/pulse cases, cross-controller isolation and late DACK/TC input edges\n",cases);
}

static void eop_failures_and_reset(void)
{
    unsigned cases=0;
    for(unsigned ch=0;ch<8;++ch)if(ch!=4)for(unsigned type=4;type<=8;type+=4)
    for(unsigned split=0;split<2;++split) {
        if(split && ch<4)continue;
        for(unsigned fail=1;fail<=2+split;++fail)for(unsigned after=0;after<2;++after)for(unsigned aut=0;aut<2;++aut) {
            fixture_t f;create(&f);f.split=(int)split;program(&f,ch,0x80|type|(aut?16:0),0xffff,1,0xff);
            request(&f,ch);grant(&f,ch);assert(step(&f)==4);
            unsigned dr=f.device_reads,dw=f.device_writes,mr=f.memory_reads,mw=f.memory_writes;
            bm_at_dma_channel_state_t before=channel(&f,ch);
            f.calls=0;f.eop_at=2;f.eop_unit=(int)(ch/4);f.fail_at=(int)fail;f.after=(int)after;
            uint64_t cycles=99;assert(bm_at_dma_service(f.dma,&cycles)==BM_STATUS_DEVICE_ERROR && !cycles);
            bm_at_dma_channel_state_t now=channel(&f,ch);
            assert(now.current_address==before.current_address && now.current_count==before.current_count && !now.terminal_count && !now.masked);
            assert(state(&f).eop[ch/4] && state(&f).stopped && !f.hrq && !f.dack[ch] && !f.pulses[ch]);
            unsigned done=fail-1+after,memunits=1+split;
            assert(f.device_reads==dr+(type==4&&done>0));
            assert(f.memory_writes==mw+(type==4&&done>0?done-1:0));
            assert(f.memory_reads==mr+(type==8?(done<memunits?done:memunits):0));
            assert(f.device_writes==dw+(type==8&&done>memunits));
            assert(bm_at_dma_service(f.dma,&cycles)==BM_STATUS_INVALID_STATE && f.calls==fail);
            release(&f);write_port(&f,13,0);write_port(&f,0xda,0);
            assert(state(&f).eop[ch/4] && bm_at_dma_service(f.dma,&cycles)==BM_STATUS_INVALID_STATE);
            bm_at_dma_reset(f.dma);assert(state(&f).eop[ch/4] && !state(&f).stopped && !channel(&f,ch).terminal_count);
            f.eop_at=0;f.fail_at=0;program(&f,ch,0x84,0,3,0);grant(&f,ch);assert(step(&f)==4 && channel(&f,ch).terminal_count && !f.pulses[ch]);
            release(&f);destroy(&f);++cases;
        }
    }
    fixture_t f,peer;create(&f);create(&peer);bm_at_dma_state_t before=state(&f);
    assert(bm_at_dma_set_eop(NULL,0,1)==BM_STATUS_INVALID_ARGUMENT);
    assert(bm_at_dma_set_eop(f.dma,2,1)==BM_STATUS_INVALID_ARGUMENT);
    assert(bm_at_dma_set_eop(f.dma,0,-1)==BM_STATUS_INVALID_ARGUMENT);
    assert(bm_at_dma_set_eop(f.dma,1,2)==BM_STATUS_INVALID_ARGUMENT);
    bm_at_dma_state_t after=state(&f);assert(!memcmp(&before,&after,sizeof(before)));
    assert(bm_at_dma_set_eop(f.dma,0,1)==BM_STATUS_OK && bm_at_dma_set_eop(f.dma,0,1)==BM_STATUS_OK);
    assert(!f.events && !state(&peer).eop[0]);bm_at_dma_reset(f.dma);assert(state(&f).eop[0]);destroy(&f);destroy(&peer);
    printf("AT DMA EOP: %u before/after ordinary/split failures dominate EOP including coincident count zero; held-input reset and invalid inputs\n",cases);
}

static void eop_bus_handoff(void)
{
    for(unsigned width=0;width<2;++width)for(unsigned aut=0;aut<2;++aut) {
        fixture_t f;create(&f);bm_host_services_t h=bm_null_host_services();
        bm_at_bus_config_t bc={0};bc.cpu_clock=(bm_clock_rate_t){12000000,1};bc.isa_clock=(bm_clock_rate_t){8000000,1};
        bc.memory=memory;bc.io=memory;bc.decode_context=&f;bc.hold=hold;bc.hold_context=&f;
        assert(bm_at_bus_create(&h,&bc,&f.bus)==BM_STATUS_OK);
        unsigned ch=width?6:2;bm_at_master_t master=width?BM_AT_MASTER_DMA16:BM_AT_MASTER_DMA8;
        program(&f,ch,0x84|(aut?16:0),0xff,7,0x37);request(&f,ch);
        assert(bm_at_bus_request(f.bus,master,1)==BM_STATUS_OK && bm_at_bus_hold_ack(f.bus,1)==BM_STATUS_OK);
        grant(&f,ch);f.waits=11;f.eop_at=2;f.eop_unit=(int)(ch/4);
        assert(step(&f)==15 && !f.hrq && !f.pulses[ch] && channel(&f,ch).terminal_count);
        assert(channel(&f,ch).current_count==(aut?7:6));
        assert(bm_at_bus_request(f.bus,master,0)==BM_STATUS_OK && !f.hold);
        uint64_t cycles=7;assert(bm_at_dma_service(f.dma,&cycles)==BM_STATUS_IDLE && !cycles && f.calls==2);
        assert(bm_at_bus_request(f.bus,master,1)==BM_STATUS_INVALID_STATE);
        assert(bm_at_bus_hold_ack(f.bus,0)==BM_STATUS_OK);
        assert(bm_at_dma_set_dreq(f.dma,ch,0)==BM_STATUS_OK);release(&f);
        /* The next owner has the opposite width and controller. The old EOP
         * stays asserted but must not terminate this owner's block early. */
        ch=width?2:6;master=width?BM_AT_MASTER_DMA8:BM_AT_MASTER_DMA16;
        program(&f,ch,0x84,0,1,0x23);f.eop_at=-1;request(&f,ch);
        assert(bm_at_bus_request(f.bus,master,1)==BM_STATUS_OK && bm_at_bus_hold_ack(f.bus,1)==BM_STATUS_OK);grant(&f,ch);
        assert(step(&f)==15 && f.hrq && !channel(&f,ch).terminal_count);assert(step(&f)==14 && f.pulses[ch]==1);
        assert(bm_at_bus_request(f.bus,master,0)==BM_STATUS_OK && bm_at_bus_hold_ack(f.bus,0)==BM_STATUS_OK);release(&f);destroy(&f);
    }
    puts("AT DMA EOP: four real AT bus early-termination/opposite-width handoffs with waits and stale-HLDA rejection");
}
static void compressed_addresses(void)
{
    fixture_t f;create(&f);unsigned cases=0;
    /* First S1+S2+S4, then S2+S4 unless carry/borrow updates the address
     * latch. Exhaust both directions/widths; a word channel latches the WORD
     * address, not its shifted physical address. Intel pp7/18, figure14. */
    for(unsigned width=0;width<2;++width)for(unsigned dec=0;dec<2;++dec)
    for(unsigned a=0;a<65536;++a) {
        unsigned ch=width?5:2,type=(a%3)*4,aut=a&1,page=(a&2)?0xff:0xfe;
        bm_at_dma_reset(f.dma);assert(bm_at_dma_set_dreq(f.dma,ch,0)==BM_STATUS_OK);
        program(&f,ch,0x80|type|(dec?32:0)|(aut?16:0),a,1,page);
        write_port(&f,port(ch/4,8),8|((a&4)?32:0));
        if(!width)write_port(&f,0xd0,(a&8)?0x20:0x28); /* cascade timing ignored */
        request(&f,ch);grant(&f,ch);f.calls=0;f.waits=a==65535?UINT32_MAX:7;
        unsigned pulses=f.pulses[ch];uint64_t waits=type?f.waits:0;
        assert(step(&f)==3+waits && f.hrq && !channel(&f,ch).terminal_count);
        unsigned next=dec?(a+65535U)%65536U:(a+1U)%65536U;
        assert(channel(&f,ch).current_address==next && !channel(&f,ch).current_count);
        /* The second unit alone must pay for byte carry/borrow or 16-bit wrap. */
        int latch=dec?!(a%256):a%256==255;
        assert(step(&f)==(latch?3U:2U)+waits && !f.hrq);
        bm_at_dma_channel_state_t s=channel(&f,ch);
        unsigned final=dec?(next+65535U)%65536U:(next+1U)%65536U;
        assert(s.current_address==(aut?a:final) && s.current_count==(aut?1:65535));
        assert(s.terminal_count && s.masked==!aut && f.pulses[ch]==pulses+1);
        assert(s.base_address==a && s.base_count==1 && s.page==page && !read_port(&f,13,1));
        assert(f.calls==(type?4U:0U));
        if(type) {
            uint32_t physical=width?(page/2)*131072U+next*2:page*65536U+next;
            assert(f.last.bus.address==physical);
            if(type==4)assert(f.last.bus.value==(width?0xfedcU:0xdcU));
            else assert(f.delivered==(uint16_t)(memory_byte(physical)|(width?(uint16_t)memory_byte(physical+1)<<8:0)));
        }
        release(&f);++cases;
    }
    destroy(&f);
    /* Full FFFF blocks with independently summed latch crossings. */
    for(unsigned width=0;width<2;++width)for(unsigned dec=0;dec<2;++dec) {
        create(&f);unsigned ch=width?7:3;
        program(&f,ch,0x80|(dec?32:0)|8,0xff80,65535,0xab);
        write_port(&f,port(ch/4,8),0x28);request(&f,ch);grant(&f,ch);
        uint64_t clocks=0;
        for(unsigned i=0;i<65536;++i) {
            f.waits=i%5;uint64_t got=step(&f);clocks+=got;
            assert(!channel(&f,ch).terminal_count || i==65535);
        }
        /* 257 S1 states including the initial unaligned block, 131070 waits. */
        assert(clocks==2ULL*65536+257+131070 && f.high[ch]==1 && f.low[ch]==1 && f.pulses[ch]==1);
        assert(channel(&f,ch).current_address==0xff80 && channel(&f,ch).current_count==65535);
        assert(f.memory_reads==65536 && f.device_writes==65536);release(&f);destroy(&f);
    }
    printf("AT DMA compressed: %u full address/direction/width cases, high-latch/wrap/page/data/autoinit/max waits; four full blocks\n",cases);
}

static void compressed_boundaries(void)
{
    unsigned cases=0;
    for(unsigned ch=0;ch<8;++ch)if(ch!=4)for(unsigned kind=0;kind<3;++kind)
    for(unsigned type=0;type<=8;type+=4)for(unsigned aut=0;aut<2;++aut)
    for(unsigned ext=0;ext<2;++ext)for(unsigned ending=0;ending<3;++ending)
    for(unsigned delay=0;delay<3;++delay) {
        fixture_t f;create(&f);unsigned initial=ending==0?0:3;
        program(&f,ch,(kind<<6)|type|(aut?16:0),0xffff,initial,0xff);
        write_port(&f,port(ch/4,8),8|(ext?32:0));
        if(ch<4)write_port(&f,0xd0,0x28); /* does not add another compressed stage */
        f.waits=delay==2?UINT32_MAX:delay;f.drop_at=ending==2?1:0;
        f.eop_at=ending==1?1:-1;f.eop_unit=(int)(ch/4);
        request(&f,ch);grant(&f,ch);assert(step(&f)==3ULL+(type?f.waits:0));
        int terminated=ending!=2;
        bm_at_dma_channel_state_t s=channel(&f,ch);
        assert(s.terminal_count==terminated && s.masked==(terminated&&!aut));
        assert(s.current_address==(terminated&&aut?65535:0));
        assert(s.current_count==(terminated&&aut?initial:(uint16_t)(initial-1)));
        assert(f.pulses[ch]==(ending==0) && f.calls==(type?2U:0U));
        assert(f.hrq==(!terminated && kind==2));
        assert(state(&f).command[ch/4]==(8|(ext?32:0)));
        if(f.hrq) { /* block ignores DREQ drop; EOP ends the next unit */
            assert(bm_at_dma_set_eop(f.dma,ch/4,1)==BM_STATUS_OK);
            assert(step(&f)==3ULL+(type?f.waits:0) && !f.hrq && !f.pulses[ch]);
        }
        release(&f);destroy(&f);++cases;
    }
    /* Demand pause and single release both require fresh S1/HLDA on resume. */
    for(unsigned ch=0;ch<8;++ch)if(ch!=4)for(unsigned kind=0;kind<2;++kind) {
        fixture_t f;create(&f);program(&f,ch,(kind<<6)|4,0x80,1,0);
        write_port(&f,port(ch/4,8),8);f.drop_at=1;request(&f,ch);grant(&f,ch);
        assert(step(&f)==3 && !f.hrq);release(&f);f.drop_at=0;
        request(&f,ch);grant(&f,ch);assert(step(&f)==3 && f.pulses[ch]==1);release(&f);destroy(&f);
    }
    /* Masked software Block request remains non-maskable in compressed mode. */
    for(unsigned ch=0;ch<8;++ch)if(ch!=4) {
        fixture_t f;create(&f);program(&f,ch,0x80,0x80,1,0);
        write_port(&f,port(ch/4,8),8);write_port(&f,port(ch/4,10),(ch%4)|4);
        write_port(&f,port(ch/4,9),(ch%4)|4);grant(&f,ch);
        assert(step(&f)==3 && step(&f)==2 && !state(&f).software_request[ch/4]);release(&f);destroy(&f);
    }
    /* Upper cascade timing is irrelevant even with a NORMAL lower channel. */
    for(unsigned command=0;command<4;++command) {
        fixture_t f;create(&f);program(&f,0,0x84,0x80,1,0);
        write_port(&f,0xd0,(command&1?8:0)|(command&2?32:0));request(&f,0);grant(&f,0);
        assert(step(&f)==4 && step(&f)==3 && !f.high[4] && !channel(&f,4).terminal_count);
        release(&f);destroy(&f);
    }
    printf("AT DMA compressed: %u channel/mode/type/autoinit/TC/EOP/DREQ/wait cases; fresh S1 on resume, software block, cascade timing isolation\n",cases);
}

static void compressed_failures(void)
{
    unsigned cases=0;
    const bm_status_t errors[]={BM_STATUS_DEVICE_ERROR,BM_STATUS_UNMAPPED,BM_STATUS_READ_ONLY,BM_STATUS_CAPACITY_EXCEEDED,BM_STATUS_IDLE};
    for(unsigned ch=0;ch<8;++ch)if(ch!=4)for(unsigned type=4;type<=8;type+=4)
    for(unsigned split=0;split<2;++split) {
        if(split && ch<4)continue;
        for(unsigned fail=1;fail<=2+split;++fail)for(unsigned after=0;after<2;++after)
        for(unsigned ext=0;ext<2;++ext)for(unsigned err=0;err<sizeof(errors)/sizeof(errors[0]);++err) {
            fixture_t f;create(&f);f.split=(int)split;program(&f,ch,0x94,0xffff,1,0xff);
            write_port(&f,port(ch/4,11),0x90|type|(ch%4));write_port(&f,port(ch/4,8),8|(ext?32:0));
            request(&f,ch);grant(&f,ch);assert(step(&f)==3);
            unsigned dr=f.device_reads,dw=f.device_writes,mr=f.memory_reads,mw=f.memory_writes;
            bm_at_dma_channel_state_t before=channel(&f,ch);
            f.calls=0;f.eop_at=2;f.eop_unit=(int)(ch/4);f.fail_at=(int)fail;f.after=(int)after;f.failure=errors[err];
            uint64_t clocks=99;bm_status_t expected=f.failure==BM_STATUS_IDLE?BM_STATUS_INVALID_STATE:f.failure;
            assert(bm_at_dma_service(f.dma,&clocks)==expected && !clocks);
            bm_at_dma_channel_state_t now=channel(&f,ch);
            assert(now.current_address==before.current_address && now.current_count==before.current_count && !now.terminal_count && !now.masked);
            assert(state(&f).eop[ch/4] && state(&f).stopped && !f.hrq && !f.dack[ch] && !f.pulses[ch]);
            unsigned done=fail-1+after,memunits=1+split;
            assert(f.device_reads==dr+(type==4&&done>0));
            assert(f.memory_writes==mw+(type==4&&done>0?done-1:0));
            assert(f.memory_reads==mr+(type==8?(done<memunits?done:memunits):0));
            assert(f.device_writes==dw+(type==8&&done>memunits));
            assert(bm_at_dma_service(f.dma,&clocks)==BM_STATUS_INVALID_STATE && f.calls==fail);
            release(&f);write_port(&f,13,0);write_port(&f,0xda,0);
            assert(bm_at_dma_service(f.dma,&clocks)==BM_STATUS_INVALID_STATE && !f.hrq);
            bm_at_dma_reset(f.dma);assert(!state(&f).stopped && state(&f).eop[ch/4]);destroy(&f);++cases;
        }
    }
    printf("AT DMA compressed: %u ordinary/split before-after endpoint errors, retained prefix/effects, EOP cannot convert error to TC or replay\n",cases);
}

static void compressed_bus(void)
{
    for(unsigned width=0;width<2;++width)for(unsigned ext=0;ext<2;++ext) {
        fixture_t f;create(&f);bm_host_services_t h=bm_null_host_services();
        bm_at_bus_config_t bc={0};bc.cpu_clock=(bm_clock_rate_t){12000000,1};bc.isa_clock=(bm_clock_rate_t){8000000,1};
        bc.memory=memory;bc.io=memory;bc.decode_context=&f;bc.hold=hold;bc.hold_context=&f;
        assert(bm_at_bus_create(&h,&bc,&f.bus)==BM_STATUS_OK);
        unsigned ch=width?6:2;bm_at_master_t master=width?BM_AT_MASTER_DMA16:BM_AT_MASTER_DMA8;
        program(&f,ch,0x84,0xfe,2,0xff);write_port(&f,port(ch/4,8),8|(ext?32:0));request(&f,ch);
        assert(bm_at_bus_set_lock(f.bus,1)==BM_STATUS_OK && bm_at_bus_request(f.bus,master,1)==BM_STATUS_OK && !f.hold);
        assert(bm_at_bus_hold_ack(f.bus,1)==BM_STATUS_INVALID_STATE);
        uint64_t clocks=9;assert(bm_at_dma_service(f.dma,&clocks)==BM_STATUS_IDLE && !clocks && !f.calls);
        assert(bm_at_bus_set_lock(f.bus,0)==BM_STATUS_OK && bm_at_bus_hold_ack(f.bus,1)==BM_STATUS_OK);
        grant(&f,ch);f.waits=UINT32_MAX;
        assert(step(&f)==3ULL+UINT32_MAX && step(&f)==2ULL+UINT32_MAX && step(&f)==3ULL+UINT32_MAX);
        assert(!f.hrq && f.pulses[ch]==1 && f.calls==6);
        assert(bm_at_bus_request(f.bus,master,0)==BM_STATUS_OK && !f.hold);
        assert(bm_at_bus_request(f.bus,master,1)==BM_STATUS_INVALID_STATE);
        assert(bm_at_bus_hold_ack(f.bus,0)==BM_STATUS_OK);release(&f);destroy(&f);
    }
    puts("AT DMA compressed: four real AT LOCK/HOLD/HLDA byte/word blocks, maximum waits, high-byte refresh, stale grant rejection");
}

static void timing_commands(void)
{
    unsigned cases=0;
    for(unsigned ch=0;ch<8;++ch)if(ch!=4)for(unsigned command=0;command<256;++command) {
        fixture_t f;create(&f);program(&f,ch,0x84,0x80,1,0);
        write_port(&f,port(ch/4,8),command);
        assert(bm_at_dma_set_dreq(f.dma,ch,1)==BM_STATUS_OK);
        uint64_t clocks=9;
        if(command&4) {
            assert(!f.hrq && bm_at_dma_service(f.dma,&clocks)==BM_STATUS_IDLE && !clocks && !f.calls);
        } else {
            grant(&f,ch);
            if(command&1) {
                assert(bm_at_dma_service(f.dma,&clocks)==BM_STATUS_UNSUPPORTED && !clocks && !f.calls && !f.high[ch]);
                assert(state(&f).stopped && channel(&f,ch).current_address==0x80 && channel(&f,ch).current_count==1);
            } else {
                assert(step(&f)==(command&8?3U:4U));
                assert(step(&f)==(command&8?2U:3U) && f.pulses[ch]==1 && f.calls==4);
            }
            release(&f);
        }
        assert(state(&f).command[ch/4]==command);destroy(&f);++cases;
    }
    puts("AT DMA timing: all 1792 channel/command combinations; disable, polarity/hold, normal/extended/compressed and retained unsupported gates");
    assert(cases==1792);
}

static void timing_profiles(void)
{
    /* Independent table from Intel p14 note3 and figures11/14, excluding
     * propagation-delay margins. Rows: late, extended, compressed, both. */
    static const unsigned expected[4][3]={{3,2,1},{3,2,2},{2,1,1},{2,1,1}};
    fixture_t f;create(&f);unsigned cases=0;
    for(unsigned ch=0;ch<8;++ch)for(unsigned cmd=0;cmd<256;++cmd)
    for(unsigned mode=0;mode<256;mode+=4) {
        bm_at_dma_reset(f.dma);program(&f,ch,mode,0xffff,0xffff,0xff);
        write_port(&f,port(ch/4,8),cmd);
        if(mode&16)write_port(&f,port(ch/4,10),(ch%4)|4);
        bm_at_dma_state_t before=state(&f);
        bm_at_dma_channel_state_t saved[8];
        for(unsigned c=0;c<8;++c)saved[c]=channel(&f,c);
        bm_at_dma_timing_t t={99,99,99};
        bm_status_t status=bm_at_dma_channel_timing(f.dma,ch,&t);
        unsigned row=(cmd&8?2:0)+(cmd&32?1:0),type=mode&12;
        if(ch==4 || (cmd&1) || mode>=192 || type==12) {
            assert(status==BM_STATUS_UNSUPPORTED && !t.transfer_clocks && !t.read_pulse_clocks && !t.write_pulse_clocks);
        } else {
            assert(status==BM_STATUS_OK && t.transfer_clocks==expected[row][0]);
            assert(t.read_pulse_clocks==(type?expected[row][1]:0) && t.write_pulse_clocks==(type?expected[row][2]:0));
        }
        bm_at_dma_state_t after=state(&f);same_state(&before,&after);
        for(unsigned c=0;c<8;++c) {bm_at_dma_channel_state_t now=channel(&f,c);same_channel(&now,&saved[c]);}
        assert(!f.calls && !f.events && !state(&f).stopped);++cases;
    }
    bm_at_dma_state_t before=state(&f);bm_at_dma_timing_t t={99,99,99};
    assert(bm_at_dma_channel_timing(NULL,0,&t)==BM_STATUS_INVALID_ARGUMENT && !t.transfer_clocks && !t.read_pulse_clocks && !t.write_pulse_clocks);
    t=(bm_at_dma_timing_t){99,99,99};
    assert(bm_at_dma_channel_timing(f.dma,8,&t)==BM_STATUS_INVALID_ARGUMENT && !t.transfer_clocks && !t.read_pulse_clocks && !t.write_pulse_clocks);
    assert(bm_at_dma_channel_timing(f.dma,0,NULL)==BM_STATUS_INVALID_ARGUMENT);
    bm_at_dma_state_t after=state(&f);same_state(&before,&after);destroy(&f);
    printf("AT DMA timing profile: %u channel/command/mode cases, disabled/masked inspection, verify, zero output on errors, no state or endpoint effects\n",cases);
}

static void extended_adapter(void)
{
    unsigned cases=0;
    const unsigned commands[]={0,32,8,40};
    const unsigned base[]={3,3,2,2},read_wait[]={1,1,2,2},write_wait[]={1,0,1,1};
    for(unsigned ch=0;ch<8;++ch)if(ch!=4)for(unsigned row=0;row<4;++row)
    for(unsigned type=4;type<=8;type+=4)for(unsigned real_bus=0;real_bus<2;++real_bus) {
        fixture_t f;create(&f);f.profile_waits=1;
        if(real_bus) {
            bm_host_services_t h=bm_null_host_services();bm_at_bus_config_t bc={0};
            bc.cpu_clock=(bm_clock_rate_t){12000000,1};bc.isa_clock=(bm_clock_rate_t){8000000,1};
            bc.memory=memory;bc.io=memory;bc.decode_context=&f;bc.hold=hold;bc.hold_context=&f;
            assert(bm_at_bus_create(&h,&bc,&f.bus)==BM_STATUS_OK);
        }
        program(&f,ch,0x80|type,0xfe,2,0xff);write_port(&f,port(ch/4,8),commands[row]);
        if(ch<4)write_port(&f,0xd0,commands[3-row]); /* controller isolation */
        request(&f,ch);bm_at_master_t master=ch<4?BM_AT_MASTER_DMA8:BM_AT_MASTER_DMA16;
        if(real_bus) {
            assert(bm_at_bus_set_lock(f.bus,1)==BM_STATUS_OK && bm_at_bus_request(f.bus,master,1)==BM_STATUS_OK && !f.hold);
            assert(bm_at_bus_hold_ack(f.bus,1)==BM_STATUS_INVALID_STATE);
            assert(bm_at_bus_set_lock(f.bus,0)==BM_STATUS_OK && bm_at_bus_hold_ack(f.bus,1)==BM_STATUS_OK);
        }
        grant(&f,ch);
        unsigned waits=type==4?write_wait[row]:read_wait[row];
        assert(step(&f)==base[row]+1+waits && f.waits==waits);
        assert(step(&f)==base[row]+waits);
        assert(step(&f)==base[row]+1+waits && f.pulses[ch]==1 && f.calls==6);
        if(real_bus) {
            assert(bm_at_bus_request(f.bus,master,0)==BM_STATUS_OK && !f.hold);
            assert(bm_at_bus_request(f.bus,master,1)==BM_STATUS_INVALID_STATE);
            assert(bm_at_bus_hold_ack(f.bus,0)==BM_STATUS_OK);
        }
        release(&f);destroy(&f);++cases;
    }
    printf("AT DMA timing adapter: %u byte/word/direction/profile/direct-or-real-AT blocks, nominal pulse driven waits, initial/carry S1, LOCK and stale grant\n",cases);
}

int main(void)
{
    word_and_page_matrix();counts_and_autoinit();bursts_and_priority();failures();gates_and_bus();partial_words_and_lifecycle();
    eop_counts();eop_boundaries();eop_failures_and_reset();eop_bus_handoff();
    compressed_addresses();compressed_boundaries();compressed_failures();compressed_bus();timing_commands();
    timing_profiles();extended_adapter();
    /* Same authored functional oracles, now with bit5 set for every programmed
     * channel. Their unchanged cycle/data expectations detect an extra cycle
     * or any loss of extended-write count/EOP/error/lifecycle behavior. */
    fixture_command=32;puts("AT DMA extended-write regression begins (normal functional oracles, bit5 set)");
    word_and_page_matrix();counts_and_autoinit();bursts_and_priority();failures();partial_words_and_lifecycle();
    eop_counts();eop_boundaries();eop_failures_and_reset();eop_bus_handoff();
    fixture_command=0;puts("AT DMA extended-write regression complete");return 0;
}
