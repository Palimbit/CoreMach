/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Authored bidirectional native/engine integration, no firmware. */
#include <blumach/components/at_keyboard_pair.h>
#include <blumach/components/at_pic.h>
#include <blumach/platforms/null_host.h>
#include "failure_injection_host.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
enum { IRQ, A20, RESET };
typedef struct fixture {
    bm_at_keyboard_pair_t *pair;
    bm_engine_t           *engine;
    bm_at_clock_link_t    *link;
    bm_at_pic_t           *pic;
    unsigned               calls, count, inspected;
    uint64_t               cycles[256];
    bm_time_point_t        times[256];
    int                    lines[256], levels[3], inspect, fail_line, after;
    bm_status_t            failure;
} fixture_t;
static bm_at_keyboard_pair_state_t
state(fixture_t *f)
{
    bm_at_keyboard_pair_state_t s = { 0 };
    assert(bm_at_keyboard_pair_state(f->pair, &s) == BM_STATUS_OK);
    assert(s.controller.cycles == s.keyboard.cycles);
    return s;
}
static bm_bus_transaction_t
txn(unsigned port, bm_bus_operation_t op, unsigned v)
{
    bm_bus_transaction_t t = { 0 };
    t.space                = BM_ADDRESS_IO;
    t.address              = port;
    t.size = t.alignment = 1;
    t.operation          = op;
    t.value              = v;
    t.wait_states        = 17;
    return t;
}
static bm_input_event_t
event(bm_key_code_t key, int down)
{
    bm_input_event_t e = { 0 };
    e.kind             = BM_INPUT_KEY;
    e.key              = key;
    e.pressed          = down;
    return e;
}
static bm_status_t
io(fixture_t *f, bm_bus_transaction_t *t)
{
    return f->link ? bm_at_clock_link_io(f->link, t) : bm_at_keyboard_pair_io(f->pair, t);
}
static bm_status_t
output(fixture_t *f, int line, int level)
{
    int fail = f->failure && f->fail_line == line;
    ++f->calls;
    if (f->inspect) {
        bm_bus_transaction_t t = txn(0x64, BM_BUS_READ, 0xbeef);
        bm_input_event_t     e = event(BM_KEY_A, 1);
        (void) state(f);
        assert(bm_at_keyboard_pair_advance(f->pair, 1) == BM_STATUS_INVALID_STATE);
        assert(bm_at_keyboard_pair_reset(f->pair) == BM_STATUS_INVALID_STATE);
        assert(bm_at_keyboard_pair_input(f->pair, &e) == BM_STATUS_INVALID_STATE);
        assert(io(f, &t) == BM_STATUS_INVALID_STATE && t.value == 0xbeef);
        t.attributes = BM_BUS_TRANSACTION_DEBUG;
        assert(io(f, &t) == BM_STATUS_OK);
        bm_at_keyboard_pair_destroy(f->pair);
        if (f->link) {
            assert(bm_at_clock_link_sync(f->link) == BM_STATUS_INVALID_STATE);
            assert(bm_at_keyboard_pair_clock_input(f->link, &e) == BM_STATUS_INVALID_STATE);
            bm_at_clock_link_destroy(f->link);
        }
        ++f->inspected;
    }
    if (fail && !f->after)
        return f->failure;
    assert(f->count < 256);
    f->cycles[f->count] = state(f).controller.cycles;
    if (f->engine)
        assert(bm_engine_now_exact(f->engine, &f->times[f->count]) == BM_STATUS_OK);
    f->lines[f->count++] = line * 2 + level;
    f->levels[line]      = level;
    if (line == IRQ && f->pic)
        assert(bm_at_pic_set_irq(f->pic, 1, level) == BM_STATUS_OK);
    return fail ? f->failure : BM_STATUS_OK;
}
static bm_status_t
irq(void *p, int v)
{
    return output(p, IRQ, v);
}
static bm_status_t
a20(void *p, int v)
{
    return output(p, A20, v);
}
static bm_status_t
reset(void *p, int v)
{
    return output(p, RESET, v);
}
static bm_at_keyboard_pair_config_t
config(fixture_t *f)
{
    bm_at_keyboard_pair_config_t c   = { 0 };
    c.controller.data_port           = 0x60;
    c.controller.command_port        = 0x64;
    c.controller.irq                 = irq;
    c.controller.a20                 = a20;
    c.controller.cpu_reset           = reset;
    c.controller.output_context      = f;
    c.controller.clock               = (bm_clock_rate_t) { 3000000, 2 };
    c.keyboard.clock                 = (bm_clock_rate_t) { 1500000, 1 };
    c.controller.input_cycles        = 2;
    c.controller.output_cycles       = 3;
    c.controller.self_test_cycles    = 5;
    c.controller.pulse_cycles        = 6;
    c.controller.input_port          = 0xa0;
    c.controller.initial_output_port = 0xc1;
    c.keyboard.power_on_cycles       = 3;
    c.keyboard.bat_cycles            = 5;
    c.keyboard.byte_cycles           = 2;
    c.keyboard.reset_accept_cycles   = 3;
    return c;
}
static void
start(fixture_t *f, int clocked)
{
    bm_host_services_t           h = bm_null_host_services();
    bm_at_keyboard_pair_config_t c;
    bm_engine_config_t           ec = { 1, 4, 2 };
    memset(f, 0, sizeof(*f));
    c = config(f);
    assert(bm_at_keyboard_pair_create(&h, &c, &f->pair) == BM_STATUS_OK && !f->calls);
    if (clocked) {
        assert(bm_engine_create_clocked(&h, &ec, &f->engine) == BM_STATUS_OK);
        assert(bm_at_keyboard_pair_attach_clock(&h, f->engine, f->pair, &f->link) == BM_STATUS_OK);
    }
    f->inspect = 1;
}
static void
done(fixture_t *f)
{
    bm_engine_destroy(f->engine);
    bm_at_clock_link_destroy(f->link);
    bm_at_keyboard_pair_destroy(f->pair);
    bm_at_pic_destroy(f->pic);
}
static uint64_t
ceil_edge(uint64_t n)
{
    return (n * 2000 + 2) / 3;
}
static void
run(fixture_t *f, uint64_t until)
{
    if (f->engine) {
        uint64_t ns = ceil_edge(until), now = bm_engine_now(f->engine);
        assert(ns >= now);
        assert(bm_engine_run_for(f->engine, ns - now) == BM_STATUS_OK);
    } else {
        uint64_t now = state(f).controller.cycles;
        assert(until >= now);
        assert(bm_at_keyboard_pair_advance(f->pair, until - now) == BM_STATUS_OK);
    }
}
static void
wr(fixture_t *f, unsigned port, unsigned v)
{
    bm_bus_transaction_t t = txn(port, BM_BUS_WRITE, v), old = t;
    assert(io(f, &t) == BM_STATUS_OK && !memcmp(&t, &old, sizeof(t)));
}
static unsigned
rd(fixture_t *f, int debug)
{
    bm_bus_transaction_t t = txn(0x60, BM_BUS_READ, 0xbeef);
    t.attributes           = debug ? BM_BUS_TRANSACTION_DEBUG : 0;
    assert(io(f, &t) == BM_STATUS_OK && t.wait_states == 17);
    return (unsigned) t.value;
}
static void
key(fixture_t *f, bm_key_code_t code, int down)
{
    bm_input_event_t e = event(code, down);
    assert((f->link ? bm_at_keyboard_pair_clock_input(f->link, &e) : bm_at_keyboard_pair_input(f->pair, &e))
           == BM_STATUS_OK);
}
static void
enable(fixture_t *f, unsigned ccb)
{
    wr(f, 0x64, 0x60);
    run(f, 2);
    wr(f, 0x60, ccb);
    run(f, 4);
}
static void
boot(fixture_t *f)
{
    enable(f, 0x41);
    run(f, 13);
    assert(state(f).controller.status & 1);
    assert(rd(f, 0) == 0xaa);
}
static void
protocol(void)
{
    for (int clocked = 0; clocked < 2; ++clocked) {
        fixture_t f;
        start(&f, clocked);
        boot(&f);
        wr(&f, 0x60, 0xed);
        run(&f, 20);
        /* BIOS 1.42 sends the LED option while ED's ACK remains in OBF. The
         * independent IBF accepts it; its second ACK waits behind inhibition. */
        wr(&f, 0x60, 7);
        run(&f, 22);
        assert(state(&f).keyboard.leds == 7 && (state(&f).controller.status & 1));
        assert(rd(&f, 0) == 0xfa);
        run(&f, 27);
        assert(rd(&f, 0) == 0xfa && state(&f).keyboard.leds == 7);
        wr(&f, 0x60, 0xf2);
        run(&f, 34);
        assert(rd(&f, 0) == 0xfa);
        run(&f, 39);
        assert(rd(&f, 0) == 0xab);
        run(&f, 44);
        assert(rd(&f, 0) == 0x41);
        wr(&f, 0x64, 0xad);
        run(&f, 46);
        assert(state(&f).keyboard.inhibited);
        /* No AE: the host byte must enable the actual peer before its ACK. */
        wr(&f, 0x60, 0xff);
        run(&f, 100);
        assert(state(&f).keyboard.phase == BM_AT_KEYBOARD_RESET_ACCEPT
               && state(&f).keyboard.phase_remaining == 3);
        assert(rd(&f, 1) == 0xfa && state(&f).keyboard.inhibited);
        assert(rd(&f, 0) == 0xfa);
        run(&f, 103);
        assert(state(&f).keyboard.phase == BM_AT_KEYBOARD_BAT);
        run(&f, 113);
        assert(rd(&f, 0) == 0xaa && !state(&f).keyboard.leds);
        key(&f, BM_KEY_A, 1);
        key(&f, BM_KEY_A, 0);
        run(&f, 120);
        assert(rd(&f, 0) == 0x1e);
        run(&f, 127);
        assert(rd(&f, 0) == 0x9e);
        assert(f.inspected == f.calls);
        done(&f);
    }
}
static void
coincidences(void)
{
    for (int clocked = 0; clocked < 2; ++clocked) {
        fixture_t f;
        start(&f, clocked);
        boot(&f);
        key(&f, BM_KEY_A, 1);
        wr(&f, 0x60, 0xf5); /* Input consume and scan send both due15. */
        run(&f, 15);
        assert(!state(&f).keyboard.enabled && !state(&f).keyboard.scan_count);
        assert(!(state(&f).controller.status & 1));
        run(&f, 20);
        assert(rd(&f, 0) == 0xfa);
        run(&f, 100);
        assert(!(state(&f).controller.status & 1));
        done(&f);
        start(&f, clocked);
        boot(&f);
        key(&f, BM_KEY_A, 1);
        wr(&f, 0x64, 0xad);
        run(&f, 15);
        assert(state(&f).keyboard.inhibited && state(&f).keyboard.scan_count == 1);
        run(&f, 20);
        wr(&f, 0x64, 0xae);
        run(&f, 22);
        assert(!state(&f).keyboard.inhibited);
        run(&f, 27);
        assert(rd(&f, 0) == 0x1e);
        done(&f);
        /* KBC consumes FF before the keyboard's BAT-completion action at8:
         * IDLE retains IBF, then retry at10 supersedes unsent AA with real ACK. */
        start(&f, clocked);
        enable(&f, 0x41);
        run(&f, 6);
        wr(&f, 0x60, 0xff);
        run(&f, 8);
        assert(state(&f).controller.status & 2);
        run(&f, 10);
        assert(state(&f).keyboard.phase == BM_AT_KEYBOARD_RESET_ACK);
        run(&f, 15);
        assert(rd(&f, 0) == 0xfa);
        done(&f);
        /* A new reset cancels acceptance exactly on its old deadline. */
        start(&f, clocked);
        boot(&f);
        wr(&f, 0x60, 0xff);
        run(&f, 20);
        assert(rd(&f, 0) == 0xfa);
        run(&f, 21);
        wr(&f, 0x60, 0xff);
        run(&f, 23);
        assert(state(&f).keyboard.phase == BM_AT_KEYBOARD_RESET_ACK && !state(&f).keyboard.leds);
        run(&f, 28);
        assert(rd(&f, 0) == 0xfa);
        done(&f);
    }
}
static void
partitioned(void)
{
    fixture_t a, b;
    unsigned  random = 17;
    start(&a, 0);
    start(&b, 0);
    boot(&a);
    boot(&b);
    key(&a, BM_KEY_PRINT_SCREEN, 1);
    key(&a, BM_KEY_PRINT_SCREEN, 0);
    key(&b, BM_KEY_PRINT_SCREEN, 1);
    key(&b, BM_KEY_PRINT_SCREEN, 0);
    for (uint64_t end = 1000; end <= 10000; end += 1000) {
        run(&a, end);
        while (state(&b).controller.cycles < end) {
            uint64_t n;
            random = random * 1664525U + 1013904223U;
            n      = state(&b).controller.cycles + 1 + random % 137;
            run(&b, n > end ? end : n);
        }
        {
            bm_at_keyboard_pair_state_t x = state(&a), y = state(&b);
            assert(!memcmp(&x, &y, sizeof(x)));
            if (x.controller.status & 1)
                assert(rd(&a, 0) == rd(&b, 0));
        }
    }
    assert(a.count == b.count && !memcmp(a.cycles, b.cycles, a.count * sizeof(*a.cycles)));
    assert(!memcmp(a.lines, b.lines, a.count * sizeof(*a.lines)));
    done(&a);
    done(&b);
    /* Compare one engine run to arbitrary smaller runs, including rational phase. */
    start(&a, 1);
    start(&b, 1);
    boot(&a);
    boot(&b);
    key(&a, BM_KEY_A, 1);
    key(&b, BM_KEY_A, 1);
    for (unsigned i = 0; i < 12; ++i) {
        uint64_t end = 100000 + i * 100000;
        run(&a, end);
        while (bm_engine_now(b.engine) < ceil_edge(end)) {
            uint64_t now = bm_engine_now(b.engine), step;
            random       = random * 1664525U + 1013904223U;
            step         = 1 + random % 10007;
            if (step > ceil_edge(end) - now)
                step = ceil_edge(end) - now;
            assert(bm_engine_run_for(b.engine, step) == BM_STATUS_OK);
        }
        assert(bm_at_clock_link_sync(a.link) == BM_STATUS_OK
               && bm_at_clock_link_sync(b.link) == BM_STATUS_OK);
        {
            bm_at_keyboard_pair_state_t x = state(&a), y = state(&b);
            assert(!memcmp(&x, &y, sizeof(x)));
            if (x.controller.status & 1)
                assert(rd(&a, 0) == rd(&b, 0));
        }
    }
    assert(a.count == b.count);
    for (unsigned i = 0; i < a.count; ++i) {
        assert(a.lines[i] == b.lines[i] && a.cycles[i] == b.cycles[i]);
        assert(a.times[i].nanoseconds == b.times[i].nanoseconds);
        assert(a.times[i].subnanosecond_numerator == b.times[i].subnanosecond_numerator);
        assert(a.times[i].subnanosecond_denominator == b.times[i].subnanosecond_denominator);
    }
    done(&a);
    done(&b);
}
static void
pic_write(fixture_t *f, unsigned port, unsigned value)
{
    bm_bus_transaction_t t = txn(port, BM_BUS_WRITE, value);
    assert(bm_at_pic_io(f->pic, &t) == BM_STATUS_OK);
}
static void
pic(void)
{
    fixture_t          f;
    bm_host_services_t h = bm_null_host_services();
    bm_at_pic_config_t c = { 0 };
    bm_at_pic_state_t  ps;
    start(&f, 1);
    c.master_base  = 0x20;
    c.slave_base   = 0xa0;
    c.cascade_line = 2;
    assert(bm_at_pic_create(&h, &c, &f.pic) == BM_STATUS_OK);
    pic_write(&f, 0x20, 0x11);
    pic_write(&f, 0xa0, 0x11);
    pic_write(&f, 0x21, 0x30);
    pic_write(&f, 0xa1, 0x70);
    pic_write(&f, 0x21, 4);
    pic_write(&f, 0xa1, 2);
    pic_write(&f, 0x21, 1);
    pic_write(&f, 0xa1, 1);
    pic_write(&f, 0x21, 0xfd);
    pic_write(&f, 0xa1, 0xff);
    enable(&f, 0x41);
    run(&f, 13);
    for (unsigned i = 0; i < 3; ++i) {
        uint8_t vector = 0;
        assert(bm_at_pic_state(f.pic, &ps) == BM_STATUS_OK && ps.intr);
        assert(bm_at_pic_acknowledge(f.pic, 0, &vector) == BM_STATUS_OK);
        assert(bm_at_pic_acknowledge(f.pic, 1, &vector) == BM_STATUS_OK && vector == 0x31);
        assert(rd(&f, 0) == (i == 0 ? 0xaaU : i == 1 ? 0x1eU : 0x9eU));
        pic_write(&f, 0x20, 0x20);
        if (!i)
            key(&f, BM_KEY_A, 1);
        else if (i == 1)
            key(&f, BM_KEY_A, 0);
        run(&f, 20 + i * 10);
    }
    /* First IRQ at native13, exactly 8666+2/3 ns. */
    assert(f.times[0].nanoseconds == 8666 && f.times[0].subnanosecond_numerator == 2
           && f.times[0].subnanosecond_denominator == 3);
    done(&f);
}
static void
failures(void)
{
    const bm_status_t errors[] = { BM_STATUS_DEVICE_ERROR, BM_STATUS_CAPACITY_EXCEEDED };
    for (unsigned err = 0; err < 2; ++err)
        for (int after = 0; after < 2; ++after)
            for (int line = 0; line < 3; ++line) {
                fixture_t f;
                unsigned  before;
                start(&f, 1);
                boot(&f);
                before      = f.calls;
                f.failure   = errors[err];
                f.fail_line = line;
                f.after     = after;
                if (line == IRQ)
                    key(&f, BM_KEY_A, 1);
                else if (line == RESET)
                    wr(&f, 0x64, 0xfe);
                else {
                    wr(&f, 0x64, 0xd1);
                    run(&f, 15);
                    wr(&f, 0x60, 0xc3);
                }
                assert(bm_engine_run_for(f.engine, 100000) == errors[err]);
                assert(state(&f).failure == errors[err] && f.calls == before + 1);
                {
                    unsigned calls = f.calls;
                    uint64_t n     = 42;
                    assert(bm_at_keyboard_pair_next_deadline(f.pair, &n) == errors[err] && n == 42);
                    assert(bm_at_clock_link_sync(f.link) == errors[err] && f.calls == calls);
                }
                f.failure = BM_STATUS_OK;
                assert(bm_engine_reset(f.engine) == BM_STATUS_OK);
                assert(bm_at_clock_link_reset(f.link) == BM_STATUS_OK);
                assert(!state(&f).failure && state(&f).keyboard.inhibited);
                done(&f);
            }
    for (int after = 0; after < 2; ++after) {
        fixture_t            f;
        bm_bus_transaction_t t = txn(0x60, BM_BUS_READ, 0xbeef), old = t;
        start(&f, 1);
        enable(&f, 0x41);
        run(&f, 13);
        f.failure   = BM_STATUS_DEVICE_ERROR;
        f.fail_line = IRQ;
        f.after     = after;
        assert(io(&f, &t) == BM_STATUS_DEVICE_ERROR && !memcmp(&t, &old, sizeof(t)));
        assert(!(state(&f).controller.status & 1) && !state(&f).keyboard.inhibited);
        assert(state(&f).failure == BM_STATUS_DEVICE_ERROR);
        done(&f);
    }
}
static void
lifecycle(void)
{
    failure_injection_host_t     a;
    bm_host_services_t           h;
    fixture_t                    f;
    bm_at_keyboard_pair_config_t c;
    for (unsigned fail = 0; fail < 3; ++fail) {
        memset(&f, 0, sizeof(f));
        c = config(&f);
        failure_injection_host_initialize(&a);
        h = failure_injection_host_services(&a);
        failure_injection_host_fail_after(&a, fail);
        assert(bm_at_keyboard_pair_create(&h, &c, &f.pair) == BM_STATUS_OUT_OF_MEMORY && !f.pair
               && !a.outstanding_allocations && !f.calls);
    }
    h = bm_null_host_services();
    memset(&f, 0, sizeof(f));
    c = config(&f);
    c.keyboard.clock.cycles_per_second_numerator++;
    assert(bm_at_keyboard_pair_create(&h, &c, &f.pair) == BM_STATUS_INVALID_ARGUMENT && !f.pair);
    c                             = config(&f);
    c.controller.keyboard_context = &f;
    assert(bm_at_keyboard_pair_create(&h, &c, &f.pair) == BM_STATUS_INVALID_ARGUMENT && !f.pair);
    start(&f, 0);
    assert(bm_at_keyboard_pair_advance(f.pair, UINT64_MAX) == BM_STATUS_OK);
    assert(state(&f).controller.cycles == UINT64_MAX);
    assert(bm_at_keyboard_pair_advance(f.pair, 1) == BM_STATUS_CAPACITY_EXCEEDED && !state(&f).failure);
    done(&f);
    start(&f, 1);
    boot(&f);
    run(&f, 1000);
    assert(bm_at_clock_link_sync(f.link) == BM_STATUS_OK);
    {
        uint64_t before = state(&f).controller.cycles;
        assert(bm_at_keyboard_pair_reset(f.pair) == BM_STATUS_OK);
        assert(bm_at_clock_link_changed(f.link) == BM_STATUS_OK && state(&f).controller.cycles == before);
        assert(bm_engine_reset(f.engine) == BM_STATUS_OK);
        assert(bm_at_clock_link_reset(f.link) == BM_STATUS_OK);
        enable(&f, 0x41);
        run(&f, 13);
        assert(rd(&f, 0) == 0xaa && state(&f).controller.cycles == before + 13);
    }
    bm_at_keyboard_pair_destroy(f.pair); /* Attached owner cannot be freed. */
    done(&f);
}
typedef struct cpu_fixture {
    fixture_t *f;
    unsigned   steps;
} cpu_fixture_t;
static bm_status_t
cpu_reset(void *context)
{
    ((cpu_fixture_t *) context)->steps = 0;
    return BM_STATUS_OK;
}
static bm_status_t
cpu_signal(void *context, uint32_t line, int level)
{
    (void) context;
    (void) line;
    (void) level;
    return BM_STATUS_OK;
}
static bm_status_t
cpu_step(void *context, bm_tick_t start, uint64_t *cycles)
{
    cpu_fixture_t *c = context;
    (void) start;
    switch (c->steps) {
        case 0:
            wr(c->f, 0x64, 0x60);
            break;
        case 4:
            wr(c->f, 0x60, 0x41);
            break;
        case 26:
            assert(rd(c->f, 0) == 0xaa);
            break;
        case 27:
            key(c->f, BM_KEY_A, 1);
            assert(state(c->f).keyboard.cycles == 13);
            break;
        case 36:
            assert(rd(c->f, 0) == 0x1e);
            break;
        case 37:
            key(c->f, BM_KEY_A, 0);
            assert(state(c->f).keyboard.cycles == 18);
            break;
        case 50:
            assert(rd(c->f, 0) == 0x9e);
            break;
        default:
            break;
    }
    ++c->steps;
    *cycles = 1;
    return BM_STATUS_OK;
}
static void
cpu_and_attachment(void)
{
    fixture_t       f;
    cpu_fixture_t   c;
    bm_cpu_t        cpu  = { 0 };
    bm_clock_rate_t rate = { 3000000, 1 };
    start(&f, 1);
    c              = (cpu_fixture_t) { &f, 0 };
    cpu.context    = &c;
    cpu.ops.reset  = cpu_reset;
    cpu.ops.signal = cpu_signal;
    assert(bm_engine_add_clocked_cpu(f.engine, &cpu, cpu_step, &rate, NULL) == BM_STATUS_OK);
    assert(bm_engine_run_for(f.engine, 100000) == BM_STATUS_OK && c.steps == 300);
    {
        bm_at_keyboard_pair_state_t before = state(&f);
        assert(cpu_reset(&c) == BM_STATUS_OK);
        bm_at_keyboard_pair_state_t after = state(&f);
        assert(!memcmp(&before, &after, sizeof(before)));
    }
    done(&f);
    {
        failure_injection_host_t     a;
        bm_host_services_t           h;
        bm_at_clock_link_t          *other = NULL;
        bm_at_keyboard_pair_config_t cfg;
        bm_engine_config_t           ec = { 1, 1, 1 };
        memset(&f, 0, sizeof(f));
        failure_injection_host_initialize(&a);
        h   = failure_injection_host_services(&a);
        cfg = config(&f);
        assert(bm_at_keyboard_pair_create(&h, &cfg, &f.pair) == BM_STATUS_OK
               && a.outstanding_allocations == 3);
        {
            bm_host_services_t normal = bm_null_host_services();
            assert(bm_engine_create_clocked(&normal, &ec, &f.engine) == BM_STATUS_OK);
        }
        failure_injection_host_fail_after(&a, 0);
        assert(bm_at_keyboard_pair_attach_clock(&h, f.engine, f.pair, &other) == BM_STATUS_OUT_OF_MEMORY
               && !other);
        assert(a.outstanding_allocations == 3);
        failure_injection_host_fail_on(&a, SIZE_MAX);
        assert(bm_at_keyboard_pair_attach_clock(&h, f.engine, f.pair, &f.link) == BM_STATUS_OK);
        assert(bm_at_keyboard_pair_attach_clock(&h, f.engine, f.pair, &other) == BM_STATUS_INVALID_STATE
               && !other);
        {
            fixture_t g;
            memset(&g, 0, sizeof(g));
            cfg = config(&g);
            assert(bm_at_keyboard_pair_create(&h, &cfg, &g.pair) == BM_STATUS_OK);
            assert(bm_at_keyboard_pair_attach_clock(&h, f.engine, g.pair, &other)
                       == BM_STATUS_CAPACITY_EXCEEDED
                   && !other);
            bm_at_keyboard_pair_destroy(g.pair);
        }
        done(&f);
        assert(!a.outstanding_allocations);
    }
    start(&f, 1);
    boot(&f);
    {
        bm_input_event_t e = event(BM_KEY_A, 1);
        assert(bm_at_keyboard_clock_input(f.link, &e) == BM_STATUS_INVALID_ARGUMENT);
        assert(bm_engine_run_for(f.engine, UINT64_MAX - 1 - bm_engine_now(f.engine)) == BM_STATUS_OK);
        assert(bm_at_keyboard_pair_clock_input(f.link, &e) == BM_STATUS_CAPACITY_EXCEEDED);
        assert(state(&f).keyboard.scan_count == 1 && !state(&f).failure);
        assert(bm_at_clock_link_sync(f.link) == BM_STATUS_CAPACITY_EXCEEDED);
    }
    done(&f);
}
static void
delay_matrix(void)
{
    for (unsigned input = 1; input <= 4; ++input)
        for (unsigned byte = 1; byte <= 4; ++byte)
            for (unsigned output = 1; output <= 4; ++output) {
                fixture_t                    f;
                bm_at_keyboard_pair_config_t c;
                bm_host_services_t           h = bm_null_host_services();
                uint64_t                     ready;
                memset(&f, 0, sizeof(f));
                c                          = config(&f);
                c.controller.input_cycles  = input;
                c.controller.output_cycles = output;
                c.keyboard.byte_cycles     = byte;
                assert(bm_at_keyboard_pair_create(&h, &c, &f.pair) == BM_STATUS_OK);
                f.inspect = 1;
                wr(&f, 0x64, 0x60);
                run(&f, input);
                wr(&f, 0x60, 0x41);
                run(&f, 2 * input);
                ready = (2 * input > 8 ? 2 * input : 8) + byte + output;
                run(&f, ready);
                assert(rd(&f, 0) == 0xaa);
                key(&f, BM_KEY_A, 1);
                wr(&f, 0x60, 0xf5);
                /* Short byte intervals meet IBF backpressure; equal intervals meet the
                 * explicit KBC-first tie; longer ones are replaced before transmission. */
                run(&f, ready + input + byte + output);
                assert(rd(&f, 0) == 0xfa);
                assert(!state(&f).keyboard.enabled && !state(&f).keyboard.scan_count);
                run(&f, ready + 100);
                assert(!(state(&f).controller.status & 1));
                done(&f);
            }
}
int
main(void)
{
    protocol();
    coincidences();
    partitioned();
    pic();
    failures();
    lifecycle();
    cpu_and_attachment();
    delay_matrix();
    puts("Keyboard pair: common-boundary peer coordination, real protocol/PIC, ties, rational clock, "
         "partitions, backpressure and failures pass");
    return 0;
}
