/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Authored GC103 geometry tests from manufacturer 07-89 (01), pp2/5-7.
 * Synthetic storage and independent expectations; no ROM or hardware capture.
 */
#include "headland_at_memory.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned long routes_checked, transfers_checked;
static const uint32_t bank_sizes[2] = {0x80000U, 0x200000U};
static const uint32_t linear_sizes[2][4] = {
    {0x80000U, 0x100000U, 0x180000U, 0x200000U},
    {0x200000U, 0x400000U, 0x600000U, 0x800000U}
};
static void port(bm_gc103_memory_t *m, uint16_t p, unsigned width, unsigned v)
{
    uint16_t value = (uint16_t)v;
    assert(bm_gc103_memory_io(m,p,width,BM_BUS_WRITE,0,&value)==BM_STATUS_OK);
}
static void mr(bm_gc103_memory_t *m, unsigned slot, unsigned value)
{
    port(m,0x1eeU,1,slot); port(m,0x1ecU,2,value);
}
static bm_gc103_memory_config_t config(unsigned density, unsigned banks)
{
    bm_gc103_memory_config_t c = {0};
    c.dram = (bm_gc103_dram_t)density; c.installed_banks = banks;
    return c;
}
static void check(const bm_gc103_memory_t *m, uint32_t address,
                  bm_gc10x_requester_t requester, int a20, bm_bus_operation_t op,
                  bm_status_t status, bm_gc10x_memory_target_t target,
                  uint32_t offset, int writable)
{
    bm_gc10x_route_t r, before;
    uint32_t physical = requester == BM_GC10X_CPU && !a20 ? address & ~0x100000U : address;
    memset(&r,0xa5,sizeof(r)); memcpy(&before,&r,sizeof(r));
    assert(bm_gc103_memory_resolve(m,requester,a20,address,op,&r)==status);
    if (status != BM_STATUS_OK) assert(memcmp(&before,&r,sizeof(r))==0);
    else {
        assert(r.target==target && r.offset==offset && r.writable==writable);
        assert(r.wait_quality==BM_GC10X_WAIT_UNKNOWN);
        assert(r.contiguous_bytes==0x4000U-(physical%0x4000U));
        if (target==BM_GC10X_RAM) assert(offset+r.contiguous_bytes<=m->ram_bytes);
    }
    ++routes_checked;
}

/* Linear expectations use explicit population tables and memory regions.
 * EMS expectations below supply bank/page offsets directly, never get_address. */
static void linear_check(const bm_gc103_memory_t *m, unsigned physical_density,
                         unsigned installed, unsigned cr, uint32_t address,
                         bm_gc10x_requester_t requester, int a20, bm_bus_operation_t op)
{
    uint32_t a = requester==BM_GC10X_CPU && !a20 ? address&~0x100000U : address;
    unsigned selected_density = cr/128U, count = (cr/32U)%4U;
    uint32_t extent = linear_sizes[selected_density][count];
    uint32_t populated = linear_sizes[physical_density][installed-1U];
    uint32_t top = extent + ((cr&4U) ? 0U : 0x60000U), offset = a;
    bm_gc10x_memory_target_t target = BM_GC10X_EXTERNAL;
    bm_status_t status = BM_STATUS_OK;
    int writable = 1, ram = 0;
    if ((a>=0xe0000U && a<0x100000U) || a>=0xfe0000U) {
        unsigned enabled = a%0x20000U<0x10000U ? 8U : 16U;
        writable = 0;
        if ((cr&4U) && (cr&enabled)) {
            ram = 1; offset = (enabled==8U ? 0xf0000U : 0xe0000U)+(a%0x10000U);
        } else { target=BM_GC10X_FIRMWARE; offset=a%0x20000U; }
    } else if (a<0xa0000U) {
        ram=1;
        if (extent==0x80000U) status=BM_STATUS_UNSUPPORTED;
    } else if (a>=0x100000U && a<top) {
        ram=1; offset=a-((cr&4U) ? 0U : 0x60000U);
    }
    if (ram) {
        if (physical_density!=selected_density) status=BM_STATUS_UNSUPPORTED;
        target=offset<populated ? BM_GC10X_RAM : BM_GC10X_OPEN_BUS;
        if (target==BM_GC10X_OPEN_BUS) { offset=0; writable=0; }
    }
    check(m,address,requester,a20,op,status,target,offset,writable);
}

static void linear_matrix(void)
{
    bm_gc103_memory_t m, snapshot, isolated, isolated_before;
    unsigned density, installed, cr, page, edge, requester;
    assert(bm_gc103_memory_initialize(&isolated,0x300000U)==BM_STATUS_OK);
    memcpy(&isolated_before,&isolated,sizeof(isolated));
    for (density=0;density<2;++density) for (installed=1;installed<=4;++installed) {
        bm_gc103_memory_config_t c=config(density,installed);
        assert(bm_gc103_memory_initialize_configured(&m,&c)==BM_STATUS_OK);
        assert(m.physical_bank_bytes==bank_sizes[density]);
        assert(m.ram_bytes==linear_sizes[density][installed-1U]);
        /* Alternate large/small writes; never initialize between CR changes. */
        for (cr=0;cr<256;++cr) {
            port(&m,0x1efU,1,cr&1U ? 0U : 255U); port(&m,0x1efU,1,cr);
            memcpy(&snapshot,&m,sizeof(m));
            for (page=0;page<1024;++page) for (edge=0;edge<2;++edge) {
                uint32_t a=page*0x4000U+(edge ? 0x3fffU : 0U);
                for (requester=0;requester<3;++requester)
                    linear_check(&m,density,installed,cr,a,(bm_gc10x_requester_t)requester,
                                 (int)(page&1U),(bm_bus_operation_t)(page%3U));
            }
            assert(memcmp(&snapshot,&m,sizeof(m))==0);
        }
        assert(memcmp(&isolated,&isolated_before,sizeof(isolated))==0);
    }
}

static void ems_matrix(void)
{
    bm_gc103_memory_t m;
    unsigned density, installed, context, count, bank, page, slot;
    for (density=0;density<2;++density) for (installed=1;installed<=4;++installed) {
        bm_gc103_memory_config_t c=config(density,installed);
        assert(bm_gc103_memory_initialize_configured(&m,&c)==BM_STATUS_OK);
        for (context=0;context<2;++context) for (count=0;count<4;++count) {
            unsigned cr=density*128U+count*32U+context+6U;
            port(&m,0x1efU,1,cr);
            for (bank=0;bank<4;++bank) for (page=0;page<128;++page) {
                uint32_t offset=bank*bank_sizes[density]+(page%(density ? 128U : 32U))*0x4000U;
                for (slot=0;slot<32;++slot) {
                    uint32_t address=slot<24U ? 0x40000U+slot*0x4000U : 0xc0000U+(slot-24U)*0x4000U;
                    unsigned i;
                    mr(&m,context*32U+slot,512U+bank*128U+page);
                    /* Inactive writes cannot change live translation. */
                    mr(&m,(1U-context)*32U+slot,0U);
                    for (i=0;i<2;++i) {
                        uint32_t displacement=i ? 0x3fffU : 0U;
                        check(&m,address+displacement,BM_GC10X_CPU,1,(bm_bus_operation_t)(slot%3U),
                              BM_STATUS_OK,bank<installed ? BM_GC10X_RAM : BM_GC10X_OPEN_BUS,
                              bank<installed ? offset+displacement : 0U,bank<installed);
                    }
                    mr(&m,context*32U+slot,0U);
                    linear_check(&m,density,installed,cr,address,BM_GC10X_CPU,1,BM_BUS_READ);
                }
            }
        }
    }
}

static void invalid_and_fixed_pins(void)
{
    bm_gc103_memory_t m, before;
    bm_gc103_memory_config_t c=config(1,2);
    unsigned i, density;
    assert(bm_gc103_memory_initialize(&m,0x100000U)==BM_STATUS_OK);
    memcpy(&before,&m,sizeof(m));
    assert(bm_gc103_memory_initialize_configured(NULL,&c)==BM_STATUS_INVALID_ARGUMENT);
    assert(bm_gc103_memory_initialize_configured(&m,NULL)==BM_STATUS_INVALID_ARGUMENT);
    for (i=0;i<10;++i) {
        c=config(1,2);
        if (i==0) c.installed_banks=0;
        if (i==1) c.installed_banks=5;
        if (i==2) c.dram=(bm_gc103_dram_t)2;
        if (i==3) c.pins.ram1m=(bm_gc103_pin_level_t)-1;
        if (i==4) c.pins.ramsw1=BM_GC103_PIN_LOW;
        if (i==5) c.pins.ramsw2=BM_GC103_PIN_HIGH;
        if (i==6) c.pins.splsw=BM_GC103_PIN_LOW;
        if (i==7) c.pins.splsw=BM_GC103_PIN_HIGH;
        if (i==8) c.pins.ram1m=BM_GC103_PIN_HIGH;
        if (i==9) { c.dram=BM_GC103_DRAM_256K; c.pins.ram1m=BM_GC103_PIN_LOW; }
        assert(bm_gc103_memory_initialize_configured(&m,&c)==
               (i<4 ? BM_STATUS_INVALID_ARGUMENT : BM_STATUS_UNSUPPORTED));
        assert(memcmp(&before,&m,sizeof(m))==0);
    }
    for (density=0;density<2;++density) {
        c=config(density,4); c.pins.ram1m=density ? BM_GC103_PIN_LOW : BM_GC103_PIN_HIGH;
        assert(bm_gc103_memory_initialize_configured(&m,&c)==BM_STATUS_OK);
        c.installed_banks=1; /* Copied configuration, no retained pointer. */
        for (i=0;i<256;++i) {
            unsigned cr=(i&127U)+density*128U;
            uint16_t read=0;
            port(&m,0x1efU,1,i);
            memcpy(&before,&m,sizeof(m));
            assert(bm_gc103_memory_io(&m,0x1efU,1,BM_BUS_READ,1,&read)==BM_STATUS_OK && read==cr);
            assert(memcmp(&before,&m,sizeof(m))==0);
            linear_check(&m,density,4,cr,0x160000U,BM_GC10X_CPU,1,BM_BUS_READ);
        }
    }
}

static void programmed_transitions(void)
{
    bm_gc103_memory_t m, before;
    unsigned density, slot, cr;
    for (density=0;density<2;++density) {
        bm_gc103_memory_config_t c=config(density,4);
        assert(bm_gc103_memory_initialize_configured(&m,&c)==BM_STATUS_OK);
        for (slot=0;slot<32;++slot) {
            mr(&m,slot,0x200U+slot);       /* Bank 0, standard context. */
            mr(&m,slot+32U,0x380U+slot);  /* Bank 3, alternate context. */
        }
        for (cr=0;cr<128;++cr) {
            unsigned control=cr+density*128U;
            port(&m,0x1efU,1,control);
            for (slot=0;slot<32;++slot) {
                uint32_t a=slot<24U ? 0x40000U+slot*0x4000U : 0xc0000U+(slot-24U)*0x4000U;
                if (cr&2U) {
                    uint32_t offset=((cr&1U) ? 3U*bank_sizes[density] : 0U)+slot*0x4000U;
                    check(&m,a,BM_GC10X_CPU,1,BM_BUS_READ,BM_STATUS_OK,BM_GC10X_RAM,offset,1);
                    check(&m,a+0x100000U,BM_GC10X_CPU,0,BM_BUS_WRITE,BM_STATUS_OK,BM_GC10X_RAM,offset,1);
                } else linear_check(&m,density,4,control,a,BM_GC10X_CPU,1,BM_BUS_READ);
            }
        }
        port(&m,0x1efU,1,(1U-density)*128U+0x62U);
        check(&m,0x40000U,BM_GC10X_DMA,1,BM_BUS_READ,BM_STATUS_UNSUPPORTED,BM_GC10X_RAM,0,0);
        memcpy(&before,&m,sizeof(m));
        {
            uint16_t value=0xffU;
            assert(bm_gc103_memory_io(&m,0x1efU,1,BM_BUS_WRITE,1,&value)==BM_STATUS_READ_ONLY);
            assert(memcmp(&m,&before,sizeof(m))==0);
        }
        port(&m,0x1efU,1,density*128U+0x62U);
        check(&m,0x40000U,BM_GC10X_ISA_MASTER,0,BM_BUS_FETCH,BM_STATUS_OK,BM_GC10X_RAM,0,1);
    }
}

/* Bounded component storage supports 8MiB without relaxing PCS286 board's
 * separate capacity contract. This is an authored endpoint, not a board. */
typedef struct backing {
    uint8_t ram[0x800000U];
    uint32_t capacity;
    unsigned calls, effects, fail_at, after;
    bm_status_t failure;
} backing_t;
static backing_t backing;
static bm_status_t access_backing(void *context, bm_pcs286_memory_region_t region,
                                 uint32_t offset, bm_bus_transaction_t *t)
{
    backing_t *b=context;
    unsigned i;
    uint64_t value=0;
    assert(t->size==1U || t->size==2U);
    assert(t->size==1U || !(t->address&1U));
    assert(offset+t->size<=(region==BM_PCS286_MEMORY_RAM ? b->capacity : 0x20000U));
    ++b->calls;
    if (b->calls==b->fail_at && !b->after) return b->failure;
    for (i=0;i<t->size;++i) {
        unsigned shift=(t->endianness==BM_ENDIAN_LITTLE ? i : t->size-i-1U)*8U;
        if (t->operation==BM_BUS_WRITE) {
            assert(region==BM_PCS286_MEMORY_RAM);
            b->ram[offset+i]=(uint8_t)(t->value>>shift);
        } else value|=(uint64_t)(region==BM_PCS286_MEMORY_RAM ? b->ram[offset+i] : 0x5aU)<<shift;
    }
    if (t->operation!=BM_BUS_WRITE) t->value=value;
    if (!(t->attributes&BM_BUS_TRANSACTION_DEBUG)) { ++b->effects; t->wait_states=1; }
    return b->calls==b->fail_at ? b->failure : BM_STATUS_OK;
}
static bm_at_transfer_t transfer(uint32_t address, unsigned size, bm_bus_operation_t op)
{
    bm_at_transfer_t t={0};
    t.master=BM_AT_MASTER_CPU; t.requester_clock=(bm_clock_rate_t){8000000U,1};
    t.bus.space=BM_ADDRESS_MEMORY; t.bus.operation=op; t.bus.address=address;
    t.bus.size=size; t.bus.alignment=1; t.bus.value=UINT64_C(0x8877665544332211);
    return t;
}
static void adapter_checks(void)
{
    bm_gc103_memory_t m, snapshot;
    bm_gc103_memory_config_t geometry=config(1,1);
    bm_headland_at_memory_t at, before;
    bm_headland_at_config_t c={0};
    bm_at_transfer_t t;
    unsigned i, after, op;
    assert(bm_gc103_memory_initialize_configured(&m,&geometry)==BM_STATUS_OK);
    c.profile=BM_HEADLAND_AT_CONFIGURED_GC103; c.routes=&m;
    c.backing=access_backing; c.backing_context=&backing; c.external_width=1;
    c.timing=BM_HEADLAND_AT_PROVISIONAL_SERVICE_CLOCK;
    c.service_clock=(bm_clock_rate_t){8000000U,1}; c.cpu_a20=1;
    backing.capacity=0x200000U;
    assert(bm_headland_at_memory_initialize(&at,&c)==BM_STATUS_OK);
    memcpy(&before,&at,sizeof(at)); c.profile=BM_HEADLAND_AT_LEGACY_GC103;
    assert(bm_headland_at_memory_initialize(&at,&c)==BM_STATUS_INVALID_ARGUMENT);
    assert(memcmp(&before,&at,sizeof(at))==0); c.profile=BM_HEADLAND_AT_CONFIGURED_GC103;
    /* Floating reset selects 256K, which differs from installed 1M DRAM.
     * ROM fetch and control I/O work; RAM fails before any endpoint call. */
    t=transfer(0xfffff0U,2,BM_BUS_FETCH);
    assert(bm_headland_at_memory_access(&at,&t)==BM_STATUS_OK && t.bus.value==0x5a5aU);
    backing.calls=backing.effects=0; t=transfer(0,8,BM_BUS_READ);
    assert(bm_headland_at_memory_access(&at,&t)==BM_STATUS_UNSUPPORTED);
    assert(backing.calls==0 && t.bus.value==UINT64_C(0x8877665544332211));
    /* Program four selected banks, only one installed. Boundary plan must
     * reject a crossing write before touching the populated first bytes. */
    port(&m,0x1efU,1,0xe4U);
    t=transfer(0x1ffffcU,8,BM_BUS_WRITE);
    assert(bm_headland_at_memory_access(&at,&t)==BM_STATUS_UNMAPPED && backing.calls==0);
    at.config.holes=BM_HEADLAND_AT_HOLES_FF;
    assert(bm_headland_at_memory_access(&at,&t)==BM_STATUS_OK && backing.calls==2);
    assert(backing.ram[0x1ffffcU]==0x11U && backing.ram[0x1fffffU]==0x44U);
    t=transfer(0x1ffffcU,8,BM_BUS_READ);
    assert(bm_headland_at_memory_access(&at,&t)==BM_STATUS_OK);
    assert(t.bus.value==UINT64_C(0xffffffff44332211)); ++transfers_checked;
    /* Live CPU-only A20, real backing through EMS, context switching,
     * and documented read-only shadow sources. */
    backing.ram[0]=0x31U; backing.ram[0x100000U]=0x42U;
    assert(bm_headland_at_memory_a20(&at,0)==BM_STATUS_OK);
    t=transfer(0x100000U,1,BM_BUS_READ);
    assert(bm_headland_at_memory_access(&at,&t)==BM_STATUS_OK && t.bus.value==0x31U);
    t=transfer(0x100000U,1,BM_BUS_READ); t.master=BM_AT_MASTER_DMA8;
    assert(bm_headland_at_memory_access(&at,&t)==BM_STATUS_OK && t.bus.value==0x42U);
    assert(bm_headland_at_memory_a20(&at,1)==BM_STATUS_OK);
    port(&m,0x1efU,1,0x82U); mr(&m,0,0x240U); mr(&m,32,0x200U);
    t=transfer(0x40000U,1,BM_BUS_READ);
    assert(bm_headland_at_memory_access(&at,&t)==BM_STATUS_OK && t.bus.value==0x42U);
    port(&m,0x1efU,1,0x83U); t=transfer(0x40000U,1,BM_BUS_READ);
    assert(bm_headland_at_memory_access(&at,&t)==BM_STATUS_OK && t.bus.value==0x31U);
    backing.ram[0xe0000U]=0x67U; backing.ram[0xf0000U]=0x89U;
    port(&m,0x1efU,1,0x9cU); t=transfer(0xfe0000U,1,BM_BUS_FETCH);
    assert(bm_headland_at_memory_access(&at,&t)==BM_STATUS_OK && t.bus.value==0x89U);
    t=transfer(0xff0000U,1,BM_BUS_FETCH);
    assert(bm_headland_at_memory_access(&at,&t)==BM_STATUS_OK && t.bus.value==0x67U);
    backing.calls=0; t=transfer(0xff0000U,1,BM_BUS_WRITE);
    assert(bm_headland_at_memory_access(&at,&t)==BM_STATUS_READ_ONLY && backing.calls==0);
    port(&m,0x1efU,1,0x64U); t=transfer(0,1,BM_BUS_READ);
    assert(bm_headland_at_memory_access(&at,&t)==BM_STATUS_UNSUPPORTED && backing.calls==0);
    port(&m,0x1efU,1,0xe4U); t=transfer(0,1,BM_BUS_READ);
    assert(bm_headland_at_memory_access(&at,&t)==BM_STATUS_OK && t.bus.value==0x31U);
    transfers_checked+=9;
    /* Every endpoint failure, before/after effects, all read/write/fetch
     * fragments: no retries, no FF substitution of installed-host errors. */
    for (op=0;op<3;++op) for (after=0;after<2;++after) for (i=1;i<=4;++i) {
        static const bm_status_t failures[]={BM_STATUS_DEVICE_ERROR,BM_STATUS_UNMAPPED,
            BM_STATUS_READ_ONLY,BM_STATUS_OUT_OF_MEMORY};
        unsigned j;
        memset(backing.ram,0xccU,8); backing.calls=backing.effects=0;
        backing.fail_at=i; backing.after=after; backing.failure=failures[i-1U];
        t=transfer(0,8,(bm_bus_operation_t)op);
        assert(bm_headland_at_memory_access(&at,&t)==backing.failure);
        assert(backing.calls==i && backing.effects==i-1U+after);
        assert(at.last.completed_bytes==2U*(i-1U) && at.last.attempted_bytes==2U*i);
        assert(t.bus.value==UINT64_C(0x8877665544332211) && t.bus.wait_states==0);
        if (op==BM_BUS_WRITE) for (j=0;j<8;++j)
            assert(backing.ram[j]==(j<2U*(i-1U+after) ? (uint8_t)((j+1U)*0x11U) : 0xccU));
        ++transfers_checked;
    }
    backing.fail_at=0; backing.calls=backing.effects=0;
    at.config.timing=BM_HEADLAND_AT_STRICT;
    t=transfer(0,2,BM_BUS_READ);
    assert(bm_headland_at_memory_access(&at,&t)==BM_STATUS_UNSUPPORTED && backing.calls==0);
    t.bus.attributes=BM_BUS_TRANSACTION_DEBUG;
    memcpy(&before,&at,sizeof(at)); memcpy(&snapshot,&m,sizeof(m));
    assert(bm_headland_at_memory_access(&at,&t)==BM_STATUS_OK && t.bus.wait_states==0);
    assert(memcmp(&before,&at,sizeof(at))==0 && memcmp(&snapshot,&m,sizeof(m))==0);
    assert(backing.effects==0);
    /* 8MiB component extent and reinitialize: caller-owned bytes retained. */
    geometry.installed_banks=4;
    assert(bm_gc103_memory_initialize_configured(&m,&geometry)==BM_STATUS_OK);
    backing.capacity=0x800000U; backing.ram[0x7fffffU]=0x73U;
    port(&m,0x1efU,1,0xe4U); t=transfer(0x7fffffU,1,BM_BUS_READ);
    t.bus.attributes=BM_BUS_TRANSACTION_DEBUG;
    assert(bm_headland_at_memory_access(&at,&t)==BM_STATUS_OK && t.bus.value==0x73U);
    port(&m,0x1efU,1,0x84U); t=transfer(0x7fffffU,1,BM_BUS_READ);
    t.bus.attributes=BM_BUS_TRANSACTION_DEBUG;
    assert(bm_headland_at_memory_access(&at,&t)==BM_STATUS_OK && t.bus.value==0xffU);
    port(&m,0x1efU,1,0x86U); mr(&m,0,0x3ffU);
    t=transfer(0x43fffU,1,BM_BUS_READ); t.bus.attributes=BM_BUS_TRANSACTION_DEBUG;
    assert(bm_headland_at_memory_access(&at,&t)==BM_STATUS_OK && t.bus.value==0x73U);
    port(&m,0x1efU,1,0xe4U); t=transfer(0x7fffffU,1,BM_BUS_READ);
    t.bus.attributes=BM_BUS_TRANSACTION_DEBUG;
    assert(bm_headland_at_memory_access(&at,&t)==BM_STATUS_OK && t.bus.value==0x73U);
    assert(bm_gc103_memory_initialize_configured(&m,&geometry)==BM_STATUS_OK);
    assert(backing.ram[0x7fffffU]==0x73U);
    transfers_checked+=4;
}

int main(void)
{
    linear_matrix(); ems_matrix(); invalid_and_fixed_pins(); programmed_transitions(); adapter_checks();
    printf("GC103 configured geometry: %lu routes, %lu transfer checks\n",routes_checked,transfers_checked);
    return 0;
}
