/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Authored composition fixtures, no firmware or hardware captures. */
#include "board_io.h"
#include <blumach/platforms/null_host.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct fixture {
    bm_pcs286_io_t io;
    bm_gc103_memory_t headland;
    bm_ioc02_legacy_registers_t ioc;
    bm_at_pic_t *pic;
    bm_at_dma_t *dma;
    bm_at_bus_t *bus;
    uint8_t bytes[65536];
    unsigned calls, effects, fail_at, after, reenter, widths[8], ports[8];
    uint32_t waits;
    bm_status_t failure;
} fixture_t;
static unsigned sweep_cases, matrix_cases, failure_cases;
static bm_at_transfer_t transaction(unsigned port, unsigned size, bm_bus_operation_t op)
{
    bm_at_transfer_t t = {0};
    t.master = BM_AT_MASTER_CPU; t.requester_clock = (bm_clock_rate_t){12, 1};
    t.bus.space = BM_ADDRESS_IO; t.bus.operation = op;
    t.bus.address = port; t.bus.size = size; t.bus.alignment = 1;
    t.bus.value = UINT64_C(0x8877665544332211);
    return t;
}
static void same(const bm_at_transfer_t *a, const bm_at_transfer_t *b)
{
    assert(a->master == b->master);
    assert(a->requester_clock.cycles_per_second_numerator == b->requester_clock.cycles_per_second_numerator);
    assert(a->requester_clock.cycles_per_second_denominator == b->requester_clock.cycles_per_second_denominator);
    assert(a->bus.space == b->bus.space && a->bus.operation == b->bus.operation);
    assert(a->bus.address == b->bus.address && a->bus.size == b->bus.size);
    assert(a->bus.value == b->bus.value && a->bus.wait_states == b->bus.wait_states);
    assert(a->bus.alignment == b->bus.alignment && a->bus.endianness == b->bus.endianness);
    assert(a->bus.attributes == b->bus.attributes);
}
static bm_status_t no_memory(void *context, bm_at_transfer_t *t)
{ (void)context; (void)t; assert(!"unexpected memory access"); return BM_STATUS_DEVICE_ERROR; }
static bm_status_t external(void *context, bm_bus_transaction_t *t)
{
    fixture_t *f = context;
    uint64_t value = 0;
    unsigned i;
    int debug = (t->attributes & BM_BUS_TRANSACTION_DEBUG) != 0;
    assert(t->wait_states == 0 && t->alignment == t->size && t->size <= 2U);
    if (!debug) {
        assert(f->calls < 8U);
        f->ports[f->calls] = (unsigned)t->address; f->widths[f->calls++] = t->size;
    }
    if (f->reenter) {
        bm_at_transfer_t nested = transaction(0x68, 1, BM_BUS_READ), before = nested;
        assert(bm_pcs286_io_access(&f->io, &nested) == BM_STATUS_INVALID_STATE);
        same(&nested, &before);
    }
    if (!debug && f->calls == f->fail_at && !f->after) {
        t->value = 0; t->wait_states = UINT32_MAX; return f->failure;
    }
    for (i = 0; i < t->size; ++i) {
        unsigned shift = (t->endianness == BM_ENDIAN_LITTLE ? i : t->size - i - 1U) * 8U;
        if (t->operation == BM_BUS_WRITE) f->bytes[t->address + i] = (uint8_t)(t->value >> shift);
        else value |= (uint64_t)f->bytes[t->address + i] << shift;
    }
    if (t->operation == BM_BUS_READ) t->value = value;
    if (!debug) ++f->effects;
    t->wait_states = f->waits;
    if (!debug && f->calls == f->fail_at) return f->failure;
    return BM_STATUS_OK;
}
static bm_status_t at_io(void *context, bm_at_transfer_t *t)
{ return bm_pcs286_io_access(&((fixture_t *)context)->io, t); }
static void start(fixture_t *f)
{
    bm_host_services_t host = bm_null_host_services();
    bm_at_pic_config_t p = {0}; bm_at_dma_config_t d = {0};
    bm_pcs286_io_config_t c = {0}; bm_at_bus_config_t b = {0};
    memset(f, 0, sizeof(*f));
    assert(bm_gc103_memory_initialize(&f->headland, 0x100000) == BM_STATUS_OK);
    assert(bm_ioc02_legacy_initialize(&f->ioc) == BM_STATUS_OK);
    p.master_base = 0x20; p.slave_base = 0xa0; p.cascade_line = 2;
    assert(bm_at_pic_create(&host, &p, &f->pic) == BM_STATUS_OK);
    d.memory = no_memory; d.clock = (bm_clock_rate_t){4, 1};
    assert(d.mem2mem_profile == BM_AT_DMA_MEM2MEM_DISABLED);
    assert(bm_at_dma_create(&host, &d, &f->dma) == BM_STATUS_OK);
    c.profile = BM_PCS286_IO_LEGACY_GC103_AT; c.headland = &f->headland;
    c.ioc02 = &f->ioc; c.pic = f->pic; c.dma = f->dma;
    c.holes = BM_PCS286_IO_FF; c.timing = BM_PCS286_IO_PROVISIONAL;
    c.service_clock = (bm_clock_rate_t){8, 1};
    assert(bm_pcs286_io_initialize(&f->io, &c) == BM_STATUS_OK);
    b.cpu_clock = (bm_clock_rate_t){12, 1}; b.isa_clock = (bm_clock_rate_t){8, 1};
    b.memory = no_memory; b.io = at_io; b.decode_context = f;
    assert(bm_at_bus_create(&host, &b, &f->bus) == BM_STATUS_OK);
    {
        static const unsigned ports[] = {0x20, 0xa0, 0x21, 0xa1, 0x21, 0xa1, 0x21, 0xa1};
        static const unsigned values[] = {0x11, 0x11, 0x20, 0x28, 4, 2, 1, 1};
        unsigned i;
        for (i = 0; i < 8; ++i) {
            bm_at_transfer_t t = transaction(ports[i], 1, BM_BUS_WRITE); t.bus.value = values[i];
            assert(bm_at_bus_access(f->bus, &t) == BM_STATUS_OK);
        }
        /* Erase adapter diagnostics only; preserve initialized children. */
        assert(bm_pcs286_io_initialize(&f->io, &c) == BM_STATUS_OK);
    }
    f->failure = BM_STATUS_DEVICE_ERROR;
}
static void finish(fixture_t *f)
{ bm_at_bus_destroy(f->bus); bm_at_pic_destroy(f->pic); bm_at_dma_destroy(f->dma); }
static uint64_t access(fixture_t *f, unsigned port, unsigned size, bm_bus_operation_t op, uint64_t value)
{
    bm_at_transfer_t t = transaction(port, size, op); t.bus.value = value;
    bm_status_t status = bm_at_bus_access(f->bus, &t);
    if (status != BM_STATUS_OK) fprintf(stderr, "I/O %x/%u op%d failed %d\n", port, size, (int)op, (int)status);
    assert(status == BM_STATUS_OK); return t.bus.value;
}
static void resource(fixture_t *f, unsigned first, unsigned last, unsigned width)
{
    bm_pcs286_io_config_t c = f->io.config;
    assert(c.resource_count < BM_PCS286_IO_RESOURCES);
    c.resources[c.resource_count++] = (bm_pcs286_io_resource_t){(uint16_t)first,
        (uint16_t)last, width, external, f, 0};
    assert(bm_pcs286_io_initialize(&f->io, &c) == BM_STATUS_OK);
}
static void reset_trace(fixture_t *f)
{
    f->calls = f->effects = f->fail_at = f->after = f->waits = f->reenter = 0;
    f->failure = BM_STATUS_DEVICE_ERROR;
}
static void rejected(fixture_t *f, bm_at_transfer_t t, bm_status_t expected)
{
    bm_at_transfer_t before = t;
    bm_status_t status = bm_pcs286_io_access(&f->io, &t);
    if (status != expected) fprintf(stderr, "I/O failure at %llx/%u: got %d expected %d\n",
        (unsigned long long)t.bus.address, t.bus.size, (int)status, (int)expected);
    assert(status == expected); same(&t, &before);
}
static void ports(void)
{
    fixture_t f; unsigned port;
    start(&f); f.io.config.holes = BM_PCS286_IO_REJECT;
    /* Independent ownership lists; compare pure inspection against actual child APIs. */
    for (port = 0; port < 65536U; ++port) {
        bm_at_transfer_t t = transaction(port, 1, BM_BUS_READ);
        bm_bus_transaction_t direct = t.bus;
        uint16_t value = 0; bm_status_t expected = BM_STATUS_UNMAPPED;
        bm_ioc02_legacy_effect_t effect;
        t.bus.attributes = direct.attributes = BM_BUS_TRANSACTION_DEBUG;
        if (port == 0x1ec || port == 0x1ed || port == 0x1ee || port == 0x1ef) {
            expected = bm_gc103_memory_io(&f.headland, (uint16_t)port, 1, BM_BUS_READ, 1, &value);
            direct.value = value;
        } else if (port == 0x68 || port == 0x6a || port == 0x6c) {
            expected = bm_ioc02_legacy_access(&f.ioc, (uint16_t)port, 1, BM_BUS_READ, 1, &value, &effect);
            direct.value = value;
        } else if (port == 0x20 || port == 0x21 || port == 0xa0 || port == 0xa1)
            expected = bm_at_pic_io(f.pic, &direct);
        else {
            static const unsigned dp[] = {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
                0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0xc0,0xc2,0xc4,0xc6,0xc8,
                0xca,0xcc,0xce,0xd0,0xd2,0xd4,0xd6,0xd8,0xda,0xdc,0xde,
                0x87,0x83,0x81,0x82,0x8b,0x89,0x8a};
            unsigned i;
            for (i = 0; i < sizeof(dp)/sizeof(dp[0]); ++i)
                if (port == dp[i]) expected = bm_at_dma_io(f.dma, &direct);
        }
        assert(bm_pcs286_io_access(&f.io, &t) == expected);
        if (expected == BM_STATUS_OK) assert(t.bus.value == direct.value && !t.bus.wait_states);
        ++sweep_cases;
    }
    assert(f.io.last.completed_bytes == 0); finish(&f);
}
static void devices(void)
{
    fixture_t f; bm_at_dma_state_t ds; bm_at_dma_channel_state_t ch; bm_gc10x_route_t route;
    bm_at_transfer_t t; bm_pcs286_io_progress_t last;
    start(&f);
    assert(access(&f, 0x6a, 1, BM_BUS_READ, 0) == 0x24); /* No first-read override. */
    access(&f, 0x68, 1, BM_BUS_WRITE, 0);
    access(&f, 0x6a, 1, BM_BUS_WRITE, 0xab); assert(f.ioc.data == 4);
    access(&f, 0x68, 1, BM_BUS_WRITE, 1);
    access(&f, 0x6a, 2, BM_BUS_WRITE, 0xabcd); assert(f.ioc.data == 0xcd);
    assert(access(&f, 0x6a, 2, BM_BUS_READ, 0) == 0xffed);
    access(&f, 0x1ee, 1, BM_BUS_WRITE, 0x80);
    access(&f, 0x1ec, 2, BM_BUS_WRITE, 0x0288);
    assert(access(&f, 0x1ee, 1, BM_BUS_READ, 0) == 0x81);
    access(&f, 0x1ee, 2, BM_BUS_WRITE, 0x0200); /* Native ignored word, not MAR/CR0 bytes. */
    assert(access(&f, 0x1ee, 2, BM_BUS_READ, 0) == 0xffff);
    assert(access(&f, 0x1ee, 1, BM_BUS_READ, 0) == 0x81);
    access(&f, 0x1ed, 2, BM_BUS_WRITE, 0x00ff); /* Odd splits: ignored selector then MAR. */
    assert(access(&f, 0x1ee, 1, BM_BUS_READ, 0) == 0);
    access(&f, 0x1ef, 1, BM_BUS_WRITE, 2);
    assert(bm_gc103_memory_resolve(&f.headland, BM_GC10X_CPU, 1, 0x40000, BM_BUS_READ, &route) == BM_STATUS_OK);
    assert(route.target == BM_GC10X_RAM && route.offset == 0xa0000);
    t = transaction(0x1ec, 2, BM_BUS_READ); t.bus.endianness = BM_ENDIAN_BIG;
    assert(bm_pcs286_io_access(&f.io, &t) == BM_STATUS_OK && t.bus.value == 0x88fe);
    t = transaction(0x1ec, 2, BM_BUS_WRITE); t.bus.endianness = BM_ENDIAN_BIG; t.bus.value = 0x8902;
    assert(bm_pcs286_io_access(&f.io, &t) == BM_STATUS_OK);
    assert(access(&f, 0x1ec, 2, BM_BUS_READ, 0) == 0xfe89);
    access(&f, 0x1ee, 1, BM_BUS_WRITE, 0xff);
    last = f.io.last; t = transaction(0x1ec, 2, BM_BUS_READ); t.bus.attributes = BM_BUS_TRANSACTION_DEBUG;
    assert(bm_pcs286_io_access(&f.io, &t) == BM_STATUS_OK);
    assert(f.io.last.completed_bytes == last.completed_bytes && f.io.last.status == last.status);
    assert(access(&f, 0x1ee, 1, BM_BUS_READ, 0) == 0xff);
    access(&f, 0x1ec, 1, BM_BUS_READ, 0);
    assert(access(&f, 0x1ee, 1, BM_BUS_READ, 0) == 0);
    access(&f, 0x21, 1, BM_BUS_WRITE, 0x5a);
    assert(access(&f, 0x21, 1, BM_BUS_READ, 0) == 0x5a);
    access(&f, 0xa1, 1, BM_BUS_WRITE, 0xa5);
    assert(access(&f, 0xa1, 1, BM_BUS_READ, 0) == 0xa5);
    access(&f, 0x0c, 1, BM_BUS_WRITE, 0);
    access(&f, 0, 2, BM_BUS_WRITE, 0x3412); /* Adjacent address/count share flipflop. */
    assert(bm_at_dma_channel_state(f.dma, 0, &ch) == BM_STATUS_OK);
    assert(ch.current_address == 0x12 && ch.current_count == 0x3400);
    access(&f, 0x87, 1, BM_BUS_WRITE, 0x56);
    assert(access(&f, 0x87, 1, BM_BUS_READ, 0) == 0x56);
    t = transaction(0, 2, BM_BUS_READ); t.bus.attributes = BM_BUS_TRANSACTION_DEBUG;
    assert(bm_pcs286_io_access(&f.io, &t) == BM_STATUS_OK && t.bus.value == 0x12);
    assert(bm_at_dma_state(f.dma, &ds) == BM_STATUS_OK && ds.byte_high[0] == 0);
    access(&f, 0xc0, 2, BM_BUS_WRITE, 0xff67); /* C1 is a hole, not C2 mirror. */
    assert(bm_at_dma_channel_state(f.dma, 4, &ch) == BM_STATUS_OK && ch.current_address == 0x67);
    assert(access(&f, 0x8f, 1, BM_BUS_READ, 0) == 0xff);
    /* Installed write-only DMA read error is never turned into FF. */
    rejected(&f, transaction(9, 1, BM_BUS_READ), BM_STATUS_UNMAPPED);
    finish(&f);
}
static void external_matrix(void)
{
    fixture_t f; unsigned width, endian, size, start_port, op, i;
    start(&f); resource(&f, 0x301, 0x308, 1); resource(&f, 0x309, 0x310, 2);
    for (width = 1; width <= 2; ++width) for (endian = 0; endian < 2; ++endian)
    for (size = 1; size <= 8; ++size) for (start_port = 0x300; start_port <= 0x311; ++start_port)
    for (op = 0; op < 2; ++op) {
        bm_at_transfer_t t = transaction(start_port, size, op ? BM_BUS_WRITE : BM_BUS_READ);
        uint64_t value = 0; t.bus.endianness = (bm_endianness_t)endian;
        f.io.config.resources[0].width = width; reset_trace(&f);
        for (i = 0x300; i < 0x320; ++i) f.bytes[i] = (uint8_t)i;
        assert(bm_pcs286_io_access(&f.io, &t) == BM_STATUS_OK);
        for (i = 0; i < size; ++i) {
            unsigned p = start_port + i, shift = (endian == BM_ENDIAN_LITTLE ? i : size - i - 1U)*8U;
            int owned = p >= 0x301 && p <= 0x310;
            if (op) assert(f.bytes[p] == (owned ? (uint8_t)(t.bus.value >> shift) : (uint8_t)p));
            else value |= (uint64_t)(owned ? (uint8_t)p : 0xffU) << shift;
        }
        if (!op) assert(t.bus.value == value);
        for (i = 0; i < f.calls; ++i) {
            assert(f.widths[i] == 1 || !(f.ports[i] & 1U));
            assert(f.ports[i] + f.widths[i] - 1 <= (f.ports[i] <= 0x308 ? 0x308U : 0x310U));
        }
        assert(f.io.last.completed_bytes == size); ++matrix_cases;
    }
    finish(&f);
}
static void failures(void)
{
    static const bm_status_t statuses[] = {BM_STATUS_DEVICE_ERROR, BM_STATUS_UNMAPPED,
        BM_STATUS_READ_ONLY, BM_STATUS_INVALID_STATE, BM_STATUS_CAPACITY_EXCEEDED,
        BM_STATUS_UNSUPPORTED, BM_STATUS_OUT_OF_MEMORY, BM_STATUS_IDLE};
    fixture_t f; unsigned width, endian, op, after, index, nth, i;
    start(&f); resource(&f, 0x300, 0x30f, 1);
    for (width = 1; width <= 2; ++width) for (endian = 0; endian < 2; ++endian)
    for (op = 0; op < 2; ++op) for (after = 0; after < 2; ++after)
    for (index = 0; index < sizeof(statuses)/sizeof(statuses[0]); ++index)
    for (nth = 1; nth <= (width == 1 ? 8U : 5U); ++nth) {
        bm_at_transfer_t t = transaction(0x301, 8, op ? BM_BUS_WRITE : BM_BUS_READ), before;
        unsigned completed = width == 1 ? nth - 1 : (nth == 1 ? 0 : 1 + 2*(nth-2));
        unsigned attempted = completed + (width == 1 || nth == 1 || nth == 5 ? 1 : 2);
        t.bus.endianness = (bm_endianness_t)endian; before = t;
        reset_trace(&f); memset(f.bytes, 0, sizeof(f.bytes));
        f.io.config.resources[0].width = width; f.fail_at = nth; f.after = after; f.failure = statuses[index];
        assert(bm_pcs286_io_access(&f.io, &t) == statuses[index]); same(&t, &before);
        assert(f.calls == nth && f.effects == nth - 1 + after);
        assert(f.io.last.completed_bytes == completed && f.io.last.attempted_bytes == attempted);
        for (i = 0; i < 8; ++i) {
            unsigned shift = (endian == BM_ENDIAN_LITTLE ? i : 7-i)*8U;
            assert(f.bytes[0x301+i] == (op && i < (after ? attempted : completed) ?
                (uint8_t)(before.bus.value >> shift) : 0));
        }
        ++failure_cases;
    }
    /* Real built-in effects precede a failing external lane; no rollback. */
    reset_trace(&f); resource(&f, 0x69, 0x69, 1); f.fail_at = 1;
    rejected(&f, transaction(0x68, 3, BM_BUS_WRITE), BM_STATUS_DEVICE_ERROR);
    assert(f.ioc.select == 0x11 && f.ioc.data == 4 && f.io.last.completed_bytes == 1);
    reset_trace(&f); resource(&f, 0x22, 0x22, 1); f.fail_at = 1;
    rejected(&f, transaction(0x21, 2, BM_BUS_WRITE), BM_STATUS_DEVICE_ERROR);
    assert(access(&f, 0x21, 1, BM_BUS_READ, 0) == 0x11);
    reset_trace(&f); resource(&f, 0x1f0, 0x1f0, 1); f.fail_at = 1;
    rejected(&f, transaction(0x1ef, 2, BM_BUS_WRITE), BM_STATUS_DEVICE_ERROR);
    assert(access(&f, 0x1ef, 1, BM_BUS_READ, 0) == 0x31);
    /* Failure in a real device after an earlier DMA read retains flipflop. */
    reset_trace(&f);
    rejected(&f, transaction(7, 3, BM_BUS_READ), BM_STATUS_UNMAPPED);
    { bm_at_dma_state_t d; assert(bm_at_dma_state(f.dma, &d) == BM_STATUS_OK && d.byte_high[0] == 1); }
    assert(f.io.last.completed_bytes == 2 && f.io.last.attempted_bytes == 3);
    finish(&f);
}
static void policies(void)
{
    fixture_t f; bm_at_transfer_t t; bm_pcs286_io_config_t c; bm_pcs286_io_t snapshot;
    unsigned i;
    start(&f); resource(&f, 0xfff8, 0xffff, 2);
    f.io.config.holes = BM_PCS286_IO_REJECT;
    rejected(&f, transaction(0x68, 2, BM_BUS_WRITE), BM_STATUS_UNMAPPED);
    assert(f.ioc.select == 4 && f.io.last.attempted_bytes == 0);
    f.io.config.holes = BM_PCS286_IO_FF;
    memcpy(&snapshot, &f.io, sizeof(snapshot));
    for (i = 0; i < 8; ++i) {
        c = f.io.config;
        if (i == 0) c.resources[0].first = 0x68;
        if (i == 1) { c.resources[1] = c.resources[0]; c.resource_count = 2; }
        if (i == 2) c.resources[0].access = NULL;
        if (i == 3) c.resources[0].width = 3;
        if (i == 4) c.resources[0].last = 0;
        if (i == 5) c.resource_count = BM_PCS286_IO_RESOURCES + 1;
        if (i == 6) c.headland = NULL;
        if (i == 7) c.service_clock.cycles_per_second_numerator = 0;
        assert(bm_pcs286_io_initialize(&f.io, &c) == BM_STATUS_INVALID_ARGUMENT);
        assert(!memcmp(&f.io, &snapshot, sizeof(f.io))); /* Failed init must write no bytes. */
    }
    t = transaction(0xffff, 2, BM_BUS_WRITE); rejected(&f, t, BM_STATUS_INVALID_ARGUMENT);
    assert(!f.calls);
    t = transaction(0xffff, 1, BM_BUS_READ); assert(bm_pcs286_io_access(&f.io, &t) == BM_STATUS_OK);
    reset_trace(&f); f.reenter = 1;
    t = transaction(0xfff8, 8, BM_BUS_READ); assert(bm_pcs286_io_access(&f.io, &t) == BM_STATUS_OK);
    assert(f.calls == 4);
    reset_trace(&f); f.io.config.timing = BM_PCS286_IO_STRICT;
    rejected(&f, transaction(0x1ec, 2, BM_BUS_READ), BM_STATUS_UNSUPPORTED);
    t = transaction(0xfff8, 8, BM_BUS_READ); t.bus.attributes = BM_BUS_TRANSACTION_DEBUG;
    f.waits = UINT32_MAX; assert(bm_pcs286_io_access(&f.io, &t) == BM_STATUS_OK && !t.bus.wait_states && !f.calls);
    t.bus.operation = BM_BUS_WRITE; rejected(&f, t, BM_STATUS_UNSUPPORTED);
    rejected(&f, transaction(0x68, 1, BM_BUS_FETCH), BM_STATUS_UNSUPPORTED);
    f.io.config.timing = BM_PCS286_IO_PROVISIONAL;
    /* Two service clocks at 8Hz -> three requester clocks at 12Hz, rounded once. */
    reset_trace(&f); f.waits = 1;
    t = transaction(0xfff8, 4, BM_BUS_READ);
    assert(bm_pcs286_io_access(&f.io, &t) == BM_STATUS_OK && t.bus.wait_states == 3);
    reset_trace(&f); f.io.config.resources[0].extra_clocks = UINT32_MAX;
    rejected(&f, transaction(0xfff8, 4, BM_BUS_WRITE), BM_STATUS_CAPACITY_EXCEEDED);
    assert(!f.calls);
    f.io.config.resources[0].extra_clocks = 0; f.waits = UINT32_MAX;
    rejected(&f, transaction(0xfff8, 4, BM_BUS_WRITE), BM_STATUS_CAPACITY_EXCEEDED);
    assert(f.calls == 1 && f.effects == 1 && f.io.last.completed_bytes == 2);
    reset_trace(&f);
    /* Actual AT ownership must precede the decoder, including LOCK windows. */
    assert(bm_at_bus_set_lock(f.bus, 1) == BM_STATUS_OK);
    assert(bm_at_bus_request(f.bus, BM_AT_MASTER_ISA, 1) == BM_STATUS_OK);
    access(&f, 0x68, 1, BM_BUS_WRITE, 0x55);
    assert(bm_at_bus_set_lock(f.bus, 0) == BM_STATUS_OK);
    assert(bm_at_bus_hold_ack(f.bus, 1) == BM_STATUS_OK);
    t = transaction(0x68, 1, BM_BUS_WRITE);
    { bm_at_transfer_t before = t; assert(bm_at_bus_access(f.bus, &t) == BM_STATUS_IDLE); same(&t, &before); }
    assert(f.ioc.select == 0x55);
    t.bus.operation = BM_BUS_READ; t.bus.attributes = BM_BUS_TRANSACTION_DEBUG;
    assert(bm_at_bus_access(f.bus, &t) == BM_STATUS_OK && t.bus.value == 0x55);
    t.bus.attributes = 0; t.master = BM_AT_MASTER_ISA;
    assert(bm_at_bus_access(f.bus, &t) == BM_STATUS_OK && t.bus.value == 0x55);
    assert(bm_at_bus_request(f.bus, BM_AT_MASTER_ISA, 0) == BM_STATUS_OK);
    assert(bm_at_bus_hold_ack(f.bus, 0) == BM_STATUS_OK);
    finish(&f);
}
static void boundaries(void)
{
    fixture_t a, b;
    bm_at_transfer_t t;
    bm_pcs286_io_config_t config;
    unsigned i, master;
    start(&a); start(&b);
    resource(&a, 0x69, 0x69, 1);
    config = a.io.config;
    config.extra_clocks[BM_PCS286_IO_IOC02] = 1;
    config.resources[0].extra_clocks = 2;
    assert(bm_pcs286_io_initialize(&a.io, &config) == BM_STATUS_OK);
    config.resources[0].access = NULL; config.extra_clocks[BM_PCS286_IO_IOC02] = 99;
    a.waits = 3;
    /* IOC02 + external + IOC02 = 1+2+3+1 service clocks: ceil(7*12/8)=11. */
    for (master = BM_AT_MASTER_CPU; master <= BM_AT_MASTER_ISA; ++master) {
        reset_trace(&a); a.waits = 3; t = transaction(0x68, 3, BM_BUS_READ);
        t.master = (bm_at_master_t)master;
        assert(bm_pcs286_io_access(&a.io, &t) == BM_STATUS_OK && t.bus.wait_states == 11);
    }
    access(&a, 0x68, 1, BM_BUS_WRITE, 0x23);
    assert(access(&b, 0x68, 1, BM_BUS_READ, 0) == 4);
    config = a.io.config;
    assert(bm_pcs286_io_initialize(&a.io, &config) == BM_STATUS_OK);
    assert(a.ioc.select == 0x23); /* Reinitialization does not reset children. */
    for (i = 0; i < 11; ++i) {
        t = transaction(0x68, 1, BM_BUS_READ);
        switch (i) {
        case 0: t.bus.size = 0; break;
        case 1: t.bus.size = 9; break;
        case 2: t.bus.address = UINT64_MAX; break;
        case 3: t.bus.operation = (bm_bus_operation_t)99; break;
        case 4: t.bus.endianness = (bm_endianness_t)99; break;
        case 5: t.bus.attributes = 4; break;
        case 6: t.bus.wait_states = 1; break;
        case 7: t.master = (bm_at_master_t)99; break;
        case 8: t.requester_clock.cycles_per_second_numerator = 0; break;
        case 9: t.requester_clock.cycles_per_second_denominator = 0; break;
        default: t.bus.address = 0x10000; break;
        }
        rejected(&a, t, BM_STATUS_INVALID_ARGUMENT);
        assert(a.io.last.attempted_bytes == 0 && a.ioc.select == 0x23);
    }
    assert(bm_pcs286_io_access(NULL, &t) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_pcs286_io_access(&a.io, NULL) == BM_STATUS_INVALID_ARGUMENT);
    t = transaction(0x68, 1, BM_BUS_READ); t.bus.space = BM_ADDRESS_MEMORY;
    rejected(&a, t, BM_STATUS_UNMAPPED);
    finish(&a); finish(&b);
}
int main(void)
{
    ports(); devices(); external_matrix(); failures(); policies(); boundaries();
    printf("Board I/O: %u port probes, %u width/endian cases, %u injected failures; real Headland/IOC02/PIC/DMA and AT ownership\n",
        sweep_cases, matrix_cases, failure_cases);
    return 0;
}
