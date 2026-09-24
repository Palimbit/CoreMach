/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 BluMach contributors
 */
#include "descriptor_286.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct transfer {
    uint64_t address;
    unsigned size, kind; /* read, write, INTA */
    bool locked;
    uint16_t value;
} transfer_t;

typedef struct fixture {
    uint8_t ram[65536];
    transfer_t trace[128];
    unsigned calls, effects, writes, acks, fail, raise_nmi;
    unsigned locks, unlocks, shutdown_asserts, shutdown_clears;
    uint64_t successful_waits;
    bool locked, after;
    uint8_t ack_vector;
    bm_status_t failure;
    bm_286_arch_state_t *observed, before;
} fixture_t;

static void same_segment(const bm_286_segment_state_t *a, const bm_286_segment_state_t *b)
{
    assert(a->selector == b->selector && a->base == b->base && a->limit == b->limit);
    assert(a->access == b->access && a->valid == b->valid);
}

static void same_frame(const bm_286_arch_state_t *a, const bm_286_arch_state_t *b)
{
    assert(a->size == b->size && a->version == b->version);
    assert(a->ax == b->ax && a->cx == b->cx && a->dx == b->dx && a->bx == b->bx);
    assert(a->sp == b->sp && a->bp == b->bp && a->si == b->si && a->di == b->di);
    assert(a->ip == b->ip && a->flags == b->flags && a->msw == b->msw && a->cpl == b->cpl);
    same_segment(&a->cs, &b->cs); same_segment(&a->ss, &b->ss);
    same_segment(&a->ds, &b->ds); same_segment(&a->es, &b->es);
    same_segment(&a->ldtr, &b->ldtr); same_segment(&a->tr, &b->tr);
    assert(a->gdtr.base == b->gdtr.base && a->gdtr.limit == b->gdtr.limit);
    assert(a->idtr.base == b->idtr.base && a->idtr.limit == b->idtr.limit);
    assert(a->halted == b->halted && a->shutdown == b->shutdown);
    assert(a->trap_pending == b->trap_pending && a->interrupt_shadow == b->interrupt_shadow);
    assert(a->nmi_blocked == b->nmi_blocked);
}

static bool begin(fixture_t *f, unsigned kind, uint64_t address, unsigned size, uint16_t value)
{
    same_frame(f->observed, &f->before); /* No early architectural frame commit. */
    assert(f->calls < sizeof(f->trace) / sizeof(f->trace[0]));
    f->trace[f->calls++] = (transfer_t){address, size, kind, f->locked, value};
    return f->calls != f->fail || f->after;
}

static bm_status_t finish(fixture_t *f, unsigned waits)
{
    ++f->effects;
    if (f->calls == f->raise_nmi) f->observed->nmi_pending = 1;
    if (f->calls == f->fail) return f->failure;
    f->successful_waits += waits;
    return BM_STATUS_OK;
}

static bm_status_t access_bus(void *context, bm_bus_transaction_t *t)
{
    fixture_t *f = context;
    unsigned i;
    assert(t->space == BM_ADDRESS_DATA && t->endianness == BM_ENDIAN_LITTLE);
    assert(t->address <= 0xffffffu);
    assert(t->size == 1 || (t->size == 2 && !(t->address & 1u)));
    assert(t->alignment == t->size && t->wait_states == 0);
    assert(t->attributes == (f->locked ? BM_BUS_TRANSACTION_LOCKED : 0u));
    assert(t->operation == BM_BUS_READ || t->operation == BM_BUS_WRITE);
    if (!begin(f, t->operation == BM_BUS_WRITE ? 1u : 0u, t->address, t->size, (uint16_t)t->value))
        return f->failure;
    if (t->operation == BM_BUS_READ) t->value = 0;
    else ++f->writes;
    for (i = 0; i < t->size; ++i) {
        unsigned at = (t->address + i) & 65535u;
        if (t->operation == BM_BUS_READ) t->value |= (uint64_t)f->ram[at] << (i * 8u);
        else f->ram[at] = (uint8_t)(t->value >> (i * 8u));
    }
    t->wait_states = 3;
    return finish(f, 3);
}

static bm_status_t acknowledge(void *context, unsigned phase, uint8_t *vector, uint32_t *waits)
{
    fixture_t *f = context;
    assert(f->locked && phase == f->acks && *waits == 0);
    ++f->acks;
    if (!begin(f, 2, phase, 0, 0)) return f->failure;
    *vector = phase == 1 ? f->ack_vector : 0xff;
    *waits = 5;
    return finish(f, 5);
}

static void lock_bus(void *context, int asserted)
{
    fixture_t *f = context;
    assert(f->locked != (asserted != 0));
    f->locked = asserted != 0;
    if (asserted) ++f->locks;
    else ++f->unlocks;
}

static void shutdown_pin(void *context, int asserted)
{
    fixture_t *f = context;
    assert(!f->locked && f->observed->shutdown == (asserted != 0));
    if (asserted) ++f->shutdown_asserts;
    else ++f->shutdown_clears;
}

static void put_word(fixture_t *f, uint32_t at, uint16_t value)
{
    f->ram[at & 65535u] = (uint8_t)value;
    f->ram[(at + 1u) & 65535u] = (uint8_t)(value >> 8);
}

static uint16_t word(const fixture_t *f, uint32_t at)
{
    return (uint16_t)(f->ram[at & 65535u] | ((uint16_t)f->ram[(at + 1u) & 65535u] << 8));
}

static void observe(fixture_t *f, bm_286_arch_state_t *a)
{
    f->observed = a; f->before = *a;
}

static void setup(fixture_t *f, bm_286_arch_state_t *a, bm_286_config_t *c,
    bm_286_pm_request_t *q, bm_286_pm_delivery_state_t *s, unsigned cpl, unsigned odd)
{
    unsigned v;
    memset(f, 0, sizeof(*f)); memset(a, 0, sizeof(*a)); memset(c, 0, sizeof(*c));
    memset(q, 0, sizeof(*q)); memset(s, 0, sizeof(*s));
    a->size = sizeof(*a); a->version = BM_286_STATE_VERSION;
    a->msw = 1; a->cpl = (uint8_t)cpl; a->sp = 0x8000; a->ip = 0x1234; a->flags = 0x3702;
    a->ax = 0x1122; a->cx = 3; a->dx = 0x4455; a->bx = 0x6677;
    a->bp = 0x8899; a->si = 0xaabb; a->di = 0xccdd;
    a->ss.selector = (uint16_t)(16 + cpl); a->ss.valid = 1;
    a->ss.limit = 0xffff; a->ss.access = (uint8_t)(0x93 | (cpl << 5)); a->ss.base = odd;
    a->cs = a->ss; a->cs.selector = (uint16_t)(24 + cpl);
    a->cs.base = 0x3000; a->cs.access = (uint8_t)(0x9b | (cpl << 5));
    a->es = a->ds = a->ss;
    a->idtr.base = 0x1000 + odd; a->idtr.limit = 0x7ff;
    a->gdtr.base = 0x2000 + odd; a->gdtr.limit = 0x1f;
    for (v = 0; v < 256; ++v) {
        uint32_t at = a->idtr.base + v * 8;
        put_word(f, at, (uint16_t)(0x400 + v)); put_word(f, at + 2, 8);
        f->ram[at + 5] = (uint8_t)(0x86 | (cpl << 5));
    }
    for (v = 8; v <= 24; v += 16) {
        uint32_t at = a->gdtr.base + v;
        put_word(f, at, 0xffff); put_word(f, at + 2, 0x3000);
        f->ram[at + 5] = (uint8_t)(0x9b | (cpl << 5));
    }
    c->access = access_bus; c->access_context = f;
    c->bus_lock = lock_bus; c->shutdown = shutdown_pin; c->pin_context = f;
    c->interrupt_ack = acknowledge; c->interrupt_context = f;
    q->source = BM_286_PM_EXCEPTION; q->vector = 6; q->restart_ip = a->ip;
    q->next_ip = 0x1237; q->error_code = 0x1234;
    f->ack_vector = 0x30; f->failure = BM_STATUS_DEVICE_ERROR;
    observe(f, a);
}

static void missing_gate(fixture_t *f, const bm_286_arch_state_t *a, unsigned vector)
{
    f->ram[a->idtr.base + vector * 8 + 5] &= 0x7fu;
}

static void classifications(void)
{
    static const uint8_t causes[] = {0, 5, 6, 7, 8, 10, 11, 12, 13, 16};
    fixture_t f;
    bm_286_arch_state_t a;
    bm_286_config_t c;
    bm_286_pm_request_t q;
    bm_286_pm_delivery_state_t s;
    bm_286_pm_delivery_result_t r;
    unsigned v, cpl, odd, mode;
    for (cpl = 0; cpl < 4; ++cpl) for (odd = 0; odd < 2; ++odd)
    for (v = 0; v < 256; ++v) {
        bool supported = false;
        unsigned i;
        setup(&f, &a, &c, &q, &s, cpl, odd); q.vector = (uint8_t)v;
        for (i = 0; i < sizeof(causes); ++i) if (causes[i] == v) supported = true;
        assert(bm_286_pm_deliver(&a, &c, &s, &q, &r) == (supported ? BM_STATUS_OK : BM_STATUS_UNSUPPORTED));
        assert(!s.stopped && r.entered == supported);
        if (!supported) { assert(!f.calls && !r.accepted); same_frame(&a, &f.before); continue; }
        assert(r.attempts == 1 && r.vector == v && !r.shutdown);
        assert(r.has_error[0] == (v == 8 || (v >= 10 && v <= 13)));
        assert(a.sp == 0x8000u - (r.has_error[0] ? 8u : 6u));
        assert(word(&f, a.ss.base + 0x7ffa) == q.restart_ip);
        if (r.has_error[0]) assert(word(&f, a.ss.base + a.sp) == (v == 8 ? 0 : q.error_code));
    }
    for (mode = 0; mode < 2; ++mode) for (v = 0; v < 256; ++v) {
        setup(&f, &a, &c, &q, &s, 3, v & 1u);
        q.source = mode ? BM_286_PM_BOUNDARY : BM_286_PM_SOFTWARE;
        q.vector = f.ack_vector = (uint8_t)v; q.intr_line = true;
        assert(bm_286_pm_deliver(&a, &c, &s, &q, &r) == BM_STATUS_OK && r.entered);
        assert(!r.has_error[0] && a.sp == 0x7ffa && f.acks == (mode ? 2u : 0u));
        assert(word(&f, a.ss.base + a.sp) == (mode ? f.before.ip : q.next_ip));
    }
}

static void escalation(void)
{
    static const uint8_t causes[] = {0, 5, 6, 7, 8, 9, 10, 11, 12, 13, 16};
    fixture_t f;
    bm_286_arch_state_t a;
    bm_286_config_t c;
    bm_286_pm_request_t q;
    bm_286_pm_delivery_state_t s;
    bm_286_pm_delivery_result_t r;
    unsigned i, failure, cpl, odd;
    for (cpl = 0; cpl < 4; ++cpl) for (odd = 0; odd < 2; ++odd)
    for (i = 0; i < sizeof(causes); ++i) for (failure = 0; failure < 2; ++failure) {
        unsigned first = causes[i];
        bool df = first == 0 || (first >= 10 && first <= 13);
        unsigned expected = df ? 8u : failure ? 13u : 11u;
        setup(&f, &a, &c, &q, &s, cpl, odd); q.vector = (uint8_t)first;
        if (first == 9) { q.source = BM_286_PM_BOUNDARY; q.extension_overrun = true; }
        f.ram[a.idtr.base + first * 8 + 5] = failure ? 0x82 : 0x06;
        assert(bm_286_pm_deliver(&a, &c, &s, &q, &r) == BM_STATUS_OK);
        assert(!s.stopped && !f.acks && !f.locked && r.attempts == (first == 8 ? 1 : 2));
        if (first == 8) { assert(r.shutdown && !r.entered && a.shutdown && f.shutdown_asserts == 1); continue; }
        assert(r.entered && r.vector == expected && r.has_error[1]);
        assert(word(&f, a.ss.base + a.sp) == (df ? 0u : first * 8u + 2u +
            ((first == 7 || first == 9 || first == 16) ? 1u : 0u)));
        assert(word(&f, a.ss.base + a.sp + 2) == q.restart_ip);
    }
    /* A vector number from INT/INTA is not an exception classification. */
    for (i = 0; i < 256; ++i) for (failure = 0; failure < 2; ++failure) {
        setup(&f, &a, &c, &q, &s, 0, i & 1u);
        q.source = failure ? BM_286_PM_BOUNDARY : BM_286_PM_SOFTWARE;
        q.intr_line = true; q.vector = f.ack_vector = (uint8_t)i;
        missing_gate(&f, &a, i);
        assert(bm_286_pm_deliver(&a, &c, &s, &q, &r) == BM_STATUS_OK && r.entered);
        assert(r.vector == (i == 11 ? 8u : 11u));
        assert(r.attempts == (i == 11 ? 3 : 2) && f.acks == (failure ? 2u : 0u));
        assert(word(&f, a.ss.base + a.sp) == (i == 11 ? 0u : i * 8u + 2u + failure));
        assert(word(&f, a.ss.base + a.sp + 2) == q.restart_ip);
    }
}

static void arbitration(void)
{
    fixture_t f;
    bm_286_arch_state_t a;
    bm_286_config_t c;
    bm_286_pm_request_t q;
    bm_286_pm_delivery_state_t s;
    bm_286_pm_delivery_result_t r;
    unsigned bits, shadow, sleep;
    for (bits = 0; bits < 64; ++bits) for (shadow = 0; shadow < 3; ++shadow)
    for (sleep = 0; sleep < 3; ++sleep) {
        unsigned expected = 256;
        setup(&f, &a, &c, &q, &s, 0, bits & 1u);
        a.flags = (uint16_t)(2u | ((bits & 1u) ? 0x200u : 0u));
        a.trap_pending = (uint8_t)((bits >> 1) & 1u);
        a.nmi_pending = (uint8_t)((bits >> 2) & 1u);
        a.nmi_blocked = (uint8_t)((bits >> 3) & 1u);
        q.intr_line = (bits & 16u) != 0; q.source = BM_286_PM_BOUNDARY;
        q.extension_overrun = (bits & 32u) != 0;
        a.interrupt_shadow = (uint8_t)shadow; a.halted = sleep == 1; a.shutdown = sleep == 2;
        observe(&f, &a);
        if (sleep != 2 && a.trap_pending && shadow != 2) expected = 1;
        else if (a.nmi_pending && !a.nmi_blocked && shadow != 2) expected = 2;
        else if (sleep != 2 && q.extension_overrun && shadow != 2) expected = 9;
        else if (sleep != 2 && q.intr_line && (a.flags & 0x200u) && !shadow) expected = 0x30;
        assert(bm_286_pm_deliver(&a, &c, &s, &q, &r) == (expected == 256 ? BM_STATUS_IDLE : BM_STATUS_OK));
        if (expected == 256) { assert(!r.accepted && !f.calls); same_frame(&a, &f.before); continue; }
        assert(r.entered && r.vector == expected && !a.halted && !a.shutdown);
        assert(f.acks == (expected == 0x30 ? 2u : 0u));
        assert(a.nmi_pending == (expected == 2 ? 0 : f.before.nmi_pending));
        assert(a.nmi_blocked == (expected == 2 ? 1 : f.before.nmi_blocked));
        assert(f.shutdown_clears == (sleep == 2 ? 1u : 0u));
    }
}

static void scenario(fixture_t *f, bm_286_arch_state_t *a, bm_286_config_t *c,
    bm_286_pm_request_t *q, bm_286_pm_delivery_state_t *s, unsigned n, unsigned cpl, unsigned odd)
{
    setup(f, a, c, q, s, cpl, odd);
    if (n == 1) { q->vector = 13; missing_gate(f, a, 13); }
    if (n == 2 || n == 3) {
        q->source = n == 2 ? BM_286_PM_SOFTWARE : BM_286_PM_BOUNDARY;
        q->vector = f->ack_vector = 0x30; q->intr_line = true;
        missing_gate(f, a, 0x30); missing_gate(f, a, 11);
    }
    if (n >= 4) {
        q->source = BM_286_PM_BOUNDARY; a->nmi_pending = 1;
        if (n == 5) a->shutdown = 1;
    }
    observe(f, a);
}

static void transport_failures(void)
{
    static const bm_status_t errors[] = {BM_STATUS_DEVICE_ERROR, BM_STATUS_IDLE,
        BM_STATUS_UNMAPPED, BM_STATUS_READ_ONLY, BM_STATUS_UNSUPPORTED};
    fixture_t f;
    bm_286_arch_state_t a;
    bm_286_config_t c;
    bm_286_pm_request_t q;
    bm_286_pm_delivery_state_t s;
    bm_286_pm_delivery_result_t r;
    transfer_t baseline[128];
    uint8_t expected_ram[65536];
    unsigned injections = 0;
    unsigned n, cpl, odd, calls, fail, after, error;
    for (n = 0; n < 6; ++n) for (cpl = 0; cpl < 4; ++cpl) for (odd = 0; odd < 2; ++odd) {
        scenario(&f, &a, &c, &q, &s, n, cpl, odd);
        assert(bm_286_pm_deliver(&a, &c, &s, &q, &r) == BM_STATUS_OK && r.entered);
        assert(r.waits == f.successful_waits); calls = f.calls;
        memcpy(baseline, f.trace, sizeof(baseline));
        for (fail = 1; fail <= calls; ++fail) for (after = 0; after < 2; ++after)
        for (error = 0; error < sizeof(errors) / sizeof(errors[0]); ++error) {
            unsigned observed_calls, j, b;
            scenario(&f, &a, &c, &q, &s, n, cpl, odd);
            memcpy(expected_ram, f.ram, sizeof(expected_ram));
            for (j = 0; j < fail - (after ? 0u : 1u); ++j) if (baseline[j].kind == 1)
                for (b = 0; b < baseline[j].size; ++b)
                    expected_ram[(baseline[j].address + b) & 65535u] = (uint8_t)(baseline[j].value >> (b * 8u));
            f.fail = fail; f.after = after != 0; f.failure = errors[error];
            assert(bm_286_pm_deliver(&a, &c, &s, &q, &r) == f.failure);
            assert(r.accepted && !r.entered && !r.shutdown && s.stopped);
            assert(r.waits == f.successful_waits && f.calls == fail);
            assert(f.effects == fail - (after ? 0u : 1u));
            assert(!memcmp(expected_ram, f.ram, sizeof(expected_ram)));
            same_frame(&a, &f.before); assert(a.nmi_pending == f.before.nmi_pending);
            assert(!f.locked && f.locks == f.unlocks && f.acks <= 2);
            assert(!f.shutdown_asserts && !f.shutdown_clears);
            observed_calls = f.calls;
            assert(bm_286_pm_deliver(&a, &c, &s, &q, &r) == BM_STATUS_INVALID_STATE);
            assert(f.calls == observed_calls && !r.accepted && !r.attempts);
            ++injections;
        }
    }
    printf("Protected delivery: %u before/after transfer failures checked, including exact retained memory effects.\n", injections);
}

static void selector_errors_and_priorities(void)
{
    fixture_t f;
    bm_286_arch_state_t a;
    bm_286_config_t c;
    bm_286_pm_request_t q;
    bm_286_pm_delivery_state_t s;
    bm_286_pm_delivery_result_t r;
    unsigned kind, external, selector;
    for (kind = 0; kind < 4; ++kind) for (external = 0; external < 2; ++external)
    for (selector = 0; selector < 32; ++selector) {
        unsigned error = (selector & 0xfffcu) | external;
        setup(&f, &a, &c, &q, &s, 0, selector & 1u);
        q.source = external ? BM_286_PM_BOUNDARY : BM_286_PM_SOFTWARE;
        q.intr_line = true; q.vector = f.ack_vector;
        put_word(&f, a.idtr.base + q.vector * 8 + 2, (uint16_t)selector);
        if (!(selector & 0xfffcu)) error = external;
        if (kind == 0) a.gdtr.limit = 0; /* Restore usable handler via LDT below. */
        else if (kind == 1) f.ram[a.gdtr.base + (selector & 0xfff8u) + 5] = 0x12;
        else if (kind == 2) f.ram[a.gdtr.base + (selector & 0xfff8u) + 5] = 0x9a;
        else f.ram[a.gdtr.base + (selector & 0xfff8u) + 5] = 0x1a;
        /* Keep fault handlers independent of the rejected descriptor/table. */
        a.ldtr.valid = 1; a.ldtr.base = 0x3000; a.ldtr.limit = 7;
        put_word(&f, 0x3000, 0xffff); f.ram[0x3005] = 0x9b;
        put_word(&f, a.idtr.base + 11 * 8 + 2, 4);
        put_word(&f, a.idtr.base + 13 * 8 + 2, 4);
        observe(&f, &a);
        /* Local index zero is valid, other local selectors are out of bounds.
         * Kind 2 is code but has a zero limit/IP failure unless index 8 or 24. */
        assert(bm_286_pm_deliver(&a, &c, &s, &q, &r) == BM_STATUS_OK && r.entered);
        if (r.attempts > 1) {
            if (!(selector & 4u) && kind == 2 && (selector & 0xfff8u) == 16) error = 0;
            assert(r.vector == 11 || r.vector == 13);
            assert(word(&f, a.ss.base + a.sp) == error);
        }
        assert(f.acks == (external ? 2u : 0u));
    }
    /* Instruction faults outrank pending events and ignore SS/STI shadows. */
    setup(&f, &a, &c, &q, &s, 3, 0);
    a.trap_pending = a.nmi_pending = 1; a.interrupt_shadow = BM_286_SHADOW_SS_LOAD;
    q.intr_line = true; observe(&f, &a);
    assert(bm_286_pm_deliver(&a, &c, &s, &q, &r) == BM_STATUS_OK && r.vector == 6);
    assert(a.nmi_pending && !a.nmi_blocked && !f.acks);
    /* Software DPL rejection precedes presence; exception gates bypass DPL. */
    setup(&f, &a, &c, &q, &s, 3, 1); q.source = BM_286_PM_SOFTWARE;
    f.ram[a.idtr.base + q.vector * 8 + 5] = 0x06;
    f.ram[a.idtr.base + 13 * 8 + 5] = 0x86;
    assert(bm_286_pm_deliver(&a, &c, &s, &q, &r) == BM_STATUS_OK && r.vector == 13);
    assert(word(&f, a.ss.base + a.sp) == q.vector * 8u + 2u);
}

static void shutdown_and_recovery(void)
{
    fixture_t f;
    bm_286_arch_state_t a;
    bm_286_config_t c;
    bm_286_pm_request_t q;
    bm_286_pm_delivery_state_t s;
    bm_286_pm_delivery_result_t r;
    unsigned reason, recover;
    for (reason = 0; reason < 4; ++reason) for (recover = 0; recover < 2; ++recover) {
        setup(&f, &a, &c, &q, &s, 0, reason & 1u);
        q.vector = 13; missing_gate(&f, &a, 13);
        if (reason == 0) missing_gate(&f, &a, 8);
        if (reason == 1) a.idtr.limit = 0;
        if (reason == 2) a.sp = 7; /* Valid SS cache; guest frame overflow. */
        if (reason == 3) f.ram[a.idtr.base + 8 * 8 + 5] = 0x82;
        observe(&f, &a);
        assert(bm_286_pm_deliver(&a, &c, &s, &q, &r) == BM_STATUS_OK && r.shutdown);
        assert(r.attempts == 2 && !r.entered && !r.vector && !f.writes && !s.stopped);
        assert(a.shutdown && f.shutdown_asserts == 1 && !a.halted);
        q.source = BM_286_PM_BOUNDARY; q.intr_line = true;
        observe(&f, &a);
        assert(bm_286_pm_deliver(&a, &c, &s, &q, &r) == BM_STATUS_IDLE);
        assert(!f.acks && f.shutdown_asserts == 1);
        a.nmi_pending = 1;
        if (recover) { a.sp = 0x8000; a.idtr.limit = 0x7ff; }
        else missing_gate(&f, &a, 2);
        observe(&f, &a);
        assert(bm_286_pm_deliver(&a, &c, &s, &q, &r) == BM_STATUS_OK);
        assert(r.attempts == 1 && a.nmi_blocked && !a.nmi_pending && !s.stopped);
        assert(r.entered == (recover != 0) && r.shutdown == (recover == 0));
        assert(a.shutdown == (recover == 0) && (a.msw & 1u));
        assert(f.shutdown_asserts == 1 && f.shutdown_clears == recover);
        if (!recover) {
            unsigned calls = f.calls;
            a.nmi_pending = 1; a.sp = 0x8000; a.idtr.limit = 0x7ff;
            f.ram[a.idtr.base + 2 * 8 + 5] = 0x86; observe(&f, &a);
            assert(bm_286_pm_deliver(&a, &c, &s, &q, &r) == BM_STATUS_IDLE);
            assert(f.calls == calls && a.shutdown && a.nmi_pending);
        }
    }
}

static void interrupt_lock_and_edges(void)
{
    fixture_t f;
    bm_286_arch_state_t a;
    bm_286_config_t c;
    bm_286_pm_request_t q;
    bm_286_pm_delivery_state_t s;
    bm_286_pm_delivery_result_t r;
    bm_286_segment_load_result_t returned;
    unsigned odd, i, calls;
    for (odd = 0; odd < 2; ++odd) {
        bool first_word_complete = false;
        scenario(&f, &a, &c, &q, &s, 3, 0, odd); /* INTA -> #NP -> #DF. */
        f.raise_nmi = 1;
        assert(bm_286_pm_deliver(&a, &c, &s, &q, &r) == BM_STATUS_OK && r.vector == 8);
        assert(f.acks == 2 && f.locks == 1 && f.unlocks == 1 && a.nmi_pending);
        for (i = 0; i < f.calls; ++i) {
            const transfer_t *t = &f.trace[i];
            assert(t->locked == !first_word_complete);
            if (t->kind == 1 && t->address + t->size == odd + 0x8000u)
                first_word_complete = true;
        }
        assert(first_word_complete);
        scenario(&f, &a, &c, &q, &s, 4, 0, odd);
        f.raise_nmi = 1;
        assert(bm_286_pm_deliver(&a, &c, &s, &q, &r) == BM_STATUS_OK && r.vector == 2);
        assert(a.nmi_pending && a.nmi_blocked && !f.acks);
        calls = f.calls; observe(&f, &a);
        assert(bm_286_pm_deliver(&a, &c, &s, &q, &r) == BM_STATUS_IDLE && f.calls == calls);
        assert(bm_286_pm_iret(&a, &c, &returned) == BM_STATUS_OK && returned.loaded);
        assert(!a.nmi_blocked && a.nmi_pending);
        observe(&f, &a);
        assert(bm_286_pm_deliver(&a, &c, &s, &q, &r) == BM_STATUS_OK && r.vector == 2);
        assert(!a.nmi_pending && a.nmi_blocked && !f.acks);
    }
}

static void pending_failure_origins(void)
{
    fixture_t f;
    bm_286_arch_state_t a;
    bm_286_config_t c;
    bm_286_pm_request_t q;
    bm_286_pm_delivery_state_t s;
    bm_286_pm_delivery_result_t r;
    unsigned vector, chain, cpl, odd;
    for (vector = 1; vector <= 2; ++vector) for (chain = 0; chain < 3; ++chain)
    for (cpl = 0; cpl < 4; ++cpl) for (odd = 0; odd < 2; ++odd) {
        setup(&f, &a, &c, &q, &s, cpl, odd);
        q.source = BM_286_PM_BOUNDARY; q.intr_line = true;
        a.trap_pending = vector == 1; a.nmi_pending = 1;
        missing_gate(&f, &a, vector);
        if (chain) missing_gate(&f, &a, 11);
        if (chain == 2) missing_gate(&f, &a, 8);
        observe(&f, &a);
        assert(bm_286_pm_deliver(&a, &c, &s, &q, &r) == BM_STATUS_OK);
        assert(r.errors[1] == vector * 8u + 3u && r.vectors[1] == 11);
        assert(r.attempts == (chain ? 3 : 2) && !f.acks && !s.stopped);
        assert(r.entered == (chain != 2) && r.shutdown == (chain == 2));
        if (r.entered) assert(r.vector == (chain ? 8 : 11));
        assert(a.nmi_pending == (vector == 1) && a.nmi_blocked == (vector == 2));
    }
    scenario(&f, &a, &c, &q, &s, 3, 0, 1); missing_gate(&f, &a, 8);
    assert(bm_286_pm_deliver(&a, &c, &s, &q, &r) == BM_STATUS_OK && r.shutdown);
    assert(f.acks == 2 && f.locks == 1 && f.unlocks == 1 && f.shutdown_asserts == 1);
    scenario(&f, &a, &c, &q, &s, 1, 0, 0);
    f.ram[a.idtr.base + 8 * 8 + 5] = 0x85;
    assert(bm_286_pm_deliver(&a, &c, &s, &q, &r) == BM_STATUS_UNSUPPORTED);
    assert(s.stopped && !a.shutdown && !r.shutdown && !f.shutdown_asserts && !f.writes);
}

static void repair_and_retry(void)
{
    fixture_t f;
    bm_286_arch_state_t a;
    bm_286_config_t c;
    bm_286_pm_request_t q;
    bm_286_pm_delivery_state_t s;
    bm_286_pm_delivery_result_t r;
    bm_286_segment_load_result_t returned;
    unsigned cpl, odd;
    for (cpl = 0; cpl < 4; ++cpl) for (odd = 0; odd < 2; ++odd) {
        setup(&f, &a, &c, &q, &s, cpl, odd);
        q.source = BM_286_PM_SOFTWARE; q.vector = 0x40;
        missing_gate(&f, &a, q.vector);
        assert(bm_286_pm_deliver(&a, &c, &s, &q, &r) == BM_STATUS_OK && r.vector == 11);
        assert(word(&f, a.ss.base + a.sp) == 0x202);
        /* Synthetic handler repairs the missing gate and discards its error.
         * This is a private helper sequence, not executed protected opcodes. */
        f.ram[a.idtr.base + q.vector * 8 + 5] |= 0x80;
        a.sp += 2; observe(&f, &a);
        assert(bm_286_pm_iret(&a, &c, &returned) == BM_STATUS_OK && returned.loaded);
        assert(a.ip == q.restart_ip && a.sp == 0x8000);
        observe(&f, &a);
        assert(bm_286_pm_deliver(&a, &c, &s, &q, &r) == BM_STATUS_OK && r.vector == 0x40);
        assert(!r.has_error[0]); observe(&f, &a);
        assert(bm_286_pm_iret(&a, &c, &returned) == BM_STATUS_OK && returned.loaded);
        assert(a.ip == q.next_ip && a.sp == 0x8000 && a.cs.selector == 24 + cpl);
        /* A later instruction fault in a handler is a new delivery, not #DF. */
        q.source = BM_286_PM_EXCEPTION; q.vector = 13; q.restart_ip = a.ip;
        observe(&f, &a);
        assert(bm_286_pm_deliver(&a, &c, &s, &q, &r) == BM_STATUS_OK && r.vector == 13);
        assert(r.attempts == 1 && !s.stopped);
    }
}

static void invalid_inputs_and_gaps(void)
{
    fixture_t f;
    bm_286_arch_state_t a;
    bm_286_config_t c;
    bm_286_pm_request_t q;
    bm_286_pm_delivery_state_t s;
    bm_286_pm_delivery_result_t r;
    unsigned n;
    for (n = 0; n < 10; ++n) {
        bm_status_t expected = BM_STATUS_UNSUPPORTED;
        setup(&f, &a, &c, &q, &s, 3, 0);
        if (n == 0) c.bus_lock = NULL;
        if (n == 1) { q.source = BM_286_PM_BOUNDARY; q.intr_line = true; c.interrupt_ack = NULL; }
        if (n == 2) f.ram[a.idtr.base + q.vector * 8 + 5] = 0x85;
        if (n == 3) f.ram[a.gdtr.base + 13] = 0x9b; /* Inner privilege path. */
        if (n == 4) { a.ss.valid = 0; expected = BM_STATUS_INVALID_STATE; }
        if (n == 5) { a.msw = 0; expected = BM_STATUS_INVALID_STATE; }
        if (n == 6) { a.interrupt_shadow = 3; expected = BM_STATUS_INVALID_STATE; }
        if (n == 7) { q.source = (bm_286_pm_source_t)3; expected = BM_STATUS_INVALID_ARGUMENT; }
        if (n == 8) { c.access = NULL; expected = BM_STATUS_INVALID_ARGUMENT; }
        if (n == 9) { a.idtr.base = 0x1000000; expected = BM_STATUS_INVALID_STATE; }
        observe(&f, &a);
        assert(bm_286_pm_deliver(&a, &c, &s, &q, &r) == expected);
        assert(!r.entered && !r.shutdown && !f.writes && !f.acks && !f.locked);
        assert(s.stopped == (n >= 2 && n <= 4)); same_frame(&a, &f.before);
    }
    setup(&f, &a, &c, &q, &s, 0, 0);
    assert(bm_286_pm_deliver(NULL, &c, &s, &q, &r) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_286_pm_deliver(&a, NULL, &s, &q, &r) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_286_pm_deliver(&a, &c, NULL, &q, &r) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_286_pm_deliver(&a, &c, &s, NULL, &r) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_286_pm_deliver(&a, &c, &s, &q, NULL) == BM_STATUS_INVALID_ARGUMENT);
    assert(!f.calls && !s.stopped);
}

static void independent_instances(void)
{
    fixture_t f, other;
    bm_286_arch_state_t a, b;
    bm_286_config_t c, d;
    bm_286_pm_request_t q, p;
    bm_286_pm_delivery_state_t s, t;
    bm_286_pm_delivery_result_t r;
    setup(&f, &a, &c, &q, &s, 0, 0);
    setup(&other, &b, &d, &p, &t, 3, 1);
    f.fail = 1;
    assert(bm_286_pm_deliver(&a, &c, &s, &q, &r) == BM_STATUS_DEVICE_ERROR && s.stopped);
    assert(!other.calls && !t.stopped); same_frame(&b, &other.before);
    assert(bm_286_pm_deliver(&b, &d, &t, &p, &r) == BM_STATUS_OK && r.entered);
    assert(b.cpl == 3 && !t.stopped && !other.locked && s.stopped);
    assert(bm_286_pm_deliver(&a, &c, &s, &q, &r) == BM_STATUS_INVALID_STATE && f.calls == 1);
}

int main(void)
{
    classifications(); escalation(); arbitration(); transport_failures();
    selector_errors_and_priorities(); shutdown_and_recovery();
    interrupt_lock_and_edges(); repair_and_retry(); invalid_inputs_and_gaps();
    pending_failure_origins(); independent_instances();
    return 0;
}
