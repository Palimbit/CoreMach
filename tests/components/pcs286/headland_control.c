/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Authored CR readback tests from Headland GC103 07-89 (01), pp5-7.
 * No classic source or production resolver supplies expected values.
 */
#include "legacy_gc103_memory.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned long reads;

static uint8_t expected(const bm_gc103_straps_t *p,unsigned software)
{
    /* Rows FLOATING/LOW/HIGH, columns software0/software1. D7 replaces
     * software when tied; D6/D5/D2 readback ORs the inverse input. */
    static const unsigned type[3][2]={{0,1},{1,1},{0,0}};
    static const unsigned combined[3][2]={{0,1},{1,1},{0,1}};
    return (uint8_t)((software&0x1bU) |
        (type[p->ram1m][(software>>7)&1U]<<7) |
        (combined[p->ramsw2][(software>>6)&1U]<<6) |
        (combined[p->ramsw1][(software>>5)&1U]<<5) |
        (combined[p->splsw][(software>>2)&1U]<<2));
}

static void check_read(bm_gc103_registers_t *r,uint8_t want,int debug)
{
    bm_gc103_registers_t before;
    bm_gc103_register_effect_t effect={BM_GC103_MAPPING_ALL,73};
    uint16_t value=0xdeadU;
    memcpy(&before,r,sizeof(before));
    assert(bm_gc103_registers_access(r,0x1efU,1,BM_BUS_READ,debug,&value,&effect)==BM_STATUS_OK);
    assert(value==want && r->cr0==want);
    assert(effect.mapping==BM_GC103_MAPPING_UNCHANGED && effect.slot==0);
    assert(memcmp(r,&before,sizeof(before))==0);
    ++reads;
}
static void write_cr(bm_gc103_registers_t *r,uint16_t value)
{
    bm_gc103_register_effect_t effect={BM_GC103_MAPPING_UNCHANGED,73};
    uint16_t before=value;
    uint16_t ems[64];
    uint8_t mar=r->mar;
    memcpy(ems,r->ems,sizeof(ems));
    assert(bm_gc103_registers_access(r,0x1efU,1,BM_BUS_WRITE,0,&value,&effect)==BM_STATUS_OK);
    assert(value==before && r->cr_written==value);
    assert(effect.mapping==BM_GC103_MAPPING_ALL && effect.slot==0);
    assert(r->mar==mar && memcmp(ems,r->ems,sizeof(ems))==0);
}

static void matrix(void)
{
    bm_gc103_registers_t r,isolated,isolated_before;
    bm_gc103_straps_t pins;
    unsigned a,b,c,d,value;
    assert(bm_gc103_registers_initialize(&isolated,0x300000U)==BM_STATUS_OK);
    memcpy(&isolated_before,&isolated,sizeof(isolated));
    for(a=0;a<3;++a) for(b=0;b<3;++b) for(c=0;c<3;++c) for(d=0;d<3;++d) {
        pins.ram1m=(bm_gc103_pin_level_t)a; pins.ramsw1=(bm_gc103_pin_level_t)b;
        pins.ramsw2=(bm_gc103_pin_level_t)c; pins.splsw=(bm_gc103_pin_level_t)d;
        assert(bm_gc103_registers_initialize_strapped(&r,&pins)==BM_STATUS_OK);
        assert(r.control_profile==BM_GC103_CONTROL_STRAP_READBACK && r.cr_written==0);
        check_read(&r,expected(&pins,0),0);
        /* Sentinel EMS/MAR detect unrelated changes from CR accesses. */
        r.mar=0xa9U;r.ems[0]=0x217U;r.ems[41]=0x3ffU;r.ems[63]=0x201U;
        for(value=0;value<256;++value) {
            write_cr(&r,0xffU);write_cr(&r,(uint16_t)value);
            check_read(&r,expected(&pins,value),0);
            check_read(&r,expected(&pins,value),1);
            write_cr(&r,0);write_cr(&r,(uint16_t)value);
            check_read(&r,expected(&pins,value),0);
            assert(r.pins.ram1m==pins.ram1m && r.pins.ramsw1==pins.ramsw1 &&
                   r.pins.ramsw2==pins.ramsw2 && r.pins.splsw==pins.splsw);
        }
        /* API reinitialization, including config aliasing the old object.
         * This is not a claim of physical reset retention of MR/MAR. */
        assert(bm_gc103_registers_initialize_strapped(&r,&r.pins)==BM_STATUS_OK);
        assert(r.cr_written==0 && r.mar==0 && r.ems[0]==0 && r.ems[41]==0 && r.ems[63]==0);
        check_read(&r,expected(&pins,0),1);
        pins.ram1m=BM_GC103_PIN_LOW;
        assert(r.pins.ram1m==(bm_gc103_pin_level_t)a); /* copy, not borrowed. */
        assert(memcmp(&isolated,&isolated_before,sizeof(isolated))==0);
    }
}

static void other_registers_and_errors(void)
{
    bm_gc103_straps_t pins={BM_GC103_PIN_HIGH,BM_GC103_PIN_LOW,BM_GC103_PIN_FLOATING,BM_GC103_PIN_LOW};
    bm_gc103_registers_t r,before;
    bm_gc103_register_effect_t effect,sentinel={BM_GC103_MAPPING_EMS_SLOT,77};
    uint16_t value;
    unsigned i;
    assert(bm_gc103_registers_initialize_strapped(&r,&pins)==BM_STATUS_OK);
    write_cr(&r,0x81U); check_read(&r,0x25U,0); /* D7 tied HIGH, D2/D5 tied LOW. */
    value=0xffU;assert(bm_gc103_registers_access(&r,0x1eeU,1,BM_BUS_WRITE,0,&value,&effect)==BM_STATUS_OK);
    value=0x39cU;assert(bm_gc103_registers_access(&r,0x1ecU,2,BM_BUS_WRITE,0,&value,&effect)==BM_STATUS_OK);
    assert(r.mar==0 && r.ems[63]==0x39cU && effect.mapping==BM_GC103_MAPPING_EMS_SLOT && effect.slot==63);
    check_read(&r,0x25U,1);
    /* Inherited non-native-width policy remains explicit and unqualified. */
    value=0;assert(bm_gc103_registers_access(&r,0x1efU,2,BM_BUS_READ,0,&value,&effect)==BM_STATUS_OK && value==0xffffU);
    value=0;assert(bm_gc103_registers_access(&r,0x1efU,2,BM_BUS_WRITE,0,&value,&effect)==BM_STATUS_OK);
    check_read(&r,0x25U,0);
    memcpy(&before,&r,sizeof(r));
    for(i=0;i<4;++i) {
        bm_gc103_straps_t invalid=pins;
        if(i==0)invalid.ram1m=(bm_gc103_pin_level_t)-1;
        if(i==1)invalid.ramsw1=(bm_gc103_pin_level_t)3;
        if(i==2)invalid.ramsw2=(bm_gc103_pin_level_t)-1;
        if(i==3)invalid.splsw=(bm_gc103_pin_level_t)3;
        assert(bm_gc103_registers_initialize_strapped(&r,&invalid)==BM_STATUS_INVALID_ARGUMENT);
        assert(memcmp(&before,&r,sizeof(r))==0);
    }
    assert(bm_gc103_registers_initialize_strapped(NULL,&pins)==BM_STATUS_INVALID_ARGUMENT);
    assert(bm_gc103_registers_initialize_strapped(&r,NULL)==BM_STATUS_INVALID_ARGUMENT);
    effect=sentinel;value=0xffU;
    assert(bm_gc103_registers_access(&r,0x1efU,1,BM_BUS_WRITE,1,&value,&effect)==BM_STATUS_READ_ONLY);
    assert(value==0xffU && effect.mapping==sentinel.mapping && effect.slot==sentinel.slot);
    value=0x100U;
    assert(bm_gc103_registers_access(&r,0x1efU,1,BM_BUS_WRITE,0,&value,&effect)==BM_STATUS_INVALID_ARGUMENT);
    assert(value==0x100U && effect.mapping==sentinel.mapping && effect.slot==sentinel.slot);
    assert(bm_gc103_registers_access(&r,0x1ebU,1,BM_BUS_READ,0,&value,&effect)==BM_STATUS_UNMAPPED);
    assert(memcmp(&before,&r,sizeof(r))==0);
    /* Selecting legacy again clears the optional control state completely. */
    assert(bm_gc103_registers_initialize(&r,0x100000U)==BM_STATUS_OK);
    assert(r.control_profile==BM_GC103_CONTROL_LEGACY && r.cr0==0 && r.cr_written==0);
    value=0;assert(bm_gc103_registers_access(&r,0x1efU,1,BM_BUS_READ,0,&value,&effect)==BM_STATUS_OK && value==0x20U);
}

static void mapper_refusal(void)
{
    bm_gc103_memory_t m,before;
    bm_gc103_straps_t pins={0};
    bm_gc10x_route_t route,sentinel;
    uint16_t value=0xffU;
    assert(bm_gc103_memory_initialize(&m,0x100000U)==BM_STATUS_OK);
    assert(bm_gc103_registers_initialize_strapped(&m.registers,&pins)==BM_STATUS_OK);
    memcpy(&before,&m,sizeof(m));memset(&sentinel,0x5a,sizeof(sentinel));memcpy(&route,&sentinel,sizeof(route));
    assert(bm_gc103_memory_io(&m,0x1efU,1,BM_BUS_WRITE,0,&value)==BM_STATUS_UNSUPPORTED);
    assert(value==0xffU && memcmp(&before,&m,sizeof(m))==0);
    assert(bm_gc103_memory_io(&m,0x1efU,1,BM_BUS_READ,1,&value)==BM_STATUS_UNSUPPORTED);
    assert(value==0xffU && memcmp(&before,&m,sizeof(m))==0);
    assert(bm_gc103_memory_resolve(&m,BM_GC10X_CPU,1,0,BM_BUS_READ,&route)==BM_STATUS_UNSUPPORTED);
    assert(memcmp(&route,&sentinel,sizeof(route))==0 && memcmp(&m,&before,sizeof(m))==0);
    assert(bm_gc103_memory_initialize(&m,0x100000U)==BM_STATUS_OK);
    assert(bm_gc103_memory_resolve(&m,BM_GC10X_CPU,1,0,BM_BUS_READ,&route)==BM_STATUS_OK);
    assert(route.target==BM_GC10X_RAM && route.offset==0);
}
int main(void)
{
    matrix();other_registers_and_errors();mapper_refusal();
    printf("GC103 configured control: %lu reads across all 81 pin configurations and 256 software values; pure debug, replacement latch, isolation and mapper refusal.\n",reads);
    return 0;
}
