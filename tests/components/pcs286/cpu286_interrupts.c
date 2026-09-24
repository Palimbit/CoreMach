/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Authored architectural fixtures, not silicon timing or PCS286 firmware.
 */
#include <blumach/components/cpu_80286.h>
#include <blumach/components/at_pic.h>
#include <blumach/platforms/null_host.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct fixture {
    unsigned locked;
    bm_cpu_t cpu;
    bm_at_pic_t *pic;
    uint8_t *ram;
    bm_bus_transaction_t trace[128];
    unsigned count, fail_at, acks, fail_ack, new_nmi_at, boundaries;
    int use_pic;
} fixture_t;
/* No competing master in this fixture; track the exclusion contract. */
static void lock_changed(void *context, int high)
{
    fixture_t *f = context;
    assert(f->locked != (unsigned) high);
    f->locked = (unsigned) high;
}

static bm_286_arch_state_t state(fixture_t *f)
{
    bm_286_arch_state_t s;
    assert(bm_286_get_arch_state(&f->cpu, &s) == BM_STATUS_OK);
    return s;
}
static void signal_cpu(fixture_t *f, bm_286_signal_t signal, int high)
{
    assert(f->cpu.ops.signal(f->cpu.context, signal, high) == BM_STATUS_OK);
}
static void intr(void *context, int high)
{
    signal_cpu(context, BM_286_SIGNAL_INTR, high);
}
static void trace_boundary(void *context, const bm_286_boundary_t *b)
{
    fixture_t *f = context;
    assert(b->timing == BM_286_TIMING_UNKNOWN);
    ++f->boundaries;
}
static bm_status_t access_bus(void *context, bm_bus_transaction_t *t)
{
    fixture_t *f = context;
    assert(f->count < 128 && !t->wait_states);
    f->trace[f->count++] = *t;
    if (f->count == f->fail_at) return BM_STATUS_DEVICE_ERROR;
    if (f->count == f->new_nmi_at) {
        signal_cpu(f, BM_286_SIGNAL_NMI, 0);
        signal_cpu(f, BM_286_SIGNAL_NMI, 1);
    }
    if (t->space == BM_ADDRESS_IO) {
        bm_status_t status = bm_at_pic_io(f->pic, t);
        t->wait_states = 2;
        return status;
    }
    assert(t->address + t->size <= 0x1000000U);
    if (t->operation != BM_BUS_WRITE) t->value = 0;
    for (unsigned i = 0; i < t->size; ++i) {
        size_t address = (size_t) t->address + i;
        if (t->operation == BM_BUS_WRITE)
            f->ram[address] = (uint8_t) (t->value >> (i * 8U));
        else t->value |= (uint64_t) f->ram[address] << (i * 8U);
    }
    t->wait_states = 2;
    return BM_STATUS_OK;
}
static bm_status_t acknowledge(void *context, unsigned phase, uint8_t *v, uint32_t *waits)
{
    fixture_t *f = context;
    assert(phase == (f->acks & 1U) && *waits == 0);
    ++f->acks;
    if (f->acks == f->fail_ack) return BM_STATUS_DEVICE_ERROR;
    *waits = phase ? 5U : 3U;
    if (f->use_pic) return bm_at_pic_acknowledge(f->pic, phase, v);
    *v = phase ? 0x31 : 0xee; /* Phase zero value must not select IVT. */
    return BM_STATUS_OK;
}
static void word(fixture_t *f, uint32_t address, uint16_t value)
{
    f->ram[address & 0xffffffU] = (uint8_t) value;
    f->ram[(address + 1U) & 0xffffffU] = (uint8_t) (value >> 8);
}
static uint16_t read_word(fixture_t *f, uint32_t address)
{
    return (uint16_t) (f->ram[address] | ((unsigned) f->ram[address + 1U] << 8));
}
static void vector(fixture_t *f, unsigned v, uint32_t base)
{
    word(f, base + v * 4U, 0x200); word(f, base + v * 4U + 2U, 0x4000);
}
static bm_286_arch_state_t setup(fixture_t *f)
{
    bm_286_arch_state_t s;
    assert(f->cpu.ops.reset(f->cpu.context) == BM_STATUS_OK);
    bm_at_pic_reset(f->pic);
    memset(f->ram, 0, 0x1000000U);
    f->count = f->fail_at = f->acks = f->fail_ack = f->new_nmi_at = f->boundaries = 0;
    f->use_pic = 0;
    s = state(f);
    s.cs.selector = 0x3000; s.cs.base = 0x30000; s.ip = 0x100;
    s.ss.selector = 0x1000; s.ss.base = 0x10000; s.sp = 0x800;
    s.flags = 0x0ed7; /* TF=0, IF=1 */
    f->ram[0x30100] = 0x90;
    f->ram[0x40200] = 0xcf;
    vector(f, 1, 0); vector(f, 2, 0); vector(f, 0x31, 0);
    return s;
}
static void set(fixture_t *f, const bm_286_arch_state_t *s)
{
    assert(bm_286_set_arch_state(&f->cpu, s) == BM_STATUS_OK);
}
static bm_286_boundary_t step(fixture_t *f)
{
    bm_286_boundary_t b;
    assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK);
    return b;
}
static void entry_and_return(fixture_t *f)
{
    for (unsigned odd = 0; odd < 2; ++odd) {
        bm_286_arch_state_t s = setup(f), a;
        bm_286_boundary_t b;
        s.sp += (uint16_t) odd; s.flags |= 0x100; /* Save TF then clear on entry. */
        s.idtr.base = 0xfffff1; /* odd IVT with 24-bit physical wrapping */
        vector(f, 0x31, s.idtr.base); set(f, &s);
        signal_cpu(f, BM_286_SIGNAL_INTR, 1); b = step(f); a = state(f);
        assert(b.kind == BM_286_BOUNDARY_INTERRUPT && b.has_vector && b.vector == 0x31);
        assert(b.instruction_ip == s.ip && b.instruction_address == 0x30100);
        assert(b.bus_wait_cycles == 8U + f->count * 2U && b.cpu_cycles == b.bus_wait_cycles);
        assert(f->acks == 2 && a.ip == 0x200 && a.cs.selector == 0x4000 && a.cs.base == 0x40000);
        assert(a.sp == s.sp - 6 && a.flags == (s.flags & ~0x300U));
        assert(read_word(f, s.ss.base + s.sp - 2U) == s.flags);
        assert(read_word(f, s.ss.base + s.sp - 4U) == s.cs.selector);
        assert(read_word(f, s.ss.base + s.sp - 6U) == s.ip);
        for (unsigned i = 0; i < f->count; ++i) assert(f->trace[i].operation != BM_BUS_FETCH);
        signal_cpu(f, BM_286_SIGNAL_INTR, 0); b = step(f); a = state(f);
        assert(b.kind == BM_286_BOUNDARY_INSTRUCTION && !b.has_vector);
        assert(a.ip == s.ip && a.cs.selector == s.cs.selector && a.sp == s.sp && a.flags == s.flags);
        assert(!a.trap_pending); /* TF restored by IRET is not sampled retroactively. */
        step(f); assert(state(f).trap_pending);
    }
    {
        bm_286_arch_state_t s = setup(f);
        s.sp = 0; set(f, &s); signal_cpu(f, BM_286_SIGNAL_INTR, 1);
        step(f); assert(state(f).sp == 0xfffa);
        signal_cpu(f, BM_286_SIGNAL_INTR, 0); step(f); assert(state(f).sp == 0);
    }
    {
        bm_286_arch_state_t s = setup(f);
        f->ram[0x30100] = 0xcf;
        word(f, s.ss.base + s.sp, 0x1234); word(f, s.ss.base + s.sp + 2, 0xabcd);
        word(f, s.ss.base + s.sp + 4, 0xffff); s.flags = 2;
        s.nmi_blocked = 1; set(f, &s); step(f);
        assert(state(f).flags == 0x0fd7 && state(f).ip == 0x1234);
        assert(state(f).cs.base == 0xabcd0 && !state(f).nmi_blocked);
    }
}
static void inhibition_and_priority(fixture_t *f)
{
    bm_286_arch_state_t s = setup(f);
    bm_286_boundary_t b;
    set(f, &s); signal_cpu(f, BM_286_SIGNAL_INTR, 1);
    signal_cpu(f, BM_286_SIGNAL_HOLD, 1);
    assert(bm_286_step(&f->cpu, &b) == BM_STATUS_IDLE);
    assert(b.kind == BM_286_BOUNDARY_HOLD && !f->acks && !f->count);
    signal_cpu(f, BM_286_SIGNAL_HOLD, 0);
    assert(step(f).vector == 0x31);
    s = setup(f);
    s.flags = 2; f->ram[0x30100] = 0xfb; f->ram[0x30101] = 0xf4;
    set(f, &s); signal_cpu(f, BM_286_SIGNAL_INTR, 1);
    step(f); assert(state(f).interrupt_shadow == BM_286_SHADOW_INTR_ONLY);
    step(f); assert(state(f).halted && state(f).ip == 0x102 && !f->acks);
    b = step(f); assert(b.vector == 0x31 && !state(f).halted);
    signal_cpu(f, BM_286_SIGNAL_INTR, 0); step(f); assert(state(f).ip == 0x102);
    s = setup(f); f->ram[0x30100] = 0xfa; set(f, &s); step(f);
    signal_cpu(f, BM_286_SIGNAL_INTR, 1); f->ram[0x30101] = 0x90; step(f);
    assert(!f->acks && !(state(f).flags & 0x200));

    s = setup(f); s.trap_pending = s.nmi_pending = 1; set(f, &s);
    signal_cpu(f, BM_286_SIGNAL_INTR, 1); b = step(f);
    assert(b.kind == BM_286_BOUNDARY_EXCEPTION && b.vector == 1 && !f->acks);
    assert(!state(f).trap_pending && state(f).nmi_pending);
    b = step(f); assert(b.vector == 2 && !f->acks && state(f).nmi_blocked);
    /* A continuously high NMI does not generate another edge. */
    signal_cpu(f, BM_286_SIGNAL_NMI, 1);
    s = state(f); s.nmi_pending = 0; set(f, &s);
    signal_cpu(f, BM_286_SIGNAL_NMI, 1); assert(!state(f).nmi_pending);
    signal_cpu(f, BM_286_SIGNAL_NMI, 0); signal_cpu(f, BM_286_SIGNAL_NMI, 1);
    b = step(f); assert(b.kind == BM_286_BOUNDARY_INSTRUCTION && !state(f).nmi_blocked);
    b = step(f); assert(b.vector == 2 && state(f).nmi_blocked);

    s = setup(f); s.nmi_pending = 1; set(f, &s); f->new_nmi_at = 1;
    step(f); assert(state(f).nmi_pending && state(f).nmi_blocked);
    step(f); b = step(f); assert(b.vector == 2); /* new edge not lost during entry */
}
static void failures(fixture_t *f)
{
    for (unsigned returning = 0; returning < 2; ++returning)
        for (unsigned odd = 0; odd < 2; ++odd)
            for (unsigned fail = 1; fail <= (returning ? 4U + odd * 3U : 5U + odd * 3U); ++fail) {
                bm_286_arch_state_t s = setup(f), after;
                bm_286_boundary_t b;
                s.sp += (uint16_t) odd;
                if (returning) f->ram[0x30100] = 0xcf;
                set(f, &s); f->fail_at = fail;
                if (!returning) signal_cpu(f, BM_286_SIGNAL_INTR, 1);
                assert(bm_286_step(&f->cpu, &b) == BM_STATUS_DEVICE_ERROR);
                after = state(f); assert(memcmp(&after, &s, sizeof(s)) == 0);
                assert(!f->boundaries && !b.has_vector);
                for (unsigned j = 0; j + 1U < fail; ++j) {
                    const bm_bus_transaction_t *t = &f->trace[j];
                    if (t->operation == BM_BUS_WRITE)
                        for (unsigned k = 0; k < t->size; ++k)
                            assert(f->ram[(size_t) t->address + k] ==
                                   (uint8_t) (t->value >> (k * 8U)));
                }
                assert(bm_286_step(&f->cpu, &b) == BM_STATUS_INVALID_STATE && f->count == fail);
                assert(f->acks == (returning ? 0U : 2U));
            }
    for (unsigned phase = 1; phase <= 2; ++phase) {
        bm_286_arch_state_t s = setup(f), after; bm_286_boundary_t b;
        set(f, &s); f->fail_ack = phase; signal_cpu(f, BM_286_SIGNAL_INTR, 1);
        assert(bm_286_step(&f->cpu, &b) == BM_STATUS_DEVICE_ERROR && !f->count);
        after = state(f); assert(memcmp(&after, &s, sizeof(s)) == 0);
        assert(bm_286_step(&f->cpu, &b) == BM_STATUS_INVALID_STATE && f->acks == phase);
    }
    for (unsigned bad = 0; bad < 4; ++bad) {
        bm_286_arch_state_t s = setup(f), after; bm_286_boundary_t b;
        s.nmi_pending = 1;
        if (bad == 0) s.idtr.limit = 10;
        if (bad == 1) s.sp = 1;
        if (bad == 2) s.ss.valid = 0;
        if (bad == 3) s.msw |= 1;
        set(f, &s);
        if (bad == 0 || bad == 1) {
            assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK && !f->count && !f->acks);
            after = state(f); s.shutdown = 1; s.nmi_pending = 0; s.nmi_blocked = 1;
            assert(memcmp(&after, &s, sizeof(s)) == 0);
            assert(b.kind == BM_286_BOUNDARY_SHUTDOWN && !b.has_vector);
            continue;
        }
        assert(bm_286_step(&f->cpu, &b) == BM_STATUS_UNSUPPORTED && !f->count && !f->acks);
        after = state(f); assert(memcmp(&after, &s, sizeof(s)) == 0);
    }
    {
        bm_286_arch_state_t s = setup(f), after; bm_286_boundary_t b;
        s.nmi_pending = 1; set(f, &s); f->fail_at = 4;
        assert(bm_286_step(&f->cpu, &b) == BM_STATUS_DEVICE_ERROR);
        after = state(f); assert(memcmp(&after, &s, sizeof(s)) == 0);
        assert(!f->acks && !after.nmi_blocked && after.nmi_pending);
    }
}
static void wr(fixture_t *f, uint16_t port, uint8_t value)
{
    bm_bus_transaction_t t = {0};
    t.space = BM_ADDRESS_IO; t.operation = BM_BUS_WRITE;
    t.address = port; t.size = 1; t.value = value;
    assert(bm_at_pic_io(f->pic, &t) == BM_STATUS_OK);
}
static void pic_roundtrip(fixture_t *f)
{
    bm_286_arch_state_t s = setup(f);
    bm_at_pic_state_t pic;
    const uint8_t handler[] = {0xb0,0x20,0xe6,0xa0,0xe6,0x20,0xcf};
    f->use_pic = 1; set(f, &s);
    wr(f, 0x20, 0x11); wr(f, 0xa0, 0x11);
    wr(f, 0x21, 0x30); wr(f, 0xa1, 0x68);
    wr(f, 0x21, 4); wr(f, 0xa1, 2);
    wr(f, 0x21, 1); wr(f, 0xa1, 1);
    wr(f, 0x21, 0xfb); wr(f, 0xa1, 0xfd);
    vector(f, 0x69, 0); memcpy(f->ram + 0x40200, handler, sizeof(handler));
    assert(bm_at_pic_set_irq(f->pic, 9, 1) == BM_STATUS_OK);
    assert(step(f).vector == 0x69 && f->acks == 2);
    assert(bm_at_pic_state(f->pic, &pic) == BM_STATUS_OK);
    assert(pic.isr[0] == 4 && pic.isr[1] == 2);
    step(f); step(f);
    assert(bm_at_pic_state(f->pic, &pic) == BM_STATUS_OK);
    assert(pic.isr[0] == 4 && pic.isr[1] == 0); /* slave EOI does not clear master */
    step(f); step(f);
    assert(bm_at_pic_state(f->pic, &pic) == BM_STATUS_OK);
    assert(!pic.isr[0] && !pic.isr[1] && state(f).ip == s.ip && state(f).sp == s.sp);
    step(f); assert(state(f).ip == s.ip + 1U && f->acks == 2);
}
int main(void)
{
    fixture_t f = {0}; bm_286_config_t config = {0};
    bm_host_services_t host = bm_null_host_services();
    bm_at_pic_config_t pc = {0x20,0xa0,2,intr,&f};
    f.ram = calloc(0x1000000U, 1); assert(f.ram);
    config.size = sizeof(config); config.version = BM_286_CONTRACT_VERSION;
    config.access = access_bus; config.access_context = &f;
    config.interrupt_ack = acknowledge; config.interrupt_context = &f;
    config.bus_lock = lock_changed; config.pin_context = &f;
    config.trace = trace_boundary; config.trace_context = &f;
    assert(bm_286_create(&host, &config, &f.cpu) == BM_STATUS_OK);
    assert(bm_at_pic_create(&host, &pc, &f.pic) == BM_STATUS_OK);
    entry_and_return(&f); inhibition_and_priority(&f); failures(&f); pic_roundtrip(&f);
    bm_at_pic_destroy(f.pic); f.cpu.ops.destroy(f.cpu.context); free(f.ram);
    return 0;
}
