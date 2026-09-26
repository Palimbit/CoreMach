/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Authored Motorola/IBM functional cases; no ROM, captured CMOS or host time. */
#include <blumach/components/rtc_at.h>
#include <blumach/components/at_pic.h>
#include <blumach/platforms/null_host.h>
#include "checks.h"
#include "failure_injection_host.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct fixture {
    bm_at_rtc_t *rtc;
    bm_at_pic_t *pic;
    bm_pcs286_checks_t checks;
    unsigned irq_calls, mask_calls;
    int irq, mask, nmi, inspect, fail_after, fail_mask;
    bm_status_t failure;
} fixture_t;
static bm_bus_transaction_t transaction(unsigned port, bm_bus_operation_t op, unsigned value)
{
    bm_bus_transaction_t t = {0};
    t.space = BM_ADDRESS_IO; t.operation = op; t.address = port; t.size = 1;
    t.value = value; t.wait_states = 19; return t;
}
static bm_status_t io(fixture_t *f, unsigned port, bm_bus_operation_t op, unsigned value)
{
    bm_bus_transaction_t t = transaction(port, op, value);
    return bm_at_rtc_io(f->rtc, &t);
}
static void select_reg(fixture_t *f, unsigned index)
{
    assert(io(f, 0x70, BM_BUS_WRITE, index | 0x80) == BM_STATUS_OK);
}
static void wr(fixture_t *f, unsigned index, unsigned value)
{
    select_reg(f, index); assert(io(f, 0x71, BM_BUS_WRITE, value) == BM_STATUS_OK);
}
static unsigned rd(fixture_t *f, unsigned index)
{
    bm_bus_transaction_t t = transaction(0x71, BM_BUS_READ, 0xbeef);
    select_reg(f, index);
    assert(bm_at_rtc_io(f->rtc, &t) == BM_STATUS_OK && t.wait_states == 19);
    return (unsigned)t.value;
}
static uint8_t peek(fixture_t *f, unsigned index)
{
    uint8_t bytes[128];
    assert(bm_at_rtc_export_cmos(f->rtc, bytes, 128) == BM_STATUS_OK);
    return bytes[index];
}
static bm_at_rtc_state_t state(fixture_t *f)
{
    bm_at_rtc_state_t s;
    assert(bm_at_rtc_state(f->rtc, &s) == BM_STATUS_OK); return s;
}
static void advance(fixture_t *f, uint64_t cycles)
{
    bm_status_t result = bm_at_rtc_advance(f->rtc, cycles);
    if (result != BM_STATUS_OK) {
        fprintf(stderr, "advance failed: status=%d requested=%llu at=%llu B=%02x date=%02x/%02x/%02x time=%02x:%02x:%02x\n",
            (int)result, (unsigned long long)cycles, (unsigned long long)state(f).cycles,
            peek(f,11),peek(f,9),peek(f,8),peek(f,7),peek(f,4),peek(f,2),peek(f,0));
    }
    assert(result == BM_STATUS_OK);
}
static uint64_t next(fixture_t *f)
{
    uint64_t cycles = UINT64_MAX;
    bm_status_t s = bm_at_rtc_next_deadline(f->rtc, &cycles);
    assert(s == (cycles ? BM_STATUS_OK : BM_STATUS_IDLE)); return cycles;
}
static void inspect_callback(fixture_t *f)
{
    bm_bus_transaction_t t = transaction(0x71, BM_BUS_READ, 0xbeef);
    unsigned calls = f->irq_calls + f->mask_calls;
    uint8_t bytes[128];
    bm_at_rtc_state_t s = state(f);
    assert(bm_at_rtc_export_cmos(f->rtc, bytes, 128) == BM_STATUS_OK);
    t.attributes = BM_BUS_TRANSACTION_DEBUG;
    assert(bm_at_rtc_io(f->rtc, &t) == BM_STATUS_OK && t.value == bytes[s.index]);
    t.attributes = 0;
    assert(bm_at_rtc_io(f->rtc, &t) == BM_STATUS_INVALID_STATE);
    assert(bm_at_rtc_advance(f->rtc, 1) == BM_STATUS_INVALID_STATE);
    assert(bm_at_rtc_reset(f->rtc) == BM_STATUS_INVALID_STATE);
    bm_at_rtc_destroy(f->rtc); /* Reentrant destruction cannot free the object. */
    assert(calls == f->irq_calls + f->mask_calls);
}
static bm_status_t nmi(void *context, int level)
{
    fixture_t *f = context; f->nmi = level; return BM_STATUS_OK;
}
static bm_status_t output(fixture_t *f, int level, int mask)
{
    bm_status_t result = BM_STATUS_OK;
    int fail = f->failure != BM_STATUS_OK && f->fail_mask == mask;
    if (mask) ++f->mask_calls; else ++f->irq_calls;
    if (f->inspect) inspect_callback(f);
    if (fail && !f->fail_after) return f->failure;
    if (mask) {
        f->mask = level;
        if (f->pic) result = bm_pcs286_checks_mask(&f->checks, level);
    } else {
        f->irq = level;
        if (f->pic) result = bm_at_pic_set_irq(f->pic, 8, level);
    }
    return fail ? f->failure : result;
}
static bm_status_t irq(void *context, int level) { return output(context, level, 0); }
static bm_status_t mask(void *context, int level) { return output(context, level, 1); }
static uint8_t enc(unsigned n, unsigned mode) { return (uint8_t)((mode & 4) ? n : (n / 10) * 16 + n % 10); }
static void seed(uint8_t *bytes, unsigned mode, unsigned year, unsigned month,
                 unsigned day, unsigned dow, unsigned hour, unsigned minute, unsigned second)
{
    memset(bytes, 0, 128); bytes[10] = 0x20; bytes[11] = (uint8_t)mode;
    bytes[0] = enc(second, mode); bytes[2] = enc(minute, mode);
    bytes[4] = (mode & 2) ? enc(hour, mode) :
        (uint8_t)(enc(hour % 12 ? hour % 12 : 12, mode) | (hour >= 12 ? 0x80 : 0));
    bytes[6] = enc(dow, mode); bytes[7] = enc(day, mode);
    bytes[8] = enc(month, mode); bytes[9] = enc(year, mode);
    bytes[1] = bytes[3] = bytes[5] = 0xc0; bytes[0x32] = 0x19;
}
static void start(fixture_t *f, uint8_t *bytes)
{
    bm_host_services_t host = bm_null_host_services();
    bm_at_rtc_config_t c = {0};
    memset(f, 0, sizeof(*f)); f->mask = 1;
    c.io_base = 0x70; c.cmos_size = 128; c.initial_cmos = bytes;
    c.initial_cmos_size = bytes ? 128 : 0; c.battery_valid = bytes != NULL;
    c.irq = irq; c.nmi_mask = mask; c.output_context = f;
    assert(bm_at_rtc_create(&host, &c, &f->rtc) == BM_STATUS_OK);
    assert(!f->irq_calls && !f->mask_calls);
}
static void done(fixture_t *f) { bm_at_rtc_destroy(f->rtc); }
static void calendar_cases(void)
{
    fixture_t f;
    uint8_t bytes[128], want[128], got[128];
    unsigned cases = 0;
    static const unsigned years[] = {0, 1, 4, 96, 99};
    static const unsigned ends[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    for (unsigned mode = 0; mode <= 6; mode += 2) {
        for (unsigned y = 0; y < 5; ++y) for (unsigned m = 1; m <= 12; ++m) {
            unsigned end = ends[m - 1] + (m == 2 && years[y] % 4 == 0);
            seed(bytes, mode, years[y], m, end, 7, 23, 59, 59);
            seed(want, mode, (years[y] + (m == 12)) % 100, m == 12 ? 1 : m + 1, 1, 1, 0, 0, 0);
            start(&f, bytes); advance(&f, 16449);
            assert(bm_at_rtc_export_cmos(f.rtc, got, 128) == BM_STATUS_OK);
            assert(!memcmp(got, want, 10) && got[0x32] == 0x19 && got[12] == 0x30);
            done(&f); ++cases;
        }
        for (unsigned hour = 0; hour < 24; ++hour) {
            seed(bytes, mode, 26, 9, 25, 6, hour, 59, 59);
            seed(want, mode, 26, 9, hour == 23 ? 26 : 25, hour == 23 ? 7 : 6, (hour + 1) % 24, 0, 0);
            start(&f, bytes); advance(&f, 16449);
            assert(bm_at_rtc_export_cmos(f.rtc, got, 128) == BM_STATUS_OK);
            assert(!memcmp(got, want, 10)); done(&f); ++cases;
        }
        /* February 28 -> 29 then March 1, and the first minute carry. */
        seed(bytes, mode, 24, 2, 28, 3, 23, 59, 59);
        start(&f, bytes); advance(&f, 16449); assert(peek(&f, 7) == enc(29, mode));
        wr(&f, 11, mode | 0x80); wr(&f, 0, enc(59, mode)); wr(&f, 2, enc(59, mode));
        wr(&f, 4, (mode & 2) ? enc(23, mode) : enc(11, mode) | 0x80);
        wr(&f, 11, mode); advance(&f, 32768);
        assert(peek(&f, 7) == 1 && peek(&f, 8) == 3); done(&f);
    }
    assert(cases == 336);
}
static void rates_and_flags(void)
{
    fixture_t f;
    uint8_t bytes[128];
    /* Independent table from Motorola table5, 32.768-kHz column. */
    static const unsigned periods[16] = {0,128,256,4,8,16,32,64,128,256,512,1024,2048,4096,8192,16384};
    for (unsigned rs = 0; rs < 16; ++rs) {
        seed(bytes, 0x86, 26, 1, 1, 1, 0, 0, 0); bytes[10] |= (uint8_t)rs;
        start(&f, bytes); assert(next(&f) == periods[rs]);
        if (!rs) { advance(&f, 1000000); assert(!peek(&f, 12)); }
        else {
            advance(&f, periods[rs] - 1); assert(!peek(&f, 12)); advance(&f, 1);
            assert(peek(&f, 12) == 0x40 && !f.irq && !next(&f));
            wr(&f, 11, 0xc6); assert(f.irq && peek(&f, 12) == 0xc0 && f.irq_calls == 1);
            advance(&f, periods[rs] * 5); assert(f.irq_calls == 1);
            assert(rd(&f, 12) == 0xc0 && !f.irq && !peek(&f, 12));
            assert(next(&f) == periods[rs]); advance(&f, periods[rs]); assert(f.irq);
            wr(&f, 11, 0x86); assert(!f.irq && peek(&f, 12) == 0x40);
        }
        done(&f);
    }
    seed(bytes, 0x86, 26, 1, 1, 1, 0, 0, 0); start(&f, bytes);
    advance(&f, 7); wr(&f, 10, 0x23); assert(next(&f) == 1); /* No RS phase restart. */
    advance(&f, 1); assert(rd(&f, 12) == 0x40);
    wr(&f, 10, 0xe6); assert(peek(&f, 10) == 0x66 && !next(&f));
    advance(&f, 10000); assert(state(&f).divider_phase == 0);
    wr(&f, 10, 0x20); wr(&f, 11, 6); assert(next(&f) == 16376);
    done(&f);
    /* Each enable controls IRQF, never whether a source flag is captured. */
    for (unsigned enables = 0; enables < 8; ++enables) {
        seed(bytes, 6, 26, 1, 1, 1, 0, 0, 0); bytes[10] = 0x26; start(&f, bytes);
        advance(&f, 16449); assert(peek(&f, 12) == 0x70);
        wr(&f, 11, 6 | (enables << 4));
        assert(f.irq == !!enables && peek(&f, 12) == (enables ? 0xf0 : 0x70));
        assert(rd(&f, 12) == (enables ? 0xf0 : 0x70) && !f.irq);
        assert(!rd(&f, 12)); done(&f);
    }
}
static void update_and_set(void)
{
    fixture_t f;
    uint8_t bytes[128];
    bm_bus_transaction_t t, before;
    seed(bytes, 6, 26, 1, 1, 1, 0, 0, 0); start(&f, bytes);
    wr(&f, 11, 0x16); advance(&f, 16375); assert(rd(&f, 10) == 0x20);
    advance(&f, 1); assert(rd(&f, 10) == 0xa0 && rd(&f, 0) == 0 && next(&f) == 8);
    advance(&f, 8); assert(state(&f).updating && next(&f) == 65);
    select_reg(&f, 0); t = transaction(0x71, BM_BUS_READ, 0xbeef); before = t;
    assert(bm_at_rtc_io(f.rtc, &t) == BM_STATUS_UNSUPPORTED && !memcmp(&t, &before, sizeof(t)));
    t.operation = BM_BUS_WRITE; assert(bm_at_rtc_io(f.rtc, &t) == BM_STATUS_UNSUPPORTED);
    t.operation = BM_BUS_READ; t.attributes = BM_BUS_TRANSACTION_DEBUG;
    assert(bm_at_rtc_io(f.rtc, &t) == BM_STATUS_OK && !t.value);
    wr(&f, 0x40, 0xa5); assert(rd(&f, 0x40) == 0xa5); /* RAM available during update. */
    assert(bm_at_rtc_reset(f.rtc) == BM_STATUS_OK);
    assert(state(&f).updating && state(&f).cycles == 16384 && rd(&f, 11) == 6);
    advance(&f, 64); assert(!peek(&f, 12) && state(&f).uip);
    advance(&f, 1); assert(rd(&f, 0) == 1 && rd(&f, 12) == 0x30 && !f.irq);
    advance(&f, 32695); assert(state(&f).uip && !state(&f).updating);
    wr(&f, 11, 0xfe); assert(rd(&f, 11) == 0xee && !state(&f).uip); /* SET clears UIE. */
    advance(&f, 100000); assert(rd(&f, 0) == 1 && !state(&f).uip);
    done(&f);
    /* SET during actual transfer aborts it; releasing SET does not reset phase. */
    start(&f, bytes); advance(&f, 16390); wr(&f, 11, 0x86); wr(&f, 0, 41);
    wr(&f, 11, 6); advance(&f, 59); assert(rd(&f, 0) == 41 && !peek(&f, 12));
    advance(&f, 32768); assert(rd(&f, 0) == 42); done(&f);
    /* Invalid guest programming is preserved, never normalized to a valid date. */
    start(&f, bytes); wr(&f, 11, 0x82); wr(&f, 8, 0x1a); wr(&f, 11, 2);
    assert(bm_at_rtc_advance(f.rtc, 20000) == BM_STATUS_UNSUPPORTED);
    assert(state(&f).cycles == 16449 && !state(&f).uip && peek(&f, 8) == 0x1a && !peek(&f, 12));
    wr(&f, 11, 0x82); wr(&f, 8, 1); wr(&f, 9, 0x26); wr(&f, 11, 2); advance(&f, 32768);
    assert(rd(&f, 0) == 1); done(&f);
}
static void alarm_cases(void)
{
    fixture_t f;
    uint8_t bytes[128];
    for (unsigned mode = 0; mode <= 6; mode += 2) for (unsigned bits = 0; bits < 8; ++bits) {
        seed(bytes, mode, 26, 1, 1, 1, 12, 34, 55);
        bytes[1] = bits & 1 ? 0xc5 : enc(56, mode);
        bytes[3] = bits & 2 ? 0xff : enc(34, mode);
        bytes[5] = bits & 4 ? 0xdf : bytes[4]; start(&f, bytes);
        wr(&f, 11, mode | 0x20); advance(&f, 16449);
        assert(f.irq && rd(&f, 12) == 0xb0);
        advance(&f, 32768); assert(!!(peek(&f, 12) & 0x20) == !!(bits & 1)); done(&f);
    }
    for (unsigned index = 1; index <= 5; index += 2) {
        seed(bytes, 6, 26, 1, 1, 1, 0, 0, 0); bytes[index] = 0x80; /* 10xxxxxx is NOT wildcard. */
        start(&f, bytes); advance(&f, 16449); assert(rd(&f, 12) == 0x10); done(&f);
    }
}
static void raw_cmos_and_reset(void)
{
    fixture_t f;
    uint8_t bytes[128], snapshot[128];
    bm_bus_transaction_t t;
    seed(bytes, 6, 26, 1, 1, 1, 0, 0, 0); start(&f, bytes);
    memset(bytes, 0xee, sizeof(bytes)); assert(rd(&f, 0) == 0); /* Copied. */
    for (unsigned i = 14; i < 128; ++i) wr(&f, i, i ^ 0xa5);
    wr(&f, 0, 0x80); assert(!rd(&f, 0)); wr(&f, 10, 0xa6); assert(rd(&f, 10) == 0x26);
    wr(&f, 11, 0x7e); advance(&f, 16449); assert(f.irq);
    wr(&f, 12, 0); wr(&f, 13, 0); assert(peek(&f, 12) == 0xf0 && peek(&f, 13) == 0x80);
    select_reg(&f, 12); t = transaction(0x71, BM_BUS_READ, 0); t.attributes = BM_BUS_TRANSACTION_DEBUG;
    assert(bm_at_rtc_io(f.rtc, &t) == BM_STATUS_OK && t.value == 0xf0 && f.irq);
    assert(bm_at_rtc_export_cmos(f.rtc, snapshot, 128) == BM_STATUS_OK);
    assert(bm_at_rtc_reset(f.rtc) == BM_STATUS_OK && !f.irq && f.mask == 1);
    assert(rd(&f, 11) == 6 && rd(&f, 10) == 0x26 && rd(&f, 0) == 1 && !rd(&f, 12));
    for (unsigned i = 14; i < 128; ++i) assert(rd(&f, i) == snapshot[i]);
    assert(!state(&f).failure && state(&f).cycles == 16449);
    done(&f);
    start(&f, NULL); assert(!next(&f) && peek(&f, 10) == 0x60 && peek(&f, 11) == 0x80);
    select_reg(&f, 13); t = transaction(0x71, BM_BUS_READ, 0); t.attributes = BM_BUS_TRANSACTION_DEBUG;
    assert(bm_at_rtc_io(f.rtc, &t) == BM_STATUS_OK && !t.value && !peek(&f, 13));
    assert(rd(&f, 13) == 0 && rd(&f, 13) == 0x80); /* MC146818A VRT read acknowledgment. */
    advance(&f, UINT64_MAX); assert(state(&f).cycles == UINT64_MAX);
    assert(bm_at_rtc_advance(f.rtc, 1) == BM_STATUS_CAPACITY_EXCEEDED && state(&f).cycles == UINT64_MAX);
    done(&f);
}
static void failure_cases(void)
{
    fixture_t f;
    uint8_t bytes[128];
    bm_bus_transaction_t t, before;
    for (int mask_output = 0; mask_output <= 1; ++mask_output)
    for (int high = 0; high <= 1; ++high) for (int after = 0; after <= 1; ++after) {
        unsigned calls;
        seed(bytes, 6, 26, 1, 1, 1, 0, 0, 0); start(&f, bytes); f.inspect = 1;
        if (mask_output) {
            if (high) assert(io(&f, 0x70, BM_BUS_WRITE, 0x32) == BM_STATUS_OK);
            t = transaction(0x70, BM_BUS_WRITE, (high ? 0x80 : 0) | 0x32);
        } else {
            advance(&f, 16449);
            if (!high) wr(&f, 11, 0x16);
            select_reg(&f, high ? 11 : 12);
            t = transaction(0x71, high ? BM_BUS_WRITE : BM_BUS_READ, high ? 0x16 : 0xbeef);
        }
        before = t; f.failure = BM_STATUS_DEVICE_ERROR; f.fail_after = after; f.fail_mask = mask_output;
        assert(bm_at_rtc_io(f.rtc, &t) == BM_STATUS_DEVICE_ERROR && !memcmp(&t, &before, sizeof(t)));
        assert(state(&f).failure == BM_STATUS_DEVICE_ERROR);
        if (mask_output) assert(state(&f).nmi_mask == high && state(&f).index == 0x32);
        else assert(state(&f).irq == high && peek(&f, 12) == (high ? 0xb0 : 0));
        assert((mask_output ? f.mask : f.irq) == (after ? high : !high));
        calls = f.irq_calls + f.mask_calls;
        assert(bm_at_rtc_io(f.rtc, &t) == BM_STATUS_DEVICE_ERROR);
        assert(bm_at_rtc_advance(f.rtc, 32768) == BM_STATUS_DEVICE_ERROR);
        assert(calls == f.irq_calls + f.mask_calls);
        t.operation = BM_BUS_READ; t.attributes = BM_BUS_TRANSACTION_DEBUG;
        assert(bm_at_rtc_io(f.rtc, &t) == BM_STATUS_OK);
        f.failure = BM_STATUS_OK; assert(bm_at_rtc_reset(f.rtc) == BM_STATUS_OK);
        assert(!state(&f).failure && !f.irq && f.mask); done(&f);
    }
    seed(bytes, 6, 26, 1, 1, 1, 0, 0, 0); bytes[10] = 0x23; start(&f, bytes);
    wr(&f, 11, 0x46); f.failure = BM_STATUS_IDLE;
    assert(bm_at_rtc_advance(f.rtc, 100) == BM_STATUS_INVALID_STATE);
    assert(state(&f).cycles == 4 && state(&f).failure == BM_STATUS_INVALID_STATE && peek(&f, 12) == 0xc0);
    f.fail_mask = 1; assert(bm_at_rtc_reset(f.rtc) == BM_STATUS_INVALID_STATE);
    f.failure = BM_STATUS_OK; assert(bm_at_rtc_reset(f.rtc) == BM_STATUS_OK); done(&f);
}
static void pic_write(bm_at_pic_t *pic, unsigned port, unsigned value)
{
    bm_bus_transaction_t t = transaction(port, BM_BUS_WRITE, value);
    assert(bm_at_pic_io(pic, &t) == BM_STATUS_OK);
}
static void integration(void)
{
    fixture_t f;
    uint8_t bytes[128], vector = 0xa5, bits;
    bm_host_services_t h = bm_null_host_services();
    bm_at_pic_config_t pc = {0}; bm_at_pic_state_t ps;
    seed(bytes, 6, 26, 1, 1, 1, 0, 0, 0); start(&f, bytes);
    pc.master_base = 0x20; pc.slave_base = 0xa0; pc.cascade_line = 2;
    assert(bm_at_pic_create(&h, &pc, &f.pic) == BM_STATUS_OK);
    assert(bm_pcs286_checks_initialize(&f.checks, nmi, &f) == BM_STATUS_OK);
    pic_write(f.pic, 0x20, 0x11); pic_write(f.pic, 0xa0, 0x11);
    pic_write(f.pic, 0x21, 0x30); pic_write(f.pic, 0xa1, 0x70);
    pic_write(f.pic, 0x21, 4); pic_write(f.pic, 0xa1, 2);
    pic_write(f.pic, 0x21, 1); pic_write(f.pic, 0xa1, 1);
    pic_write(f.pic, 0x21, 0xfb); pic_write(f.pic, 0xa1, 0xfe);
    wr(&f, 11, 0x16); advance(&f, 16449);
    assert(bm_at_pic_state(f.pic, &ps) == BM_STATUS_OK && ps.intr);
    assert(bm_at_pic_acknowledge(f.pic, 0, &vector) == BM_STATUS_OK && vector == 0xa5);
    assert(bm_at_pic_acknowledge(f.pic, 1, &vector) == BM_STATUS_OK && vector == 0x70);
    assert(rd(&f, 12) == 0xb0 && !f.irq);
    pic_write(f.pic, 0xa0, 0x20); pic_write(f.pic, 0x20, 0x20);
    advance(&f, 32768); assert(bm_at_pic_state(f.pic, &ps) == BM_STATUS_OK && ps.intr);
    assert(bm_pcs286_checks_memory_sample(&f.checks, 1) == BM_STATUS_OK && !f.nmi);
    assert(io(&f, 0x70, BM_BUS_WRITE, 0x32) == BM_STATUS_OK && f.nmi);
    assert(io(&f, 0x71, BM_BUS_WRITE, 0x20) == BM_STATUS_OK && f.nmi);
    assert(io(&f, 0x70, BM_BUS_WRITE, 0xb2) == BM_STATUS_OK && !f.nmi);
    assert(bm_pcs286_checks_status(&f.checks, &bits) == BM_STATUS_OK && bits == 0x80);
    assert(bm_at_rtc_reset(f.rtc) == BM_STATUS_OK); bm_at_pic_destroy(f.pic); done(&f);
}
static void partition_and_isolation(void)
{
    fixture_t a, b, c;
    uint8_t bytes[128], x[128], y[128];
    uint64_t left = 3000000; unsigned random = 12345;
    seed(bytes, 2, 99, 12, 31, 7, 23, 59, 40); bytes[10] = 0x26;
    start(&a, bytes); start(&b, bytes); start(&c, bytes);
    wr(&a, 11, 0x72); wr(&b, 11, 0x72); advance(&a, left);
    while (left) { unsigned step; random = random * 1664525U + 1013904223U;
        step = 1 + random % 5000; if (step > left) step = (unsigned)left;
        advance(&b, step); left -= step; }
    assert(bm_at_rtc_export_cmos(a.rtc, x, 128) == BM_STATUS_OK);
    assert(bm_at_rtc_export_cmos(b.rtc, y, 128) == BM_STATUS_OK && !memcmp(x, y, 128));
    assert(state(&a).divider_phase == state(&b).divider_phase && a.irq_calls == b.irq_calls);
    assert(peek(&c, 0) == 0x40 && state(&c).cycles == 0 && !c.irq_calls);
    done(&a); done(&b); done(&c);
}
static void invalid_and_capacity(void)
{
    fixture_t f = {0}; bm_host_services_t h = bm_null_host_services();
    bm_at_rtc_config_t c = {0}; bm_at_rtc_t *rtc = NULL;
    failure_injection_host_t allocator; uint8_t bytes[128];
    bm_bus_transaction_t t, before;
    c.io_base = 0x70; c.cmos_size = 64; c.irq = irq; c.nmi_mask = mask; c.output_context = &f;
    assert(bm_at_rtc_create(&h, &c, &rtc) == BM_STATUS_OK);
    f.rtc = rtc; assert(io(&f, 0x70, BM_BUS_WRITE, 0xff) == BM_STATUS_OK);
    assert(state(&f).index == 63 && state(&f).nmi_mask);
    assert(io(&f, 0x71, BM_BUS_WRITE, 0xa5) == BM_STATUS_OK);
    assert(bm_at_rtc_export_cmos(rtc, bytes, 64) == BM_STATUS_OK && bytes[63] == 0xa5);
    assert(bm_at_rtc_export_cmos(rtc, bytes, 128) == BM_STATUS_INVALID_ARGUMENT); done(&f);
    c.cmos_size = 65; assert(bm_at_rtc_create(&h, &c, &rtc) == BM_STATUS_INVALID_ARGUMENT && !rtc);
    c.cmos_size = 128; c.battery_valid = 1;
    assert(bm_at_rtc_create(&h, &c, &rtc) == BM_STATUS_INVALID_ARGUMENT);
    c.battery_valid = 0; c.io_base = 0xffff;
    assert(bm_at_rtc_create(&h, &c, &rtc) == BM_STATUS_INVALID_ARGUMENT);
    c.io_base = 0x70; c.irq = NULL; assert(bm_at_rtc_create(&h, &c, &rtc) == BM_STATUS_INVALID_ARGUMENT);
    c.irq = irq; c.initial_cmos_size = 128;
    assert(bm_at_rtc_create(&h, &c, &rtc) == BM_STATUS_INVALID_ARGUMENT);
    c.initial_cmos = bytes; seed(bytes, 6, 26, 1, 1, 1, 0, 0, 0); bytes[10] = 0x10;
    assert(bm_at_rtc_create(&h, &c, &rtc) == BM_STATUS_UNSUPPORTED);
    bytes[10] = 0x20; bytes[11] = 7;
    assert(bm_at_rtc_create(&h, &c, &rtc) == BM_STATUS_UNSUPPORTED);
    bytes[11] = 6; failure_injection_host_initialize(&allocator);
    h = failure_injection_host_services(&allocator); failure_injection_host_fail_after(&allocator, 0);
    assert(bm_at_rtc_create(&h, &c, &rtc) == BM_STATUS_OUT_OF_MEMORY && !rtc && !allocator.outstanding_allocations);
    failure_injection_host_fail_on(&allocator, SIZE_MAX);
    assert(bm_at_rtc_create(&h, &c, &rtc) == BM_STATUS_OK && allocator.outstanding_allocations == 1);
    bm_at_rtc_destroy(rtc); assert(!allocator.outstanding_allocations);
    start(&f, bytes); select_reg(&f, 10);
    for (unsigned dv = 0; dv < 8; ++dv) if (dv != 2 && dv < 6) {
        assert(io(&f, 0x71, BM_BUS_WRITE, dv << 4) == BM_STATUS_UNSUPPORTED && peek(&f, 10) == 0x20);
    }
    select_reg(&f, 11); assert(io(&f, 0x71, BM_BUS_WRITE, 7) == BM_STATUS_UNSUPPORTED && peek(&f, 11) == 6);
    t = transaction(0x71, BM_BUS_READ, 0xbeef); t.attributes = BM_BUS_TRANSACTION_DEBUG;
    t.operation = BM_BUS_WRITE; before = t;
    assert(bm_at_rtc_io(f.rtc, &t) == BM_STATUS_UNSUPPORTED && !memcmp(&t, &before, sizeof(t)));
    t = transaction(0x70, BM_BUS_READ, 0xbeef); before = t;
    assert(bm_at_rtc_io(f.rtc, &t) == BM_STATUS_UNMAPPED && !memcmp(&t, &before, sizeof(t)));
    t.address = 0x72; assert(bm_at_rtc_io(f.rtc, &t) == BM_STATUS_UNMAPPED);
    t.address = 0x71; t.size = 2; assert(bm_at_rtc_io(f.rtc, &t) == BM_STATUS_UNSUPPORTED);
    t.size = 1; t.operation = BM_BUS_FETCH; assert(bm_at_rtc_io(f.rtc, &t) == BM_STATUS_UNSUPPORTED);
    t.operation = BM_BUS_READ; t.alignment = 2; assert(bm_at_rtc_io(f.rtc, &t) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_at_rtc_io(NULL, &t) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_at_rtc_reset(NULL) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_at_rtc_state(f.rtc, NULL) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_at_rtc_next_deadline(f.rtc, NULL) == BM_STATUS_INVALID_ARGUMENT);
    done(&f);
}
static void classic_divider_policy(void)
{
    fixture_t f = {0};
    uint8_t bytes[128];
    bm_host_services_t h = bm_null_host_services();
    bm_at_rtc_config_t c = {0};
    seed(bytes, 6, 26, 1, 1, 1, 0, 0, 0);
    c.io_base=0x70; c.cmos_size=128; c.initial_cmos=bytes; c.initial_cmos_size=128;
    c.battery_valid=1; c.irq=irq; c.nmi_mask=mask; c.output_context=&f;
    c.divider_policy=(bm_at_rtc_divider_policy_t)2;
    assert(bm_at_rtc_create(&h,&c,&f.rtc)==BM_STATUS_INVALID_ARGUMENT && !f.rtc);
    c.divider_policy=BM_AT_RTC_DIVIDER_CLASSIC_STOP; bytes[10]=0;
    assert(bm_at_rtc_create(&h,&c,&f.rtc)==BM_STATUS_OK);
    for (unsigned dv=0;dv<8;++dv) if (dv!=2) {
        unsigned second=rd(&f,0);
        wr(&f,10,0x60); /* Explicit divider reset before each new trial. */
        wr(&f,10,0x20); advance(&f,16376); assert(state(&f).uip);
        wr(&f,10,(dv<<4)|6U);
        assert(rd(&f,10)==((dv<<4)|6U) && next(&f)==0);
        assert(!state(&f).uip && !state(&f).updating && !state(&f).divider_phase);
        (void)rd(&f,12); advance(&f,65536);
        assert(rd(&f,0)==second && rd(&f,12)==0 && !state(&f).divider_phase);
        assert(bm_at_rtc_reset(f.rtc)==BM_STATUS_OK && rd(&f,10)==((dv<<4)|6U));
        wr(&f,10,0x20); assert(next(&f)==16376);
        advance(&f,16449); assert(rd(&f,0)==second+1U && (rd(&f,12)&0x10U));
    }
    done(&f);
}
int main(void)
{
    invalid_and_capacity(); calendar_cases(); rates_and_flags(); update_and_set(); alarm_cases();
    raw_cmos_and_reset(); failure_cases(); integration(); partition_and_isolation();
    classic_divider_policy();
    puts("AT RTC:336 calendar rollovers,all rates/enables,BCD/12h,UIP/SET,alarms,C/D,CMOS,NMI/PIC,failure/reset/partition tests passed");
    return 0;
}
