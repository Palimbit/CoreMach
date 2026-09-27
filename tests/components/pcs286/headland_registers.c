/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Authored differential harness. The generated include retains the authors
 * of the pinned classic implementation. Equivalence is NOT silicon evidence.
 */
#include "legacy_gc103_registers.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Only memory-map publication/logging are replaced in the reference. Actual
 * classic byte/word register handlers and RAM tables compile unchanged.
 * This test-local global mirrors classic mem_size, never the portable core. */
static uint32_t mem_size;
typedef struct headland_t {
    uint8_t revision, has_cri, has_sleep, cri, cr[7], ems_mar;
    struct { uint16_t mr; } ems_mr[64];
    unsigned map_updates, ems_updates;
    uint8_t last_slot;
} headland_t;
static void memmap_state_update(headland_t *dev) { ++dev->map_updates; }
static void hl_ems_update(headland_t *dev, uint8_t slot)
{
    ++dev->ems_updates;
    dev->last_slot = slot;
}
#define headland_log(...) ((void)0)
#include "headland_classic_registers.inc"

static unsigned long comparisons;
static void same(const bm_gc103_registers_t *a, const bm_gc103_registers_t *b)
{
    unsigned i;
    assert(a->mar == b->mar && a->cr0 == b->cr0 && a->ram_straps == b->ram_straps);
    for (i = 0; i < 64; ++i) assert(a->ems[i] == b->ems[i]);
}
static void equivalent(const bm_gc103_registers_t *r, const headland_t *classic)
{
    unsigned i;
    assert(r->mar == classic->ems_mar && r->cr0 == classic->cr[0]);
    for (i = 0; i < 64; ++i) assert(r->ems[i] == classic->ems_mr[i].mr);
    ++comparisons;
}
static void start(bm_gc103_registers_t *r, headland_t *classic, unsigned mib)
{
    mem_size = mib * 1024U;
    assert(bm_gc103_registers_initialize(r, mib * 0x100000U) == BM_STATUS_OK);
    memset(classic, 0, sizeof(*classic)); /* calloc + PCS286 CR0=0; GC103. */
    equivalent(r, classic);
}
static uint16_t transfer(bm_gc103_registers_t *r, headland_t *classic,
                         uint16_t port, unsigned width,
                         bm_bus_operation_t op, uint16_t value)
{
    bm_gc103_register_effect_t effect = {BM_GC103_MAPPING_ALL, 99U};
    uint16_t expected = value;
    unsigned map_before = classic->map_updates, ems_before = classic->ems_updates;
    assert(bm_gc103_registers_access(r, port, width, op, 0, &value, &effect) == BM_STATUS_OK);
    if (op == BM_BUS_READ)
        expected = width == 1 ? hl_read(port, classic) : hl_readw(port, classic);
    else if (width == 1)
        hl_write(port, (uint8_t)value, classic);
    else
        hl_writew(port, value, classic);
    assert(value == expected);
    if (classic->map_updates != map_before) {
        assert(classic->map_updates == map_before + 1U);
        assert(effect.mapping == BM_GC103_MAPPING_ALL && effect.slot == 0U);
    } else if (classic->ems_updates != ems_before) {
        assert(classic->ems_updates == ems_before + 1U);
        assert(effect.mapping == BM_GC103_MAPPING_EMS_SLOT && effect.slot == classic->last_slot);
    } else {
        assert(effect.mapping == BM_GC103_MAPPING_UNCHANGED && effect.slot == 0U);
    }
    equivalent(r, classic);
    return value;
}
static void peek(bm_gc103_registers_t *r, const headland_t *classic,
                  uint16_t port, unsigned width)
{
    bm_gc103_registers_t before = *r;
    headland_t copy = *classic;
    bm_gc103_register_effect_t effect;
    uint16_t value = 0xdeadU;
    uint16_t expected = width == 1 ? hl_read(port, &copy) : hl_readw(port, &copy);
    assert(bm_gc103_registers_access(r, port, width, BM_BUS_READ, 1, &value, &effect) == BM_STATUS_OK);
    assert(value == expected && effect.mapping == BM_GC103_MAPPING_UNCHANGED && effect.slot == 0U);
    same(r, &before);
    equivalent(r, classic);
}

static void exhaustive(void)
{
    unsigned mib, mar, value, port, width;
    bm_gc103_registers_t r;
    headland_t classic;
    for (mib = 1; mib <= 4; ++mib) {
        start(&r, &classic, mib);
        for (value = 0; value < 256; ++value) {
            transfer(&r, &classic, 0x1efU, 1, BM_BUS_WRITE, (uint16_t)value);
            transfer(&r, &classic, 0x1efU, 1, BM_BUS_READ, 0);
            /* The classic GC103 has NO CR index: writing 1ED must never
             * redirect 1EF, including indexes that crash later variants. */
            transfer(&r, &classic, 0x1edU, 1, BM_BUS_WRITE, (uint16_t)value);
            assert(transfer(&r, &classic, 0x1edU, 1, BM_BUS_READ, 0) == 0xffU);
            transfer(&r, &classic, 0x1efU, 1, BM_BUS_READ, 0);
        }
        for (mar = 0; mar < 256; ++mar) {
            for (value = 0; value < 256; ++value) {
                transfer(&r, &classic, 0x1eeU, 1, BM_BUS_WRITE, (uint16_t)mar);
                transfer(&r, &classic, 0x1ecU, 1, BM_BUS_WRITE, (uint16_t)value);
                assert(r.ems[mar & 63U] == (value | 0xff00U));
                transfer(&r, &classic, 0x1eeU, 1, BM_BUS_WRITE, (uint16_t)mar);
                peek(&r, &classic, 0x1ecU, 2);
                assert(transfer(&r, &classic, 0x1ecU, 2, BM_BUS_READ, 0) == (value | 0xff00U));
            }
        }
        for (value = 0; value < 65536U; ++value) {
            mar = value & 255U;
            transfer(&r, &classic, 0x1eeU, 1, BM_BUS_WRITE, (uint16_t)mar);
            transfer(&r, &classic, 0x1ecU, 2, BM_BUS_WRITE, (uint16_t)value);
            assert(r.ems[mar & 63U] == value);
            transfer(&r, &classic, 0x1eeU, 1, BM_BUS_WRITE, (uint16_t)mar);
            peek(&r, &classic, 0x1ecU, 1);
            assert(transfer(&r, &classic, 0x1ecU, 2, BM_BUS_READ, 0) == (value | 0xfc00U));
        }
        /* Continuous MAR wrap: FF->00 also clears auto-increment. */
        transfer(&r, &classic, 0x1eeU, 1, BM_BUS_WRITE, 0x80U);
        for (value = 0; value < 300; ++value)
            transfer(&r, &classic, 0x1ecU, 2, BM_BUS_WRITE, (uint16_t)value);
        assert(r.mar == 0U && r.ems[0] == 299U);
        for (port = 0x1ecU; port <= 0x1efU; ++port)
            for (width = 1; width <= 2; ++width) {
                peek(&r, &classic, (uint16_t)port, width);
                transfer(&r, &classic, (uint16_t)port, width, BM_BUS_WRITE, 0xa5U);
                transfer(&r, &classic, (uint16_t)port, width, BM_BUS_READ, 0);
            }
    }
}

static void failure(bm_gc103_registers_t *r, uint16_t port, unsigned width,
                     bm_bus_operation_t op, int debug, uint16_t initial, bm_status_t expected)
{
    bm_gc103_registers_t before = *r;
    bm_gc103_register_effect_t effect = {BM_GC103_MAPPING_ALL, 57U};
    uint16_t value = initial;
    assert(bm_gc103_registers_access(r, port, width, op, debug, &value, &effect) == expected);
    assert(value == initial && effect.mapping == BM_GC103_MAPPING_ALL && effect.slot == 57U);
    same(r, &before);
}
static void isolation_and_errors(void)
{
    bm_gc103_registers_t a, b, before;
    bm_gc103_register_effect_t effect = {BM_GC103_MAPPING_ALL, 57U};
    headland_t ca, cb;
    uint16_t value = 0xa5U;
    unsigned port, i;
    uint32_t random = 0x145286U;
    start(&a, &ca, 1);
    start(&b, &cb, 4);
    for (i = 0; i < 100000U; ++i) {
        bm_gc103_registers_t *r = (i & 1U) ? &a : &b;
        headland_t *c = (i & 1U) ? &ca : &cb;
        bm_gc103_registers_t other = (i & 1U) ? b : a;
        mem_size = (i & 1U) ? 1024U : 4096U;
        random = random * 1664525U + 1013904223U;
        transfer(r, c, (uint16_t)(0x1ecU + ((random >> 27) & 3U)),
                 1U + ((random >> 26) & 1U),
                 (random & 0x1000000U) ? BM_BUS_READ : BM_BUS_WRITE, (uint8_t)random);
        same((i & 1U) ? &b : &a, &other);
        if (i % 997U == 0U) start(r, c, (i & 1U) ? 1U : 4U);
    }
    before = a;
    for (port = 0; port < 65536U; ++port) {
        if (port >= 0x1ecU && port <= 0x1efU) {
            failure(&a, (uint16_t)port, 1, BM_BUS_WRITE, 1, 0xa5U, BM_STATUS_READ_ONLY);
            failure(&a, (uint16_t)port, 2, BM_BUS_WRITE, 1, 0xa55aU, BM_STATUS_READ_ONLY);
        } else {
            failure(&a, (uint16_t)port, 1, BM_BUS_READ, 0, 0xdeadU, BM_STATUS_UNMAPPED);
            failure(&a, (uint16_t)port, 2, BM_BUS_WRITE, 0, 0xbeefU, BM_STATUS_UNMAPPED);
        }
    }
    failure(&a, 0x1ecU, 0, BM_BUS_READ, 0, 0, BM_STATUS_INVALID_ARGUMENT);
    failure(&a, 0x1ecU, 4, BM_BUS_READ, 0, 0, BM_STATUS_INVALID_ARGUMENT);
    failure(&a, 0x1ecU, 1, BM_BUS_FETCH, 0, 0, BM_STATUS_INVALID_ARGUMENT);
    failure(&a, 0x1ecU, 1, BM_BUS_READ, 2, 0, BM_STATUS_INVALID_ARGUMENT);
    failure(&a, 0x1ecU, 1, BM_BUS_WRITE, 0, 0x100U, BM_STATUS_INVALID_ARGUMENT);
    assert(bm_gc103_registers_access(NULL, 0x1ecU, 1, BM_BUS_READ, 0, &value, &effect) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_gc103_registers_access(&a, 0x1ecU, 1, BM_BUS_READ, 0, NULL, &effect) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_gc103_registers_access(&a, 0x1ecU, 1, BM_BUS_READ, 0, &value, NULL) == BM_STATUS_INVALID_ARGUMENT);
    assert(value == 0xa5U && effect.mapping == BM_GC103_MAPPING_ALL && effect.slot == 57U);
    assert(bm_gc103_registers_initialize(NULL, 0x100000U) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_gc103_registers_initialize(&a, 0) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_gc103_registers_initialize(&a, 0x180000U) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_gc103_registers_initialize(&a, 0x500000U) == BM_STATUS_INVALID_ARGUMENT);
    same(&a, &before);
}
int main(void)
{
    /* The unchanged classic extraction includes dword handlers; the 286
     * migration intentionally exercises only its native byte/word forms. */
    (void)hl_readl;
    (void)hl_writel;
    exhaustive();
    isolation_and_errors();
    printf("Headland classic register equivalence: %lu comparisons; full port rejection sweep.\n", comparisons);
    return 0;
}
