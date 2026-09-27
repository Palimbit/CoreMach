/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Authored route/oracle harness. Generated classic code preserves its notices.
 */
#include "legacy_gc103_memory.h"
#include <blumach/systems/pcs286_memory.h>
#include <blumach/platforms/null_host.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Test-only adapter for classic mapping publication. It records windows and
 * memory policy; it never calls the portable implementation to build an oracle.
 * set_addr enables a mapping, as src/mem/mem.c:mem_mapping_set_addr does. */
typedef struct mem_mapping_t { uint32_t base, size; int enable; uint8_t *exec; } mem_mapping_t;
typedef struct headland_mr_t { uint8_t valid, enabled; uint16_t mr; uint32_t virt_base; } headland_mr_t;
typedef struct headland_t {
    uint8_t revision, has_cri, has_sleep, supports_386_banks, cri, cr[7], ems_mar;
    headland_mr_t null_mr, ems_mr[64];
    mem_mapping_t low_mapping, mid_mapping, high_mapping, upper_mapping[24],
                  shadow_mapping[2], ems_mapping[64];
} headland_t;
enum { MEM_READ_INTERNAL=1, MEM_WRITE_INTERNAL=2,
       MEM_READ_EXTANY=4, MEM_WRITE_EXTANY=8, MEM_READ_ROMCS=16,
       MEM_WRITE_ROMCS=32, MEM_WRITE_DISABLED=64,
       MEM_READ_EXTERNAL=4, MEM_WRITE_EXTERNAL=8 };
static uint32_t mem_size;
static uint8_t ram[0x400000];
static unsigned classic_access[1024];
static void mem_mapping_set_exec(mem_mapping_t *m, uint8_t *p) { m->exec=p; }
static void mem_mapping_disable(mem_mapping_t *m) { m->enable=0; }
static void mem_mapping_enable(mem_mapping_t *m) { m->enable=1; }
static void mem_mapping_set_addr(mem_mapping_t *m, uint32_t base, uint32_t size)
{ m->base=base; m->size=size; m->enable=1; }
static void mem_set_mem_state(uint32_t base, uint32_t size, unsigned state)
{
    uint32_t i;
    assert((base & 0x3fffU)==0 && (size & 0x3fffU)==0 && base+size<=0x1000000U);
    for(i=base>>14; i<(base+size)>>14; ++i) classic_access[i]=state;
}
#define flushmmucache() ((void)0)
#define headland_log(...) ((void)0)
/* The unused legacy parameter remains syntactically unchanged. */
#define UNUSED(x) x
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4100)
#endif
#include "headland_classic_memory.inc"
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

static unsigned long queries, documented_differences, shadow_differences;
static void classic_initialize(headland_t *c, unsigned mib)
{
    unsigned i;
    memset(c,0,sizeof(*c));
    mem_size=mib*1024U;
    for(i=0;i<1024;++i) classic_access[i]=MEM_READ_EXTANY|MEM_WRITE_EXTANY;
    mem_set_mem_state(0,0xa0000U,MEM_READ_INTERNAL|MEM_WRITE_INTERNAL);
    mem_set_mem_state(0x100000U,(mem_size-1024U)*1024U,MEM_READ_INTERNAL|MEM_WRITE_INTERNAL);
    mem_mapping_set_addr(&c->low_mapping,0,0x40000U);
    mem_mapping_set_addr(&c->mid_mapping,0xa0000U,0x60000U);
    mem_mapping_disable(&c->mid_mapping);
    if(mib>1) mem_mapping_set_addr(&c->high_mapping,0x100000U,(mib-1U)*0x100000U);
    for(i=0;i<24;++i) mem_mapping_set_addr(&c->upper_mapping[i],0x40000U+(i<<14),0x4000U);
    mem_mapping_set_addr(&c->shadow_mapping[0],0xe0000U,0x20000U);
    mem_mapping_set_addr(&c->shadow_mapping[1],0xfe0000U,0x20000U);
    mem_mapping_disable(&c->shadow_mapping[0]);
    mem_mapping_disable(&c->shadow_mapping[1]);
    for(i=0;i<64;++i) {
        c->ems_mr[i].valid=1;
        mem_mapping_set_addr(&c->ems_mapping[i],((i&31U)+((i&31U)>=24?24U:16U))<<14,0x4000U);
        mem_mapping_disable(&c->ems_mapping[i]);
    }
    memmap_state_update(c);
}
static void start(bm_gc103_memory_t *m, headland_t *c, unsigned mib)
{
    assert(bm_gc103_memory_initialize(m,mib*0x100000U)==BM_STATUS_OK);
    classic_initialize(c,mib);
}
static void write_port(bm_gc103_memory_t *m, headland_t *c, uint16_t port, unsigned width, uint16_t value)
{
    unsigned i;
    uint16_t before=value;
    assert(bm_gc103_memory_io(m,port,width,BM_BUS_WRITE,0,&value)==BM_STATUS_OK);
    assert(value==before);
    if(width==1) hl_write(port,(uint8_t)value,c); else hl_writew(port,value,c);
    assert(m->registers.cr0==c->cr[0] && m->registers.mar==c->ems_mar);
    for(i=0;i<64;++i) assert(m->registers.ems[i]==c->ems_mr[i].mr);
}
static int includes(const mem_mapping_t *w,uint32_t a)
{ return w->enable && a>=w->base && a-w->base<w->size; }
static bm_gc10x_route_t oracle(headland_t *c,uint32_t a)
{
    bm_gc10x_route_t r={0};
    headland_mr_t *mr=&c->null_mr;
    int i,found=0;
    unsigned access=classic_access[a>>14];
    r.contiguous_bytes=0x4000U-(a&0x3fffU);
    r.wait_quality=BM_GC10X_WAIT_UNKNOWN;
    r.target=BM_GC10X_OPEN_BUS;
    if(access & MEM_READ_ROMCS) { r.target=BM_GC10X_FIRMWARE; r.offset=a&0x1ffffU; return r; }
    if(access & MEM_READ_EXTANY) { r.target=BM_GC10X_EXTERNAL; r.offset=a; r.writable=1; return r; }
    for(i=63;i>=0;--i) if(includes(&c->ems_mapping[i],a)) {mr=&c->ems_mr[i]; found=1; break;}
    if(!found) {
        found=includes(&c->shadow_mapping[1],a)||includes(&c->shadow_mapping[0],a);
        for(i=23;!found && i>=0;--i) found=includes(&c->upper_mapping[i],a);
        found=found||includes(&c->high_mapping,a)||includes(&c->mid_mapping,a)||includes(&c->low_mapping,a);
    }
    if(found) {
        r.offset=get_addr(c,a,mr);
        if(r.offset<mem_size*1024U) { r.target=BM_GC10X_RAM; r.writable=!(access & MEM_WRITE_DISABLED); }
        else r.offset=0;
    }
    return r;
}
static void equal_route(bm_gc10x_route_t a,bm_gc10x_route_t b)
{
    assert(a.target==b.target && a.offset==b.offset && a.writable==b.writable);
    assert(a.contiguous_bytes==b.contiguous_bytes && a.contiguous_bytes>0);
    assert(a.wait_quality==b.wait_quality && a.extra_memory_clocks==b.extra_memory_clocks);
}
/* GC103 07-89 (01), pp2/3/5/7: memory uses CR.D0, independently of
 * MAR.D5 used for I/O. Keep the raw classic oracle intact and expose its
 * known upper-window artifact; never patch/rebuild the reference to hide it.
 * Shadow pointer table p6 independently qualifies the E/F backing selection.
 * Outside these two corrections the reference remains the expected route. */
static bm_gc10x_route_t documented_oracle(headland_t *c,uint32_t a)
{
    bm_gc10x_route_t raw=oracle(c,a), expected=raw;
    unsigned page, slot, mr, bank_bytes, page_mask;
    if(raw.target==BM_GC10X_RAM && !raw.writable &&
       ((a>=0xe0000U && a<=0xfffffU) || a>=0xfe0000U)) {
        /* Literal MR page values from p6 resolve to E0000 for F shadow and
         * F0000 for E shadow in both documented DRAM geometries. */
        expected.offset=(a&0xffffU)+(a&0x10000U?0xe0000U:0xf0000U);
        assert(raw.offset==(a&0xfffffU));
        ++shadow_differences;
        return expected;
    }
    if(!(c->cr[0]&2U)) return raw;
    if(a>=0x40000U && a<0xa0000U) page=(a-0x40000U)/0x4000U;
    else if(a>=0xc0000U && a<0xe0000U) page=24U+(a-0xc0000U)/0x4000U;
    else return raw;
    slot=page+32U*(c->cr[0]&1U);
    mr=c->ems_mr[slot].mr;
    if(!(mr&0x200U)) return raw;
    bank_bytes=(c->cr[0]&0x80U)?0x200000U:0x80000U;
    page_mask=(c->cr[0]&0x80U)?127U:31U;
    expected.offset=((mr/128U)%4U)*bank_bytes+(mr&page_mask)*0x4000U+a%0x4000U;
    expected.target=expected.offset<mem_size*1024U?BM_GC10X_RAM:BM_GC10X_OPEN_BUS;
    expected.writable=expected.target==BM_GC10X_RAM;
    if(expected.target==BM_GC10X_OPEN_BUS) expected.offset=0;
    if(raw.target!=expected.target) {
        assert(page>=24U && raw.target==BM_GC10X_EXTERNAL);
        ++documented_differences;
    } else equal_route(raw,expected);
    return expected;
}
static bm_gc10x_route_t compare(bm_gc103_memory_t *m,headland_t *c,uint32_t a)
{
    bm_gc10x_route_t got,expected=documented_oracle(c,a);
    assert(bm_gc103_memory_resolve(m,BM_GC10X_CPU,1,a,BM_BUS_READ,&got)==BM_STATUS_OK);
    equal_route(got,expected);
    ++queries;
    return got;
}
static void grid(bm_gc103_memory_t *m,headland_t *c)
{
    uint32_t a;
    for(a=0;a<0x1000000U;a+=0x4000U) {
        compare(m,c,a); compare(m,c,a+0x123U); compare(m,c,a+0x3fffU);
    }
}
static void matrices(void)
{
    bm_gc103_memory_t m;
    headland_t c;
    unsigned mib,cr,word,bank,i;
    for(mib=1;mib<=4;++mib) {
        start(&m,&c,mib); grid(&m,&c);
        for(cr=0;cr<32;++cr) {
            write_port(&m,&c,0x1efU,1,(uint16_t)cr);
            grid(&m,&c);
        }
        for(bank=0;bank<2;++bank) {
            write_port(&m,&c,0x1efU,1,(uint16_t)(2U|bank));
            for(word=0;word<65536U;++word) {
                unsigned slot=(word&31U)|(bank<<5);
                uint32_t base=((slot&31U)+((slot&31U)>=24U?24U:16U))<<14;
                write_port(&m,&c,0x1eeU,1,(uint16_t)slot);
                write_port(&m,&c,0x1ecU,2,(uint16_t)word);
                compare(&m,&c,base); compare(&m,&c,base+0x3fffU);
            }
        }
        /* Reconfigure registers and maps in arbitrary chronological order. */
        for(i=0;i<512;++i) {
            uint16_t mar=(uint16_t)((i*19U)&255U);
            write_port(&m,&c,0x1efU,1,(uint16_t)((i*13U)&31U));
            write_port(&m,&c,0x1eeU,1,mar);
            write_port(&m,&c,0x1ecU,(i&1U)+1U,(uint16_t)(i&255U));
            grid(&m,&c);
        }
    }
}

static bm_gc10x_route_t resolve(bm_gc103_memory_t *m,bm_gc10x_requester_t who,int a20,uint32_t a,bm_bus_operation_t op)
{
    bm_gc10x_route_t r;
    assert(bm_gc103_memory_resolve(m,who,a20,a,op,&r)==BM_STATUS_OK);
    return r;
}
static void special_sequences(void)
{
    bm_gc103_memory_t m,isolated,before,isolation_before;
    headland_t c;
    uint32_t a;
    uint16_t value;
    bm_gc10x_route_t r,sentinel={BM_GC10X_EXTERNAL,17U,19U,23U,1,BM_GC10X_WAIT_PROVISIONAL};
    unsigned op,who;
    start(&m,&c,4);
    assert(bm_gc103_memory_initialize(&isolated,0x100000U)==BM_STATUS_OK);
    memcpy(&isolation_before,&isolated,sizeof(isolation_before));
    /* Reset/raw CR0 and first write differ in bank geometry on 4MiB. */
    write_port(&m,&c,0x1eeU,1,0);
    write_port(&m,&c,0x1ecU,2,0x280U);
    write_port(&m,&c,0x1efU,1,2);
    r=compare(&m,&c,0x40000U); assert(r.target==BM_GC10X_RAM && r.offset==0x200000U);
    /* The unchanged classic oracle still demonstrates the artifact, while
     * the portable result follows documented memory-context selection. */
    write_port(&m,&c,0x1eeU,1,24); write_port(&m,&c,0x1ecU,2,0x200U);
    r=compare(&m,&c,0xc0000U); assert(r.target==BM_GC10X_RAM);
    write_port(&m,&c,0x1eeU,1,56); write_port(&m,&c,0x1ecU,2,0);
    assert(oracle(&c,0xc0000U).target==BM_GC10X_EXTERNAL);
    r=compare(&m,&c,0xc0000U); assert(r.target==BM_GC10X_RAM && r.offset==0);
    write_port(&m,&c,0x1efU,1,2);
    r=compare(&m,&c,0xc0000U); assert(r.target==BM_GC10X_RAM);
    /* Inactive lower-bank write must leave the active higher-priority EMS
     * mapping effective, despite reenabling the ordinary upper window. */
    write_port(&m,&c,0x1eeU,1,32); write_port(&m,&c,0x1ecU,2,0);
    r=compare(&m,&c,0x40000U); assert(r.offset==0x200000U);
    for(who=0;who<3;++who) for(op=0;op<3;++op) {
        for(a=0;a<0x1000000U;a+=0x3fffU) {
            r=resolve(&m,(bm_gc10x_requester_t)who,0,a,(bm_bus_operation_t)op);
            equal_route(r,documented_oracle(&c,who==0?(a & ~0x100000U):a));
            ++queries;
        }
    }
    /* Query and debug I/O must not change the model or another instance. */
    memcpy(&before,&m,sizeof(before));
    value=0;
    assert(bm_gc103_memory_io(&m,0x1ecU,2,BM_BUS_READ,1,&value)==BM_STATUS_OK);
    assert(memcmp(&m,&before,sizeof(m))==0);
    r=sentinel;
    assert(bm_gc103_memory_resolve(&m,BM_GC10X_CPU,1,0x1000000U,BM_BUS_READ,&r)==BM_STATUS_INVALID_ARGUMENT);
    equal_route(r,sentinel);
    assert(bm_gc103_memory_resolve(NULL,BM_GC10X_CPU,1,0,BM_BUS_READ,&r)==BM_STATUS_INVALID_ARGUMENT);
    assert(bm_gc103_memory_resolve(&m,(bm_gc10x_requester_t)3,1,0,BM_BUS_READ,&r)==BM_STATUS_INVALID_ARGUMENT);
    assert(bm_gc103_memory_resolve(&m,BM_GC10X_CPU,2,0,BM_BUS_READ,&r)==BM_STATUS_INVALID_ARGUMENT);
    assert(bm_gc103_memory_resolve(&m,BM_GC10X_CPU,1,0,(bm_bus_operation_t)3,&r)==BM_STATUS_INVALID_ARGUMENT);
    assert(bm_gc103_memory_resolve(&m,BM_GC10X_CPU,1,0,BM_BUS_READ,NULL)==BM_STATUS_INVALID_ARGUMENT);
    assert(bm_gc103_memory_initialize(&m,0x180000U)==BM_STATUS_INVALID_ARGUMENT);
    assert(bm_gc103_memory_io(&m,0x1ecU,2,BM_BUS_WRITE,1,&value)==BM_STATUS_READ_ONLY);
    assert(bm_gc103_memory_io(&m,0x1ebU,1,BM_BUS_READ,0,&value)==BM_STATUS_UNMAPPED);
    assert(bm_gc103_memory_io(&m,0x1ecU,4,BM_BUS_READ,0,&value)==BM_STATUS_INVALID_ARGUMENT);
    assert(memcmp(&m,&before,sizeof(m))==0);
    r=resolve(&isolated,BM_GC10X_CPU,1,0x40000U,BM_BUS_READ);
    assert(r.target==BM_GC10X_RAM && r.offset==0x40000U);
    assert(memcmp(&isolated,&isolation_before,sizeof(isolated))==0);
}

/* Authored storage fixture: actual portable backing memory, synthetic ROM.
 * No production board adapter/clock policy or firmware is smuggled in. */
static bm_status_t backing(bm_gc103_memory_t *map,bm_pcs286_memory_t *bytes,
                            uint32_t address,bm_bus_operation_t op,uint8_t *value)
{
    bm_gc10x_route_t r=resolve(map,BM_GC10X_CPU,1,address,op);
    bm_bus_transaction_t t={0};
    bm_status_t status;
    if(r.target==BM_GC10X_EXTERNAL || r.target==BM_GC10X_OPEN_BUS) return BM_STATUS_UNMAPPED;
    if(op==BM_BUS_WRITE && !r.writable) return BM_STATUS_READ_ONLY;
    t.space=BM_ADDRESS_MEMORY; t.operation=op; t.address=address;
    t.size=1; t.value=*value; t.wait_states=17;
    status=bm_pcs286_memory_access(bytes,r.target==BM_GC10X_RAM?BM_PCS286_MEMORY_RAM:BM_PCS286_MEMORY_ROM,r.offset,&t);
    assert(t.wait_states==17); /* No fabricated known timing. */
    if(status==BM_STATUS_OK) *value=(uint8_t)t.value;
    return status;
}
static void real_backing(void)
{
    static uint8_t image[BM_PCS286_FIRMWARE_BYTES];
    bm_host_services_t host;
    bm_pcs286_firmware_t firmware={0};
    bm_pcs286_memory_t *bytes=NULL;
    bm_gc103_memory_t m;
    headland_t c;
    uint8_t value;
    uint32_t i;
    for(i=0;i<sizeof(image);++i) image[i]=(uint8_t)(i^(i>>8));
    host=bm_null_host_services();
    firmware.image[0].data=image; firmware.image[0].size=sizeof(image);
    assert(bm_pcs286_memory_create(&host,0x100000U,&firmware,&bytes)==BM_STATUS_OK);
    start(&m,&c,1);
    value=0xa5U; assert(backing(&m,bytes,0x100000U,BM_BUS_WRITE,&value)==BM_STATUS_OK);
    value=0; assert(backing(&m,bytes,0x100000U,BM_BUS_READ,&value)==BM_STATUS_OK && value==0xa5U);
    /* Enable EMS to the same A0000 backing; no permanent 60000/80000 alias. */
    write_port(&m,&c,0x1efU,1,2); write_port(&m,&c,0x1eeU,1,0);
    /* Bank1 (80000) + page8 (20000), with enable bit9. */
    write_port(&m,&c,0x1ecU,2,0x288U);
    value=0; assert(backing(&m,bytes,0x40000U,BM_BUS_READ,&value)==BM_STATUS_OK && value==0xa5U);
    /* Stage an alternate upper-window map without disturbing current bytes.
     * Actual writes reach only the context chosen by CR0, not by MAR. */
    write_port(&m,&c,0x1eeU,1,24); write_port(&m,&c,0x1ecU,2,0x288U);
    write_port(&m,&c,0x1eeU,1,56); write_port(&m,&c,0x1ecU,2,0x28cU);
    value=0; assert(backing(&m,bytes,0xc0000U,BM_BUS_READ,&value)==BM_STATUS_OK && value==0xa5U);
    value=0x5aU; assert(backing(&m,bytes,0xc0000U,BM_BUS_WRITE,&value)==BM_STATUS_OK);
    value=0; assert(backing(&m,bytes,0x100000U,BM_BUS_READ,&value)==BM_STATUS_OK && value==0x5aU);
    write_port(&m,&c,0x1efU,1,3);
    value=0; assert(backing(&m,bytes,0xc0000U,BM_BUS_READ,&value)==BM_STATUS_OK && value==0);
    value=0x72U; assert(backing(&m,bytes,0xc0000U,BM_BUS_WRITE,&value)==BM_STATUS_OK);
    write_port(&m,&c,0x1efU,1,2);
    value=0; assert(backing(&m,bytes,0xc0000U,BM_BUS_READ,&value)==BM_STATUS_OK && value==0x5aU);
    value=0; assert(backing(&m,bytes,0x110000U,BM_BUS_READ,&value)==BM_STATUS_OK && value==0x72U);
    value=0x61U; assert(backing(&m,bytes,0x60000U,BM_BUS_WRITE,&value)==BM_STATUS_OK);
    value=0; assert(backing(&m,bytes,0x80000U,BM_BUS_READ,&value)==BM_STATUS_OK && value==0);
    value=0; assert(backing(&m,bytes,0xfffff0U,BM_BUS_FETCH,&value)==BM_STATUS_OK && value==image[0x1fff0U]);
    value=0x42U; assert(backing(&m,bytes,0xe0000U,BM_BUS_WRITE,&value)==BM_STATUS_READ_ONLY);
    /* p6: E shadow uses backing F0000 (relocated to 150000 while CR2=0). */
    value=0x42U; assert(backing(&m,bytes,0x150000U,BM_BUS_WRITE,&value)==BM_STATUS_OK);
    write_port(&m,&c,0x1efU,1,0x1cU);
    value=0; assert(backing(&m,bytes,0xfe0000U,BM_BUS_READ,&value)==BM_STATUS_OK && value==0x42U);
    value=0x99U; assert(backing(&m,bytes,0xfe0000U,BM_BUS_WRITE,&value)==BM_STATUS_READ_ONLY);
    assert(bm_gc103_memory_initialize(&m,0x100000U)==BM_STATUS_OK);
    value=0; assert(backing(&m,bytes,0x150000U,BM_BUS_READ,&value)==BM_STATUS_OK && value==0x42U);
    bm_pcs286_memory_destroy(bytes);
}
int main(void)
{
    (void)hl_read; (void)hl_readw; (void)hl_readl; (void)hl_writel;
    matrices(); special_sequences(); real_backing();
    assert(documented_differences>0);
    assert(shadow_differences>0);
    printf("Headland memory: %lu route checks, %lu context and %lu shadow divergences from unchanged classic; A20, bounds and real backing checks.\n",queries,documented_differences,shadow_differences);
    return 0;
}
