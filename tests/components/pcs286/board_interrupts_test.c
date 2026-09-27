/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Authored wiring diagnostic, NOT a PCS286 map, BIOS or timing model.
 */
#include <blumach/systems/pcs286_memory.h>
#include <blumach/components/at_bus.h>
#include <blumach/components/at_pic.h>
#include <blumach/platforms/null_host.h>
#include "failure_injection_host.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct board {
    bm_pcs286_memory_t *memory;
    bm_at_bus_t *bus;
    bm_at_pic_t *pic;
    bm_cpu_t cpu;
    unsigned accesses, acknowledgements, high_fetches, fail_ack;
    uint64_t waits;
    int fail_stack;
} board_t;

/* Guest instructions initialize IVT and BOTH real PICs through the AT bus.
 * No host-side import of CPU state or preset PIC registers. */
#define OUT8(port, value) 0xb0, value, 0xe6, port
#define STORE16(address_lo, address_hi, value_lo, value_hi) \
    0xc7, 0x06, address_lo, address_hi, value_lo, value_hi
static const uint8_t program[] = {
    0xfa,                         /* CLI */
    0xb8, 0x00, 0x20, 0x8e, 0xd0, /* MOV AX,2000h; MOV SS,AX */
    0xbc, 0x00, 0x10,             /* MOV SP,1000h */
    STORE16(0xc4,0,0,3), STORE16(0xc6,0,0,0), /* vector 31h -> 0000:0300 */
    STORE16(0xa4,1,0,4), STORE16(0xa6,1,0,0), /* vector 69h -> 0000:0400 */
    OUT8(0x20,0x11), OUT8(0xa0,0x11), /* edge triggered, ICW4 follows */
    OUT8(0x21,0x30), OUT8(0xa1,0x68), /* vector bases */
    OUT8(0x21,4), OUT8(0xa1,2),       /* cascade on master IRQ2 */
    OUT8(0x21,1), OUT8(0xa1,1),       /* x86, separate explicit EOI */
    OUT8(0x21,0xf9), OUT8(0xa1,0xfd), /* unmask IRQ1/2 and IRQ9 */
    0xb8,0x34,0x12, 0xfb,0xf4,       /* AX sentinel, STI, HLT */
    0x90,0xf4                       /* resume: NOP, HLT */
};
static const uint8_t master_handler[] = {
    0x50, 0xb8,0xfe,0xca, 0xa3,0x00,0x06, /* PUSH AX; marker at 0600 */
    OUT8(0x20,0x20), 0x58,0xcf             /* master EOI; POP AX; IRET */
};
static const uint8_t slave_handler[] = {
    0x50, 0xb8,0xef,0xbe, 0xa3,0x02,0x06, /* marker at 0602 */
    OUT8(0xa0,0x20), OUT8(0x20,0x20), 0x58,0xcf
};
#undef OUT8
#undef STORE16

static bm_286_arch_state_t cpu_state(board_t *b)
{
    bm_286_arch_state_t s;
    assert(bm_286_get_arch_state(&b->cpu, &s) == BM_STATUS_OK);
    return s;
}
static bm_at_pic_state_t pic_state(board_t *b)
{
    bm_at_pic_state_t s;
    assert(bm_at_pic_state(b->pic, &s) == BM_STATUS_OK);
    return s;
}
static void same_segment(const bm_286_segment_state_t *a, const bm_286_segment_state_t *b)
{
    assert(a->selector == b->selector && a->base == b->base && a->limit == b->limit);
    assert(a->access == b->access && a->valid == b->valid);
}
static void same_cpu(const bm_286_arch_state_t *a, const bm_286_arch_state_t *b)
{
    /* Struct assignment/return need not retain padding bytes in optimized C. */
#define SAME(field) assert(a->field == b->field)
    SAME(size); SAME(version); SAME(ax); SAME(cx); SAME(dx); SAME(bx);
    SAME(sp); SAME(bp); SAME(si); SAME(di); SAME(ip); SAME(flags); SAME(msw);
    SAME(gdtr.base); SAME(gdtr.limit); SAME(idtr.base); SAME(idtr.limit);
    SAME(cpl); SAME(halted); SAME(shutdown); SAME(interrupt_shadow);
    SAME(nmi_blocked); SAME(nmi_pending); SAME(trap_pending);
#undef SAME
    same_segment(&a->es, &b->es); same_segment(&a->cs, &b->cs);
    same_segment(&a->ss, &b->ss); same_segment(&a->ds, &b->ds);
    same_segment(&a->ldtr, &b->ldtr); same_segment(&a->tr, &b->tr);
}
static void same_pic(const bm_at_pic_state_t *a, const bm_at_pic_state_t *b)
{
    for (unsigned i = 0; i < 2; ++i) {
        assert(a->irr[i] == b->irr[i] && a->isr[i] == b->isr[i]);
        assert(a->imr[i] == b->imr[i] && a->input_lines[i] == b->input_lines[i]);
        assert(a->vector_base[i] == b->vector_base[i]);
    }
    assert(a->acknowledge_phase == b->acknowledge_phase && a->intr == b->intr);
}
static void intr_changed(void *context, int high)
{
    board_t *b = context;
    assert(b->cpu.ops.signal(b->cpu.context, BM_286_SIGNAL_INTR, high) == BM_STATUS_OK);
}
static void hold_changed(void *context, int high)
{
    board_t *b = context;
    assert(b->cpu.ops.signal(b->cpu.context, BM_286_SIGNAL_HOLD, high) == BM_STATUS_OK);
}
static void hlda_changed(void *context, int high)
{
    board_t *b = context;
    assert(bm_at_bus_hold_ack(b->bus, high) == BM_STATUS_OK);
}
static void lock_changed(void *context, int high)
{
    board_t *b = context;
    assert(bm_at_bus_set_lock(b->bus, high) == BM_STATUS_OK);
}
static bm_status_t memory_access(void *context, bm_at_transfer_t *transfer)
{
    board_t *b = context;
    bm_bus_transaction_t *t = &transfer->bus;
    bm_status_t result;
    int debug = (t->attributes & BM_BUS_TRANSACTION_DEBUG) != 0;
    if (!debug) ++b->accesses;
    if (!debug && b->fail_stack && t->operation == BM_BUS_WRITE &&
        t->address >= 0x20000 && t->address < 0x21000)
        return BM_STATUS_DEVICE_ERROR;
    /* Deliberately only two test windows. No undocumented Headland aliases. */
    if (t->address < 0x100000 && t->size <= 0x100000 - t->address)
        result = bm_pcs286_memory_access(b->memory, BM_PCS286_MEMORY_RAM,
                                         (uint32_t)t->address, t);
    else if (t->address >= 0xfe0000 && t->address < 0x1000000 &&
             t->size <= 0x1000000 - t->address) {
        if (!debug && t->operation == BM_BUS_FETCH) ++b->high_fetches;
        result = bm_pcs286_memory_access(b->memory, BM_PCS286_MEMORY_ROM,
                                         (uint32_t)(t->address - 0xfe0000), t);
    } else result = BM_STATUS_UNMAPPED;
    if (result == BM_STATUS_OK && !debug) {
        /* Authored endpoint wait fixture, already in requester clocks. */
        t->wait_states = 3;
        if (transfer->master == BM_AT_MASTER_CPU) b->waits += 3;
    }
    return result;
}
static bm_status_t io_access(void *context, bm_at_transfer_t *transfer)
{
    board_t *b = context;
    bm_status_t result = bm_at_pic_io(b->pic, &transfer->bus);
    if (!(transfer->bus.attributes & BM_BUS_TRANSACTION_DEBUG)) {
        ++b->accesses;
        if (result == BM_STATUS_OK) {
            transfer->bus.wait_states = 7;
            if (transfer->master == BM_AT_MASTER_CPU) b->waits += 7;
        }
    }
    return result;
}
static bm_status_t interrupt_ack(void *context, unsigned phase, uint8_t *v, uint32_t *waits)
{
    board_t *b = context;
    bm_status_t result;
    assert(phase == (b->acknowledgements & 1U) && !*waits);
    ++b->acknowledgements;
    if (b->acknowledgements == b->fail_ack) return BM_STATUS_DEVICE_ERROR;
    result = bm_at_pic_acknowledge(b->pic, phase, v);
    if (result == BM_STATUS_OK) {
        *waits = phase ? 4U : 2U;
        b->waits += *waits;
    }
    return result;
}
static void destroy(board_t *b)
{
    /* Output callbacks still have live consumers while disconnecting. */
    if (b->pic) bm_at_pic_reset(b->pic);
    bm_at_pic_destroy(b->pic);
    bm_at_bus_destroy(b->bus);
    if (b->cpu.context) b->cpu.ops.destroy(b->cpu.context);
    bm_pcs286_memory_destroy(b->memory);
    memset(b, 0, sizeof(*b));
}
static bm_status_t create(board_t *b, const bm_host_services_t *host, const uint8_t *image)
{
    bm_pcs286_firmware_t firmware = {0};
    bm_at_bus_config_t bus = {0};
    bm_286_config_t cpu = {0};
    bm_at_pic_config_t pic = {0x20,0xa0,2,intr_changed,b};
    bm_status_t result;
    memset(b, 0, sizeof(*b));
    firmware.image[0].data = image; firmware.image[0].size = BM_PCS286_FIRMWARE_BYTES;
    result = bm_pcs286_memory_create(host, 0x100000, &firmware, &b->memory);
    if (result != BM_STATUS_OK) goto failed;
    bus.cpu_clock = (bm_clock_rate_t){12000000,1};
    bus.isa_clock = (bm_clock_rate_t){8000000,1}; /* test rates, not board evidence */
    bus.memory = memory_access; bus.io = io_access; bus.decode_context = b;
    bus.hold = hold_changed; bus.hold_context = b;
    result = bm_at_bus_create(host, &bus, &b->bus);
    if (result != BM_STATUS_OK) goto failed;
    cpu.size = sizeof(cpu); cpu.version = BM_286_CONTRACT_VERSION;
    cpu.access = bm_at_bus_cpu_access; cpu.access_context = b->bus;
    cpu.interrupt_ack = interrupt_ack; cpu.interrupt_context = b;
    cpu.hold_ack = hlda_changed; cpu.pin_context = b;
    cpu.bus_lock = lock_changed;
    result = bm_286_create(host, &cpu, &b->cpu);
    if (result != BM_STATUS_OK) goto failed;
    result = bm_at_pic_create(host, &pic, &b->pic);
    if (result == BM_STATUS_OK) return result;
failed:
    destroy(b);
    return result;
}
static bm_bus_transaction_t transaction(uint32_t address, bm_bus_operation_t operation,
                                         uint64_t value)
{
    bm_bus_transaction_t t = {0};
    t.space = BM_ADDRESS_MEMORY; t.address = address; t.operation = operation;
    t.size = 2; t.alignment = 2; t.endianness = BM_ENDIAN_LITTLE; t.value = value;
    return t;
}
static uint16_t inspect_word(board_t *b, uint32_t address)
{
    bm_bus_transaction_t t = transaction(address, BM_BUS_READ, 0);
    t.attributes = BM_BUS_TRANSACTION_DEBUG;
    assert(bm_at_bus_cpu_access(b->bus, &t) == BM_STATUS_OK && !t.wait_states);
    return (uint16_t)t.value;
}
static void load(board_t *b, uint32_t address, const uint8_t *code, size_t size)
{
    for (size_t i = 0; i < size; ++i) {
        bm_bus_transaction_t t = transaction(address + (uint32_t)i, BM_BUS_WRITE, code[i]);
        t.size = t.alignment = 1;
        assert(bm_pcs286_memory_access(b->memory, BM_PCS286_MEMORY_RAM,
                                      address + (uint32_t)i, &t) == BM_STATUS_OK);
    }
}
static bm_286_boundary_t step(board_t *b)
{
    bm_286_boundary_t boundary;
    uint64_t before = b->waits;
    assert(bm_286_step(&b->cpu, &boundary) == BM_STATUS_OK);
    assert(boundary.timing == BM_286_TIMING_UNKNOWN);
    assert(boundary.bus_wait_cycles == b->waits - before);
    assert(boundary.cpu_cycles == boundary.bus_wait_cycles); /* lower bound only */
    return boundary;
}
static void prepare(board_t *b)
{
    load(b, 0x100, program, sizeof(program));
    load(b, 0x300, master_handler, sizeof(master_handler));
    load(b, 0x400, slave_handler, sizeof(slave_handler));
    assert(step(b).instruction_address == 0xfffff0);
    for (unsigned limit = 0; !cpu_state(b).halted; ++limit) {
        assert(limit < 100); step(b);
    }
    assert(cpu_state(b).ax == 0x1234 && cpu_state(b).sp == 0x1000);
    assert(cpu_state(b).ip == 0x100 + sizeof(program) - 2U);
    assert(b->high_fetches == 5 && !b->acknowledgements);
    assert(inspect_word(b, 0xc4) == 0x300 && inspect_word(b, 0x1a4) == 0x400);
}
static void irq(board_t *b, unsigned pin, int high)
{
    assert(bm_at_pic_set_irq(b->pic, pin, high) == BM_STATUS_OK);
}
static void finish_handler(board_t *b, uint16_t resume_ip, uint16_t marker_address,
                             uint16_t marker, unsigned instructions)
{
    bm_at_pic_state_t p;
    for (unsigned i = 0; i < instructions; ++i) step(b);
    assert(inspect_word(b, marker_address) == marker);
    assert(cpu_state(b).ip == resume_ip && cpu_state(b).sp == 0x1000);
    assert(cpu_state(b).ax == 0x1234 && (cpu_state(b).flags & 0x200));
    p = pic_state(b); assert(!p.isr[0] && !p.isr[1]);
}
static void roundtrip_and_hold(board_t *b)
{
    bm_286_boundary_t boundary;
    bm_at_pic_state_t p, before;
    uint16_t resume_ip = cpu_state(b).ip;
    unsigned accesses;
    bm_at_transfer_t external = {0};
    bm_bus_transaction_t t;
    irq(b, 9, 1);
    assert(bm_at_bus_request(b->bus, BM_AT_MASTER_ISA, 1) == BM_STATUS_OK);
    accesses = b->accesses;
    assert(bm_286_step(&b->cpu, &boundary) == BM_STATUS_IDLE);
    assert(boundary.kind == BM_286_BOUNDARY_HOLD && b->accesses == accesses && !b->acknowledgements);
    t = transaction(0x600, BM_BUS_READ, 0x55);
    assert(bm_at_bus_cpu_access(b->bus, &t) == BM_STATUS_IDLE && t.value == 0x55);
    /* External requester exercises grant only: this is NOT a DMA chip. */
    external.master = BM_AT_MASTER_ISA;
    external.requester_clock = (bm_clock_rate_t){8000000,1};
    external.bus = transaction(0x700, BM_BUS_WRITE, 0x55aa);
    assert(bm_at_bus_access(b->bus, &external) == BM_STATUS_OK && external.bus.wait_states == 3);
    assert(inspect_word(b, 0x700) == 0x55aa);
    before = pic_state(b);
    t = transaction(0xa1, BM_BUS_READ, 0); t.size = t.alignment = 1;
    t.space = BM_ADDRESS_IO; t.attributes = BM_BUS_TRANSACTION_DEBUG;
    assert(bm_at_bus_cpu_access(b->bus, &t) == BM_STATUS_OK && t.value == 0xfd && !t.wait_states);
    p = pic_state(b); same_pic(&before, &p);
    t.operation = BM_BUS_WRITE; t.value = 0xff;
    assert(bm_at_bus_cpu_access(b->bus, &t) == BM_STATUS_UNSUPPORTED);
    p = pic_state(b); same_pic(&before, &p);
    assert(bm_at_bus_request(b->bus, BM_AT_MASTER_ISA, 0) == BM_STATUS_OK);
    boundary = step(b);
    assert(boundary.kind == BM_286_BOUNDARY_INTERRUPT && boundary.vector == 0x69);
    assert(b->acknowledgements == 2 && !cpu_state(b).halted);
    assert(inspect_word(b, 0x20ffa) == resume_ip);
    p = pic_state(b); assert(p.isr[0] == 4 && p.isr[1] == 2);
    irq(b, 9, 0);
    finish_handler(b, resume_ip, 0x602, 0xbeef, 9);
    step(b); step(b); /* NOP, second HLT */
    resume_ip = cpu_state(b).ip;
    assert(cpu_state(b).halted);
    irq(b, 1, 1); boundary = step(b);
    assert(boundary.vector == 0x31 && b->acknowledgements == 4);
    p = pic_state(b); assert(p.isr[0] == 2 && !p.isr[1]);
    irq(b, 1, 0);
    finish_handler(b, resume_ip, 0x600, 0xcafe, 7);
}
static void reset(board_t *b)
{
    /* Synthetic lifecycle policy; RAM retained, no physical-reset claim. */
    bm_at_pic_reset(b->pic); bm_at_bus_reset(b->bus);
    assert(b->cpu.ops.reset(b->cpu.context) == BM_STATUS_OK);
    b->accesses = b->acknowledgements = b->high_fetches = b->fail_ack = 0;
    b->waits = 0; b->fail_stack = 0;
}
static void failure_and_recovery(board_t *b)
{
    for (unsigned mode = 0; mode < 2; ++mode) {
        bm_286_boundary_t boundary;
        bm_at_pic_state_t p;
        bm_286_arch_state_t before, after;
        unsigned accesses, acknowledgements;
        reset(b); prepare(b); before = cpu_state(b);
        irq(b, 9, 1);
        if (mode == 0) b->fail_ack = 2; /* phase zero already changed PIC */
        else b->fail_stack = 1;
        assert(bm_286_step(&b->cpu, &boundary) == BM_STATUS_DEVICE_ERROR);
        after = cpu_state(b); same_cpu(&before, &after);
        p = pic_state(b); assert(p.isr[0] == 4 && p.isr[1] == 2);
        assert(p.acknowledge_phase == (mode == 0 ? 1 : 0));
        accesses = b->accesses; acknowledgements = b->acknowledgements;
        assert(bm_286_step(&b->cpu, &boundary) == BM_STATUS_INVALID_STATE);
        assert(b->accesses == accesses && b->acknowledgements == acknowledgements);
        /* CPU reset cannot undo already accepted PIC state. */
        assert(b->cpu.ops.reset(b->cpu.context) == BM_STATUS_OK);
        p = pic_state(b); assert(p.isr[0] == 4 && p.isr[1] == 2);
        reset(b); p = pic_state(b);
        assert(!p.isr[0] && !p.isr[1] && !p.acknowledge_phase && !p.intr);
        assert(inspect_word(b, 0x700) == 0x55aa); /* RAM survives composition reset */
        prepare(b); roundtrip_and_hold(b);
    }
}
int main(void)
{
    uint8_t *image = calloc(BM_PCS286_FIRMWARE_BYTES, 1);
    const uint8_t trampoline[] = {0xea,0x00,0x01,0,0};
    failure_injection_host_t tracker;
    bm_host_services_t host;
    board_t b, other;
    size_t allocations = 0;
    assert(image);
    memcpy(image + BM_PCS286_FIRMWARE_BYTES - 16U, trampoline, sizeof(trampoline));
    for (size_t fail = 0; fail < 16; ++fail) {
        bm_status_t status;
        failure_injection_host_initialize(&tracker);
        failure_injection_host_fail_on(&tracker, fail);
        host = failure_injection_host_services(&tracker);
        status = create(&b, &host, image);
        if (status == BM_STATUS_OK) { allocations = tracker.allocation_calls; destroy(&b); }
        else assert(status == BM_STATUS_OUT_OF_MEMORY);
        assert(!b.memory && !b.bus && !b.pic && !b.cpu.context && !tracker.outstanding_allocations);
        if (status == BM_STATUS_OK) break;
    }
    assert(allocations == 6);
    failure_injection_host_initialize(&tracker); host = failure_injection_host_services(&tracker);
    assert(create(&b, &host, image) == BM_STATUS_OK);
    assert(create(&other, &host, image) == BM_STATUS_OK);
    prepare(&b); roundtrip_and_hold(&b); failure_and_recovery(&b);
    assert(cpu_state(&other).ip == 0xfff0 && !inspect_word(&other, 0x600));
    assert(!pic_state(&other).intr && !other.acknowledgements && !other.accesses);
    /* Teardown with live HOLD and pending IRQ must disconnect safely. */
    irq(&b, 9, 1);
    assert(bm_at_bus_request(b.bus, BM_AT_MASTER_ISA, 1) == BM_STATUS_OK);
    {
        bm_286_boundary_t boundary;
        assert(bm_286_step(&b.cpu, &boundary) == BM_STATUS_IDLE);
        assert(boundary.kind == BM_286_BOUNDARY_HOLD);
    }
    destroy(&b); destroy(&other);
    assert(!tracker.outstanding_allocations);
    free(image);
    return 0;
}
