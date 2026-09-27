/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Authored Intel 231164-005 register/waveform fixtures, not hardware captures. */
#include "pit8254_private.h"
#include <blumach/components/at_clock.h>
#include "board_io.h"
#include "failure_injection_host.h"
#include <86box/pit_exact.h>
#include <blumach/platforms/null_host.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct event { uint64_t clock; unsigned channel; int level; } event_t;
typedef struct fixture {
    bm_pit8254_t *pit;
    event_t events[4096];
    unsigned count;
    int reenter;
    bm_at_pic_t *pic;
    bm_status_t callback_status;
} fixture_t;
static unsigned status_cases, count_cases, wave_cases, batch_cases, classic_cases;
static bm_bus_transaction_t transaction(unsigned port, bm_bus_operation_t op, unsigned value)
{
    bm_bus_transaction_t t = {0};
    t.space = BM_ADDRESS_IO; t.operation = op; t.address = port;
    t.size = t.alignment = 1; t.value = value; t.wait_states = 17;
    return t;
}
static void output(void *context, unsigned channel, int level)
{
    fixture_t *f = context;
    int observed;
    assert(f->pit && f->count < 4096);
    assert(bm_pit8254_output(f->pit, channel, &observed) == BM_STATUS_OK && observed == level);
    f->events[f->count++] = (event_t){f->pit->exact.channel[channel].clocks, channel, level};
    if (f->pic && channel == 0) f->callback_status = bm_at_pic_set_irq(f->pic, 0, level);
    if (f->reenter) {
        bm_bus_transaction_t t = transaction(0x40, BM_BUS_READ, 0);
        uint64_t deadline;
        assert(bm_pit8254_io(f->pit, &t) == BM_STATUS_INVALID_STATE);
        t.attributes = BM_BUS_TRANSACTION_DEBUG;
        assert(bm_pit8254_io(f->pit, &t) == BM_STATUS_OK);
        assert(bm_pit8254_set_gate(f->pit, 0, 0) == BM_STATUS_INVALID_STATE);
        assert(bm_pit8254_advance(f->pit, 1) == BM_STATUS_INVALID_STATE);
        assert(bm_pit8254_next_deadline(f->pit, &deadline) >= BM_STATUS_OK);
        bm_pit8254_reset(f->pit); bm_pit8254_destroy(f->pit); /* Forbidden: ignored. */
    }
}
static void start(fixture_t *f)
{
    bm_host_services_t host = bm_null_host_services();
    bm_pit8254_config_t c = {0x40, output, f};
    memset(f, 0, sizeof(*f));
    assert(bm_pit8254_create(&host, &c, &f->pit) == BM_STATUS_OK && f->pit);
    assert(!f->count);
}
static void wr(fixture_t *f, unsigned port, unsigned value)
{
    bm_bus_transaction_t t = transaction(port, BM_BUS_WRITE, value);
    assert(bm_pit8254_io(f->pit, &t) == BM_STATUS_OK && t.wait_states == 17);
}
static uint8_t rd(fixture_t *f, unsigned port, int debug)
{
    bm_bus_transaction_t t = transaction(port, BM_BUS_READ, 0xdead);
    t.attributes = debug ? BM_BUS_TRANSACTION_DEBUG : 0;
    assert(bm_pit8254_io(f->pit, &t) == BM_STATUS_OK && t.wait_states == 17);
    return (uint8_t)t.value;
}
static uint8_t status(fixture_t *f, unsigned ch)
{ wr(f, 0x43, 0xe0U | (2U << ch)); return rd(f, 0x40 + ch, 0); }
static unsigned latched(fixture_t *f, unsigned ch, unsigned rw)
{
    unsigned v;
    wr(f, 0x43, ch << 6); v = rd(f, 0x40 + ch, 0);
    if (rw == 3) v |= (unsigned)rd(f, 0x40 + ch, 0) << 8;
    if (rw == 2) v <<= 8;
    return v;
}
static void program(fixture_t *f, unsigned ch, unsigned mode, int bcd, unsigned value)
{
    wr(f, 0x43, (ch << 6) | 0x30U | (mode << 1) | (unsigned)bcd);
    wr(f, 0x40 + ch, value & 255); wr(f, 0x40 + ch, value >> 8);
}
static void step(fixture_t *f, uint64_t clocks)
{ assert(bm_pit8254_advance(f->pit, clocks) == BM_STATUS_OK); }
static void register_matrix(void)
{
    fixture_t f;
    unsigned ch, rw, mode, bcd, command, i;
    start(&f);
    for (ch = 0; ch < 3; ++ch) for (rw = 1; rw <= 3; ++rw)
    for (mode = 0; mode < 8; ++mode) for (bcd = 0; bcd < 2; ++bcd) {
        unsigned control = (ch << 6) | (rw << 4) | (mode << 1) | bcd;
        bm_pit8254_reset(f.pit); f.count = 0;
        wr(&f, 0x43, control);
        assert(status(&f, ch) == ((mode ? 0x80U : 0U) | 0x40U | (control & 63U)));
        for (command = 0xc0; command < 256; command += 2) {
            bm_pit8254_reset(f.pit); f.count = 0;
            for (i = 0; i < 3; ++i) {
                unsigned ctl = (i << 6) | (control & 63U);
                wr(&f, 0x43, ctl);
                wr(&f, 0x40 + i, 0x24);
                if (rw == 3) wr(&f, 0x40 + i, 0x12);
                assert(bm_pit8254_set_gate(f.pit, i, 1) == BM_STATUS_OK);
            }
            step(&f, 1);
            wr(&f, 0x43, command);
            for (i = 0; i < 3; ++i) {
                bm_pit_exact_channel_t *c = &f.pit->exact.channel[i];
                int selected = (command & (2U << i)) != 0;
                assert(c->status_latched == (selected && !(command & 0x10)));
                assert(c->count_latched == (selected && !(command & 0x20)));
                if (c->status_latched) {
                    unsigned expected = (c->output ? 0x80U : 0U) | (c->null_count ? 0x40U : 0U) | (control & 63U);
                    assert(rd(&f, 0x40 + i, 1) == expected);
                    assert(rd(&f, 0x40 + i, 1) == expected);
                    assert(rd(&f, 0x40 + i, 0) == expected);
                    assert(!c->status_latched && c->read_phase == 0);
                }
            }
            ++status_cases;
        }
    }
    bm_pit8254_destroy(f.pit);
}
static void counts_and_interleaving(void)
{
    fixture_t f; unsigned rw, value;
    start(&f);
    for (rw = 1; rw <= 3; ++rw) for (value = 0; value < 65536; ++value) {
        unsigned expected = rw == 1 ? value & 255 : (rw == 2 ? value & 0xff00 : value);
        wr(&f, 0x43, rw << 4);
        if (rw != 2) wr(&f, 0x40, value & 255);
        if (rw != 1) wr(&f, 0x40, value >> 8);
        assert(status(&f, 0) & 0x40);
        step(&f, 1); assert(!(status(&f, 0) & 0x40));
        assert(latched(&f, 0, rw) == expected); ++count_cases;
    }
    bm_pit8254_reset(f.pit); f.count = 0;
    program(&f, 0, 0, 0, 0x1234); step(&f, 1);
    wr(&f, 0x43, 0xc2); /* Both status and count. */
    step(&f, 5); wr(&f, 0x43, 0xc2); /* Neither retained latch is overwritten. */
    assert(rd(&f, 0x40, 0) == 0x30);
    assert(rd(&f, 0x40, 0) == 0x34);
    wr(&f, 0x40, 0x78); /* First write must not return inverted LSB or reset read phase. */
    assert(!(status(&f, 0) & 0x40));
    assert(rd(&f, 0x40, 0) == 0x12);
    wr(&f, 0x40, 0x56); assert(status(&f, 0) & 0x40);
    assert(latched(&f, 0, 3) == 0x122f); /* Reads do not force CR -> CE. */
    assert(status(&f, 0) & 0x40); step(&f, 1);
    assert(latched(&f, 0, 3) == 0x5678 && !(status(&f, 0) & 0x40));
    /* Status takes priority even inserted BETWEEN the two latched count bytes. */
    wr(&f, 0x43, 0); assert(rd(&f, 0x40, 0) == 0x78);
    assert(status(&f, 0) == 0x30); assert(rd(&f, 0x40, 0) == 0x56);
    wr(&f, 0x43, 0xc2); wr(&f, 0x43, 0x14);
    assert(!f.pit->exact.channel[0].status_latched && !f.pit->exact.channel[0].count_latched);
    program(&f, 0, 0, 0, 2); wr(&f, 0x43, 0xe2); /* NULL=1, OUT=0 latched. */
    step(&f, 3); wr(&f, 0x43, 0xe2); /* Live NULL=0, OUT=1 must not replace it. */
    assert(rd(&f, 0x40, 0) == 0x70 && status(&f, 0) == 0xb0);
    bm_pit8254_destroy(f.pit);
}
static void waveforms(void)
{
    static const unsigned counts[] = {2,3,4,5,9,10,31,32,99};
    fixture_t f;
    unsigned mode, n, t, i;
    start(&f);
    for (mode = 0; mode < 6; ++mode) for (i = 0; i < sizeof(counts)/sizeof(counts[0]); ++i) {
        n = counts[i]; bm_pit8254_reset(f.pit); f.count = 0;
        program(&f, 0, mode, 0, n);
        if (mode == 1 || mode == 5) {
            step(&f, 3); assert(status(&f, 0) & 0x40);
            assert(bm_pit8254_set_gate(f.pit, 0, 0) == BM_STATUS_OK);
            assert(bm_pit8254_set_gate(f.pit, 0, 1) == BM_STATUS_OK);
        }
        step(&f, 1);
        for (t = 0; t < 3*n; ++t) {
            int expected, actual;
            uint64_t deadline, wanted;
            if (mode == 0 || mode == 1) expected = t >= n;
            else if (mode == 2) expected = t % n != n-1;
            else if (mode == 3) expected = t % n < (n+1)/2;
            else expected = t != n;
            assert(bm_pit8254_output(f.pit, 0, &actual) == BM_STATUS_OK);
            if (actual != expected) fprintf(stderr, "wave mode%u count%u pulse%u OUT%d expected%d CE%x state%d\n",
                mode, n, t, actual, expected, f.pit->exact.channel[0].counting_element, (int)f.pit->exact.channel[0].state);
            assert(actual == expected);
            if (mode == 0 || mode == 1) wanted = t < n ? n-t : 0;
            else if (mode == 2) wanted = t % n < n-1 ? n-1-t%n : 1;
            else if (mode == 3) wanted = t%n < (n+1)/2 ? (n+1)/2-t%n : n-t%n;
            else wanted = t < n ? n-t : (t == n ? 1 : 0);
            assert(bm_pit8254_next_deadline(f.pit, &deadline) == (wanted ? BM_STATUS_OK : BM_STATUS_IDLE));
            assert(deadline == wanted);
            if (mode == 3) {
                unsigned phase = t % n, ce;
                if (!(n & 1)) ce = n - 2*(phase % (n/2));
                else if (phase < (n+1)/2) ce = n-1-2*phase;
                else ce = n-1-2*(phase-(n+1)/2);
                assert(latched(&f, 0, 3) == ce);
            } else if (mode != 2) {
                assert(latched(&f, 0, 3) == ((n-t) & 0xffffU));
            }
            step(&f, 1); ++wave_cases;
        }
    }
    /* A completed odd->even rewrite must not alter the running odd half-cycle. */
    bm_pit8254_reset(f.pit); f.count = 0; program(&f, 0, 3, 0, 5); step(&f, 1);
    wr(&f, 0x40, 4); step(&f, 1); assert(latched(&f, 0, 3) == 2);
    wr(&f, 0x40, 0); assert(status(&f, 0) & 0x40);
    step(&f, 1); assert(latched(&f, 0, 3) == 0 && (status(&f, 0) & 0x80));
    step(&f, 1); assert(latched(&f, 0, 3) == 4 && status(&f, 0) == 0x36);
    /* A reload during an incomplete write retains old CR until the MSB. */
    wr(&f, 0x40, 9); step(&f, 2); assert(latched(&f, 0, 3) == 4);
    wr(&f, 0x40, 0); step(&f, 2); assert(latched(&f, 0, 3) == 8);
    assert(!(status(&f, 0) & 0x40));
    bm_pit8254_destroy(f.pit);
}
static void same(fixture_t *a, fixture_t *b)
{
    unsigned i;
    for (i = 0; i < 3; ++i) {
        const bm_pit_exact_channel_t *x = &a->pit->exact.channel[i], *y = &b->pit->exact.channel[i];
        if (x->counting_element != y->counting_element || x->output != y->output)
            fprintf(stderr, "batch ch%u mode%u bcd%d divisor%x at%llu CE%x/%x OUT%d/%d states%d/%d\n",
                i, x->mode, (int)x->bcd, x->count_register, (unsigned long long)x->clocks,
                x->counting_element, y->counting_element, (int)x->output, (int)y->output, (int)x->state, (int)y->state);
        assert(x->counting_element == y->counting_element && x->output == y->output);
        assert(x->null_count == y->null_count && x->state == y->state && x->clocks == y->clocks);
        assert(x->toggle_on_reload == y->toggle_on_reload && x->active_count == y->active_count);
    }
    assert(a->count == b->count);
    for (i = 0; i < a->count; ++i)
        assert(a->events[i].clock == b->events[i].clock && a->events[i].channel == b->events[i].channel &&
            a->events[i].level == b->events[i].level);
}
static void batching(void)
{
    static const unsigned values[] = {0,2,3,4,5,0x11,0x99,0x1234,0x9999};
    fixture_t a, b;
    unsigned mode, bcd, v, i, stage;
    start(&a); start(&b);
    for (mode = 0; mode < 6; ++mode) for (bcd = 0; bcd < 2; ++bcd)
    for (v = 0; v < sizeof(values)/sizeof(values[0]); ++v) {
        bm_pit8254_reset(a.pit); bm_pit8254_reset(b.pit); a.count = b.count = 0;
        for (i = 0; i < 3; ++i) {
            program(&a, i, mode, (int)bcd, values[v]); program(&b, i, mode, (int)bcd, values[v]);
            assert(bm_pit8254_set_gate(a.pit, i, 0) == BM_STATUS_OK);
            assert(bm_pit8254_set_gate(b.pit, i, 0) == BM_STATUS_OK);
            assert(bm_pit8254_set_gate(a.pit, i, 1) == BM_STATUS_OK);
            assert(bm_pit8254_set_gate(b.pit, i, 1) == BM_STATUS_OK);
        }
        for (stage = 0; stage < 4; ++stage) {
            uint64_t deadline;
            unsigned chunk = 137U + stage*17U;
            assert(bm_pit8254_next_deadline(a.pit, &deadline) >= BM_STATUS_OK);
            for (i = 0; i < chunk; ++i) step(&a, 1);
            step(&b, chunk); same(&a, &b); ++batch_cases;
            a.count = b.count = 0;
        }
    }
    bm_pit8254_reset(a.pit); a.count = 0;
    step(&a, UINT64_MAX); assert(!a.count);
    assert(bm_pit8254_advance(a.pit, 1) == BM_STATUS_CAPACITY_EXCEEDED);
    assert(bm_pit8254_advance(a.pit, 0) == BM_STATUS_OK);
    bm_pit8254_reset(a.pit); a.count = 0;
    program(&a, 0, 0, 0, 2); step(&a, 3); a.count = 0;
    step(&a, UINT64_MAX-3); assert(!a.count && latched(&a, 0, 3) == 4);
    bm_pit8254_destroy(a.pit); bm_pit8254_destroy(b.pit);
}
static void errors_and_debug(void)
{
    fixture_t f; bm_pit_exact_device_t snapshot;
    bm_bus_transaction_t t;
    unsigned i;
    start(&f); f.reenter = 1; program(&f, 0, 2, 0, 4); step(&f, 8);
    wr(&f, 0x43, 0xc2); memcpy(&snapshot, &f.pit->exact, sizeof(snapshot));
    for (i = 0; i < 10; ++i) { rd(&f, 0x40, 1); assert(!memcmp(&snapshot, &f.pit->exact, sizeof(snapshot))); }
    for (i = 0; i < 8; ++i) {
        bm_status_t wanted = BM_STATUS_INVALID_ARGUMENT;
        t = transaction(0x40, BM_BUS_WRITE, 1);
        if (i == 0) { t.size = 2; wanted = BM_STATUS_UNSUPPORTED; }
        if (i == 1) { t.operation = BM_BUS_FETCH; wanted = BM_STATUS_UNSUPPORTED; }
        if (i == 2) { t.attributes = BM_BUS_TRANSACTION_DEBUG; wanted = BM_STATUS_UNSUPPORTED; }
        if (i == 3) { t.address = 0x44; wanted = BM_STATUS_UNMAPPED; }
        if (i == 4) t.alignment = 2;
        if (i == 5) t.endianness = (bm_endianness_t)99;
        if (i == 6) { t.address = 0x43; t.value = 0xc3; wanted = BM_STATUS_UNSUPPORTED; }
        if (i == 7) { t.address = 0x43; t.operation = BM_BUS_READ; wanted = BM_STATUS_UNMAPPED; }
        assert(bm_pit8254_io(f.pit, &t) == wanted);
        assert(!memcmp(&snapshot, &f.pit->exact, sizeof(snapshot)) && t.wait_states == 17);
    }
    wr(&f, 0x43, 0x34); wr(&f, 0x40, 1);
    memcpy(&snapshot, &f.pit->exact, sizeof(snapshot)); t = transaction(0x40, BM_BUS_WRITE, 0);
    assert(bm_pit8254_io(f.pit, &t) == BM_STATUS_UNSUPPORTED);
    assert(!memcmp(&snapshot, &f.pit->exact, sizeof(snapshot)));
    wr(&f, 0x40, 1); /* Complete with 0101 instead, not coerced to another divisor. */
    wr(&f, 0x43, 0x31); wr(&f, 0x40, 0xff);
    t = transaction(0x40, BM_BUS_WRITE, 0x12);
    assert(bm_pit8254_io(f.pit, &t) == BM_STATUS_UNSUPPORTED);
    bm_pit8254_destroy(f.pit);
}
static void classic_comparison(void)
{
    fixture_t f; pitx_device_t classic;
    static const unsigned values[] = {0,2,3,4,5,0x11,0x1234,0x9999};
    unsigned mode, bcd, v, i, tick;
    start(&f);
    for (mode = 0; mode < 6; ++mode) for (bcd = 0; bcd < 2; ++bcd)
    for (v = 0; v < sizeof(values)/sizeof(values[0]); ++v) {
        bm_pit8254_reset(f.pit); f.count = 0; pitx_init(&classic, PITX_8254);
        for (i = 0; i < 3; ++i) {
            unsigned control = (i << 6) | 0x30 | (mode << 1) | bcd;
            program(&f, i, mode, (int)bcd, values[v]);
            pitx_control_write(&classic, (uint8_t)control);
            pitx_data_write(&classic, i, (uint8_t)values[v]);
            pitx_data_write(&classic, i, (uint8_t)(values[v] >> 8));
            assert(bm_pit8254_set_gate(f.pit, i, 0) == BM_STATUS_OK);
            assert(bm_pit8254_set_gate(f.pit, i, 1) == BM_STATUS_OK);
            pitx_set_gate(&classic, i, false); pitx_set_gate(&classic, i, true);
        }
        for (tick = 0; tick < 256; ++tick) {
            step(&f, 1); pitx_tick(&classic);
            for (i = 0; i < 3; ++i) {
                int level;
                assert(bm_pit8254_output(f.pit, i, &level) == BM_STATUS_OK);
                assert(level == (int)pitx_get_output(&classic, i));
                {
                    unsigned expected_count = pitx_get_count(&classic, i);
                    /* Classic loses the CE decrement during strobe recovery.
                     * Assert the intentional one-count correction, not equality
                     * to that artifact. No second strobe occurs in this window. */
                    unsigned divisor = values[v];
                    if (bcd) divisor = (divisor & 15U) + 10*((divisor >> 4)&15U) +
                        100*((divisor >> 8)&15U) + 1000*((divisor >> 12)&15U);
                    if (!divisor) divisor = bcd ? 10000U : 65536U;
                    if ((mode == 4 || mode == 5) && tick >= divisor+1) {
                        if (bcd) {
                            unsigned dec = (expected_count & 15U) + 10*((expected_count >> 4)&15U) +
                                100*((expected_count >> 8)&15U) + 1000*((expected_count >> 12)&15U);
                            dec = (dec+9999U)%10000U;
                            expected_count = dec%10 + ((dec/10)%10)*16 + ((dec/100)%10)*256 + (dec/1000)*4096;
                        } else expected_count = (expected_count-1U)&0xffffU;
                    }
                    assert(latched(&f, i, 3) == expected_count);
                }
                wr(&f, 0x43, 0xe0 | (2U << i)); pitx_control_write(&classic, (uint8_t)(0xe0 | (2U << i)));
                assert(rd(&f, 0x40+i, 0) == pitx_data_read(&classic, i));
                ++classic_cases;
            }
        }
    }
    bm_pit8254_destroy(f.pit);
}
static void one_shots_and_gates(void)
{
    fixture_t f; unsigned mode;
    start(&f);
    for (mode = 0; mode < 6; ++mode) {
        int before, after;
        bm_pit8254_reset(f.pit); f.count = 0; program(&f, 0, mode, 0, 5);
        assert(bm_pit8254_set_gate(f.pit, 0, 0) == BM_STATUS_OK);
        assert(bm_pit8254_set_gate(f.pit, 0, 1) == BM_STATUS_OK);
        step(&f, 2);
        assert(bm_pit8254_output(f.pit, 0, &before) == BM_STATUS_OK);
        assert(bm_pit8254_set_gate(f.pit, 0, 0) == BM_STATUS_OK);
        assert(bm_pit8254_output(f.pit, 0, &after) == BM_STATUS_OK);
        assert(after == ((mode == 2 || mode == 3) ? 1 : before));
        if (mode == 0 || mode == 4) {
            unsigned old = latched(&f, 0, 3); step(&f, 15); assert(latched(&f, 0, 3) == old);
        } else if (mode == 1 || mode == 5) {
            step(&f, 6); f.count = 0;
            step(&f, 140000); assert(!f.count); /* No spurious second one-shot after wrap. */
            wr(&f, 0x40, 6); wr(&f, 0x40, 0); step(&f, 140000); assert(!f.count);
            assert(status(&f, 0) & 0x40); /* CR rewrite does not trigger. */
            assert(bm_pit8254_set_gate(f.pit, 0, 1) == BM_STATUS_OK);
            step(&f, 1); assert(!(status(&f, 0) & 0x40));
        }
    }
    bm_pit8254_reset(f.pit); f.count = 0; program(&f, 0, 4, 0, 2);
    step(&f, 4); f.count = 0; step(&f, 140000); assert(!f.count);
    wr(&f, 0x40, 3); wr(&f, 0x40, 0); step(&f, 5); assert(f.count == 2);
    bm_pit8254_destroy(f.pit);
}
static void construction(void)
{
    failure_injection_host_t allocation;
    bm_host_services_t host;
    bm_pit8254_config_t c = {0}; bm_pit8254_t *pit = (bm_pit8254_t *)(uintptr_t)1;
    failure_injection_host_initialize(&allocation); host = failure_injection_host_services(&allocation);
    c.io_base = 0x40;
    failure_injection_host_fail_after(&allocation, 0);
    assert(bm_pit8254_create(&host, &c, &pit) == BM_STATUS_OUT_OF_MEMORY && !pit);
    assert(!allocation.outstanding_allocations);
    failure_injection_host_fail_on(&allocation, SIZE_MAX);
    assert(bm_pit8254_create(&host, &c, &pit) == BM_STATUS_OK);
    assert(allocation.outstanding_allocations == 1);
    bm_pit8254_destroy(pit); assert(!allocation.outstanding_allocations);
    pit = (bm_pit8254_t *)(uintptr_t)1; c.io_base = 0xfffd;
    assert(bm_pit8254_create(&host, &c, &pit) == BM_STATUS_INVALID_ARGUMENT && !pit);
    assert(bm_pit8254_create(NULL, &c, &pit) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_pit8254_io(NULL, NULL) == BM_STATUS_INVALID_ARGUMENT);
}
static bm_status_t no_memory(void *context, bm_at_transfer_t *t)
{ (void)context; (void)t; assert(!"unexpected DMA memory"); return BM_STATUS_DEVICE_ERROR; }
static void board_integration(int clocked)
{
    fixture_t f;
    bm_host_services_t host = bm_null_host_services();
    bm_at_pic_config_t pc = {0}; bm_at_dma_config_t dc = {0}; bm_at_bus_config_t bc = {0};
    bm_at_dma_t *dma; bm_at_bus_t *bus; bm_pcs286_io_t io; bm_pcs286_io_config_t c = {0};
    bm_gc103_memory_t headland; bm_ioc02_legacy_registers_t ioc;
    bm_engine_t *engine = NULL; bm_at_clock_link_t *link = NULL;
    bm_engine_config_t ec = {1,1,1}; bm_clock_rate_t rate = {1000000000,1};
    static const unsigned ports[] = {0x20,0xa0,0x21,0xa1,0x21,0xa1,0x21,0xa1,0x21,0x43,0x40};
    static const unsigned values[] = {0x11,0x11,0x20,0x28,4,2,1,1,0xfe,0x14,4};
    unsigned i; uint8_t vector = 0;
    start(&f); pc.master_base = 0x20; pc.slave_base = 0xa0; pc.cascade_line = 2;
    if (clocked) {
        assert(bm_engine_create_clocked(&host, &ec, &engine) == BM_STATUS_OK);
        assert(bm_pit8254_attach_clock(&host, engine, f.pit, &rate, &link) == BM_STATUS_OK);
    }
    assert(bm_at_pic_create(&host, &pc, &f.pic) == BM_STATUS_OK);
    dc.memory = no_memory; dc.clock = (bm_clock_rate_t){4,1};
    assert(bm_at_dma_create(&host, &dc, &dma) == BM_STATUS_OK);
    assert(bm_gc103_memory_initialize(&headland, 0x100000) == BM_STATUS_OK);
    assert(bm_ioc02_legacy_initialize(&ioc) == BM_STATUS_OK);
    c.profile = BM_PCS286_IO_LEGACY_GC103_AT; c.headland = &headland; c.ioc02 = &ioc;
    c.pic = f.pic; c.dma = dma; c.service_clock = (bm_clock_rate_t){8,1}; c.timing = BM_PCS286_IO_PROVISIONAL;
    c.resource_count = 1; c.resources[0] = (bm_pcs286_io_resource_t){0x40,0x43,1,bm_pit8254_io,f.pit,1};
    if (clocked) { c.resources[0].access = bm_at_clock_link_io; c.resources[0].context = link; }
    assert(bm_pcs286_io_initialize(&io, &c) == BM_STATUS_OK);
    bc.cpu_clock = (bm_clock_rate_t){12,1}; bc.isa_clock = (bm_clock_rate_t){8,1};
    bc.memory = no_memory; bc.io = bm_pcs286_io_access; bc.decode_context = &io;
    assert(bm_at_bus_create(&host, &bc, &bus) == BM_STATUS_OK);
    for (i = 0; i < sizeof(ports)/sizeof(ports[0]); ++i) {
        bm_bus_transaction_t t = transaction(ports[i], BM_BUS_WRITE, values[i]); t.wait_states = 0;
        assert(bm_at_bus_cpu_access(bus, &t) == BM_STATUS_OK);
    }
    assert(f.callback_status == BM_STATUS_OK);
    /* Programming OUT high is a real edge; acknowledge it before periodic IRQ. */
    assert(bm_at_pic_acknowledge(f.pic, 0, &vector) == BM_STATUS_OK);
    assert(bm_at_pic_acknowledge(f.pic, 1, &vector) == BM_STATUS_OK && vector == 0x20);
    { bm_bus_transaction_t t = transaction(0x20, BM_BUS_WRITE, 0x20); t.wait_states = 0;
      assert(bm_at_bus_cpu_access(bus, &t) == BM_STATUS_OK); }
    if (clocked) assert(bm_engine_run_for(engine, 4) == BM_STATUS_OK);
    else step(&f, 4); /* Load +3 decrements: OUT low. */
    { bm_at_pic_state_t s; assert(bm_at_pic_state(f.pic, &s) == BM_STATUS_OK && !(s.irr[0]&1)); }
    if (clocked) assert(bm_engine_run_for(engine, 1) == BM_STATUS_OK);
    else step(&f, 1); /* Reload: IRQ0 edge. */
    assert(bm_at_pic_acknowledge(f.pic, 0, &vector) == BM_STATUS_OK);
    assert(bm_at_pic_acknowledge(f.pic, 1, &vector) == BM_STATUS_OK && vector == 0x20);
    { bm_bus_transaction_t t = transaction(0x43, BM_BUS_READ, 0x1234); t.wait_states = 0;
      assert(bm_at_bus_cpu_access(bus, &t) == BM_STATUS_UNMAPPED && t.value == 0x1234); }
    bm_engine_destroy(engine); bm_at_bus_destroy(bus); bm_at_clock_link_destroy(link);
    bm_at_dma_destroy(dma); bm_at_pic_destroy(f.pic); f.pic = NULL;
    bm_pit8254_destroy(f.pit);
}
int main(void)
{
    register_matrix(); counts_and_interleaving(); waveforms(); batching(); errors_and_debug();
    classic_comparison(); one_shots_and_gates(); construction(); board_integration(0); board_integration(1);
    printf("8254: %u read-back cases, %u counts, %u waveform pulses, %u batch comparisons, %u classic comparisons; AT/PIC IRQ0\n",
        status_cases, count_cases, wave_cases, batch_cases, classic_cases);
    return 0;
}
