/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Authored architectural tests, not physical timing or hardware captures.
 */
#include <blumach/components/cpu_80286.h>
#include <blumach/platforms/null_host.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct fixture {
    unsigned locked;
    bm_cpu_t cpu;
    uint8_t *ram;
    bm_bus_transaction_t trace[64];
    unsigned count, fail_at, acknowledgements;
} fixture_t;
/* No competing master in this fixture; track the exclusion contract. */
static void lock_changed(void *context, int high)
{
    fixture_t *f = context;
    assert(f->locked != (unsigned) high);
    f->locked = (unsigned) high;
}
static bm_status_t access_bus(void *context, bm_bus_transaction_t *t)
{
    fixture_t *f = context;
    assert(t->space != BM_ADDRESS_IO && t->address + t->size <= 0x100000);
    assert(!t->wait_states && f->count < 64);
    f->trace[f->count++] = *t;
    if (f->count == f->fail_at) return BM_STATUS_DEVICE_ERROR;
    if (t->operation != BM_BUS_WRITE) t->value = 0;
    for (unsigned i = 0; i < t->size; ++i) {
        if (t->operation == BM_BUS_WRITE)
            f->ram[(size_t)t->address + i] = (uint8_t)(t->value >> (8U * i));
        else t->value |= (uint64_t)f->ram[(size_t)t->address + i] << (8U * i);
    }
    t->wait_states = 2;
    return BM_STATUS_OK;
}
static bm_status_t ack(void *context, unsigned phase, uint8_t *v, uint32_t *waits)
{
    fixture_t *f = context;
    assert(phase == (f->acknowledgements & 1U) && !*waits);
    ++f->acknowledgements; *v = 0x30; return BM_STATUS_OK;
}
static bm_286_arch_state_t state(fixture_t *f)
{
    bm_286_arch_state_t s;
    assert(bm_286_get_arch_state(&f->cpu, &s) == BM_STATUS_OK);
    return s;
}
static void same(const bm_286_arch_state_t *a, const bm_286_arch_state_t *b)
{
#define EQ(field) assert(a->field == b->field)
    EQ(size); EQ(version); EQ(ax); EQ(cx); EQ(dx); EQ(bx); EQ(sp); EQ(bp); EQ(si); EQ(di);
    EQ(ip); EQ(flags); EQ(msw); EQ(cpl); EQ(halted); EQ(shutdown);
    EQ(interrupt_shadow); EQ(nmi_blocked); EQ(nmi_pending); EQ(trap_pending);
    EQ(gdtr.base); EQ(gdtr.limit); EQ(idtr.base); EQ(idtr.limit);
#define SEG(s) EQ(s.selector); EQ(s.base); EQ(s.limit); EQ(s.access); EQ(s.valid)
    SEG(es); SEG(cs); SEG(ss); SEG(ds); SEG(ldtr); SEG(tr);
#undef SEG
#undef EQ
}
static void set(fixture_t *f, const bm_286_arch_state_t *s)
{
    assert(bm_286_set_arch_state(&f->cpu, s) == BM_STATUS_OK);
}
static void word(fixture_t *f, uint32_t address, uint16_t v)
{
    f->ram[address] = (uint8_t)v; f->ram[address + 1] = (uint8_t)(v >> 8);
}
static uint16_t read_word(fixture_t *f, uint32_t address)
{
    return (uint16_t)(f->ram[address] | ((unsigned)f->ram[address + 1] << 8));
}
static bm_286_arch_state_t setup(fixture_t *f, const uint8_t *code, size_t size)
{
    bm_286_arch_state_t s;
    assert(f->cpu.ops.reset(f->cpu.context) == BM_STATUS_OK);
    f->count = f->fail_at = f->acknowledgements = 0;
    s = state(f); s.ip = 0x100;
    s.cs.selector = 0x3000; s.cs.base = 0x30000;
    s.ss.selector = 0x1000; s.ss.base = 0x10000; s.sp = 0x800;
    s.es.selector = 0x2000; s.es.base = 0x20000; s.ax = 0xa55a;
    memcpy(f->ram + 0x30100, code, size);
    f->ram[0x40200] = 0xcf; /* IRET handler */
    for (unsigned i = 0; i < 256; ++i) {
        word(f, i * 4U, 0x200); word(f, i * 4U + 2U, 0x4000);
    }
    return s;
}
static bm_286_boundary_t step(fixture_t *f)
{
    bm_286_boundary_t b;
    unsigned before = f->count;
    assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK);
    assert(b.timing == BM_286_TIMING_UNKNOWN && b.bus_wait_cycles == (f->count - before) * 2U);
    assert(b.cpu_cycles == b.bus_wait_cycles);
    return b;
}
static void interrupts(fixture_t *f)
{
    for (unsigned v = 0; v < 256; ++v) {
        uint8_t code[] = {0x26,0xcd,(uint8_t)v,0x90};
        bm_286_arch_state_t s = setup(f, code, sizeof(code)), a;
        bm_286_boundary_t b;
        s.flags = 0x0fd7; /* IF and TF set: preserve saved image, clear on entry */
        s.sp += (uint16_t)(v & 1U); set(f, &s); b = step(f); a = state(f);
        assert(b.kind == BM_286_BOUNDARY_INSTRUCTION && b.has_vector && b.vector == v);
        assert(b.instruction_ip == 0x100 && b.instruction_address == 0x30100);
        assert(!f->acknowledgements && a.ip == 0x200 && a.cs.base == 0x40000);
        assert(a.sp == s.sp - 6 && a.flags == (s.flags & ~0x300U));
        assert(!a.trap_pending && !a.nmi_blocked); /* INT 2 is not a hardware NMI. */
        assert(read_word(f, s.ss.base + s.sp - 6U) == 0x103);
        assert(read_word(f, s.ss.base + s.sp - 4U) == s.cs.selector);
        assert(read_word(f, s.ss.base + s.sp - 2U) == s.flags);
        step(f); a = state(f);
        assert(a.ip == 0x103 && a.cs.base == s.cs.base && a.sp == s.sp && a.flags == s.flags);
        assert(!a.trap_pending); step(f); assert(state(f).trap_pending);
    }
    for (unsigned into = 0; into < 2; ++into)
        for (unsigned overflow = 0; overflow < 2; ++overflow) {
            uint8_t code[] = {(uint8_t)(into ? 0xce : 0xcc),0x90};
            bm_286_arch_state_t s = setup(f, code, sizeof(code));
            bm_286_boundary_t b;
            s.flags = (uint16_t)(0x102U | (overflow ? 0x800U : 0)); /* IF clear */
            set(f, &s); b = step(f);
            if (into && !overflow) {
                assert(!b.has_vector && f->count == 1 && state(f).sp == s.sp);
                assert(state(f).ip == 0x101 && state(f).trap_pending);
            } else {
                assert(b.has_vector && b.vector == (into ? 4 : 3));
                assert(!state(f).trap_pending && read_word(f, 0x107fa) == 0x101);
                step(f); assert(state(f).ip == 0x101);
            }
            assert(!f->acknowledgements);
        }
    {
        /* A software interrupt is not inhibited by the SS-load shadow. */
        const uint8_t code[] = {0xcd,0x21};
        bm_286_arch_state_t s = setup(f, code, sizeof(code));
        s.interrupt_shadow = BM_286_SHADOW_SS_LOAD; s.nmi_blocked = 1;
        set(f, &s); assert(step(f).vector == 0x21);
        assert(!state(f).interrupt_shadow && state(f).nmi_blocked);
    }
    {
        const uint8_t code[] = {0xcc};
        bm_286_arch_state_t s = setup(f, code, sizeof(code));
        s.ip = 0xffff; f->ram[0x3ffff] = 0xcc; set(f, &s); step(f);
        assert(read_word(f, 0x107fa) == 0); /* return IP wraps after final byte */
    }
}
static void flags(fixture_t *f)
{
    const uint8_t popf[] = {0x9d};
    bm_286_arch_state_t base = setup(f, popf, sizeof(popf));
    /* All stack flag images, including reserved bits. No per-case RAM clear. */
    for (unsigned v = 0; v < 65536; ++v) {
        bm_286_arch_state_t s = base;
        s.flags = 0x5002; f->count = 0; word(f, 0x10800, (uint16_t)v);
        set(f, &s); step(f);
        assert(state(f).flags == (0x5002U | (v & 0x0fd5U)));
        assert(state(f).sp == 0x802 && !state(f).interrupt_shadow && !state(f).trap_pending);
    }
    for (unsigned v = 0; v < 256; ++v) {
        const uint8_t code[] = {0x9e,0x9f};
        bm_286_arch_state_t s = setup(f, code, sizeof(code));
        s.flags = 0x0ed7; s.ax = (uint16_t)((v << 8) | 0x5a);
        set(f, &s); step(f); step(f);
        assert(state(f).ax == (((v & 0xd5U) | 2U) << 8 | 0x5a));
        assert(state(f).flags == (0x0e02U | (v & 0xd5U)));
        assert(f->count == 2); /* no data bus accesses */
    }
    for (unsigned odd = 0; odd < 2; ++odd) {
        const uint8_t code[] = {0x26,0x9c,0x9d};
        bm_286_arch_state_t s = setup(f, code, sizeof(code));
        s.flags = 0xfed7; s.sp += (uint16_t)odd;
        set(f, &s); step(f);
        assert(read_word(f, s.ss.base + s.sp - 2U) == 0x7ed7);
        assert(state(f).flags == s.flags);
        step(f); assert(state(f).flags == 0x7ed7 && state(f).sp == s.sp);
    }
    {
        const uint8_t code[] = {0x9c,0x9d};
        bm_286_arch_state_t s = setup(f, code, sizeof(code));
        s.sp = 0; s.flags = 0x0ed7; set(f, &s);
        step(f); assert(state(f).sp == 0xfffe && read_word(f, 0x1fffe) == s.flags);
        step(f); assert(state(f).sp == 0 && state(f).flags == s.flags);
    }
    {
        const uint8_t code[] = {0xf8,0xf9,0xf5,0xfd,0xfc};
        bm_286_arch_state_t s = setup(f, code, sizeof(code));
        const uint16_t expected[] = {0x0ad6,0x0ad7,0x0ad6,0x0ed6,0x0ad6};
        s.flags = 0x0ad7; set(f, &s);
        for (unsigned i = 0; i < 5; ++i) { step(f); assert(state(f).flags == expected[i]); }
        assert(state(f).ax == s.ax && f->count == 5);
    }
}
static void popf_events(fixture_t *f)
{
    const uint8_t code[] = {0x9d,0x90};
    bm_286_arch_state_t s = setup(f, code, sizeof(code));
    word(f, 0x10800, 0x202); set(f, &s);
    assert(f->cpu.ops.signal(f->cpu.context, BM_286_SIGNAL_INTR, 1) == BM_STATUS_OK);
    step(f); assert(!state(f).interrupt_shadow && !f->acknowledgements);
    assert(step(f).kind == BM_286_BOUNDARY_INTERRUPT && f->acknowledgements == 2);
    assert(read_word(f, 0x107fc) == 0x101); /* no intervening NOP */
    for (unsigned clear = 0; clear < 2; ++clear) {
        s = setup(f, code, sizeof(code)); s.flags = (uint16_t)(clear ? 0x102 : 2);
        word(f, 0x10800, (uint16_t)(clear ? 2 : 0x102)); set(f, &s); step(f);
        assert(state(f).trap_pending == clear);
        if (!clear) { step(f); assert(state(f).trap_pending); }
        assert(step(f).kind == BM_286_BOUNDARY_EXCEPTION);
    }
}
static void failures(fixture_t *f)
{
    const uint8_t codes[][3] = {{0xcd,0xff,0}, {0xcc,0,0}, {0xce,0,0},
        {0x9c,0,0}, {0x9d,0,0}, {0x26,0xcd,0xff}};
    for (unsigned form = 0; form < 6; ++form)
        for (unsigned odd = 0; odd < 2; ++odd) {
            unsigned transfers = 0;
            for (unsigned fail = 0; fail <= transfers; ++fail) {
                bm_286_arch_state_t s = setup(f, codes[form], 3), after;
                bm_286_boundary_t b;
                s.sp += (uint16_t)odd; s.flags = 0x0fd7; set(f, &s); f->fail_at = fail;
                if (!fail) { step(f); transfers = f->count; }
                else {
                    assert(bm_286_step(&f->cpu, &b) == BM_STATUS_DEVICE_ERROR);
                    after = state(f); same(&s, &after);
                    assert(!b.has_vector && !f->acknowledgements);
                    for (unsigned j = 0; j + 1U < fail; ++j) {
                        const bm_bus_transaction_t *t = &f->trace[j];
                        if (t->operation == BM_BUS_WRITE)
                            for (unsigned k = 0; k < t->size; ++k)
                                assert(f->ram[(size_t)t->address + k] == (uint8_t)(t->value >> (k * 8U)));
                    }
                    assert(bm_286_step(&f->cpu, &b) == BM_STATUS_INVALID_STATE && f->count == fail);
                }
            }
        }
    for (unsigned bad = 0; bad < 6; ++bad) {
        const uint8_t code[] = {0xcd,0x80};
        bm_286_arch_state_t s = setup(f, code, sizeof(code)), after;
        bm_286_boundary_t b;
        if (bad == 0) s.idtr.limit = 0x202;
        if (bad == 1) s.sp = 1;
        if (bad == 2) s.ss.valid = 0;
        /* Imported PE refusal now tests strict clocks; functional PE is enabled. */
        uint64_t gate_cycles = 99;
        if (bad == 3) s.msw |= 1;
        if (bad == 4) f->ram[0x30100] = 0xf0; /* no LOCK */
        if (bad == 5) f->ram[0x30100] = 0xf3; /* no REP */
        if (bad == 1) {
            set(f, &s); assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK);
            after = state(f); s.shutdown = 1; same(&s, &after);
            assert(b.kind == BM_286_BOUNDARY_SHUTDOWN && !b.has_vector && f->count == 2);
            assert(!f->acknowledgements);
            continue;
        }
        if (bad == 0) {
            word(f, 32, 0x200); word(f, 34, 0x4000);
            set(f, &s); assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK);
            after = state(f);
            assert(b.kind == BM_286_BOUNDARY_EXCEPTION && b.has_vector && b.vector == 8);
            assert(after.ip == 0x200 && after.cs.base == 0x40000 && after.sp == s.sp-6);
            assert(read_word(f, s.ss.base+s.sp-6) == s.ip && !f->acknowledgements);
            continue;
        }
        set(f, &s); assert((s.msw & 1U ? bm_286_step_clocked(f->cpu.context, 0, &gate_cycles) : bm_286_step(&f->cpu, &b)) == BM_STATUS_UNSUPPORTED);
        if (s.msw & 1U) assert(gate_cycles == 0);
        after = state(f); same(&s, &after);
        for (unsigned j = 0; j < f->count; ++j) assert(f->trace[j].operation == BM_BUS_FETCH);
        assert(!f->acknowledgements);
    }
    for (unsigned pop = 0; pop < 2; ++pop)
        for (unsigned bad = 0; bad < 3; ++bad) {
            uint8_t code[] = {(uint8_t)(pop ? 0x9d : 0x9c)};
            bm_286_arch_state_t s = setup(f, code, sizeof(code)), after;
            bm_286_boundary_t b;
            if (bad == 0) s.sp = (uint16_t)(pop ? 0xffff : 1);
            if (bad == 1) s.ss.valid = 0;
            if (bad == 2) s.ss.limit = 0x100;
            if (bad != 1) {
                set(f, &s); assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK);
                after = state(f);
                if (pop && bad == 0) {
                    assert(b.kind == BM_286_BOUNDARY_EXCEPTION && b.has_vector && b.vector == 13);
                    assert(after.sp == (uint16_t)(s.sp-6));
                    assert(read_word(f, s.ss.base+after.sp) == s.ip);
                    assert(read_word(f, s.ss.base+after.sp+4) == s.flags);
                } else {
                    s.shutdown = 1; same(&s, &after);
                    assert(b.kind == BM_286_BOUNDARY_SHUTDOWN && !b.has_vector && f->count == 1);
                }
                continue;
            }
            set(f, &s); assert(bm_286_step(&f->cpu, &b) == BM_STATUS_UNSUPPORTED);
            after = state(f); same(&s, &after);
            assert(f->count == 1 && f->trace[0].operation == BM_BUS_FETCH);
        }
}
int main(void)
{
    fixture_t f = {0}; bm_286_config_t c = {0};
    bm_host_services_t host = bm_null_host_services();
    f.ram = calloc(0x100000, 1); assert(f.ram);
    c.size = sizeof(c); c.version = BM_286_CONTRACT_VERSION;
    c.access = access_bus; c.access_context = &f; c.interrupt_ack = ack; c.interrupt_context = &f;
    c.bus_lock = lock_changed; c.pin_context = &f;
    assert(bm_286_create(&host, &c, &f.cpu) == BM_STATUS_OK);
    interrupts(&f); flags(&f); popf_events(&f); failures(&f);
    f.cpu.ops.destroy(f.cpu.context); free(f.ram); return 0;
}
