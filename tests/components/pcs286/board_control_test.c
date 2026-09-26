/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors */
#include "board_fixture.h"
static void
a20_and_ownership(void)
{
    fixture_t f;
    start(&f);
    poke(&f, 0, 0x12);
    poke(&f, 0xa0000, 0x34);
    for (unsigned enabled = 0; enabled < 2; ++enabled) {
        output(&f, 0xc1 | (enabled << 1));
        bm_bus_transaction_t t = transaction(BM_ADDRESS_MEMORY, 0x100000, BM_BUS_READ, 0);
        assert(f.memory.config.cpu_a20 == (int) enabled);
        assert(bm_at_bus_cpu_access(f.bus, &t) == BM_STATUS_OK);
        assert(t.value == (enabled ? 0x34U : 0x12U));
        for (unsigned master = BM_AT_MASTER_DMA8; master <= BM_AT_MASTER_ISA; ++master) {
            bm_at_transfer_t d = {
                (bm_at_master_t) master, t, { 8000000, 1 }
            };
            bm_pcs286_control_event_t e;
            bm_at_bus_arbitration_t   a;
            d.bus.attributes = 0;
            assert(bm_at_bus_request(f.bus, d.master, 1) == BM_STATUS_OK);
            assert(bm_pcs286_control_step(&f.control, &e) == BM_STATUS_IDLE);
            assert(bm_at_bus_access(f.bus, &d) == BM_STATUS_OK && d.bus.value == 0x34);
            assert(bm_pcs286_control_reset_cpu(&f.control) == BM_STATUS_OK);
            assert(bm_at_bus_arbitration(f.bus, &a) == BM_STATUS_OK && a.hold && !a.hlda && a.requested);
            assert(bm_at_bus_access(f.bus, &d) == BM_STATUS_IDLE);
            assert(bm_pcs286_control_step(&f.control, &e) == BM_STATUS_IDLE);
            assert(bm_at_bus_access(f.bus, &d) == BM_STATUS_OK && d.bus.value == 0x34);
            t.attributes = BM_BUS_TRANSACTION_DEBUG;
            assert(bm_at_bus_cpu_access(f.bus, &t) == BM_STATUS_OK && t.value == (enabled ? 0x34U : 0x12U));
            assert(bm_at_bus_request(f.bus, d.master, 0) == BM_STATUS_OK);
            assert(bm_at_bus_arbitration(f.bus, &a) == BM_STATUS_OK && !a.hlda);
        }
    }
    done(&f);
}
static void
reset_paths(void)
{
    fixture_t                   f;
    bm_pcs286_control_event_t   e, before;
    bm_at_keyboard_pair_state_t pair_before, pair_after;
    static const uint8_t        out[] = { 0xe6, 0x64 };
    start(&f);
    program(&f, out, sizeof(out));
    bm_286_arch_state_t s = arch(&f);
    s.ax                  = 0xfe;
    assert(bm_286_set_arch_state(&f.cpu, &s) == BM_STATUS_OK);
    f.during_io = f.trace_reset = 1;
    e                           = step(&f);
    assert(e.kind == BM_PCS286_CONTROL_CPU && e.reset_applied && e.cpu.instruction_ip == 0x100);
    assert(arch(&f).cs.base == 0xff0000 && arch(&f).ip == 0xfff0);
    before         = e;
    unsigned calls = f.calls;
    assert(bm_pcs286_control_step(&f.control, &e) == BM_STATUS_IDLE);
    assert(memcmp(&before, &e, sizeof(e)) == 0 && f.calls == calls);
    f.during_io = f.trace_reset = 0;
    run(&f, 10000); /* Pulse release does not request another reset. */
    assert(!f.control.state.reset_pending && !f.control.state.reset_level);
    /* Pulse finishes entirely between CPU boundaries, still must reset. */
    command(&f, 0xfe);
    run(&f, 10000);
    assert(f.control.state.reset_pending && !f.control.state.reset_level);
    assert(bm_at_keyboard_pair_state(f.pair, &pair_before) == BM_STATUS_OK);
    bm_tick_t now = bm_engine_now(f.engine);
    poke(&f, 0x500, 0x77);
    port(&f, 0x81, BM_BUS_WRITE, 0x42);
    port(&f, 0x1ef, BM_BUS_WRITE, 2);
    e = step(&f);
    assert(e.kind == BM_PCS286_CONTROL_RESET && e.reset_applied && f.calls == calls);
    assert(peek(&f, 0x500) == 0x77 && bm_engine_now(f.engine) == now);
    assert(port(&f, 0x81, BM_BUS_READ, 0) == 0x42 && f.routes.registers.cr0 == 0x62);
    assert(bm_at_keyboard_pair_state(f.pair, &pair_after) == BM_STATUS_OK);
    assert(pair_before.controller.cycles == pair_after.controller.cycles);
    assert(pair_before.controller.output_port == pair_after.controller.output_port);
    assert(pair_before.keyboard.cycles == pair_after.keyboard.cycles);
    output(&f, 0xc2); /* A20 high, RESET held active. */
    assert(step(&f).kind == BM_PCS286_CONTROL_RESET);
    assert(f.memory.config.cpu_a20 && !(arch(&f).msw & 1));
    output(&f, 0xc2);
    command(&f, 0xfe);
    run(&f, 10000);
    assert(!f.control.state.reset_pending && f.control.state.reset_level);
    assert(bm_pcs286_control_step(&f.control, &e) == BM_STATUS_IDLE);
    output(&f, 0xc3);
    assert(!f.control.state.reset_pending && !f.control.state.reset_level);
    /* No event, input signal or decode access is fabricated by inspection. */
    assert(bm_pcs286_control_state(&f.control, &f.control.state) == BM_STATUS_OK);
    done(&f);
}
static void
irq_and_nmi(void)
{
    fixture_t            f;
    bm_at_pic_state_t    pic;
    bm_input_event_t     key   = { 0 };
    static const uint8_t nop[] = { 0x90 };
    start(&f);
    program(&f, nop, sizeof(nop));
    port(&f, 0x20, BM_BUS_WRITE, 0x11);
    port(&f, 0x21, BM_BUS_WRITE, 0x30);
    port(&f, 0x21, BM_BUS_WRITE, 4);
    port(&f, 0x21, BM_BUS_WRITE, 1);
    port(&f, 0x21, BM_BUS_WRITE, 0xfd);
    command(&f, 0x60);
    data(&f, 0x41);
    run(&f, 30000);
    assert(port(&f, 0x60, BM_BUS_READ, 0) == 0xaa);
    key.kind    = BM_INPUT_KEY;
    key.key     = BM_KEY_A;
    key.pressed = 1;
    assert(bm_at_keyboard_pair_clock_input(f.link, &key) == BM_STATUS_OK);
    run(&f, 10000);
    assert(bm_at_pic_state(f.pic, &pic) == BM_STATUS_OK && pic.intr && (pic.irr[0] & 2));
    assert(bm_pcs286_control_reset_cpu(&f.control) == BM_STATUS_OK);
    program(&f, nop, sizeof(nop));
    bm_286_arch_state_t s = arch(&f);
    s.flags |= 0x200;
    assert(bm_286_set_arch_state(&f.cpu, &s) == BM_STATUS_OK);
    poke(&f, 0x31 * 4, 0);
    poke(&f, 0x31 * 4 + 1, 2);
    poke(&f, 0x200, 0xcf);
    bm_pcs286_control_event_t e = step(&f);
    assert(e.cpu.kind == BM_286_BOUNDARY_INTERRUPT && e.cpu.vector == 0x31);
    assert(port(&f, 0x60, BM_BUS_READ, 0) == 0x1e);
    assert(bm_at_pic_state(f.pic, &pic) == BM_STATUS_OK && !pic.intr && (pic.isr[0] & 2));
    port(&f, 0x20, BM_BUS_WRITE, 0x20);
    step(&f); /* IRET */
    assert(arch(&f).ip == 0x100);
    assert(bm_pcs286_control_nmi(&f.control, 1) == BM_STATUS_OK && arch(&f).nmi_pending);
    assert(bm_pcs286_control_reset_cpu(&f.control) == BM_STATUS_OK && arch(&f).nmi_pending);
    assert(bm_pcs286_control_nmi(&f.control, 0) == BM_STATUS_OK);
    assert(bm_pcs286_control_reset_cpu(&f.control) == BM_STATUS_OK && !arch(&f).nmi_pending);
    done(&f);
}
static void
failures(void)
{
    static const uint8_t     store[]  = { 0xc7, 0x06, 0x01, 0x05, 0x34, 0x12 };
    static const bm_status_t errors[] = { BM_STATUS_DEVICE_ERROR, BM_STATUS_CAPACITY_EXCEEDED,
                                          BM_STATUS_UNMAPPED, BM_STATUS_READ_ONLY, BM_STATUS_UNSUPPORTED, BM_STATUS_INVALID_STATE };
    fixture_t                f;
    start(&f);
    program(&f, store, sizeof(store));
    step(&f);
    unsigned transfers = f.calls;
    done(&f);
    for (unsigned error = 0; error < sizeof(errors) / sizeof(errors[0]); ++error)
        for (unsigned after = 0; after < 2; ++after)
            for (unsigned at = 1; at <= transfers; ++at) {
                bm_pcs286_control_event_t e, before;
                start(&f);
                program(&f, store, sizeof(store));
                f.failure = errors[error];
                f.fail_at = at;
                f.after   = after;
                memset(&e, 0xa5, sizeof(e));
                memcpy(&before, &e, sizeof(e));
                assert(bm_pcs286_control_step(&f.control, &e) == f.failure);
                assert(memcmp(&before, &e, sizeof(e)) == 0);
                assert(f.control.state.reset_pending && arch(&f).ip == 0x100);
                assert(bm_pcs286_control_step(&f.control, &e) == f.failure && f.calls == at);
                unsigned lo = peek(&f, 0x501), hi = peek(&f, 0x502);
                assert(transfers == 8);
                assert(lo == ((at > 7 || (at == 7 && after)) ? 0x34U : 0U));
                assert(hi == ((at == 8 && after) ? 0x12U : 0U));
                f.fail_at = 0;
                assert(bm_pcs286_control_reset_cpu(&f.control) == BM_STATUS_OK);
                assert(!f.control.state.reset_pending && arch(&f).ip == 0xfff0);
                assert(peek(&f, 0x501) == lo && peek(&f, 0x502) == hi);
                done(&f);
            }
    start(&f);
    program(&f, store, sizeof(store));
    f.reject_a20 = 1;
    bm_pcs286_control_event_t e;
    assert(bm_pcs286_control_step(&f.control, &e) == BM_STATUS_INVALID_STATE);
    assert(!f.memory.config.cpu_a20 && f.calls == transfers);
    assert(bm_pcs286_control_step(&f.control, &e) == BM_STATUS_INVALID_STATE && f.calls == transfers);
    assert(bm_pcs286_control_reset_cpu(&f.control) == BM_STATUS_OK);
    bm_pcs286_control_hold(&f.control, 2); /* Void callback errors cannot disappear. */
    assert(f.control.state.failure == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_pcs286_control_reset_cpu(&f.control) == BM_STATUS_OK);
    done(&f);
    printf("board control: %u transfer failures plus A20/reentry/pin failures\n",
           transfers * 12);
}
static void
locked_reset(void)
{
    fixture_t               f;
    static const uint8_t    rep[] = { 0xf0, 0xf3, 0xa4 };
    bm_at_bus_arbitration_t bus;
    start(&f);
    program(&f, rep, sizeof(rep));
    bm_286_arch_state_t s = arch(&f);
    s.cx                  = 3;
    s.si                  = 0x500;
    s.di                  = 0x600;
    assert(bm_286_set_arch_state(&f.cpu, &s) == BM_STATUS_OK);
    poke(&f, 0x500, 0x11);
    poke(&f, 0x501, 0x22);
    assert(step(&f).cpu.kind == BM_286_BOUNDARY_REP_ITERATION);
    assert(bm_at_bus_request(f.bus, BM_AT_MASTER_DMA8, 1) == BM_STATUS_OK);
    assert(bm_at_bus_arbitration(f.bus, &bus) == BM_STATUS_OK && bus.locked && !bus.hold);
    command(&f, 0xfe);
    run(&f, 10000);
    assert(step(&f).kind == BM_PCS286_CONTROL_RESET);
    assert(bm_at_bus_arbitration(f.bus, &bus) == BM_STATUS_OK && !bus.locked && bus.hold);
    bm_pcs286_control_event_t e;
    assert(bm_pcs286_control_step(&f.control, &e) == BM_STATUS_IDLE);
    assert(peek(&f, 0x600) == 0x11 && peek(&f, 0x601) == 0 && arch(&f).cx == 0);
    assert(bm_at_bus_request(f.bus, BM_AT_MASTER_DMA8, 0) == BM_STATUS_OK);
    done(&f);
}
static void
validation_and_isolation(void)
{
    fixture_t                 a, b;
    bm_pcs286_control_event_t event;
    bm_pcs286_control_state_t saved;
    start(&a);
    start(&b);
    assert(bm_pcs286_control_step(NULL, &event) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_pcs286_control_step(&a.control, NULL) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_pcs286_control_reset_cpu(NULL) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_pcs286_control_state(NULL, &saved) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_pcs286_control_state(&a.control, NULL) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_pcs286_control_initialize(NULL, &a.control.config) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_pcs286_control_initialize(&a.control, NULL) == BM_STATUS_INVALID_ARGUMENT);
    for (unsigned missing = 0; missing < 4; ++missing) {
        bm_pcs286_control_config_t config = a.control.config;
        if (missing == 0)
            config.cpu = NULL;
        if (missing == 1)
            config.bus = NULL;
        if (missing == 2)
            config.pic = NULL;
        if (missing == 3)
            config.memory = NULL;
        assert(bm_pcs286_control_initialize(&a.control, &config) == BM_STATUS_INVALID_ARGUMENT);
        assert(a.control.config.cpu == &a.cpu && a.control.config.bus == a.bus);
    }
    assert(bm_pcs286_control_irq1(NULL, 0) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_pcs286_control_a20(NULL, 0) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_pcs286_control_reset_line(NULL, 0) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_pcs286_control_nmi(NULL, 0) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_pcs286_control_irq1(&a.control, 2) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_pcs286_control_a20(&a.control, -1) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_pcs286_control_reset_line(&a.control, 2) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_pcs286_control_nmi(&a.control, -1) == BM_STATUS_INVALID_ARGUMENT);
    assert(!a.control.state.failure); /* Rejected user calls are not host faults. */
    output(&a, 0xc3);
    assert(a.memory.config.cpu_a20 && !b.memory.config.cpu_a20);
    for (unsigned mode = 0; mode < 3; ++mode) {
        bm_286_arch_state_t s = arch(&a);
        s.msw |= 1;
        s.cpl         = 3;
        s.halted      = mode == 1;
        s.shutdown    = mode == 2;
        s.nmi_blocked = s.trap_pending = 1;
        assert(bm_286_set_arch_state(&a.cpu, &s) == BM_STATUS_OK);
        assert(bm_pcs286_control_reset_line(&a.control, 1) == BM_STATUS_OK);
        assert(bm_pcs286_control_reset_line(&a.control, 0) == BM_STATUS_OK);
        assert(step(&a).kind == BM_PCS286_CONTROL_RESET);
        s = arch(&a);
        assert(!(s.msw & 1) && !s.cpl && !s.halted && !s.shutdown);
        assert(!s.nmi_blocked && !s.trap_pending && s.ip == 0xfff0 && s.cs.base == 0xff0000);
        assert(!a.calls && !b.calls && !b.control.state.reset_pending);
    }
    /* A strict-clock refusal stays a refusal; the owner does not supply cycles. */
    uint64_t cycles = 99;
    /* Engine callbacks take the private context, not the public descriptor. */
    assert(bm_286_step_clocked(b.cpu.context, 0, &cycles) == BM_STATUS_UNSUPPORTED && !cycles && !b.calls);
    cycles = 99;
    assert(bm_286_step_clocked(b.cpu.context, 0, &cycles) == BM_STATUS_INVALID_STATE && !cycles && !b.calls);
    assert(bm_pcs286_control_reset_cpu(&b.control) == BM_STATUS_OK);
    done(&a);
    done(&b);
}
int
main(void)
{
    a20_and_ownership();
    reset_paths();
    irq_and_nmi();
    failures();
    locked_reset();
    validation_and_isolation();
    puts("Board A20/reset/IRQ wiring passed; strict CPU timing and full machine remain pending");
    return 0;
}
