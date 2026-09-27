/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Authored individual KBC/keyboard scheduler contracts, no firmware. */
#include <blumach/components/at_clock.h>
#include <blumach/components/at_pic.h>
#include <blumach/platforms/null_host.h>
#include "failure_injection_host.h"
#include "kbc8042_private.h"
#include "keyboard_at_private.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { IRQ, A20, RESET, INHIBIT, COMMAND, BYTE };
typedef struct edge {
    bm_time_point_t time;
    unsigned        kind, value;
} edge_t;
typedef struct fixture {
    bm_engine_t        *engine;
    bm_at_clock_link_t *link;
    bm_kbc8042_t       *kbc;
    bm_at_keyboard_t   *keyboard;
    bm_at_pic_t        *pic;
    edge_t              edges[128];
    unsigned            count, calls, inspected;
    int                 kind, inspect, fail_kind, after, levels[4];
    bm_status_t         failure;
} fixture_t;
static const bm_clock_rate_t rate = { 3000000, 2 };
static bm_bus_transaction_t
txn(unsigned port, bm_bus_operation_t op, unsigned v)
{
    bm_bus_transaction_t t = { 0 };
    t.space                = BM_ADDRESS_IO;
    t.address              = port;
    t.operation            = op;
    t.value                = v;
    t.size = t.alignment = 1;
    t.wait_states        = 17;
    return t;
}
static bm_input_event_t
key_event(bm_key_code_t key, int down)
{
    bm_input_event_t e = { 0 };
    e.kind             = BM_INPUT_KEY;
    e.key              = key;
    e.pressed          = down;
    return e;
}
static uint64_t
native(fixture_t *f)
{
    return f->kind ? f->keyboard->s.cycles : f->kbc->s.cycles;
}
static bm_status_t
input(fixture_t *f)
{
    bm_input_event_t e = key_event(BM_KEY_A, 1);
    return f->kind ? bm_at_keyboard_clock_input(f->link, &e) : bm_kbc8042_clock_receive(f->link, 0x1c);
}
static bm_status_t
emit(fixture_t *f, unsigned kind, unsigned v)
{
    int fail = f->failure && (unsigned) f->fail_kind == kind;
    ++f->calls;
    if (f->inspect && f->link) {
        bm_bus_transaction_t t = txn(0x64, BM_BUS_READ, 0xbeef), old = t;
        assert(bm_at_clock_link_sync(f->link) == BM_STATUS_INVALID_STATE);
        assert(bm_at_clock_link_changed(f->link) == BM_STATUS_INVALID_STATE);
        assert(bm_at_clock_link_reset(f->link) == BM_STATUS_INVALID_STATE);
        assert(input(f) == BM_STATUS_INVALID_STATE);
        assert(bm_at_clock_link_io(f->link, &t) == BM_STATUS_INVALID_STATE && !memcmp(&t, &old, sizeof(t)));
        t.attributes = BM_BUS_TRANSACTION_DEBUG;
        assert(bm_at_clock_link_io(f->link, &t) == (f->kind ? BM_STATUS_UNSUPPORTED : BM_STATUS_OK));
        bm_at_clock_link_destroy(f->link);
        if (f->kind)
            bm_at_keyboard_destroy(f->keyboard);
        else
            bm_kbc8042_destroy(f->kbc);
        ++f->inspected;
    }
    if (fail && !f->after)
        return f->failure;
    assert(f->count < 128);
    assert(bm_engine_now_exact(f->engine, &f->edges[f->count].time) == BM_STATUS_OK);
    f->edges[f->count].kind    = kind;
    f->edges[f->count++].value = v;
    if (kind < 4)
        f->levels[kind] = (int) v;
    if (kind == IRQ && f->pic)
        assert(bm_at_pic_set_irq(f->pic, 1, (int) v) == BM_STATUS_OK);
    return fail ? f->failure : BM_STATUS_OK;
}
static bm_status_t
irq(void *p, int v)
{
    return emit(p, IRQ, (unsigned) v);
}
static bm_status_t
a20(void *p, int v)
{
    return emit(p, A20, (unsigned) v);
}
static bm_status_t
reset(void *p, int v)
{
    return emit(p, RESET, (unsigned) v);
}
static bm_status_t
inhibit(void *p, int v)
{
    return emit(p, INHIBIT, (unsigned) v);
}
static bm_status_t
command(void *p, uint8_t v)
{
    return emit(p, COMMAND, v);
}
static bm_status_t
byte(void *p, uint8_t v)
{
    return emit(p, BYTE, v);
}
static void
create(fixture_t *f, int kind, const bm_host_services_t *h, size_t slots)
{
    bm_engine_config_t ec = { 1, 4, slots };
    memset(f, 0, sizeof(*f));
    f->kind = kind;
    assert(bm_engine_create_clocked(h, &ec, &f->engine) == BM_STATUS_OK);
    if (kind) {
        bm_at_keyboard_config_t c = { 0 };
        c.send                    = byte;
        c.send_context            = f;
        c.clock                   = rate;
        c.power_on_cycles         = 3;
        c.bat_cycles              = 5;
        c.byte_cycles             = 2;
        c.reset_accept_cycles     = 3;
        assert(bm_at_keyboard_create(h, &c, &f->keyboard) == BM_STATUS_OK);
    } else {
        bm_kbc8042_config_t c = { 0 };
        c.data_port           = 0x60;
        c.command_port        = 0x64;
        c.irq                 = irq;
        c.a20                 = a20;
        c.cpu_reset           = reset;
        c.output_context      = f;
        c.keyboard_inhibit    = inhibit;
        c.keyboard_command    = command;
        c.keyboard_context    = f;
        c.clock               = rate;
        c.input_cycles        = 2;
        c.self_test_cycles    = 5;
        c.output_cycles       = 3;
        c.pulse_cycles        = 6;
        c.input_port          = 0xa0;
        c.initial_output_port = 0xc1;
        assert(bm_kbc8042_create(h, &c, &f->kbc) == BM_STATUS_OK);
    }
}
static bm_status_t
attach(fixture_t *f, const bm_host_services_t *h, const bm_clock_rate_t *r, bm_at_clock_link_t **out)
{
    return f->kind ? bm_at_keyboard_attach_clock(h, f->engine, f->keyboard, r, out)
                   : bm_kbc8042_attach_clock(h, f->engine, f->kbc, r, out);
}
static void
start(fixture_t *f, int kind)
{
    bm_host_services_t h = bm_null_host_services();
    create(f, kind, &h, 3);
    assert(attach(f, &h, &rate, &f->link) == BM_STATUS_OK && !f->calls);
}
static void
done(fixture_t *f)
{
    bm_engine_destroy(f->engine);
    bm_at_clock_link_destroy(f->link);
    if (f->kind) {
        assert(!f->keyboard->clock_link);
        bm_at_keyboard_destroy(f->keyboard);
    } else {
        assert(!f->kbc->clock_link);
        bm_kbc8042_destroy(f->kbc);
    }
    if (f->pic)
        bm_at_pic_destroy(f->pic);
}
static uint64_t
ceil_edge(uint64_t n)
{
    return (n * 2000 + 2) / 3;
}
static void
run_to(fixture_t *f, uint64_t ns)
{
    assert(ns >= bm_engine_now(f->engine));
    assert(bm_engine_run_for(f->engine, ns - bm_engine_now(f->engine)) == BM_STATUS_OK);
}
static void
run(fixture_t *f, uint64_t n)
{
    run_to(f, ceil_edge(n));
}
static void
exact(edge_t *e, uint64_t n)
{
    assert(e->time.nanoseconds == n * 2000 / 3);
    assert(e->time.subnanosecond_numerator == n * 2000 % 3);
    assert(e->time.subnanosecond_denominator == (n % 3 ? 3U : 1U));
}
static void
wr(fixture_t *f, unsigned port, unsigned v)
{
    bm_bus_transaction_t t = txn(port, BM_BUS_WRITE, v), old = t;
    assert(bm_at_clock_link_io(f->link, &t) == BM_STATUS_OK && !memcmp(&t, &old, sizeof(t)));
}
static unsigned
rd(fixture_t *f, unsigned port, int debug)
{
    bm_bus_transaction_t t = txn(port, BM_BUS_READ, 0xbeef);
    t.attributes           = debug ? BM_BUS_TRANSACTION_DEBUG : 0;
    assert(bm_at_clock_link_io(f->link, &t) == BM_STATUS_OK && t.wait_states == 17);
    return (unsigned) t.value;
}
static void
keyboard_protocol(void)
{
    fixture_t            f;
    bm_input_event_t     e;
    bm_bus_transaction_t t, old;
    start(&f, 1);
    f.inspect = 1;
    run_to(&f, ceil_edge(3) - 1);
    assert(native(&f) == 0 && !f.count);
    run(&f, 3);
    assert(native(&f) == 3 && f.keyboard->s.leds == 7);
    run(&f, 8);
    assert(!f.count && !f.keyboard->s.enabled);
    run(&f, 10);
    assert(f.count == 1 && f.edges[0].value == 0xaa);
    exact(&f.edges[0], 10);
    run(&f, 1000);
    assert(native(&f) == 10); /* Idle source retains lazy cursor. */
    t            = txn(0x60, BM_BUS_READ, 0xbeef);
    t.attributes = BM_BUS_TRANSACTION_DEBUG;
    old          = t;
    assert(bm_at_clock_link_io(f.link, &t) == BM_STATUS_UNSUPPORTED && !memcmp(&t, &old, sizeof(t))
           && native(&f) == 10);
    assert(bm_at_clock_link_changed(f.link) == BM_STATUS_INVALID_STATE);
    e = key_event(BM_KEY_A, 1);
    assert(bm_at_keyboard_clock_input(f.link, &e) == BM_STATUS_OK && native(&f) == 1000);
    run(&f, 1002);
    assert(f.count == 2 && f.edges[1].value == 0x1c);
    exact(&f.edges[1], 1002);
    assert(bm_at_keyboard_clock_inhibit(f.link, 1) == BM_STATUS_OK);
    run(&f, 1000000);
    assert(f.count == 2 && native(&f) == 1002);
    assert(bm_at_keyboard_clock_inhibit(f.link, 0) == BM_STATUS_OK && native(&f) == 1000000);
    assert(f.keyboard->s.repeat_remaining == 750000);
    run(&f, 1750002);
    assert(f.count == 3 && f.edges[2].value == 0x1c);
    exact(&f.edges[2], 1750002);
    e.pressed = 0;
    assert(bm_at_keyboard_clock_input(f.link, &e) == BM_STATUS_OK);
    run(&f, 1750006);
    assert(f.count == 5 && f.edges[3].value == 0xf0 && f.edges[4].value == 0x1c);
    assert(bm_at_keyboard_clock_command(f.link, 0xff) == BM_STATUS_OK);
    run(&f, 1750008);
    assert(f.edges[5].value == 0xfa);
    assert(bm_at_keyboard_clock_inhibit(f.link, 1) == BM_STATUS_OK);
    run(&f, 2000000);
    assert(f.keyboard->s.phase == BM_AT_KEYBOARD_RESET_ACCEPT);
    assert(bm_at_keyboard_clock_inhibit(f.link, 0) == BM_STATUS_OK);
    run(&f, 2000003);
    assert(f.keyboard->s.phase == BM_AT_KEYBOARD_BAT);
    run(&f, 2000010);
    assert(f.edges[6].value == 0xaa);
    exact(&f.edges[6], 2000010);
    assert(f.inspected == f.calls);
    done(&f);
}
static void
pic_write(fixture_t *f, unsigned port, unsigned v)
{
    bm_bus_transaction_t t = txn(port, BM_BUS_WRITE, v);
    assert(bm_at_pic_io(f->pic, &t) == BM_STATUS_OK);
}
static void
kbc_protocol(void)
{
    fixture_t          f;
    bm_host_services_t h  = bm_null_host_services();
    bm_at_pic_config_t pc = { 0 };
    bm_at_pic_state_t  ps;
    uint8_t            vector = 0;
    start(&f, 0);
    f.inspect       = 1;
    pc.master_base  = 0x20;
    pc.slave_base   = 0xa0;
    pc.cascade_line = 2;
    assert(bm_at_pic_create(&h, &pc, &f.pic) == BM_STATUS_OK);
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
    wr(&f, 0x64, 0xaa);
    run(&f, 7);
    assert(!(rd(&f, 0x64, 1) & 1));
    run(&f, 10);
    assert(rd(&f, 0x60, 0) == 0x55);
    wr(&f, 0x64, 0x60);
    run(&f, 12);
    wr(&f, 0x60, 0x41);
    run(&f, 14);
    assert(bm_kbc8042_clock_receive(f.link, 0x1c) == BM_STATUS_OK);
    assert(bm_kbc8042_clock_receive(f.link, 0x32) == BM_STATUS_CAPACITY_EXCEEDED);
    run(&f, 17);
    assert(f.levels[IRQ]);
    assert(bm_at_pic_state(f.pic, &ps) == BM_STATUS_OK && ps.intr);
    assert(bm_at_pic_acknowledge(f.pic, 0, &vector) == BM_STATUS_OK);
    assert(bm_at_pic_acknowledge(f.pic, 1, &vector) == BM_STATUS_OK && vector == 0x31);
    assert(rd(&f, 0x60, 1) == 0x1e && f.levels[IRQ]);
    assert(rd(&f, 0x60, 0) == 0x1e && !f.levels[IRQ]);
    pic_write(&f, 0x20, 0x20);
    wr(&f, 0x64, 0xfe);
    run(&f, 19);
    assert(f.levels[RESET]);
    run_to(&f, ceil_edge(25) - 1);
    assert(f.levels[RESET]);
    run(&f, 25);
    assert(!f.levels[RESET]);
    {
        unsigned n = f.count;
        wr(&f, 0x60, 0xed);
        run(&f, 27);
        assert(f.edges[n].kind == COMMAND && f.edges[n].value == 0xed);
        exact(&f.edges[n], 27);
        assert(!(rd(&f, 0x64, 1) & 1));
    } /* Endpoint supplies ACK, not adapter. */
    run(&f, 1000);
    assert(native(&f) == 27);
    assert(bm_kbc8042_clock_receive(f.link, 0xfa) == BM_STATUS_OK && native(&f) == 1000);
    run(&f, 1003);
    assert(rd(&f, 0x60, 0) == 0xfa);
    assert(f.inspected == f.calls);
    done(&f);
}
static void
rejects_and_reset(void)
{
    for (int kind = 0; kind < 2; ++kind) {
        fixture_t            f;
        uint64_t             preserved;
        bm_bus_transaction_t t = txn(0x777, BM_BUS_READ, 0xbeef), old = t;
        start(&f, kind);
        if (kind)
            run(&f, 10);
        run(&f, 100);
        assert(bm_at_clock_link_io(f.link, &t) == (kind ? BM_STATUS_UNSUPPORTED : BM_STATUS_UNMAPPED));
        assert(!memcmp(&t, &old, sizeof(t)) && native(&f) == 100);
        assert(bm_at_clock_link_sync(f.link) == BM_STATUS_OK);
        if (kind) {
            bm_input_event_t e = key_event((bm_key_code_t) 255, 1);
            assert(bm_at_keyboard_clock_input(f.link, &e) == BM_STATUS_UNSUPPORTED);
            assert(bm_at_keyboard_clock_inhibit(f.link, 2) == BM_STATUS_INVALID_ARGUMENT);
            assert(bm_kbc8042_clock_receive(f.link, 0) == BM_STATUS_INVALID_ARGUMENT);
        } else
            assert(bm_at_keyboard_clock_command(f.link, 0xed) == BM_STATUS_INVALID_ARGUMENT);
        run(&f, 123);
        assert(bm_at_clock_link_reset(f.link) == BM_STATUS_INVALID_STATE);
        assert(bm_at_clock_link_sync(f.link) == BM_STATUS_OK);
        preserved = native(&f);
        assert((kind ? bm_at_keyboard_reset(f.keyboard) : bm_kbc8042_reset(f.kbc)) == BM_STATUS_OK);
        assert(bm_at_clock_link_changed(f.link) == BM_STATUS_OK && native(&f) == preserved);
        assert(bm_engine_reset(f.engine) == BM_STATUS_OK);
        assert(bm_at_clock_link_sync(f.link) == BM_STATUS_INVALID_STATE);
        assert(bm_at_clock_link_reset(f.link) == BM_STATUS_OK && native(&f) == preserved);
        run(&f, 10);
        assert(bm_at_clock_link_sync(f.link) == BM_STATUS_OK && native(&f) == preserved + 10);
        done(&f);
    }
}
static void
attachment(void)
{
    for (int kind = 0; kind < 2; ++kind) {
        fixture_t                f, g;
        failure_injection_host_t allocator;
        bm_host_services_t       h          = bm_null_host_services(), a;
        bm_at_clock_link_t      *link       = NULL;
        bm_clock_rate_t          equivalent = { 1500000, 1 }, bad = { 1500001, 1 };
        create(&f, kind, &h, 1);
        create(&g, kind, &h, 1);
        failure_injection_host_initialize(&allocator);
        a = failure_injection_host_services(&allocator);
        failure_injection_host_fail_after(&allocator, 0);
        assert(attach(&f, &a, &rate, &link) == BM_STATUS_OUT_OF_MEMORY && !link
               && !allocator.outstanding_allocations);
        failure_injection_host_fail_on(&allocator, SIZE_MAX);
        assert(attach(&f, &a, &bad, &link) == BM_STATUS_INVALID_ARGUMENT && !link);
        bad.cycles_per_second_denominator = 0;
        assert(attach(&f, &a, &bad, &link) == BM_STATUS_INVALID_ARGUMENT);
        assert(attach(&f, &a, &equivalent, &f.link) == BM_STATUS_OK);
        assert(attach(&f, &a, &rate, &link) == BM_STATUS_INVALID_STATE && !link);
        {
            bm_engine_t *own = g.engine;
            g.engine         = f.engine;
            assert(attach(&g, &a, &rate, &link) == BM_STATUS_CAPACITY_EXCEEDED && !link);
            g.engine = own;
        }
        assert(allocator.outstanding_allocations == 1);
        /* Attached native destroy must not invalidate the engine callback. */
        if (kind)
            bm_at_keyboard_destroy(f.keyboard);
        else
            bm_kbc8042_destroy(f.kbc);
        run(&f, 10);
        done(&f);
        assert(!allocator.outstanding_allocations);
        assert((kind ? bm_at_keyboard_advance(g.keyboard, 1) : bm_kbc8042_advance(g.kbc, 1)) == BM_STATUS_OK);
        assert(attach(&g, &h, &rate, &link) == BM_STATUS_INVALID_STATE);
        done(&g);
    }
}
static void
failures(void)
{
    for (int kind = 0; kind < 2; ++kind)
        for (int after = 0; after < 2; ++after) {
            fixture_t f;
            unsigned  calls;
            start(&f, kind);
            f.failure   = BM_STATUS_DEVICE_ERROR;
            f.after     = after;
            f.fail_kind = kind ? BYTE : RESET;
            if (!kind)
                wr(&f, 0x64, 0xfe);
            assert(bm_engine_run_for(f.engine, 100000) == BM_STATUS_DEVICE_ERROR);
            assert(native(&f) == (kind ? 10U : 2U));
            calls = f.calls;
            assert(input(&f) == BM_STATUS_DEVICE_ERROR && f.calls == calls);
            assert(bm_at_clock_link_sync(f.link) == BM_STATUS_DEVICE_ERROR);
            f.failure = BM_STATUS_OK;
            assert((kind ? bm_at_keyboard_reset(f.keyboard) : bm_kbc8042_reset(f.kbc)) == BM_STATUS_OK);
            assert(bm_at_clock_link_sync(f.link)
                   == BM_STATUS_DEVICE_ERROR); /* Device alone cannot clear link failure. */
            assert(bm_engine_reset(f.engine) == BM_STATUS_OK);
            assert(bm_at_clock_link_reset(f.link) == BM_STATUS_OK);
            run(&f, 10);
            done(&f);
        }
    {
        fixture_t f;
        start(&f, 1);
        f.failure   = BM_STATUS_CAPACITY_EXCEEDED;
        f.fail_kind = BYTE;
        run(&f, 20);
        assert(!f.count && f.keyboard->s.response_count == 1 && !f.keyboard->s.failure);
        f.failure = BM_STATUS_OK;
        run(&f, 22);
        assert(f.count == 1 && f.edges[0].value == 0xaa);
        exact(&f.edges[0], 22);
        done(&f);
    }
    {
        fixture_t        f;
        bm_input_event_t e = key_event(BM_KEY_A, 1);
        start(&f, 1);
        run(&f, 10);
        run_to(&f, UINT64_MAX - 1);
        assert(bm_at_keyboard_clock_input(f.link, &e) == BM_STATUS_CAPACITY_EXCEEDED);
        assert(f.keyboard->s.scan_count == 1 && !f.keyboard->s.failure);
        assert(bm_at_clock_link_sync(f.link) == BM_STATUS_CAPACITY_EXCEEDED);
        done(&f);
    }
}
static void
partitioned(void)
{
    fixture_t        f, g;
    bm_input_event_t e      = key_event(BM_KEY_A, 1);
    unsigned         random = 73;
    uint64_t         at     = 0;
    start(&f, 1);
    start(&g, 1);
    run(&f, 10);
    run(&g, 10);
    assert(bm_at_keyboard_clock_input(f.link, &e) == BM_STATUS_OK);
    assert(bm_at_keyboard_clock_input(g.link, &e) == BM_STATUS_OK);
    run_to(&f, 1000000000);
    at = bm_engine_now(g.engine);
    while (at < 1000000000) {
        random = random * 1664525U + 1013904223U;
        at += 1 + random % 10000000;
        if (at > 1000000000)
            at = 1000000000;
        run_to(&g, at);
    }
    assert(bm_at_clock_link_sync(f.link) == BM_STATUS_OK && bm_at_clock_link_sync(g.link) == BM_STATUS_OK);
    assert(f.count == g.count && native(&f) == 1500000 && native(&g) == 1500000);
    for (unsigned i = 0; i < f.count; ++i) {
        assert(f.edges[i].value == g.edges[i].value && f.edges[i].kind == g.edges[i].kind);
        assert(f.edges[i].time.nanoseconds == g.edges[i].time.nanoseconds);
        assert(f.edges[i].time.subnanosecond_numerator == g.edges[i].time.subnanosecond_numerator);
        assert(f.edges[i].time.subnanosecond_denominator == g.edges[i].time.subnanosecond_denominator);
    }
    assert(bm_at_keyboard_clock_inhibit(f.link, 1) == BM_STATUS_OK && !g.keyboard->s.inhibited);
    done(&f);
    done(&g);
}
static void
input_failures_and_shared_engine(void)
{
    for (int after = 0; after < 2; ++after) {
        fixture_t f;
        start(&f, 0);
        wr(&f, 0x64, 0xae);
        run(&f, 2);
        f.failure   = BM_STATUS_DEVICE_ERROR;
        f.fail_kind = INHIBIT;
        f.after     = after;
        assert(bm_kbc8042_clock_receive(f.link, 0x1c) == BM_STATUS_DEVICE_ERROR);
        assert(f.kbc->s.output_remaining == 3 && f.kbc->s.failure == BM_STATUS_DEVICE_ERROR);
        {
            unsigned calls = f.calls;
            assert(bm_kbc8042_clock_receive(f.link, 0x32) == BM_STATUS_DEVICE_ERROR && f.calls == calls);
        }
        done(&f);
    }
    {
        fixture_t f;
        start(&f, 0);
        wr(&f, 0x64, 0xae);
        run(&f, 2);
        f.failure   = BM_STATUS_IDLE;
        f.fail_kind = COMMAND;
        wr(&f, 0x60, 0xed);
        run(&f, 6);
        assert((f.kbc->s.status & 2) && !f.kbc->s.failure);
        f.failure = BM_STATUS_OK;
        run(&f, 8);
        assert(!(f.kbc->s.status & 2));
        assert(f.edges[f.count - 1].kind == COMMAND && f.edges[f.count - 1].value == 0xed);
        exact(&f.edges[f.count - 1], 8);
        done(&f);
    }
    {
        fixture_t          f, g;
        bm_host_services_t h = bm_null_host_services();
        start(&f, 1);
        create(&g, 0, &h, 1);
        bm_engine_destroy(g.engine);
        g.engine = f.engine;
        assert(attach(&g, &h, &rate, &g.link) == BM_STATUS_OK);
        wr(&g, 0x64, 0xaa);
        run(&f, 10);
        assert(f.count == 1 && f.edges[0].value == 0xaa && rd(&g, 0x60, 0) == 0x55);
        exact(&f.edges[0], 10);
        assert(bm_at_clock_link_sync(g.link) == BM_STATUS_OK && native(&g) == 10);
        /* Shared scheduler, independent endpoints. No claim of peer coordination. */
        bm_engine_destroy(f.engine);
        bm_at_clock_link_destroy(f.link);
        bm_at_clock_link_destroy(g.link);
        bm_at_keyboard_destroy(f.keyboard);
        bm_kbc8042_destroy(g.kbc);
    }
    for (int kind = 0; kind < 2; ++kind) {
        fixture_t           f;
        bm_host_services_t  h    = bm_null_host_services();
        bm_at_clock_link_t *link = NULL;
        create(&f, kind, &h, 1);
        run_to(&f, 1);
        assert(attach(&f, &h, &rate, &link) == BM_STATUS_INVALID_ARGUMENT && !link);
        done(&f);
    }
}
typedef struct cpu_state {
    fixture_t *f;
    unsigned   steps;
} cpu_state_t;
static bm_status_t
cpu_reset(void *context)
{
    ((cpu_state_t *) context)->steps = 0;
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
    cpu_state_t     *cpu = context;
    bm_input_event_t e   = key_event(BM_KEY_A, 1);
    e.repeat             = 1;
    (void) start;
    assert(bm_at_keyboard_clock_input(cpu->f->link, &e) == BM_STATUS_OK);
    assert(native(cpu->f) == cpu->steps / 2U);
    ++cpu->steps;
    *cycles = 1;
    return BM_STATUS_OK;
}
static void
cpu_boundaries(void)
{
    fixture_t       f;
    bm_cpu_t        cpu = { 0 };
    cpu_state_t     s;
    bm_clock_rate_t cpu_rate = { 3000000, 1 };
    start(&f, 1);
    s              = (cpu_state_t) { &f, 0 };
    cpu.context    = &s;
    cpu.ops.reset  = cpu_reset;
    cpu.ops.signal = cpu_signal;
    assert(bm_engine_add_clocked_cpu(f.engine, &cpu, cpu_step, &cpu_rate, NULL) == BM_STATUS_OK);
    run_to(&f, 100000);
    assert(s.steps == 300 && f.count == 1);
    done(&f);
}
int
main(void)
{
    keyboard_protocol();
    kbc_protocol();
    rejects_and_reset();
    attachment();
    failures();
    partitioned();
    input_failures_and_shared_engine();
    cpu_boundaries();
    puts("KBC/keyboard clocks: rational edges, lazy sync/typed inputs, PIC, reset, failures, ownership, "
         "partitioning and CPU boundaries pass");
    return 0;
}
