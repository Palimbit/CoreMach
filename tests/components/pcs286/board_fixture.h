#ifndef BM_PCS286_TEST_BOARD_FIXTURE_H
#define BM_PCS286_TEST_BOARD_FIXTURE_H
/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Authored software-visible board integration; no firmware or timed CPU. */
#include "board_control.h"
#include "board_io.h"
#include <blumach/components/at_keyboard_pair.h>
#include <blumach/platforms/null_host.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct fixture {
    bm_pcs286_control_t         control;
    bm_headland_at_memory_t     memory;
    bm_gc103_memory_t           routes;
    bm_ioc02_legacy_registers_t ioc;
    bm_pcs286_io_t              io;
    bm_pcs286_memory_t         *bytes;
    bm_cpu_t                    cpu;
    bm_at_bus_t                *bus;
    bm_at_pic_t                *pic;
    bm_at_dma_t                *dma;
    bm_at_keyboard_pair_t      *pair;
    bm_engine_t                *engine;
    bm_at_clock_link_t         *link;
    unsigned                    calls, fail_at, after, trace_calls;
    int                         during_io, trace_reset, reject_a20;
    bm_status_t                 failure;
} fixture_t;
static inline bm_286_arch_state_t
arch(fixture_t *f)
{
    bm_286_arch_state_t s;
    assert(bm_286_get_arch_state(&f->cpu, &s) == BM_STATUS_OK);
    return s;
}
static inline bm_status_t
backing(void *p, bm_pcs286_memory_region_t r, uint32_t offset,
        bm_bus_transaction_t *t)
{
    fixture_t  *f = p;
    bm_status_t s;
    ++f->calls;
    if (f->reject_a20) {
        f->reject_a20 = 0;
        assert(bm_pcs286_control_a20(&f->control, 1) == BM_STATUS_INVALID_STATE);
    }
    if (f->calls == f->fail_at) {
        assert(bm_pcs286_control_reset_line(&f->control, 1) == BM_STATUS_OK);
        assert(bm_pcs286_control_reset_line(&f->control, 0) == BM_STATUS_OK);
        if (!f->after)
            return f->failure;
    }
    s = bm_headland_at_memory_backing(f->bytes, r, offset, t);
    return f->calls == f->fail_at ? f->failure : s;
}
static inline bm_status_t
memory(void *p, bm_at_transfer_t *t)
{
    return bm_headland_at_memory_access(&((fixture_t *) p)->memory, t);
}
static inline bm_status_t
io(void *p, bm_at_transfer_t *t)
{
    fixture_t  *f = p;
    bm_status_t s = bm_pcs286_io_access(&f->io, t);
    if (s == BM_STATUS_OK && f->during_io && !(t->bus.attributes & BM_BUS_TRANSACTION_DEBUG)) {
        /* Explicit synthetic peripheral-time advancement during synchronous
         * CPU I/O, not an instruction duration inferred from waits. */
        s = bm_engine_run_for(f->engine, 2000);
    }
    return s;
}
static inline bm_status_t
inta(void *p, unsigned phase, uint8_t *v, uint32_t *waits)
{
    *waits = 0;
    return bm_at_pic_acknowledge(((fixture_t *) p)->pic, phase, v);
}
static inline void
trace(void *p, const bm_286_boundary_t *b)
{
    fixture_t                *f = p;
    bm_pcs286_control_event_t nested;
    bm_pcs286_control_state_t s;
    ++f->trace_calls;
    assert(bm_pcs286_control_step(&f->control, &nested) == BM_STATUS_INVALID_STATE);
    assert(bm_pcs286_control_reset_cpu(&f->control) == BM_STATUS_INVALID_STATE);
    assert(bm_pcs286_control_state(&f->control, &s) == BM_STATUS_OK);
    if (f->trace_reset) {
        assert(s.reset_pending && arch(f).cs.base == 0 && arch(f).ip == 0x102);
        assert(b->kind == BM_286_BOUNDARY_INSTRUCTION);
    }
}
static inline bm_bus_transaction_t
transaction(bm_address_space_t space, unsigned address,
            bm_bus_operation_t op, unsigned value)
{
    bm_bus_transaction_t t = { 0 };
    t.space                = space;
    t.address              = address;
    t.operation            = op;
    t.size = t.alignment = 1;
    t.value              = value;
    return t;
}
static inline void
poke(fixture_t *f, unsigned offset, unsigned value)
{
    bm_bus_transaction_t t = transaction(BM_ADDRESS_MEMORY, offset, BM_BUS_WRITE, value);
    assert(bm_pcs286_memory_access(f->bytes, BM_PCS286_MEMORY_RAM, offset, &t) == BM_STATUS_OK);
}
static inline unsigned
peek(fixture_t *f, unsigned offset)
{
    bm_bus_transaction_t t = transaction(BM_ADDRESS_MEMORY, offset, BM_BUS_READ, 0);
    assert(bm_pcs286_memory_access(f->bytes, BM_PCS286_MEMORY_RAM, offset, &t) == BM_STATUS_OK);
    return (unsigned) t.value;
}
static inline unsigned
port(fixture_t *f, unsigned address, bm_bus_operation_t op, unsigned v)
{
    bm_bus_transaction_t t = transaction(BM_ADDRESS_IO, address, op, v);
    assert(bm_at_bus_cpu_access(f->bus, &t) == BM_STATUS_OK);
    return (unsigned) t.value;
}
static inline void
run(fixture_t *f, uint64_t ns)
{
    assert(bm_engine_run_for(f->engine, ns) == BM_STATUS_OK);
}
static inline void
command(fixture_t *f, unsigned v)
{
    port(f, 0x64, BM_BUS_WRITE, v);
    run(f, 2000);
}
static inline void
data(fixture_t *f, unsigned v)
{
    port(f, 0x60, BM_BUS_WRITE, v);
    run(f, 2000);
}
static inline void
output(fixture_t *f, unsigned v)
{
    command(f, 0xd1);
    data(f, v);
}
static inline void
start(fixture_t *f)
{
    static const uint8_t         image[BM_PCS286_FIRMWARE_BYTES] = { 0 };
    bm_host_services_t           h                               = bm_null_host_services();
    bm_pcs286_firmware_t         fw                              = { 0 };
    bm_headland_at_config_t      m                               = { 0 };
    bm_at_bus_config_t           bus                             = { 0 };
    bm_at_pic_config_t           pic                             = { 0 };
    bm_at_dma_config_t           dma                             = { 0 };
    bm_286_config_t              cpu                             = { 0 };
    bm_at_keyboard_pair_config_t k                               = { 0 };
    bm_engine_config_t           e                               = { 1, 4, 2 };
    bm_pcs286_io_config_t        io_config                       = { 0 };
    bm_pcs286_control_config_t   c;
    memset(f, 0, sizeof(*f));
    fw.image[0].data = image;
    fw.image[0].size = sizeof(image);
    assert(bm_pcs286_memory_create(&h, 0x200000, &fw, &f->bytes) == BM_STATUS_OK);
    assert(bm_gc103_memory_initialize(&f->routes, 0x200000) == BM_STATUS_OK);
    m.profile          = BM_HEADLAND_AT_LEGACY_GC103;
    m.routes           = &f->routes;
    m.backing          = backing;
    m.backing_context  = f;
    m.external_width   = 1;
    m.holes            = BM_HEADLAND_AT_HOLES_FF;
    m.protected_writes = BM_HEADLAND_AT_PROTECTED_IGNORE;
    m.timing           = BM_HEADLAND_AT_PROVISIONAL_SERVICE_CLOCK;
    m.service_clock    = (bm_clock_rate_t) { 8000000, 1 };
    assert(bm_headland_at_memory_initialize(&f->memory, &m) == BM_STATUS_OK);
    pic.master_base  = 0x20;
    pic.slave_base   = 0xa0;
    pic.cascade_line = 2;
    pic.intr         = bm_pcs286_control_intr;
    pic.intr_context = &f->control;
    assert(bm_at_pic_create(&h, &pic, &f->pic) == BM_STATUS_OK);
    dma.clock          = (bm_clock_rate_t) { 4000000, 1 };
    dma.memory         = memory;
    dma.memory_context = f;
    assert(bm_at_dma_create(&h, &dma, &f->dma) == BM_STATUS_OK);
    assert(bm_ioc02_legacy_initialize(&f->ioc) == BM_STATUS_OK);
    bus.memory         = memory;
    bus.io             = io;
    bus.decode_context = f;
    bus.cpu_clock      = (bm_clock_rate_t) { 12000000, 1 };
    bus.isa_clock      = m.service_clock;
    bus.hold           = bm_pcs286_control_hold;
    bus.hold_context   = &f->control;
    assert(bm_at_bus_create(&h, &bus, &f->bus) == BM_STATUS_OK);
    cpu.size              = sizeof(cpu);
    cpu.version           = BM_286_CONTRACT_VERSION;
    cpu.access            = bm_at_bus_cpu_access;
    cpu.access_context    = f->bus;
    cpu.interrupt_ack     = inta;
    cpu.interrupt_context = f;
    cpu.hold_ack          = bm_pcs286_control_hlda;
    cpu.bus_lock          = bm_pcs286_control_lock;
    cpu.pin_context       = &f->control;
    cpu.trace             = trace;
    cpu.trace_context     = f;
    assert(bm_286_create(&h, &cpu, &f->cpu) == BM_STATUS_OK);
    c = (bm_pcs286_control_config_t) { &f->cpu, f->bus, f->pic, &f->memory };
    assert(bm_pcs286_control_initialize(&f->control, &c) == BM_STATUS_OK);
    k.controller.data_port      = 0x60;
    k.controller.command_port   = 0x64;
    k.controller.irq            = bm_pcs286_control_irq1;
    k.controller.a20            = bm_pcs286_control_a20;
    k.controller.cpu_reset      = bm_pcs286_control_reset_line;
    k.controller.output_context = &f->control;
    k.controller.clock = k.keyboard.clock = (bm_clock_rate_t) { 1500000, 1 };
    k.controller.input_cycles             = 2;
    k.controller.output_cycles            = 3;
    k.controller.self_test_cycles         = 5;
    k.controller.pulse_cycles             = 6;
    k.controller.input_port               = 0xa0;
    k.controller.initial_output_port      = 0xc1;
    k.keyboard.power_on_cycles            = 3;
    k.keyboard.bat_cycles                 = 5;
    k.keyboard.byte_cycles                = 2;
    k.keyboard.reset_accept_cycles        = 3;
    assert(bm_at_keyboard_pair_create(&h, &k, &f->pair) == BM_STATUS_OK);
    assert(bm_engine_create_clocked(&h, &e, &f->engine) == BM_STATUS_OK);
    assert(bm_at_keyboard_pair_attach_clock(&h, f->engine, f->pair, &f->link) == BM_STATUS_OK);
    io_config.profile        = BM_PCS286_IO_LEGACY_GC103_AT;
    io_config.headland       = &f->routes;
    io_config.ioc02          = &f->ioc;
    io_config.pic            = f->pic;
    io_config.dma            = f->dma;
    io_config.timing         = BM_PCS286_IO_PROVISIONAL;
    io_config.service_clock  = m.service_clock;
    io_config.resource_count = 2;
    io_config.resources[0]   = (bm_pcs286_io_resource_t) { 0x60, 0x60, 1, bm_at_clock_link_io, f->link, 0 };
    io_config.resources[1]   = (bm_pcs286_io_resource_t) { 0x64, 0x64, 1, bm_at_clock_link_io, f->link, 0 };
    assert(bm_pcs286_io_initialize(&f->io, &io_config) == BM_STATUS_OK);
}
static inline void
done(fixture_t *f)
{
    bm_engine_destroy(f->engine);
    bm_at_clock_link_destroy(f->link);
    bm_at_keyboard_pair_destroy(f->pair);
    f->cpu.ops.destroy(f->cpu.context);
    bm_at_bus_destroy(f->bus);
    bm_at_pic_destroy(f->pic);
    bm_at_dma_destroy(f->dma);
    bm_pcs286_memory_destroy(f->bytes);
}
static inline void
program(fixture_t *f, const uint8_t *bytes, size_t n)
{
    bm_286_arch_state_t s = arch(f);
    for (size_t i = 0; i < n; ++i)
        poke(f, 0x100 + (unsigned) i, bytes[i]);
    s.cs.base     = 0;
    s.cs.selector = 0;
    s.ip          = 0x100;
    s.sp          = 0x800;
    assert(bm_286_set_arch_state(&f->cpu, &s) == BM_STATUS_OK);
}
static inline bm_pcs286_control_event_t
step(fixture_t *f)
{
    bm_pcs286_control_event_t e;
    assert(bm_pcs286_control_step(&f->control, &e) == BM_STATUS_OK);
    if (e.kind == BM_PCS286_CONTROL_CPU)
        assert(e.cpu.timing == BM_286_TIMING_UNKNOWN);
    return e;
}

#endif
