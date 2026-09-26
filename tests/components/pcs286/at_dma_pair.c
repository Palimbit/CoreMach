/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Private transport tests; not public mem2mem or chipset acceptance.
 */
#include "at_dma_pair.h"
#include <blumach/platforms/null_host.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct fixture {
    bm_at_dma_pair_t pair;
    uint8_t ram[65536], page, eop;
    uint16_t source, destination;
    unsigned calls, reads, writes, fail_at, after, eop_at[2], reenter;
    uint32_t waits[2];
    bm_status_t error;
    bm_at_bus_t *bus;
    int hold;
} fixture_t;

static bm_status_t memory(void *context, bm_at_transfer_t *t)
{
    fixture_t *f=context; unsigned phase=f->calls++;
    assert(phase<2 && f->pair.phase==BM_AT_DMA_PAIR_BUSY);
    assert(t->master==BM_AT_MASTER_DMA8 && t->bus.space==BM_ADDRESS_MEMORY);
    assert(t->requester_clock.cycles_per_second_numerator==4000000 &&
           t->requester_clock.cycles_per_second_denominator==3);
    assert(t->bus.operation==(phase ? BM_BUS_WRITE : BM_BUS_READ));
    assert(t->bus.address==((uint32_t)f->page<<16 | (phase?f->destination:f->source)));
    assert(t->bus.size==1 && t->bus.alignment==1 && t->bus.endianness==BM_ENDIAN_LITTLE);
    assert(!t->bus.attributes && !t->bus.wait_states);
    if(f->reenter) {
        uint64_t clocks=99;
        assert(bm_at_dma_pair_step(&f->pair,&clocks)==BM_STATUS_INVALID_STATE && !clocks);
        assert(f->pair.phase==BM_AT_DMA_PAIR_BUSY);
    }
    f->eop=(uint8_t)f->eop_at[phase];
    if(f->fail_at==f->calls && !f->after) return f->error;
    if(!phase) {++f->reads; t->bus.value=0x123400U|f->ram[f->source];}
    else {++f->writes; assert(t->bus.value<=255U); f->ram[f->destination]=(uint8_t)t->bus.value;}
    t->bus.wait_states=f->waits[phase];
    return f->fail_at==f->calls ? f->error : BM_STATUS_OK;
}

static void prepare(fixture_t *f, unsigned page, unsigned source, unsigned dest)
{
    f->page=(uint8_t)page; f->source=(uint16_t)source; f->destination=(uint16_t)dest;
    f->calls=f->reads=f->writes=f->fail_at=f->after=f->eop_at[0]=f->eop_at[1]=0;
    f->eop=0; f->reenter=0; f->waits[0]=f->waits[1]=0; f->error=BM_STATUS_DEVICE_ERROR;
    assert(bm_at_dma_pair_prepare(&f->pair,memory,f,(bm_clock_rate_t){4000000,3},
        f->page,f->source,f->destination,&f->eop)==BM_STATUS_OK);
    assert(f->pair.phase==BM_AT_DMA_PAIR_READ && !f->pair.completed_clocks && !f->calls);
}

static void copied(fixture_t *f, int change_source)
{
    unsigned original=f->ram[f->source]; uint64_t clocks=99;
    assert(bm_at_dma_pair_step(&f->pair,&clocks)==BM_STATUS_OK && clocks==4ULL+f->waits[0]);
    assert(f->calls==1 && f->reads==1 && !f->writes && f->pair.read_complete && !f->pair.write_complete);
    assert(f->pair.temporary==original && f->pair.source_eop==(f->eop_at[0]!=0));
    assert(f->pair.phase==BM_AT_DMA_PAIR_WRITE && f->pair.completed_clocks==clocks);
    if(change_source) f->ram[f->source]=(uint8_t)(original^255U);
    assert(bm_at_dma_pair_step(&f->pair,&clocks)==BM_STATUS_OK && clocks==4ULL+f->waits[1]);
    assert(f->calls==2 && f->reads==1 && f->writes==1 && f->ram[f->destination]==original);
    assert(f->pair.temporary==original && f->pair.destination_eop==(f->eop_at[1]!=0));
    assert(f->pair.phase==BM_AT_DMA_PAIR_COMPLETE && f->pair.read_complete && f->pair.write_complete);
    assert(f->pair.completed_clocks==8ULL+f->waits[0]+f->waits[1]);
    clocks=99;
    assert(bm_at_dma_pair_step(&f->pair,&clocks)==BM_STATUS_INVALID_STATE && !clocks && f->calls==2);
}

static void addresses_and_data(void)
{
    fixture_t f={0}; unsigned cases=0;
    for(unsigned address=0;address<65536;++address) for(unsigned route=0;route<4;++route) {
        unsigned destination=route==0?address:route==1?(address^65535U):route==2?0:65535;
        prepare(&f,address&255U,address,destination);
        f.ram[f.source]=(uint8_t)(address>>8); f.reenter=1;
        f.waits[0]=address&15U; f.waits[1]=destination&31U;
        copied(&f,1); ++cases;
    }
    for(unsigned value=0;value<256;++value) for(unsigned prior=0;prior<256;++prior) {
        prepare(&f,0xff,0xffff,0); f.ram[0]=(uint8_t)prior; f.ram[0xffff]=(uint8_t)value;
        copied(&f,1); ++cases;
    }
    printf("AT DMA private pair: %u address/data cases, all offsets/pages/bytes, aliases and boundary destinations, TEMP isolation, reentry and no replay\n",cases);
}

static void phase_inputs(void)
{
    fixture_t f={0}; const uint32_t waits[]={0,1,17,UINT32_MAX}; unsigned cases=0;
    for(unsigned eop=0;eop<4;++eop) for(unsigned a=0;a<4;++a) for(unsigned b=0;b<4;++b) {
        prepare(&f,0x92,0x1234,0x5678); f.ram[f.source]=0xa5;
        f.eop_at[0]=eop&1U; f.eop_at[1]=(eop>>1)*255U;
        f.waits[0]=waits[a]; f.waits[1]=waits[b]; copied(&f,0); ++cases;
    }
    /* A pulse wholly between completion samples is not queued. Caller can
     * abandon the pair after the read; no automatic write/termination occurs. */
    prepare(&f,0,1,2); f.ram[1]=0x53; f.ram[2]=0x76; f.eop=1; f.eop=0;
    uint64_t clocks=99; assert(bm_at_dma_pair_step(&f.pair,&clocks)==BM_STATUS_OK);
    assert(!f.pair.source_eop && !f.writes && f.ram[2]==0x76);
    puts("AT DMA private pair: completed read can remain pending without an automatic write or EOP decision");
    printf("AT DMA private pair: %u independent EOP/uint32 wait combinations, 64-bit prefix accounting\n",cases);
}

static void failures(void)
{
    const bm_status_t errors[]={BM_STATUS_DEVICE_ERROR,BM_STATUS_UNMAPPED,BM_STATUS_READ_ONLY,
        BM_STATUS_CAPACITY_EXCEEDED,BM_STATUS_UNSUPPORTED,BM_STATUS_OUT_OF_MEMORY,BM_STATUS_IDLE};
    fixture_t f={0}; unsigned cases=0;
    for(unsigned phase=1;phase<=2;++phase) for(unsigned after=0;after<2;++after)
    for(unsigned err=0;err<sizeof(errors)/sizeof(errors[0]);++err)
    for(unsigned alias=0;alias<2;++alias) for(unsigned eop=0;eop<4;++eop) {
        prepare(&f,0x83,0x1234,alias?0x1234:0x5678);
        f.ram[f.destination]=0x76; f.ram[f.source]=0x35;
        f.fail_at=phase; f.after=after; f.error=errors[err]; f.reenter=1;
        f.waits[0]=f.waits[1]=UINT32_MAX; f.eop_at[0]=eop&1U; f.eop_at[1]=eop>>1;
        uint64_t clocks=99;
        if(phase==2) assert(bm_at_dma_pair_step(&f.pair,&clocks)==BM_STATUS_OK);
        assert(bm_at_dma_pair_step(&f.pair,&clocks)==(errors[err]==BM_STATUS_IDLE?BM_STATUS_INVALID_STATE:errors[err]) && !clocks);
        assert(f.pair.phase==BM_AT_DMA_PAIR_STOPPED && f.calls==phase);
        assert(f.reads==(phase==2 || after) && f.writes==(phase==2 && after));
        assert(f.ram[f.destination]==(alias || (phase==2 && after)?0x35:0x76));
        assert(f.pair.read_complete==(phase==2) && !f.pair.write_complete);
        assert(f.pair.temporary==(phase==2?0x35:0));
        assert(f.pair.source_eop==(phase==2?(eop&1U):0) && !f.pair.destination_eop);
        assert(f.pair.completed_clocks==(phase==2?4ULL+UINT32_MAX:0));
        for(unsigned retry=0;retry<2;++retry) {
            clocks=99; assert(bm_at_dma_pair_step(&f.pair,&clocks)==BM_STATUS_INVALID_STATE && !clocks && f.calls==phase);
        }
        ++cases;
    }
    printf("AT DMA private pair: %u before/after phase failures, retained prefix/TEMP/partial writes, no false completion or replay\n",cases);
}

static void hold(void *context,int level) { ((fixture_t*)context)->hold=level; }
static bm_status_t bus_memory(void *context,bm_at_transfer_t *transfer)
{ return bm_at_bus_access(context,transfer); }
static void real_bus(void)
{
    fixture_t f={0}; bm_host_services_t host=bm_null_host_services(); bm_at_bus_config_t config={0};
    config.memory=memory; config.io=memory; config.decode_context=&f; config.hold=hold; config.hold_context=&f;
    config.cpu_clock=(bm_clock_rate_t){12000000,1}; config.isa_clock=(bm_clock_rate_t){8000000,1};
    assert(bm_at_bus_create(&host,&config,&f.bus)==BM_STATUS_OK);
    for(unsigned loss=0;loss<3;++loss) {
        prepare(&f,0x92,0xffff,0); f.ram[0xffff]=0x49; f.ram[0]=0x76; f.waits[0]=17; f.waits[1]=UINT32_MAX;
        f.pair.memory=bus_memory; f.pair.context=f.bus;
        assert(bm_at_bus_set_lock(f.bus,1)==BM_STATUS_OK);
        assert(bm_at_bus_request(f.bus,BM_AT_MASTER_DMA8,1)==BM_STATUS_OK && !f.hold);
        assert(bm_at_bus_hold_ack(f.bus,1)==BM_STATUS_INVALID_STATE);
        assert(bm_at_bus_set_lock(f.bus,0)==BM_STATUS_OK && f.hold);
        if(loss!=1) assert(bm_at_bus_hold_ack(f.bus,1)==BM_STATUS_OK);
        uint64_t clocks=99;
        bm_status_t result=bm_at_dma_pair_step(&f.pair,&clocks);
        if(loss==1) assert(result==BM_STATUS_INVALID_STATE && !clocks && !f.calls);
        else {
            assert(result==BM_STATUS_OK && clocks==21 && f.calls==1);
            if(loss==2) assert(bm_at_bus_request(f.bus,BM_AT_MASTER_DMA8,0)==BM_STATUS_OK);
            result=bm_at_dma_pair_step(&f.pair,&clocks);
            if(loss==2) assert(result==BM_STATUS_INVALID_STATE && !clocks && f.calls==1 && f.pair.completed_clocks==21);
            else assert(result==BM_STATUS_OK && clocks==4ULL+UINT32_MAX && f.ram[0]==0x49);
        }
        if(loss) assert(f.ram[0]==0x76 && f.pair.phase==BM_AT_DMA_PAIR_STOPPED);
        assert(bm_at_bus_request(f.bus,BM_AT_MASTER_DMA8,0)==BM_STATUS_OK && !f.hold);
        assert(bm_at_bus_hold_ack(f.bus,0)==BM_STATUS_OK);
    }
    bm_at_bus_destroy(f.bus);
    puts("AT DMA private pair: real AT LOCK/HLDA ownership, successful pair and grant loss before each phase, native waits");
}

static void invalid(void)
{
    fixture_t f={0}; uint64_t clocks=99;
    assert(bm_at_dma_pair_step(NULL,&clocks)==BM_STATUS_INVALID_ARGUMENT && !clocks);
    assert(bm_at_dma_pair_step(&f.pair,&clocks)==BM_STATUS_INVALID_STATE && !clocks);
    for(unsigned bad=0;bad<3;++bad) {
        assert(bm_at_dma_pair_prepare(&f.pair,bad?memory:NULL,&f,
            (bm_clock_rate_t){bad==1?0:1,bad==2?0:1},0,0,0,NULL)==BM_STATUS_INVALID_ARGUMENT);
        assert(f.pair.phase==BM_AT_DMA_PAIR_STOPPED && !f.calls);
    }
    assert(bm_at_dma_pair_prepare(NULL,memory,&f,(bm_clock_rate_t){1,1},0,0,0,NULL)==BM_STATUS_INVALID_ARGUMENT);
    prepare(&f,0,0,1); f.pair.eop=NULL;
    assert(bm_at_dma_pair_step(&f.pair,NULL)==BM_STATUS_INVALID_ARGUMENT && f.pair.phase==BM_AT_DMA_PAIR_READ && !f.calls);
    f.eop_at[0]=f.eop_at[1]=1; f.ram[0]=0x42;
    assert(bm_at_dma_pair_step(&f.pair,&clocks)==BM_STATUS_OK && !f.pair.source_eop);
    assert(bm_at_dma_pair_step(&f.pair,&clocks)==BM_STATUS_OK && !f.pair.destination_eop);
    puts("AT DMA private pair: invalid preparation/arguments and optional EOP input");
}

int main(void) {addresses_and_data();phase_inputs();failures();real_bus();invalid();return 0;}
