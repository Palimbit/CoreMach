/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Authored GC103 shadow regression from Headland 07-89 (01), pp5-7.
 * Literal EMS pointer table supplies the expected RAM source, independently
 * of the classic mapper. RAM populations remain inherited test fixtures.
 */
#include "legacy_gc103_memory.h"
#include <blumach/systems/pcs286_memory.h>
#include <blumach/platforms/null_host.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned long route_checks, byte_checks;
/* p6 rows: D4/F then D3/E, four 16KiB pages each. */
static const uint16_t pointers[2][2][4] = {
    {{0x298,0x299,0x29a,0x29b},{0x29c,0x29d,0x29e,0x29f}},
    {{0x238,0x239,0x23a,0x23b},{0x23c,0x23d,0x23e,0x23f}}
};

static void write_port(bm_gc103_memory_t *m,uint16_t port,unsigned width,uint16_t value)
{
    assert(bm_gc103_memory_io(m,port,width,BM_BUS_WRITE,0,&value)==BM_STATUS_OK);
}
static uint32_t table_offset(unsigned geometry,unsigned half,unsigned page)
{
    unsigned word=pointers[geometry][half][page];
    unsigned pages=geometry?128U:32U;
    return (((word>>7)&3U)*pages+(word&(pages-1U)))*0x4000U;
}
static void route_matrix(void)
{
    bm_gc103_memory_t m, before;
    unsigned mib,cr,who,op,half,page,high,a20,i;
    const uint32_t within[]={0,1,0x1234U,0x3ffeU,0x3fffU};
    for(mib=1;mib<=4;++mib) {
        assert(bm_gc103_memory_initialize(&m,mib*0x100000U)==BM_STATUS_OK);
        for(cr=0;cr<32;++cr) {
            write_port(&m,0x1efU,1,(uint16_t)cr);
            memcpy(&before,&m,sizeof(m));
            for(half=0;half<2;++half) for(page=0;page<4;++page)
            for(high=0;high<2;++high) for(a20=0;a20<2;++a20)
            for(who=0;who<3;++who) for(op=0;op<3;++op)
            for(i=0;i<5;++i) {
                bm_gc10x_route_t got;
                uint32_t address=(half?0xe0000U:0xf0000U)+page*0x4000U+within[i];
                int shadow=(cr&4U) && (cr&(half?8U:16U));
                if(high) address+=0xf00000U;
                assert(bm_gc103_memory_resolve(&m,(bm_gc10x_requester_t)who,(int)a20,
                    address,(bm_bus_operation_t)op,&got)==BM_STATUS_OK);
                if(high && !a20 && who==BM_GC10X_CPU) {
                    /* A20 low takes FE/FF to EE/EF, outside these decodes. */
                    assert(got.target==BM_GC10X_EXTERNAL && got.offset==(address&~0x100000U));
                    assert(got.writable);
                } else if(shadow) {
                    assert(got.target==BM_GC10X_RAM && !got.writable);
                    assert(got.offset==table_offset(mib==4,half,page)+within[i]);
                } else {
                    assert(got.target==BM_GC10X_FIRMWARE && !got.writable);
                    assert(got.offset==(address&0x1ffffU));
                }
                assert(got.contiguous_bytes==0x4000U-within[i]);
                assert(got.wait_quality==BM_GC10X_WAIT_UNKNOWN && got.extra_memory_clocks==0);
                ++route_checks;
            }
            assert(memcmp(&before,&m,sizeof(m))==0);
        }
    }
}

static bm_status_t transfer(bm_gc103_memory_t *m,bm_pcs286_memory_t *bytes,
                            uint32_t address,bm_bus_operation_t op,uint8_t *value)
{
    bm_gc10x_route_t route;
    bm_bus_transaction_t t={0};
    bm_status_t status;
    assert(bm_gc103_memory_resolve(m,BM_GC10X_CPU,1,address,op,&route)==BM_STATUS_OK);
    if(op==BM_BUS_WRITE && !route.writable) return BM_STATUS_READ_ONLY;
    assert(route.target==BM_GC10X_RAM || route.target==BM_GC10X_FIRMWARE);
    t.space=BM_ADDRESS_MEMORY;t.operation=op;t.address=address;t.size=1;t.value=*value;
    status=bm_pcs286_memory_access(bytes,route.target==BM_GC10X_RAM?BM_PCS286_MEMORY_RAM:BM_PCS286_MEMORY_ROM,route.offset,&t);
    if(status==BM_STATUS_OK)*value=(uint8_t)t.value;
    ++byte_checks;
    return status;
}
static uint8_t pattern(unsigned half,unsigned page,uint32_t offset)
{
    return (uint8_t)(0x35U+half*83U+page*17U+(offset^(offset>>8)));
}
static void actual_bytes(void)
{
    static uint8_t image[BM_PCS286_FIRMWARE_BYTES];
    bm_host_services_t host=bm_null_host_services();
    bm_pcs286_firmware_t firmware={0};
    bm_pcs286_memory_t *bytes;
    bm_gc103_memory_t m;
    unsigned mib,context,half,page,high,mask;
    uint32_t offset;
    uint8_t value;
    memset(image,0xa7,sizeof(image));
    firmware.image[0].data=image;firmware.image[0].size=sizeof(image);
    for(mib=1;mib<=4;++mib) for(context=0;context<2;++context) {
        assert(bm_pcs286_memory_create(&host,mib*0x100000U,&firmware,&bytes)==BM_STATUS_OK);
        assert(bm_gc103_memory_initialize(&m,mib*0x100000U)==BM_STATUS_OK);
        write_port(&m,0x1efU,1,(uint16_t)(6U|context));
        /* Populate every byte using precisely the MR values printed in p6.
         * No direct backing writes or implicit ROM-to-RAM copy. */
        for(half=0;half<2;++half) for(page=0;page<4;++page) {
            write_port(&m,0x1eeU,1,(uint16_t)(context*32U+24U));
            write_port(&m,0x1ecU,2,pointers[mib==4][half][page]);
            for(offset=0;offset<0x4000U;++offset) {
                value=pattern(half,page,offset);
                assert(transfer(&m,bytes,0xc0000U+offset,BM_BUS_WRITE,&value)==BM_STATUS_OK);
            }
        }
        /* Changes to the EMS window after filling cannot move shadow RAM. */
        write_port(&m,0x1ecU,2,0x200U);
        for(mask=0;mask<4;++mask) {
            write_port(&m,0x1efU,1,(uint16_t)(4U|context|(mask<<3)));
            for(half=0;half<2;++half) for(page=0;page<4;++page) for(high=0;high<2;++high)
            for(offset=0;offset<0x4000U;++offset) {
                uint32_t address=(half?0xe0000U:0xf0000U)+page*0x4000U+offset+high*0xf00000U;
                uint8_t expected=(mask&(half?1U:2U))?pattern(half,page,offset):0xa7U;
                value=0;
                assert(transfer(&m,bytes,address,BM_BUS_FETCH,&value)==BM_STATUS_OK && value==expected);
                value=(uint8_t)~expected;
                assert(transfer(&m,bytes,address,BM_BUS_WRITE,&value)==BM_STATUS_READ_ONLY);
                assert(transfer(&m,bytes,address,BM_BUS_READ,&value)==BM_STATUS_OK && value==expected);
            }
        }
        /* Reinitialization reveals ROM but retains both RAM halves. */
        assert(bm_gc103_memory_initialize(&m,mib*0x100000U)==BM_STATUS_OK);
        value=0;assert(transfer(&m,bytes,0xfffff0U,BM_BUS_FETCH,&value)==BM_STATUS_OK && value==0xa7U);
        write_port(&m,0x1efU,1,0x1cU);
        for(half=0;half<2;++half) for(page=0;page<4;++page) {
            value=0;
            assert(transfer(&m,bytes,(half?0xe0000U:0xf0000U)+page*0x4000U,
                            BM_BUS_READ,&value)==BM_STATUS_OK && value==pattern(half,page,0));
        }
        bm_pcs286_memory_destroy(bytes);
    }
}
int main(void)
{
    route_matrix();actual_bytes();
    printf("GC103 shadow: %lu routes and %lu completed backing transfers; literal manufacturer EMS pointers, read-only aliases and retention.\n",route_checks,byte_checks);
    return 0;
}
