/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Authored functional AT cases, no firmware or captured keyboard traffic. */
#include <blumach/components/kbc8042.h>
#include <blumach/components/at_pic.h>
#include <blumach/platforms/null_host.h>
#include "failure_injection_host.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { IRQ, A20, RESET, INHIBIT, KEYBOARD };
typedef struct fixture {
    bm_kbc8042_t *k;
    bm_at_pic_t *pic;
    int levels[4], fail_line, after, inspect, blocked, auxiliary_blocked;
    unsigned calls[5], count;
    uint8_t bytes[16];
    unsigned auxiliary_count;
    uint8_t auxiliary_bytes[16];
    bm_status_t failure;
} fixture_t;
static bm_bus_transaction_t txn(unsigned port, bm_bus_operation_t op, unsigned v)
{
    bm_bus_transaction_t t = {0};
    t.space = BM_ADDRESS_IO; t.address = port; t.operation = op;
    t.size = 1; t.value = v; t.wait_states = 17; return t;
}
static bm_kbc8042_state_t state(fixture_t *f)
{
    bm_kbc8042_state_t s; assert(bm_kbc8042_state(f->k, &s) == BM_STATUS_OK); return s;
}
static int same_state(const bm_kbc8042_state_t *a, const bm_kbc8042_state_t *b)
{
    /* Struct-return/assignment padding is unspecified, especially optimized
     * tail padding after the vendor latch. Compare every observable field. */
#define SAME(field) (a->field == b->field)
    return SAME(cycles) && SAME(input_remaining) && SAME(output_remaining) &&
        SAME(pulse_remaining) && SAME(status) && SAME(command_byte) &&
        SAME(output_port) && SAME(output_byte) && SAME(parameter) && SAME(irq) &&
        SAME(a20) && SAME(cpu_reset) && SAME(inhibited) && SAME(break_pending) &&
        SAME(auxiliary_enabled) &&
        SAME(failure) && SAME(olivetti_p2) &&
        !memcmp(a->internal_ram, b->internal_ram, sizeof(a->internal_ram));
#undef SAME
}
static bm_status_t wr(fixture_t *f, unsigned port, unsigned v)
{
    bm_bus_transaction_t t = txn(port, BM_BUS_WRITE, v), old = t;
    bm_status_t r = bm_kbc8042_io(f->k, &t);
    assert(memcmp(&old, &t, sizeof(t)) == 0); return r;
}
static unsigned rd(fixture_t *f, unsigned port, int debug)
{
    bm_bus_transaction_t t = txn(port, BM_BUS_READ, 0xbeef);
    t.attributes = debug ? BM_BUS_TRANSACTION_DEBUG : 0;
    assert(bm_kbc8042_io(f->k, &t) == BM_STATUS_OK && t.wait_states == 17);
    return (unsigned)t.value;
}
static void advance(fixture_t *f, uint64_t n)
{
    assert(bm_kbc8042_advance(f->k, n) == BM_STATUS_OK);
}
static void cmd(fixture_t *f, unsigned v)
{
    assert(wr(f, 0x64, v) == BM_STATUS_OK); advance(f, v == 0xaa ? 12 : 2);
}
static void data(fixture_t *f, unsigned v)
{
    assert(wr(f, 0x60, v) == BM_STATUS_OK); advance(f, 2);
}
static void ccb(fixture_t *f, unsigned v) { cmd(f, 0x60); data(f, v); }
static unsigned response(fixture_t *f) { advance(f, 3); return rd(f, 0x60, 0); }
static void callback_inspect(fixture_t *f)
{
    bm_kbc8042_state_t s = state(f);
    assert(rd(f, 0x64, 1) == s.status);
    assert(wr(f, 0x64, 0x20) == BM_STATUS_INVALID_STATE);
    assert(bm_kbc8042_advance(f->k, 1) == BM_STATUS_INVALID_STATE);
    assert(bm_kbc8042_receive_keyboard(f->k, 0x1c) == BM_STATUS_INVALID_STATE);
    assert(bm_kbc8042_reset(f->k) == BM_STATUS_INVALID_STATE);
    bm_kbc8042_destroy(f->k); /* Must not free a callback's live owner. */
}
static bm_status_t output(fixture_t *f, unsigned which, int level)
{
    int failing = f->failure != BM_STATUS_OK && (unsigned)f->fail_line == which;
    ++f->calls[which];
    if (f->inspect) callback_inspect(f);
    if (failing && !f->after) return f->failure;
    f->levels[which] = level;
    if (which == IRQ && f->pic) assert(bm_at_pic_set_irq(f->pic, 1, level) == BM_STATUS_OK);
    return failing ? f->failure : BM_STATUS_OK;
}
static bm_status_t irq(void *p, int v) { return output(p, IRQ, v); }
static bm_status_t a20(void *p, int v) { return output(p, A20, v); }
static bm_status_t reset(void *p, int v) { return output(p, RESET, v); }
static bm_status_t inhibit(void *p, int v) { return output(p, INHIBIT, v); }
static bm_status_t keyboard(void *p, uint8_t v)
{
    fixture_t *f = p;
    int failing = f->failure != BM_STATUS_OK && f->fail_line == KEYBOARD;
    ++f->calls[KEYBOARD];
    if (f->inspect) callback_inspect(f);
    if (f->blocked) return BM_STATUS_IDLE;
    if (failing && !f->after) return f->failure;
    assert(f->count < sizeof(f->bytes)); f->bytes[f->count++] = v;
    return failing ? f->failure : BM_STATUS_OK;
}
static bm_kbc8042_config_t config(fixture_t *f)
{
    bm_kbc8042_config_t c = {0};
    c.data_port = 0x60; c.command_port = 0x64;
    c.irq = irq; c.a20 = a20; c.cpu_reset = reset; c.output_context = f;
    c.keyboard_inhibit = inhibit; c.keyboard_command = keyboard; c.keyboard_context = f;
    c.clock.cycles_per_second_numerator = 1000000;
    c.clock.cycles_per_second_denominator = 1;
    c.input_cycles = 2; c.self_test_cycles = 10; c.output_cycles = 3; c.pulse_cycles = 6;
    c.input_port = 0xa0; c.initial_output_port = 0xc1;
    return c;
}
static void start(fixture_t *f)
{
    bm_host_services_t host = bm_null_host_services();
    bm_kbc8042_config_t c;
    memset(f, 0, sizeof(*f)); f->levels[INHIBIT] = 1; c = config(f);
    assert(bm_kbc8042_create(&host, &c, &f->k) == BM_STATUS_OK);
    for (unsigned i = 0; i < 5; ++i) assert(!f->calls[i]);
}
static void done(fixture_t *f) { bm_kbc8042_destroy(f->k); }
static void buffers(void)
{
    fixture_t f; bm_bus_transaction_t t, old; uint64_t n = 99;
    start(&f);
    assert(state(&f).command_byte == 0x10 && state(&f).status == 0);
    assert(bm_kbc8042_next_deadline(f.k, &n) == BM_STATUS_IDLE && n == 0);
    assert(bm_kbc8042_receive_keyboard(f.k, 0x1c) == BM_STATUS_IDLE);
    t = txn(0x60, BM_BUS_READ, 0xbeef); old = t;
    old.value=0;
    assert(bm_kbc8042_io(f.k, &t) == BM_STATUS_OK && !memcmp(&old, &t, sizeof(t)));
    assert(state(&f).status==0 && !f.calls[IRQ]);
    assert(wr(&f, 0x64, 0xaa) == BM_STATUS_OK);
    assert(rd(&f, 0x64, 0) == 0x0a);
    assert(bm_kbc8042_next_deadline(f.k, &n) == BM_STATUS_OK && n == 12);
    assert(wr(&f, 0x64, 0xae) == BM_STATUS_CAPACITY_EXCEEDED);
    advance(&f, 11); assert(rd(&f, 0x64, 0) == 0x0a);
    advance(&f, 1); assert(rd(&f, 0x64, 0) == 0x0c && !f.levels[IRQ]);
    advance(&f, 2); assert(!(rd(&f, 0x64, 0) & 1));
    advance(&f, 1); assert(rd(&f, 0x64, 0) == 0x1d);
    for (unsigned i = 0; i < 10; ++i) assert(rd(&f, 0x60, 1) == 0x55);
    assert(wr(&f, 0x64, 0x20) == BM_STATUS_CAPACITY_EXCEEDED);
    assert(rd(&f, 0x60, 0) == 0x55 && !(rd(&f, 0x64, 0) & 1));
    {
        unsigned calls=f.calls[IRQ];
        uint64_t cycles=state(&f).cycles;
        for (unsigned i=0;i<3;++i) assert(rd(&f,0x60,0)==0x55);
        assert(!(state(&f).status&1) && f.calls[IRQ]==calls && state(&f).cycles==cycles);
        cmd(&f,0x20); /* Reply pending; empty reads must not consume it. */
        assert(rd(&f,0x60,0)==0x55 && state(&f).output_remaining==3);
        assert(response(&f)==0x14); /* AA set the system flag. */
    }
    ccb(&f, 0x45); assert(!f.levels[INHIBIT]);
    assert(bm_kbc8042_receive_keyboard(f.k, 0x1c) == BM_STATUS_OK);
    assert(f.levels[INHIBIT] && !f.levels[IRQ]);
    assert(bm_kbc8042_receive_keyboard(f.k, 0x32) == BM_STATUS_CAPACITY_EXCEEDED);
    advance(&f, 3); assert(f.levels[IRQ]);
    ccb(&f, 0x44); assert(!f.levels[IRQ]); /* Disabling IRQ preserves OBF. */
    ccb(&f, 0x45); assert(f.levels[IRQ]);
    assert(rd(&f, 0x60, 0) == 0x1e && !f.levels[IRQ] && !f.levels[INHIBIT]);
    cmd(&f, 0x20); assert(response(&f) == 0x45); /* Controller reply is raw. */
    cmd(&f, 0xc0); assert(response(&f) == 0xa0);
    cmd(&f, 0xad); assert(f.levels[INHIBIT]);
    assert(bm_kbc8042_receive_keyboard(f.k, 0xfa) == BM_STATUS_IDLE);
    data(&f, 0xed); /* Keyboard-bound host write enables without AE. */
    assert(!f.levels[INHIBIT] && !(state(&f).command_byte & 0x10));
    assert(f.count == 1 && f.bytes[0] == 0xed && !(rd(&f, 0x64, 0) & 1));
    /* A real keyboard must provide its own ACK. */
    assert(bm_kbc8042_receive_keyboard(f.k, 0xfa) == BM_STATUS_OK);
    assert(response(&f) == 0xfa);
    done(&f);
}
static void streams(void)
{
    fixture_t f;
    /* A make/break, right Ctrl, Print Screen and Pause: authored set2->set1. */
    const uint8_t input[] = {0x1c,0xf0,0x1c,0xe0,0x14,0xe0,0xf0,0x14,
        0xe0,0x12,0xe0,0x7c,0xe0,0xf0,0x7c,0xe0,0xf0,0x12,
        0xe1,0x14,0x77,0xe1,0xf0,0x14,0xf0,0x77};
    const uint8_t expected[] = {0x1e,0x9e,0xe0,0x1d,0xe0,0x9d,
        0xe0,0x2a,0xe0,0x37,0xe0,0xb7,0xe0,0xaa,0xe1,0x1d,0x45,0xe1,0x9d,0xc5};
    unsigned out = 0;
    start(&f); ccb(&f, 0x40);
    for (unsigned i = 0; i < sizeof(input); ++i) {
        assert(bm_kbc8042_receive_keyboard(f.k, input[i]) == BM_STATUS_OK);
        if (input[i] == 0xf0) { assert(!state(&f).output_remaining); continue; }
        assert(out < sizeof(expected) && response(&f) == expected[out++]);
    }
    assert(out == sizeof(expected));
    /* Inherited high-bit suppression after malformed break prefix is bounded. */
    assert(bm_kbc8042_receive_keyboard(f.k, 0xf0) == BM_STATUS_OK);
    assert(bm_kbc8042_receive_keyboard(f.k, 0xfa) == BM_STATUS_OK);
    assert(!state(&f).break_pending && !state(&f).output_remaining);
    for (unsigned mode = 0; mode < 2; ++mode) {
        ccb(&f, mode ? 0x60 : 0); /* PC mode overrides translation. */
        for (unsigned i = 0; i < 256; ++i) {
            assert(bm_kbc8042_receive_keyboard(f.k, (uint8_t)i) == BM_STATUS_OK);
            assert(response(&f) == i);
        }
    }
    ccb(&f, 0x40); assert(bm_kbc8042_receive_keyboard(f.k, 0xf0) == BM_STATUS_OK);
    ccb(&f, 0); ccb(&f, 0x40);
    assert(bm_kbc8042_receive_keyboard(f.k, 0x1c) == BM_STATUS_OK);
    assert(response(&f) == 0x1e); /* Mode change cancels partial translation. */
    assert(bm_kbc8042_receive_keyboard(f.k, 0xf0) == BM_STATUS_OK);
    cmd(&f, 0x20);
    assert(bm_kbc8042_receive_keyboard(f.k, 0x1c) == BM_STATUS_CAPACITY_EXCEEDED);
    assert(state(&f).break_pending && response(&f) == 0x40);
    assert(bm_kbc8042_receive_keyboard(f.k, 0x1c) == BM_STATUS_OK);
    assert(response(&f) == 0x9e); /* Rejected delivery did not consume F0 state. */
    done(&f);
}
static void pulses(void)
{
    fixture_t f;
    for (unsigned initial = 0; initial < 16; ++initial) for (unsigned mask = 0; mask < 16; ++mask) {
        unsigned changed = initial & ~mask;
        start(&f); cmd(&f, 0xd1); data(&f, 0xc0 | initial);
        assert(f.levels[A20] == !!(initial & 2) && f.levels[RESET] == !(initial & 1));
        cmd(&f, 0xf0 | mask);
        assert(state(&f).output_port == (0xc0 | (initial & mask)));
        assert(state(&f).pulse_remaining == (changed ? 6U : 0U));
        if (changed) {
            unsigned reset_calls = f.calls[RESET];
            assert(wr(&f, 0x64, 0xfe) == BM_STATUS_UNSUPPORTED);
            advance(&f, 5); assert(f.calls[RESET] == reset_calls);
            advance(&f, 1);
        }
        assert(state(&f).output_port == (0xc0 | initial));
        assert(f.levels[A20] == !!(initial & 2) && f.levels[RESET] == !(initial & 1));
        cmd(&f, 0xd0); assert(response(&f) == (0xa0 | initial));
        done(&f);
    }
    start(&f); cmd(&f, 0xd1); data(&f, 0xc0); /* Reset held low, never forced high. */
    { unsigned calls = f.calls[RESET];
      cmd(&f, 0xfe); advance(&f, 50); assert(f.calls[RESET] == calls); }
    cmd(&f, 0xd1); data(&f, 0xc3);
    assert(!f.levels[RESET] && f.levels[A20]);
    cmd(&f, 0xae); cmd(&f, 0xd0); assert(response(&f) == 0xe3);
    done(&f);
}
static void rejected_and_backpressure(void)
{
    fixture_t f; bm_kbc8042_state_t before, after;
    static const unsigned unsupported[] = {0xa7,0xa8,0xab,0xac,0xe0,0xd2,0xd3,0xd4,0xdd,0xdf,0xc1,0xcf,0x80,0x84};
    start(&f); cmd(&f, 0x60); before = state(&f);
    for (unsigned i = 0; i < sizeof(unsupported)/sizeof(*unsupported); ++i) {
        assert(wr(&f, 0x64, unsupported[i]) == BM_STATUS_UNSUPPORTED);
        after = state(&f); assert(same_state(&before, &after));
    }
    data(&f, 0x82); /* Reserved CCB bits are retained RAM, without line effects. */
    assert(state(&f).command_byte == 0x82 && !f.levels[IRQ] && !f.levels[INHIBIT]);
    cmd(&f, 0x60); cmd(&f, 0xae); data(&f, 0xed); /* Accepted command did. */
    assert(f.count == 1 && f.bytes[0] == 0xed);
    f.blocked = 1; data(&f, 0xf4);
    assert(f.count == 1 && (state(&f).status & 2));
    assert(bm_kbc8042_receive_keyboard(f.k, 0x1c) == BM_STATUS_CAPACITY_EXCEEDED);
    advance(&f, 4); assert(f.count == 1 && f.calls[KEYBOARD] == 4);
    f.blocked = 0; advance(&f, 2);
    assert(f.count == 2 && f.bytes[1] == 0xf4 && !(state(&f).status & 2));
    cmd(&f, 0xd1); assert(wr(&f, 0x60, 0x01) == BM_STATUS_UNSUPPORTED);
    assert(state(&f).parameter == 0xd1); data(&f, 0xc3);
    { bm_bus_transaction_t t = txn(0x64, BM_BUS_WRITE, 0xaa), old;
      t.attributes = BM_BUS_TRANSACTION_DEBUG; old = t;
      assert(bm_kbc8042_io(f.k, &t) == BM_STATUS_UNSUPPORTED && !memcmp(&old, &t, sizeof(t)));
      t = txn(0x63, BM_BUS_READ, 0xbeef); old = t;
      assert(bm_kbc8042_io(f.k, &t) == BM_STATUS_UNMAPPED && !memcmp(&old, &t, sizeof(t)));
      t = txn(0x64, BM_BUS_READ, 0xbeef); t.size = 2;
      assert(bm_kbc8042_io(f.k, &t) == BM_STATUS_UNSUPPORTED);
      t.size = 1; t.operation = BM_BUS_FETCH;
      assert(bm_kbc8042_io(f.k, &t) == BM_STATUS_UNSUPPORTED); }
    done(&f);
}
static void failures(void)
{
    fixture_t f;
    for (unsigned which = 0; which < 5; ++which) for (int after = 0; after < 2; ++after) {
        unsigned calls;
        bm_status_t r;
        start(&f); ccb(&f, 1); f.fail_line = (int)which;
        f.after = after; f.failure = BM_STATUS_DEVICE_ERROR;
        if (which == IRQ) {
            assert(bm_kbc8042_receive_keyboard(f.k, 0x1c) == BM_STATUS_OK);
            r = bm_kbc8042_advance(f.k, 100);
            assert(state(&f).output_byte == 0x1c && (state(&f).status & 1));
        } else if (which == INHIBIT) r = bm_kbc8042_receive_keyboard(f.k, 0x1c);
        else if (which == KEYBOARD) {
            assert(wr(&f, 0x60, 0xed) == BM_STATUS_OK);
            r = bm_kbc8042_advance(f.k, 100); assert(f.count == (unsigned)after);
        } else {
            cmd(&f, 0xd1); assert(wr(&f, 0x60, which == A20 ? 0xc3 : 0xc0) == BM_STATUS_OK);
            r = bm_kbc8042_advance(f.k, 100);
        }
        assert(r == BM_STATUS_DEVICE_ERROR && state(&f).failure == r);
        calls = f.calls[which];
        assert(bm_kbc8042_advance(f.k, 1) == r && wr(&f, 0x64, 0xaa) == r);
        assert(bm_kbc8042_receive_keyboard(f.k, 0xfa) == r);
        { uint64_t n = 71; assert(bm_kbc8042_next_deadline(f.k, &n) == r && n == 71); }
        assert(f.calls[which] == calls); (void)rd(&f, 0x64, 1);
        f.failure = BM_STATUS_OK; assert(bm_kbc8042_reset(f.k) == BM_STATUS_OK);
        assert(!state(&f).failure && !state(&f).input_remaining && !state(&f).output_remaining);
        assert(!f.levels[IRQ] && !f.levels[A20] && !f.levels[RESET] && f.levels[INHIBIT]);
        done(&f);
    }
    /* Failure on OBF read preserves the caller, but does not replay consumed data. */
    start(&f); ccb(&f, 1);
    assert(bm_kbc8042_receive_keyboard(f.k, 0x1c) == BM_STATUS_OK); advance(&f, 3);
    f.failure = BM_STATUS_DEVICE_ERROR; f.fail_line = IRQ;
    { bm_bus_transaction_t t = txn(0x60, BM_BUS_READ, 0xbeef), old = t;
      assert(bm_kbc8042_io(f.k, &t) == BM_STATUS_DEVICE_ERROR && !memcmp(&old, &t, sizeof(t)));
      assert(!(state(&f).status & 1)); }
    done(&f);
    for (unsigned which = 0; which < 4; ++which) {
        start(&f); f.fail_line = (int)which; f.failure = BM_STATUS_IDLE;
        assert(bm_kbc8042_reset(f.k) == BM_STATUS_INVALID_STATE);
        assert(state(&f).failure == BM_STATUS_INVALID_STATE); done(&f);
    }
    start(&f); f.inspect = 1;
    assert(bm_kbc8042_reset(f.k) == BM_STATUS_OK);
    ccb(&f, 1); data(&f, 0xed);
    assert(bm_kbc8042_receive_keyboard(f.k, 0xfa) == BM_STATUS_OK);
    assert(response(&f) == 0xfa); done(&f);
}
static void lifecycle(void)
{
    fixture_t f, g;
    failure_injection_host_t allocator;
    bm_host_services_t host;
    bm_kbc8042_config_t c;
    start(&f); c = config(&f); done(&f); f.k = (bm_kbc8042_t *)&f;
    failure_injection_host_initialize(&allocator); host = failure_injection_host_services(&allocator);
    failure_injection_host_fail_after(&allocator, 0);
    assert(bm_kbc8042_create(&host, &c, &f.k) == BM_STATUS_OUT_OF_MEMORY && !f.k);
    assert(!allocator.outstanding_allocations);
    c.irq = NULL; assert(bm_kbc8042_create(&host, &c, &f.k) == BM_STATUS_INVALID_ARGUMENT && !f.k);
    c = config(&f); c.input_port &= 0x7f;
    assert(bm_kbc8042_create(&host, &c, &f.k) == BM_STATUS_INVALID_ARGUMENT);
    for (unsigned invalid = 0; invalid < 10; ++invalid) {
        c = config(&f);
        switch (invalid) {
            case 0: c.input_cycles = 0; break;
            case 1: c.output_cycles = 0; break;
            case 2: c.self_test_cycles = 0; break;
            case 3: c.pulse_cycles = 0; break;
            case 4: c.self_test_cycles = UINT64_MAX; break;
            case 5: c.clock.cycles_per_second_denominator = 0; break;
            case 6: c.initial_output_port = 0xc0; break;
            case 7: c.command_port = c.data_port; break;
            case 8: c.a20 = NULL; break;
            default: c.keyboard_inhibit = NULL; break;
        }
        assert(bm_kbc8042_create(&host, &c, &f.k) == BM_STATUS_INVALID_ARGUMENT && !f.k);
    }
    host = bm_null_host_services(); c = config(&f); c.keyboard_command = NULL;
    assert(bm_kbc8042_create(&host, &c, &f.k) == BM_STATUS_OK);
    cmd(&f, 0xae); assert(wr(&f, 0x60, 0xed) == BM_STATUS_UNSUPPORTED); done(&f);
    start(&f); start(&g); ccb(&f, 0x41); ccb(&g, 0x41);
    for (unsigned i = 0; i < 100; ++i) {
        assert(bm_kbc8042_receive_keyboard(f.k, 0x1c) == BM_STATUS_OK);
        assert(bm_kbc8042_receive_keyboard(g.k, 0x1c) == BM_STATUS_OK);
        advance(&f, 123); for (unsigned j = 0; j < 123; ++j) advance(&g, 1);
        assert(response(&f) == response(&g));
        { bm_kbc8042_state_t a = state(&f), b = state(&g); assert(same_state(&a, &b)); }
    }
    cmd(&f, 0xfe); assert(f.levels[RESET] && !g.levels[RESET]);
    { uint64_t cycles = state(&f).cycles;
      assert(bm_kbc8042_reset(f.k) == BM_STATUS_OK && state(&f).cycles == cycles);
      advance(&f, 100); assert(!f.levels[RESET] && !state(&f).pulse_remaining); }
    done(&f); done(&g);
    start(&f); advance(&f, UINT64_MAX);
    assert(bm_kbc8042_advance(f.k, 1) == BM_STATUS_CAPACITY_EXCEEDED);
    assert(state(&f).cycles == UINT64_MAX && state(&f).failure == BM_STATUS_OK); done(&f);
}
static bm_status_t auxiliary(void *p, uint8_t v)
{
    fixture_t *f = p;
    int failing = f->failure != BM_STATUS_OK && f->fail_line == KEYBOARD;
    if (f->inspect) callback_inspect(f);
    if (f->auxiliary_blocked) return BM_STATUS_IDLE;
    if (failing && !f->after) return f->failure;
    assert(f->auxiliary_count < sizeof(f->auxiliary_bytes));
    f->auxiliary_bytes[f->auxiliary_count++] = v;
    return failing ? f->failure : BM_STATUS_OK;
}
static void internal_ram(void)
{
    fixture_t f;
    bm_kbc8042_state_t before, after;
    unsigned calls[5];

    start(&f);
    for (unsigned i = 1; i < 32; ++i) {
        unsigned value = (i * 0x3dU + 0x27U) & 0xffU;
        cmd(&f, 0x20U + i); assert(response(&f) == 0);
        memcpy(calls, f.calls, sizeof(calls)); before = state(&f);
        cmd(&f, 0x60U + i); assert(state(&f).parameter == 0x60U + i);
        data(&f, value); after = state(&f);
        assert(after.internal_ram[i - 1] == value && !after.parameter);
        assert(after.command_byte == before.command_byte &&
               after.output_port == before.output_port &&
               !memcmp(calls, f.calls, sizeof(calls)) && !f.count);
        cmd(&f, 0x20U + i); assert(response(&f) == value);
    }
    /* The BIOS 1.42 path specifically clears bit 7 of cell 2Dh through
     * commands 2Dh/6Dh; it must not alias the command byte at 20h/60h. */
    cmd(&f, 0x6d); data(&f, 0xff); cmd(&f, 0x2d);
    assert((response(&f) & 0x7fU) == 0x7fU);
    cmd(&f, 0x6d); data(&f, 0x7f); cmd(&f, 0x2d); assert(response(&f) == 0x7f);
    cmd(&f, 0x20); assert(response(&f) == 0x10);
    assert(!f.calls[A20] && !f.calls[RESET]);
    assert(bm_kbc8042_reset(f.k) == BM_STATUS_OK);
    cmd(&f, 0x2d); assert(response(&f) == 0); /* Peripheral reset clears RAM. */
    done(&f);
}
static void pic_write(bm_at_pic_t *pic, unsigned port, unsigned v)
{
    bm_bus_transaction_t t = txn(port, BM_BUS_WRITE, v);
    assert(bm_at_pic_io(pic, &t) == BM_STATUS_OK);
}
static void pic_integration(void)
{
    fixture_t f;
    bm_host_services_t host = bm_null_host_services();
    bm_at_pic_config_t pc = {0}; bm_at_pic_state_t ps;
    start(&f); pc.master_base = 0x20; pc.slave_base = 0xa0; pc.cascade_line = 2;
    assert(bm_at_pic_create(&host, &pc, &f.pic) == BM_STATUS_OK);
    pic_write(f.pic, 0x20, 0x11); pic_write(f.pic, 0xa0, 0x11);
    pic_write(f.pic, 0x21, 0x30); pic_write(f.pic, 0xa1, 0x70);
    pic_write(f.pic, 0x21, 4); pic_write(f.pic, 0xa1, 2);
    pic_write(f.pic, 0x21, 1); pic_write(f.pic, 0xa1, 1);
    pic_write(f.pic, 0x21, 0xfd); pic_write(f.pic, 0xa1, 0xff);
    ccb(&f, 0x41);
    for (unsigned i = 0; i < 2; ++i) {
        uint8_t vector = 0xa5;
        if (i) assert(bm_kbc8042_receive_keyboard(f.k, 0xf0) == BM_STATUS_OK);
        assert(bm_kbc8042_receive_keyboard(f.k, 0x1c) == BM_STATUS_OK);
        advance(&f, 3);
        assert(bm_at_pic_state(f.pic, &ps) == BM_STATUS_OK && ps.intr);
        assert(bm_at_pic_acknowledge(f.pic, 0, &vector) == BM_STATUS_OK && vector == 0xa5);
        assert(bm_at_pic_acknowledge(f.pic, 1, &vector) == BM_STATUS_OK && vector == 0x31);
        assert(rd(&f, 0x60, 1) == (i ? 0x9eU : 0x1eU) && f.levels[IRQ]);
        assert(rd(&f, 0x60, 0) == (i ? 0x9eU : 0x1eU) && !f.levels[IRQ]);
        assert(bm_at_pic_state(f.pic, &ps) == BM_STATUS_OK && ps.isr[0] == 2);
        pic_write(f.pic, 0x20, 0x20);
        assert(bm_at_pic_state(f.pic, &ps) == BM_STATUS_OK && !ps.isr[0] && !ps.intr);
    }
    bm_at_pic_destroy(f.pic); f.pic = NULL; done(&f);
}
static void simultaneous_and_release(void)
{
    fixture_t f;
    /* CCB input completion and output readiness at one edge must not uninhibit
     * the sender transiently while OBF is becoming full. */
    start(&f); ccb(&f, 1); cmd(&f, 0x60);
    assert(bm_kbc8042_receive_keyboard(f.k, 0x1c) == BM_STATUS_OK); advance(&f, 1);
    { unsigned calls = f.calls[INHIBIT];
      data(&f, 0x41); assert(f.calls[INHIBIT] == calls && f.levels[IRQ]); }
    assert(rd(&f, 0x60, 0) == 0x1c); /* Translation fixed when byte was accepted. */
    cmd(&f, 0xfe); assert(f.levels[RESET]);
    f.fail_line = RESET; f.failure = BM_STATUS_DEVICE_ERROR;
    { uint64_t before = state(&f).cycles;
      assert(bm_kbc8042_advance(f.k, 100) == BM_STATUS_DEVICE_ERROR);
      assert(state(&f).cycles == before + 6 && !state(&f).pulse_remaining);
      assert(!state(&f).cpu_reset); }
    done(&f);
}
static void output_clock(void)
{
    fixture_t f;
    start(&f);
    /* D0/D1 read-modify-write while disabled retains the actual low clock,
     * changes A20 and leaves reset deasserted. No pin-level serializer needed. */
    cmd(&f, 0xd0);
    { unsigned v = response(&f); assert(v == 0xa1);
      cmd(&f, 0xd1); data(&f, v | 2); }
    assert(f.levels[A20] && !f.levels[RESET] && f.levels[INHIBIT]);
    cmd(&f, 0xae); assert(!f.levels[INHIBIT]);
    cmd(&f, 0xd0); assert(response(&f) == 0xe3);
    /* Direct low-clock drive inhibits reception even with CCB bit4 clear. */
    cmd(&f, 0xd1); data(&f, 0x83);
    assert(f.levels[INHIBIT] && !(state(&f).command_byte & 0x10));
    assert(bm_kbc8042_receive_keyboard(f.k, 0x1c) == BM_STATUS_IDLE);
    data(&f, 0xed);
    assert(!f.levels[INHIBIT] && state(&f).output_port == 0xc3);
    assert(state(&f).olivetti_p2 == 0x83); /* No vendor-latch side effect. */
    cmd(&f, 0xd1); data(&f, 0xc3); assert(!f.levels[INHIBIT]);
    cmd(&f, 0xd1); data(&f, 0x83); ccb(&f, 0); assert(!f.levels[INHIBIT]);
    done(&f);
}
static void olivetti_cf(void)
{
    fixture_t f;
    bm_host_services_t h = bm_null_host_services();
    bm_kbc8042_config_t c;
    bm_kbc8042_state_t before, after;
    unsigned calls[5];
    uint64_t n;
    start(&f); done(&f); c = config(&f);
    c.command_profile = (bm_kbc8042_command_profile_t)99;
    assert(bm_kbc8042_create(&h, &c, &f.k) == BM_STATUS_INVALID_ARGUMENT && !f.k);
    c.command_profile = BM_KBC8042_COMMANDS_OLIVETTI_PCS286;
    assert(bm_kbc8042_create(&h, &c, &f.k) == BM_STATUS_OK);
    c.command_profile = BM_KBC8042_COMMANDS_AT; /* Instance owns the selection. */
    ccb(&f, 0x41); /* Translation and IRQ enabled. */
    assert(bm_kbc8042_receive_keyboard(f.k, 0xf0) == BM_STATUS_OK);
    cmd(&f, 0x60); /* Unfinished parameter canceled only on consumption. */
    before = state(&f); memcpy(calls, f.calls, sizeof(calls));
    assert(wr(&f, 0x64, 0xcf) == BM_STATUS_OK);
    assert(state(&f).parameter == 0x60 && (state(&f).status & 2));
    assert(wr(&f, 0x64, 0xcf) == BM_STATUS_CAPACITY_EXCEEDED);
    assert(bm_kbc8042_next_deadline(f.k, &n) == BM_STATUS_OK && n == 2);
    advance(&f, 1); assert(state(&f).parameter == 0x60);
    advance(&f, 1); after = state(&f);
    assert(!after.parameter && !(after.status & 3) && after.break_pending);
    assert(after.command_byte == before.command_byte && after.output_port == before.output_port);
    assert(!after.output_remaining && !after.pulse_remaining);
    assert(!memcmp(calls, f.calls, sizeof(calls)) && !f.count);
    assert(bm_kbc8042_receive_keyboard(f.k, 0x1c) == BM_STATUS_OK);
    cmd(&f, 0xcf); /* Pending byte's deadline is not canceled/restarted. */
    assert(state(&f).output_remaining == 1 && !(state(&f).status & 1));
    advance(&f, 1); assert(f.levels[IRQ]);
    memcpy(calls, f.calls, sizeof(calls));
    cmd(&f, 0xcf); /* Already full OBF/IRQ unchanged, no extra reply. */
    assert(f.levels[IRQ] && (state(&f).status & 1));
    assert(!memcmp(calls, f.calls, sizeof(calls)));
    assert(rd(&f, 0x60, 0) == 0x9e && !f.levels[IRQ]);
    assert(bm_kbc8042_next_deadline(f.k, &n) == BM_STATUS_IDLE && n == 0);
    cmd(&f, 0xfe); assert(f.levels[RESET]);
    cmd(&f, 0xcf); assert(f.levels[RESET] && state(&f).pulse_remaining == 4);
    advance(&f, 4); assert(!f.levels[RESET]);
    before = state(&f);
    for (unsigned v = 0xce; v <= 0xd2; ++v) {
        if (v == 0xcf || v == 0xd0 || v == 0xd1) continue;
        assert(wr(&f, 0x64, v) == BM_STATUS_UNSUPPORTED);
        after = state(&f); assert(same_state(&before, &after));
    }
    { bm_bus_transaction_t t = txn(0x64, BM_BUS_WRITE, 0xcf);
      t.attributes = BM_BUS_TRANSACTION_DEBUG;
      assert(bm_kbc8042_io(f.k, &t) == BM_STATUS_UNSUPPORTED);
      after = state(&f); assert(same_state(&before, &after)); }
    f.failure = BM_STATUS_DEVICE_ERROR; f.fail_line = A20;
    assert(bm_kbc8042_reset(f.k) == BM_STATUS_DEVICE_ERROR);
    assert(wr(&f, 0x64, 0xcf) == BM_STATUS_DEVICE_ERROR);
    f.failure = BM_STATUS_OK; assert(bm_kbc8042_reset(f.k) == BM_STATUS_OK);
    cmd(&f, 0xcf); assert(!(state(&f).status & 3)); /* Profile survives reset. */
    done(&f);
}
static void olivetti_pc_mode_translation(void)
{
    fixture_t f;
    bm_host_services_t h = bm_null_host_services();
    bm_kbc8042_config_t c;

    /* Generic AT retains its documented functional PC-mode bypass. */
    start(&f); ccb(&f, 0x60);
    assert(bm_kbc8042_receive_keyboard(f.k, 0x05) == BM_STATUS_OK);
    assert(response(&f) == 0x05);
    assert(bm_kbc8042_receive_keyboard(f.k, 0xf0) == BM_STATUS_OK);
    assert(response(&f) == 0xf0);
    assert(bm_kbc8042_receive_keyboard(f.k, 0x05) == BM_STATUS_OK);
    assert(response(&f) == 0x05);
    done(&f);

    /* The explicit Olivetti profile follows the classic: XLAT wins when the
     * BIOS leaves PCMODE set, yielding set-1 F1 make/break 3B/BB. */
    start(&f); done(&f); c = config(&f);
    c.command_profile = BM_KBC8042_COMMANDS_OLIVETTI_PCS286;
    assert(bm_kbc8042_create(&h, &c, &f.k) == BM_STATUS_OK);
    ccb(&f, 0x60);
    assert(bm_kbc8042_receive_keyboard(f.k, 0x05) == BM_STATUS_OK);
    assert(response(&f) == 0x3b);
    assert(bm_kbc8042_receive_keyboard(f.k, 0xf0) == BM_STATUS_OK);
    assert(state(&f).break_pending && !(state(&f).status & 1));
    assert(bm_kbc8042_receive_keyboard(f.k, 0x05) == BM_STATUS_OK);
    assert(response(&f) == 0xbb && !state(&f).break_pending);
    done(&f);
}
static void olivetti_p2(void)
{
    fixture_t f;
    bm_host_services_t h = bm_null_host_services();
    bm_kbc8042_config_t c;
    bm_kbc8042_state_t before, after;
    unsigned calls[5];
    start(&f); done(&f); c = config(&f);
    c.command_profile = BM_KBC8042_COMMANDS_OLIVETTI_PCS286;
    assert(bm_kbc8042_create(&h, &c, &f.k) == BM_STATUS_OK);
    cmd(&f, 0x80); assert(response(&f) == 0xc1);
    for (unsigned v = 0; v < 256; ++v) {
        unsigned old = state(&f).olivetti_p2;
        cmd(&f, 0x84); assert(state(&f).parameter == 0x84);
        memcpy(calls, f.calls, sizeof(calls)); before = state(&f);
        assert(wr(&f, 0x60, v) == BM_STATUS_OK);
        advance(&f, 1); assert(state(&f).olivetti_p2 == old);
        advance(&f, 1); after = state(&f);
        assert(after.olivetti_p2 == v && !after.parameter);
        assert(after.output_port == before.output_port && !after.output_remaining);
        assert(!memcmp(calls, f.calls, sizeof(calls))); /* No reset/A20/keyboard edge. */
        cmd(&f, 0x80);
        assert(state(&f).output_remaining == 3 && !(state(&f).status & 1));
        before = state(&f);
        assert(wr(&f, 0x64, 0x80) == BM_STATUS_CAPACITY_EXCEEDED);
        after = state(&f); assert(same_state(&before, &after));
        assert(response(&f) == v);
    }
    assert(!f.count && !f.calls[A20] && !f.calls[RESET]);
    ccb(&f, 1);
    cmd(&f, 0x80); advance(&f, 3); assert(f.levels[IRQ]);
    assert(wr(&f, 0x64, 0x80) == BM_STATUS_CAPACITY_EXCEEDED);
    cmd(&f, 0x84); data(&f, 0); /* Full output is a snapshot, not a live latch alias. */
    assert(f.levels[IRQ] && rd(&f, 0x60, 0) == 255);
    cmd(&f, 0x80); assert(response(&f) == 0);
    assert(!f.levels[RESET] && !f.levels[A20] && !f.levels[INHIBIT]);
    cmd(&f, 0xd0); assert(response(&f) == 0xe1); /* Actual outputs, not raw zero. */
    cmd(&f, 0xd1); data(&f, 0xc3);
    assert(f.levels[A20] && !f.levels[RESET]);
    cmd(&f, 0x80); assert(response(&f) == 0xc3); /* Standard write refreshes latch. */
    cmd(&f, 0xfe); assert(f.levels[RESET]);
    cmd(&f, 0x84); data(&f, 0x5a);
    assert(f.levels[RESET] && state(&f).pulse_remaining == 2);
    advance(&f, 2); assert(!f.levels[RESET] && f.levels[A20]);
    cmd(&f, 0x80); assert(response(&f) == 0x5a); /* Pulse did not rewrite vendor byte. */
    cmd(&f, 0x84); cmd(&f, 0xcf); data(&f, 0xed);
    assert(f.count == 1 && f.bytes[0] == 0xed); /* Accepted replacement canceled 84. */
    cmd(&f, 0x84); before = state(&f);
    assert(wr(&f, 0x64, 0x8b) == BM_STATUS_UNSUPPORTED);
    after = state(&f); assert(same_state(&before, &after));
    data(&f, 0xa5); /* Rejected command preserved parameter. */
    cmd(&f, 0xc0); assert(response(&f) == 0xa0); /* No inherited polling P1 counter. */
    cmd(&f, 0x80); advance(&f, 3);
    f.failure = BM_STATUS_DEVICE_ERROR; f.fail_line = IRQ;
    { bm_bus_transaction_t t = txn(0x60, BM_BUS_READ, 0xbeef), old = t;
      assert(bm_kbc8042_io(f.k, &t) == BM_STATUS_DEVICE_ERROR);
      assert(!memcmp(&t, &old, sizeof(t))); }
    assert(wr(&f, 0x64, 0x84) == BM_STATUS_DEVICE_ERROR);
    assert(state(&f).olivetti_p2 == 0xa5);
    f.failure = BM_STATUS_OK; assert(bm_kbc8042_reset(f.k) == BM_STATUS_OK);
    cmd(&f, 0x80); assert(response(&f) == 0xc1);
    done(&f);
}
static void host_enable(void)
{
    fixture_t f;
    bm_host_services_t h = bm_null_host_services();
    for (unsigned profile=0; profile<2; ++profile) for (unsigned v=0; v<256; ++v) {
        bm_kbc8042_config_t c;
        start(&f); done(&f); c=config(&f);
        c.command_profile=(bm_kbc8042_command_profile_t)profile;
        assert(bm_kbc8042_create(&h,&c,&f.k)==BM_STATUS_OK);
        ccb(&f,0x45); cmd(&f,0xad);
        assert(state(&f).command_byte==0x55 && f.levels[INHIBIT]);
        f.inspect=1;
        assert(wr(&f,0x60,v)==BM_STATUS_OK);
        assert(state(&f).status&2);
        assert(wr(&f,0x64,0xae)==BM_STATUS_CAPACITY_EXCEEDED);
        advance(&f,1);
        assert(state(&f).command_byte==0x55 && !f.count && f.levels[INHIBIT]);
        assert(bm_kbc8042_receive_keyboard(f.k,0xfa)==BM_STATUS_IDLE);
        advance(&f,1);
        assert(state(&f).command_byte==0x45 && !(state(&f).status&3));
        assert(!f.levels[INHIBIT] && f.count==1 && f.bytes[0]==v);
        assert(state(&f).olivetti_p2==0xc1 && !f.calls[A20] && !f.calls[RESET]);
        assert(!state(&f).output_remaining && !f.levels[IRQ]); /* No fabricated reply. */
        advance(&f,20); assert(f.count==1);
        done(&f);
    }
    /* Enable survives endpoint backpressure, with IBF held and no duplicate
     * inhibit edge. The endpoint alone decides when the byte was accepted. */
    start(&f); f.blocked=1;
    assert(wr(&f,0x60,0xff)==BM_STATUS_OK); advance(&f,6);
    assert(f.calls[KEYBOARD]==3 && !f.count && f.calls[INHIBIT]==1);
    assert(!f.levels[INHIBIT] && (state(&f).status&2));
    assert(bm_kbc8042_receive_keyboard(f.k,0xaa)==BM_STATUS_CAPACITY_EXCEEDED);
    f.blocked=0; advance(&f,2);
    assert(f.count==1 && f.bytes[0]==0xff && !(state(&f).status&2));
    done(&f);
    /* Controller parameters remain distinct from keyboard-bound data. */
    start(&f); ccb(&f,0x10); cmd(&f,0xd1); data(&f,0x81);
    assert(f.levels[INHIBIT] && !f.count && state(&f).command_byte==0x10);
    done(&f); /* 84h parameter has exhaustive no-output tests in olivetti_p2. */
    for (unsigned which=INHIBIT; which<=KEYBOARD; ++which) for (int after=0; after<2; ++after) {
        unsigned calls;
        start(&f); f.fail_line=(int)which; f.after=after; f.failure=BM_STATUS_DEVICE_ERROR;
        assert(wr(&f,0x60,0xff)==BM_STATUS_OK);
        assert(bm_kbc8042_advance(f.k,100)==BM_STATUS_DEVICE_ERROR);
        assert(state(&f).cycles==2 && state(&f).failure==BM_STATUS_DEVICE_ERROR);
        assert(!(state(&f).command_byte&0x10) && (state(&f).status&2));
        assert(f.count==(unsigned)(which==KEYBOARD && after));
        calls=f.calls[which];
        assert(bm_kbc8042_advance(f.k,10)==BM_STATUS_DEVICE_ERROR && f.calls[which]==calls);
        assert(wr(&f,0x60,0xff)==BM_STATUS_DEVICE_ERROR);
        f.failure=BM_STATUS_OK; assert(bm_kbc8042_reset(f.k)==BM_STATUS_OK);
        assert(state(&f).command_byte==0x10 && !state(&f).input_remaining);
        done(&f);
    }
    /* A pending controller response still occupies the modeled serial
     * boundary. Once it reaches OBF, the independent host input buffer accepts
     * a keyboard byte without consuming or replacing the retained response. */
    for (unsigned ready=0; ready<2; ++ready) {
        bm_kbc8042_state_t a,b;
        start(&f); cmd(&f,0xc0); if (ready) advance(&f,3);
        a=state(&f);
        if (!ready) {
            assert(wr(&f,0x60,0xff)==BM_STATUS_CAPACITY_EXCEEDED);
            b=state(&f); assert(same_state(&a,&b) && f.levels[INHIBIT] && !f.count);
        } else {
            assert(wr(&f,0x60,0xff)==BM_STATUS_OK); advance(&f,2);
            b=state(&f); assert((b.status&1) && b.output_byte==a.output_byte);
            assert(f.count==1 && f.bytes[0]==0xff && f.levels[INHIBIT]);
            assert(rd(&f,0x60,0)==0xa0);
        }
        done(&f);
    }
}
static void command_byte_latches(void)
{
    fixture_t f;
    bm_host_services_t h = bm_null_host_services();
    for (unsigned profile=0; profile<2; ++profile) for (unsigned v=0; v<256; ++v) {
        bm_kbc8042_config_t c;
        start(&f); done(&f); c=config(&f);
        c.command_profile=(bm_kbc8042_command_profile_t)profile;
        assert(bm_kbc8042_create(&h,&c,&f.k)==BM_STATUS_OK);
        ccb(&f,v);
        assert(state(&f).command_byte==v);
        assert((state(&f).status&4)==(v&4));
        assert(f.levels[INHIBIT]==((v&0x10)!=0));
        assert(!f.levels[IRQ] && !f.levels[A20] && !f.levels[RESET]);
        cmd(&f,0x20); assert(response(&f)==v);
        assert(!f.levels[IRQ]);
        done(&f);
    }
}
static void olivetti_auxiliary_channel(void)
{
    fixture_t f;
    bm_host_services_t h = bm_null_host_services();
    bm_kbc8042_config_t c;
    bm_kbc8042_state_t before, after;

    /* The observed PCS286 no-mouse configuration accepts the complete
     * A8/D4/data/A7 sequence and produces no invented device response. */
    start(&f); done(&f); c = config(&f);
    c.command_profile = BM_KBC8042_COMMANDS_OLIVETTI_PCS286;
    assert(bm_kbc8042_create(&h, &c, &f.k) == BM_STATUS_OK);
    assert(!state(&f).auxiliary_enabled);
    cmd(&f, 0xa8); assert(state(&f).auxiliary_enabled);
    cmd(&f, 0xd4); assert(state(&f).parameter == 0xd4);
    data(&f, 0xff);
    assert(!state(&f).parameter && !(state(&f).status & 1));
    assert(!f.count && !f.auxiliary_count);
    cmd(&f, 0xa7); assert(!state(&f).auxiliary_enabled);
    cmd(&f, 0xd4); data(&f, 0xf4);
    assert(!f.auxiliary_count && !(state(&f).status & 1));
    cmd(&f, 0xa8); assert(state(&f).auxiliary_enabled);
    assert(wr(&f, 0x64, 0xaa) == BM_STATUS_OK); advance(&f, 12);
    assert(!state(&f).auxiliary_enabled); advance(&f, 3);
    assert(rd(&f, 0x60, 0) == 0x55);
    cmd(&f, 0xa8); assert(state(&f).auxiliary_enabled);
    assert(bm_kbc8042_reset(f.k) == BM_STATUS_OK);
    assert(!state(&f).auxiliary_enabled);
    done(&f);

    /* An attached byte sink receives exactly one accepted byte. Its
     * backpressure retains IBF and the D4 parameter without duplication. */
    memset(&f, 0, sizeof(f)); f.levels[INHIBIT] = 1; c = config(&f);
    c.command_profile = BM_KBC8042_COMMANDS_OLIVETTI_PCS286;
    c.auxiliary_command = auxiliary; c.auxiliary_context = &f;
    assert(bm_kbc8042_create(&h, &c, &f.k) == BM_STATUS_OK);
    cmd(&f, 0xa8); cmd(&f, 0xd4);
    f.auxiliary_blocked = 1;
    assert(wr(&f, 0x60, 0xe8) == BM_STATUS_OK);
    advance(&f, 6);
    assert((state(&f).status & 2) && state(&f).parameter == 0xd4);
    assert(!f.auxiliary_count);
    f.auxiliary_blocked = 0; advance(&f, 2);
    assert(!(state(&f).status & 2) && !state(&f).parameter);
    assert(f.auxiliary_count == 1 && f.auxiliary_bytes[0] == 0xe8);
    cmd(&f, 0xd4); before = state(&f);
    assert(wr(&f, 0x64, 0xab) == BM_STATUS_UNSUPPORTED);
    after = state(&f); assert(same_state(&before, &after));
    f.inspect = 1;
    data(&f, 0xf3);
    assert(f.auxiliary_count == 2 && f.auxiliary_bytes[1] == 0xf3);
    f.inspect = 0; cmd(&f, 0xa7); cmd(&f, 0xd4); data(&f, 0xf4);
    assert(f.auxiliary_count == 2);
    done(&f);

    for (int after = 0; after < 2; ++after) {
        memset(&f, 0, sizeof(f)); f.levels[INHIBIT] = 1; c = config(&f);
        c.command_profile = BM_KBC8042_COMMANDS_OLIVETTI_PCS286;
        c.auxiliary_command = auxiliary; c.auxiliary_context = &f;
        assert(bm_kbc8042_create(&h, &c, &f.k) == BM_STATUS_OK);
        cmd(&f, 0xa8); cmd(&f, 0xd4);
        f.failure = BM_STATUS_DEVICE_ERROR; f.fail_line = KEYBOARD; f.after = after;
        assert(wr(&f, 0x60, 0xf2) == BM_STATUS_OK);
        assert(bm_kbc8042_advance(f.k, 2) == BM_STATUS_DEVICE_ERROR);
        assert(state(&f).failure == BM_STATUS_DEVICE_ERROR);
        assert((state(&f).status & 2) && state(&f).parameter == 0xd4);
        assert(f.auxiliary_count == (unsigned)after);
        assert(wr(&f, 0x64, 0xa7) == BM_STATUS_DEVICE_ERROR);
        f.failure = BM_STATUS_OK;
        assert(bm_kbc8042_reset(f.k) == BM_STATUS_OK);
        assert(!state(&f).auxiliary_enabled && !state(&f).parameter);
        done(&f);
    }
}
static void password_query(void)
{
    for (int profile = BM_KBC8042_COMMANDS_AT;
         profile <= BM_KBC8042_COMMANDS_OLIVETTI_PCS286; ++profile) {
        fixture_t f;
        bm_host_services_t h = bm_null_host_services();
        bm_kbc8042_config_t c;
        start(&f); done(&f); c = config(&f);
        c.command_profile = (bm_kbc8042_command_profile_t)profile;
        assert(bm_kbc8042_create(&h, &c, &f.k) == BM_STATUS_OK);
        cmd(&f, 0xa4);
        assert(response(&f) == 0xf1);
        assert(!(state(&f).status & 1));
        done(&f);
    }
}
int main(void)
{
    buffers(); streams(); pulses(); rejected_and_backpressure(); internal_ram(); failures(); lifecycle();
    pic_integration(); simultaneous_and_release(); output_clock(); olivetti_cf(); olivetti_p2();
    olivetti_pc_mode_translation();
    host_enable(); command_byte_latches(); olivetti_auxiliary_channel(); password_query();
    puts("KBC8042: functional buffers/translation/control, 256 pulse combinations, 512 command-byte latches, 512 raw bytes, failure/lifecycle cases pass");
    return 0;
}
