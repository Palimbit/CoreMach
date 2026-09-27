/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Authored peripheral-time/boundary composition; no CPU timing/firmware oracle. */
#include "board_fixture.h"
#include "board_services.h"
#include "failure_injection_host.h"

typedef struct bench {
    fixture_t             board;
    bm_pcs286_services_t *services;
    unsigned              sounds, inspections;
    int                   sound, inspect;
} bench_t;
static bm_pcs286_services_state_t
state(bench_t *b)
{
    bm_pcs286_services_state_t s;
    assert(bm_pcs286_services_state(b->services, &s) == BM_STATUS_OK);
    return s;
}
static void
digital(void *p, int v)
{
    bench_t *b = p;
    b->sound   = v;
    ++b->sounds;
    if (b->inspect) {
        bm_pcs286_services_step_t e;
        bm_input_event_t          key = { 0 };
        key.kind                      = BM_INPUT_KEY;
        key.key                       = BM_KEY_A;
        bm_bus_transaction_t t        = transaction(BM_ADDRESS_IO, 0x61, BM_BUS_READ, 0xbeef);
        (void) state(b);
        assert(bm_pcs286_services_step(b->services, &e) == BM_STATUS_INVALID_STATE);
        assert(bm_pcs286_services_advance(b->services, 1) == BM_STATUS_INVALID_STATE);
        assert(bm_pcs286_services_reset_cpu(b->services) == BM_STATUS_INVALID_STATE);
        assert(bm_pcs286_services_reset_epoch(b->services, 1) == BM_STATUS_INVALID_STATE);
        assert(bm_pcs286_services_input(b->services, &key) == BM_STATUS_INVALID_STATE);
        assert(bm_pcs286_services_io_check(b->services, 1) == BM_STATUS_INVALID_STATE);
        assert(bm_pcs286_services_parity(b->services, 1) == BM_STATUS_INVALID_STATE);
        assert(bm_pcs286_services_io(b->services, &t) == BM_STATUS_INVALID_STATE && t.value == 0xbeef);
        t.attributes = BM_BUS_TRANSACTION_DEBUG;
        assert(bm_pcs286_services_io(b->services, &t) == BM_STATUS_OK);
        bm_pcs286_services_destroy(b->services); /* Refused while callback active. */
        ++b->inspections;
    }
}
static bm_pcs286_services_config_t
config(bench_t *b)
{
    bm_pcs286_services_config_t c      = { 0 };
    c.control                          = &b->board.control;
    c.pit_clock                        = (bm_clock_rate_t) { 1000000, 1 };
    c.rtc.io_base                      = 0x70;
    c.rtc.cmos_size                    = 128;
    c.keyboard.controller.data_port    = 0x60;
    c.keyboard.controller.command_port = 0x64;
    c.keyboard.controller.clock = c.keyboard.keyboard.clock = (bm_clock_rate_t) { 1500000, 1 };
    c.keyboard.controller.input_cycles                      = 2;
    c.keyboard.controller.output_cycles                     = 3;
    c.keyboard.controller.self_test_cycles                  = 5;
    c.keyboard.controller.pulse_cycles                      = 6;
    c.keyboard.controller.input_port                        = 0xa0;
    c.keyboard.controller.initial_output_port               = 0xc1;
    c.keyboard.keyboard.power_on_cycles                     = 3;
    c.keyboard.keyboard.bat_cycles                          = 5;
    c.keyboard.keyboard.byte_cycles                         = 2;
    c.keyboard.keyboard.reset_accept_cycles                 = 3;
    c.speaker                                               = digital;
    c.speaker_context                                       = b;
    return c;
}
static void
base(bench_t *b)
{
    memset(b, 0, sizeof(*b));
    start(&b->board);
    /* Same previously validated CPU/AT fixture, now with one services owner
     * instead of the standalone pair/engine. No callbacks after release. */
    bm_engine_destroy(b->board.engine);
    b->board.engine = NULL;
    bm_at_clock_link_destroy(b->board.link);
    b->board.link = NULL;
    bm_at_keyboard_pair_destroy(b->board.pair);
    b->board.pair = NULL;
}
static void
attach(bench_t *b)
{
    bm_pcs286_io_config_t io = b->board.io.config;
    io.resource_count        = 4;
    io.resources[0]          = (bm_pcs286_io_resource_t) { 0x40, 0x43, 1, bm_pcs286_services_io, b->services, 0 };
    io.resources[1]          = (bm_pcs286_io_resource_t) { 0x60, 0x61, 1, bm_pcs286_services_io, b->services, 0 };
    io.resources[2]          = (bm_pcs286_io_resource_t) { 0x64, 0x64, 1, bm_pcs286_services_io, b->services, 0 };
    io.resources[3]          = (bm_pcs286_io_resource_t) { 0x70, 0x71, 1, bm_pcs286_services_io, b->services, 0 };
    assert(bm_pcs286_io_initialize(&b->board.io, &io) == BM_STATUS_OK);
}
static void
begin(bench_t *b)
{
    bm_host_services_t h = bm_null_host_services();
    base(b);
    bm_pcs286_services_config_t c = config(b);
    assert(bm_pcs286_services_create(&h, &c, &b->services) == BM_STATUS_OK);
    attach(b);
    b->inspect = 1;
}
static void
end(bench_t *b)
{
    b->inspect = 0;
    bm_pcs286_services_destroy(b->services);
    b->services = NULL;
    done(&b->board);
}
static void
advance(bench_t *b, uint64_t ns)
{
    assert(bm_pcs286_services_advance(b->services, ns) == BM_STATUS_OK);
}
static unsigned
in(bench_t *b, unsigned address)
{
    return port(&b->board, address, BM_BUS_READ, 0);
}
static void
out(bench_t *b, unsigned address, unsigned v)
{
    port(&b->board, address, BM_BUS_WRITE, v);
}
static bm_pcs286_services_step_t
boundary(bench_t *b)
{
    bm_pcs286_services_step_t r;
    assert(bm_pcs286_services_step(b->services, &r) == BM_STATUS_OK);
    return r;
}
static void
timer(bench_t *b, unsigned channel, unsigned mode, unsigned divisor)
{
    out(b, 0x43, (channel << 6) | 0x30 | (mode << 1));
    out(b, 0x40 + channel, divisor & 255);
    out(b, 0x40 + channel, divisor >> 8);
}
static void
rtc_write(bench_t *b, unsigned index, unsigned value)
{
    out(b, 0x70, index);
    out(b, 0x71, value);
}
static unsigned
rtc_read(bench_t *b, unsigned index)
{
    out(b, 0x70, index);
    return in(b, 0x71);
}
static void
pic_init(bench_t *b)
{
    out(b, 0x20, 0x11);
    out(b, 0x21, 0x30);
    out(b, 0x21, 4);
    out(b, 0x21, 1);
    out(b, 0xa0, 0x11);
    out(b, 0xa1, 0x38);
    out(b, 0xa1, 2);
    out(b, 0xa1, 1);
    out(b, 0x21, 0xf8);
    out(b, 0xa1, 0xfe);
}
static void
refresh_boundaries(void)
{
    bench_t       b;
    const uint8_t nops[] = { 0x90, 0x90, 0x90, 0x90 };
    begin(&b);
    program(&b.board, nops, sizeof(nops));
    timer(&b, 1, 2, 4);
    assert(state(&b).refresh_pending && !state(&b).refdet && !b.board.calls);
    bm_pcs286_services_step_t e = boundary(&b);
    assert(!e.cpu_completed && e.refresh_completed && !b.board.calls);
    assert(state(&b).refdet && !state(&b).refresh_pending);
    e = boundary(&b);
    assert(e.cpu_completed && !e.refresh_completed && arch(&b.board).ip == 0x101);
    assert(state(&b).time.nanoseconds == 0); /* No fabricated instruction time. */
    advance(&b, 100000);
    assert(state(&b).refresh_pending && state(&b).refdet && arch(&b.board).ip == 0x101);
    e = boundary(&b);
    assert(e.refresh_completed && !e.cpu_completed);
    assert(!state(&b).refdet && !state(&b).refresh_pending);
    /* An external owner blocks refresh and epoch reset without losing input. */
    assert(bm_at_bus_request(b.board.bus, BM_AT_MASTER_DMA8, 1) == BM_STATUS_OK);
    bm_pcs286_services_step_t untouched;
    memset(&untouched, 0x55, sizeof(untouched));
    e = untouched;
    assert(bm_pcs286_services_step(b.services, &e) == BM_STATUS_IDLE);
    assert(memcmp(&untouched, &e, sizeof(e)) == 0);
    advance(&b, 20000);
    bm_pcs286_services_state_t before = state(&b);
    assert(bm_pcs286_services_reset_epoch(b.services, 0) == BM_STATUS_INVALID_STATE);
    assert(bm_pcs286_services_reset_epoch(b.services, 1) == BM_STATUS_INVALID_STATE);
    assert(state(&b).time.nanoseconds == before.time.nanoseconds && state(&b).refresh_pending);
    assert(bm_at_bus_request(b.board.bus, BM_AT_MASTER_DMA8, 0) == BM_STATUS_OK);
    assert(boundary(&b).refresh_completed);
    end(&b);
}
static void
interrupts_and_speaker(void)
{
    bench_t       b;
    const uint8_t code[] = { 0x90, 0xe4, 0x61, 0x90 };
    begin(&b);
    program(&b.board, code, sizeof(code));
    pic_init(&b);
    for (unsigned v = 0x30; v <= 0x38; ++v) {
        poke(&b.board, v * 4, 0);
        poke(&b.board, v * 4 + 1, 2);
    }
    poke(&b.board, 0x200, 0xcf);
    bm_286_arch_state_t a = arch(&b.board);
    a.flags |= 0x200;
    assert(bm_286_set_arch_state(&b.board.cpu, &a) == BM_STATUS_OK);
    timer(&b, 0, 2, 4);
    bm_pcs286_services_step_t e = boundary(&b);
    assert(e.cpu_completed && e.cpu.cpu.kind == BM_286_BOUNDARY_INTERRUPT && e.cpu.cpu.vector == 0x30);
    out(&b, 0x20, 0x20);
    boundary(&b); /* IRET, no time elapses. */
    /* Mask IRQ0 while testing RTC IRQ8. Periodic flags accumulate with SET. */
    out(&b, 0x21, 0xfb);
    rtc_write(&b, 0x0a, 0x26);
    rtc_write(&b, 0x0b, 0xc2);
    advance(&b, 1000000);
    e = boundary(&b);
    assert(e.cpu.cpu.kind == BM_286_BOUNDARY_INTERRUPT && e.cpu.cpu.vector == 0x38);
    assert((rtc_read(&b, 0x0c) & 0xc0) == 0xc0);
    out(&b, 0xa0, 0x20);
    out(&b, 0x20, 0x20);
    boundary(&b);
    out(&b, 0x70, 0x80); /* Global mask before qualified parity sample. */
    assert(bm_pcs286_services_parity(b.services, 1) == BM_STATUS_OK && !state(&b).nmi);
    assert(in(&b, 0x61) & 0x80);
    out(&b, 0x70, 0);
    assert(state(&b).nmi && arch(&b.board).nmi_pending);
    out(&b, 0x61, 4);
    assert(!state(&b).nmi && !(in(&b, 0x61) & 0x80));
    /* CPU-only reset clears accepted NMI edge, keeps all devices/clock. */
    uint64_t now = state(&b).time.nanoseconds;
    assert(bm_pcs286_services_reset_cpu(b.services) == BM_STATUS_OK);
    assert(!arch(&b.board).nmi_pending && state(&b).time.nanoseconds == now);
    timer(&b, 2, 3, 4);
    out(&b, 0x61, 3);
    advance(&b, 10000);
    assert(b.sounds >= 4 && b.inspections == b.sounds);
    unsigned raw = in(&b, 0x61) & 0x20;
    out(&b, 0x61, 0);
    assert(!b.sound && (in(&b, 0x61) & 0x20) == raw);
    end(&b);
}
static void
keyboard_reset_and_epoch(void)
{
    bench_t b;
    begin(&b);
    rtc_write(&b, 0x8e, 0x5a); /* Battery-backed RAM, mask NMI. */
    out(&b, 0x64, 0x60);
    advance(&b, 2000);
    out(&b, 0x60, 0x41);
    advance(&b, 30000);
    assert(in(&b, 0x60) == 0xaa);
    bm_input_event_t key = { 0 };
    key.kind             = BM_INPUT_KEY;
    key.key              = BM_KEY_A;
    key.pressed          = 1;
    assert(bm_pcs286_services_input(b.services, &key) == BM_STATUS_OK);
    advance(&b, 10000);
    assert(in(&b, 0x60) == 0x1e);
    out(&b, 0x64, 0xfe);
    advance(&b, 2000);
    bm_pcs286_services_step_t e = boundary(&b);
    assert(e.cpu_completed && e.cpu.kind == BM_PCS286_CONTROL_RESET);
    assert(bm_pcs286_services_step(b.services, &e) == BM_STATUS_IDLE);
    advance(&b, 10000); /* Engine continues while CPU reset is held. */
    assert(!b.board.control.state.reset_level);
    timer(&b, 1, 2, 5);
    assert(state(&b).refresh_pending);
    assert(bm_pcs286_services_io_check(b.services, 1) == BM_STATUS_OK);
    poke(&b.board, 0x500, 0xab);
    out(&b, 0x81, 0x44);
    out(&b, 0x1ef, 2);
    /* The epoch reset owns its refresh request, but not external DMA/cards. */
    assert(bm_pcs286_services_reset_epoch(b.services, 0) == BM_STATUS_OK);
    bm_pcs286_services_state_t s = state(&b);
    assert(s.time.nanoseconds == 0 && !s.refresh_pending && !s.refdet && !s.port61);
    assert(s.io_check_active && !s.nmi && (in(&b, 0x61) & 0xc0) == 0x40);
    assert(rtc_read(&b, 0x8e) == 0x5a && peek(&b.board, 0x500) == 0xab);
    assert(in(&b, 0x81) == 0x44 && b.board.routes.registers.cr0 == 0x62);
    assert(!b.board.memory.config.cpu_a20 && arch(&b.board).ip == 0xfff0);
    bm_at_pic_state_t pic;
    assert(bm_at_pic_state(b.board.pic, &pic) == BM_STATUS_OK && pic.imr[0] == 0xff && pic.imr[1] == 0xff);
    out(&b, 0x70, 0);
    assert(state(&b).nmi); /* External IO check really survived. */
    assert(bm_pcs286_services_io_check(b.services, 0) == BM_STATUS_OK);
    out(&b, 0x61, 8);
    assert(!state(&b).nmi);
    end(&b);
}
static void
partitions(void)
{
    for (unsigned seed = 1; seed <= 32; ++seed) {
        bench_t  a, b;
        unsigned rng = seed;
        begin(&a);
        begin(&b);
        const uint8_t nops[] = { 0x90, 0x90, 0x90, 0x90 };
        program(&a.board, nops, sizeof(nops));
        program(&b.board, nops, sizeof(nops));
        timer(&a, 1, 2, 3 + seed);
        timer(&b, 1, 2, 3 + seed);
        timer(&a, 2, 3, 4 + seed);
        timer(&b, 2, 3, 4 + seed);
        out(&a, 0x61, 3);
        out(&b, 0x61, 3);
        rtc_write(&a, 0x8a, 0x26);
        rtc_write(&b, 0x8a, 0x26);
        rtc_write(&a, 0x8b, 0xc2);
        rtc_write(&b, 0x8b, 0xc2);
        for (unsigned boundary_index = 0; boundary_index < 4; ++boundary_index) {
            advance(&a, 25000);
            unsigned remaining = 25000;
            while (remaining) {
                rng        = rng * 1664525U + 1013904223U;
                unsigned n = 1 + (rng % 701);
                if (n > remaining)
                    n = remaining;
                advance(&b, n);
                remaining -= n;
            }
            assert(state(&a).time.nanoseconds == state(&b).time.nanoseconds);
            assert(in(&a, 0x61) == in(&b, 0x61) && a.sounds == b.sounds && a.sound == b.sound);
            assert(state(&a).refresh_pending == state(&b).refresh_pending);
            int                       pending = state(&a).refresh_pending;
            bm_pcs286_services_step_t ea = boundary(&a), eb = boundary(&b);
            assert(ea.refresh_completed == pending && eb.refresh_completed == pending);
            assert(ea.cpu_completed == eb.cpu_completed);
            assert(state(&a).refdet == state(&b).refdet);
        }
        end(&a);
        end(&b);
    }
}
static void
cpu_io_and_lock(void)
{
    bench_t       b;
    const uint8_t code[] = { 0xb0, 0x74, 0xe6, 0x43, 0xb0, 4, 0xe6, 0x41,
                             0xb0, 0, 0xe6, 0x41, 0xe4, 0x61, 0xf4 };
    begin(&b);
    program(&b.board, code, sizeof(code));
    unsigned instructions = 0, refreshes = 0;
    for (unsigned i = 0; i < 12 && !arch(&b.board).halted; ++i) {
        bm_pcs286_services_step_t e = boundary(&b);
        instructions += (unsigned) e.cpu_completed;
        refreshes += (unsigned) e.refresh_completed;
    }
    assert(instructions == 8 && refreshes == 1 && arch(&b.board).halted);
    assert((arch(&b.board).ax & 0x10) && state(&b).time.nanoseconds == 0);
    advance(&b, 32000);
    assert(boundary(&b).refresh_completed && arch(&b.board).halted);
    assert(bm_pcs286_services_reset_epoch(b.services, 0) == BM_STATUS_OK);
    const uint8_t rep[] = { 0xf0, 0xf3, 0xa4 };
    program(&b.board, rep, sizeof(rep));
    bm_286_arch_state_t a = arch(&b.board);
    a.cx                  = 3;
    a.si                  = 0x500;
    a.di                  = 0x600;
    assert(bm_286_set_arch_state(&b.board.cpu, &a) == BM_STATUS_OK);
    poke(&b.board, 0x500, 0x12);
    poke(&b.board, 0x501, 0x34);
    assert(boundary(&b).cpu.cpu.kind == BM_286_BOUNDARY_REP_ITERATION);
    timer(&b, 1, 2, 4);
    bm_at_bus_arbitration_t bus;
    assert(bm_at_bus_arbitration(b.board.bus, &bus) == BM_STATUS_OK && bus.locked && !bus.hold);
    advance(&b, 30000);
    assert(state(&b).refresh_pending);
    assert(bm_pcs286_services_reset_epoch(b.services, 0) == BM_STATUS_OK);
    assert(bm_at_bus_arbitration(b.board.bus, &bus) == BM_STATUS_OK && !bus.locked && !bus.requested);
    assert(peek(&b.board, 0x600) == 0x12 && peek(&b.board, 0x601) == 0);
    assert(!state(&b).refresh_pending && !arch(&b.board).cx);
    end(&b);
}
static void
validation_and_device_stop(void)
{
    bench_t            b;
    bm_host_services_t h = bm_null_host_services();
    base(&b);
    bm_pcs286_services_config_t original = config(&b);
    for (unsigned invalid = 0; invalid < 13; ++invalid) {
        bm_pcs286_services_config_t c = original;
        switch (invalid) {
            case 0:
                c.control = NULL;
                break;
            case 1:
                c.rtc.io_base = 0x72;
                break;
            case 2:
                c.rtc.output_context = &b;
                break;
            case 3:
                c.keyboard.controller.data_port = 0x61;
                break;
            case 4:
                c.keyboard.controller.command_port = 0x65;
                break;
            case 5:
                c.keyboard.controller.output_context = &b;
                break;
            case 6:
                c.keyboard.controller.keyboard_context = &b;
                break;
            case 7:
                c.keyboard.keyboard.send_context = &b;
                break;
            case 8:
                c.pit_clock.cycles_per_second_numerator = 0;
                break;
            case 9:
                c.keyboard.keyboard.clock.cycles_per_second_numerator = 123;
                break;
            case 10:
                c.rtc.cmos_size = 12;
                break;
            case 11:
                c.keyboard.controller.a20 = bm_pcs286_control_a20;
                break;
            case 12:
                c.rtc.nmi_mask = bm_pcs286_control_nmi;
                break;
        }
        assert(bm_pcs286_services_create(&h, &c, &b.services) == BM_STATUS_INVALID_ARGUMENT && !b.services);
    }
    assert(bm_pcs286_services_create(&h, &original, NULL) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_pcs286_services_create(NULL, &original, &b.services) == BM_STATUS_INVALID_ARGUMENT);
    done(&b.board);
    begin(&b);
    bm_pcs286_services_step_t  result;
    bm_pcs286_services_state_t st;
    assert(bm_pcs286_services_step(NULL, &result) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_pcs286_services_step(b.services, NULL) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_pcs286_services_advance(NULL, 1) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_pcs286_services_state(NULL, &st) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_pcs286_services_state(b.services, NULL) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_pcs286_services_input(b.services, NULL) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_pcs286_services_parity(b.services, 2) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_pcs286_services_io_check(b.services, -1) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_pcs286_services_reset_epoch(b.services, 2) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_pcs286_services_reset_cpu(NULL) == BM_STATUS_INVALID_ARGUMENT);
    bm_bus_transaction_t t = transaction(BM_ADDRESS_IO, 0x92, BM_BUS_READ, 0xbeef);
    assert(bm_pcs286_services_io(b.services, &t) == BM_STATUS_UNMAPPED && t.value == 0xbeef);
    t.address = 0x43;
    assert(bm_pcs286_services_io(b.services, &t) == BM_STATUS_UNMAPPED && !state(&b).failure);
    t.operation  = BM_BUS_WRITE;
    t.attributes = BM_BUS_TRANSACTION_DEBUG;
    assert(bm_pcs286_services_io(b.services, &t) == BM_STATUS_UNSUPPORTED);
    /* Real RTC calendar failure stops its clock without inventing a guest NMI. */
    rtc_write(&b, 0x8a, 0x20);
    rtc_write(&b, 0x8b, 2); /* Release SET with invalid month/day0. */
    assert(bm_pcs286_services_advance(b.services, 600000000) == BM_STATUS_UNSUPPORTED);
    st = state(&b);
    assert(st.failure == BM_STATUS_UNSUPPORTED && !st.nmi && !b.board.calls);
    assert(st.time.nanoseconds > 500000000 && st.time.nanoseconds < 600000000);
    {
        bm_at_rtc_state_t rtc, old;
        uint8_t bytes[128], again[128];
        memset(&rtc, 0xa5, sizeof(rtc)); old = rtc;
        memset(bytes, 0x5a, sizeof(bytes)); memcpy(again, bytes, sizeof(bytes));
        assert(bm_pcs286_services_inspect_rtc(b.services, &rtc, bytes, 1) == BM_STATUS_INVALID_ARGUMENT);
        assert(!memcmp(&rtc, &old, sizeof(rtc)) && !memcmp(bytes, again, sizeof(bytes)));
        assert(bm_pcs286_services_inspect_rtc(b.services, &rtc, bytes, sizeof(bytes)) == BM_STATUS_OK);
        assert(rtc.divider_phase == 16449 && !rtc.updating && bytes[7] == 0 && bytes[8] == 0);
        assert(bm_pcs286_services_inspect_rtc(b.services, &old, again, sizeof(again)) == BM_STATUS_OK);
        assert(!memcmp(&rtc, &old, sizeof(rtc)) && !memcmp(bytes, again, sizeof(bytes)));
        assert(state(&b).time.nanoseconds == st.time.nanoseconds && state(&b).failure == st.failure);
    }
    assert(bm_pcs286_services_advance(b.services, 1) == BM_STATUS_UNSUPPORTED);
    assert(state(&b).time.nanoseconds == st.time.nanoseconds);
    assert(bm_pcs286_services_reset_epoch(b.services, 0) == BM_STATUS_UNSUPPORTED);
    assert(bm_pcs286_services_reset_epoch(b.services, 1) == BM_STATUS_OK);
    rtc_write(&b, 0x8b, 0x82);       /* Guest/owner explicitly repairs controls. */
    assert(rtc_read(&b, 0x88) == 0); /* Bad month was never silently repaired. */
    advance(&b, 600000000);
    assert(!state(&b).failure && !state(&b).nmi);
    end(&b);
}
static void
allocation_and_failures(void)
{
    bench_t                  b;
    failure_injection_host_t failures;
    failure_injection_host_initialize(&failures);
    bm_host_services_t h = failure_injection_host_services(&failures);
    base(&b);
    bm_pcs286_services_config_t c = config(&b);
    assert(bm_pcs286_services_create(&h, &c, &b.services) == BM_STATUS_OK);
    size_t count = failures.allocation_calls;
    bm_pcs286_services_destroy(b.services);
    b.services = NULL;
    assert(!failures.outstanding_allocations);
    for (size_t n = 0; n < count; ++n) {
        failure_injection_host_fail_after(&failures, n);
        assert(bm_pcs286_services_create(&h, &c, &b.services) == BM_STATUS_OUT_OF_MEMORY && !b.services);
        assert(!failures.outstanding_allocations);
    }
    failure_injection_host_fail_on(&failures, 0);
    b.board.memory.busy = 1; /* Initial A20 publication fails after allocations. */
    assert(bm_pcs286_services_create(&h, &c, &b.services) == BM_STATUS_INVALID_STATE && !b.services);
    assert(!failures.outstanding_allocations);
    b.board.memory.busy = 0;
    assert(bm_pcs286_control_reset_cpu(&b.board.control) == BM_STATUS_OK);
    done(&b.board);
    begin(&b);
    advance(&b, UINT64_MAX);
    bm_pcs286_services_state_t before = state(&b);
    assert(bm_pcs286_services_advance(b.services, 1) == BM_STATUS_INVALID_ARGUMENT);
    assert(!state(&b).failure); /* Invalid duration has no accepted effects. */
    out(&b, 0x43, 0x34);
    out(&b, 0x40, 4);
    bm_bus_transaction_t failed = transaction(BM_ADDRESS_IO, 0x40, BM_BUS_WRITE, 0);
    assert(bm_pcs286_services_io(b.services, &failed) == BM_STATUS_CAPACITY_EXCEEDED);
    assert(state(&b).failure == BM_STATUS_CAPACITY_EXCEEDED && state(&b).time.nanoseconds == before.time.nanoseconds);
    bm_bus_transaction_t t = transaction(BM_ADDRESS_IO, 0x64, BM_BUS_READ, 0xbeef);
    assert(bm_pcs286_services_io(b.services, &t) == BM_STATUS_CAPACITY_EXCEEDED && t.value == 0xbeef);
    t.attributes = BM_BUS_TRANSACTION_DEBUG;
    assert(bm_pcs286_services_io(b.services, &t) == BM_STATUS_OK);
    assert(bm_pcs286_services_reset_cpu(b.services) == BM_STATUS_CAPACITY_EXCEEDED);
    assert(bm_pcs286_services_reset_epoch(b.services, 0) == BM_STATUS_CAPACITY_EXCEEDED);
    assert(bm_pcs286_services_reset_epoch(b.services, 1) == BM_STATUS_OK);
    assert(state(&b).time.nanoseconds == 0 && !state(&b).failure);
    advance(&b, 10000);
    end(&b);
    printf("services: %zu allocation failures, publication failure, overflow recovery\n", count);
}
int
main(void)
{
    refresh_boundaries();
    interrupts_and_speaker();
    keyboard_reset_and_epoch();
    partitions();
    cpu_io_and_lock();
    validation_and_device_stop();
    allocation_and_failures();
    puts("Board services passed: explicit peripheral time, real CPU boundaries, no timed CPU or boot claim");
    return 0;
}
