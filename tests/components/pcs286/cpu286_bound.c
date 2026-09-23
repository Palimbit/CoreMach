/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Authored Intel 286 architectural tests, not physical captures or timings.
 */
#include <blumach/components/cpu_80286.h>
#include <blumach/platforms/null_host.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct fixture {
    bm_cpu_t cpu;
    uint8_t *ram;
    bm_bus_transaction_t trace[32];
    unsigned count, fail_at, fail_after, acknowledgements;
} fixture_t;

static bm_status_t access_bus(void *context, bm_bus_transaction_t *t)
{
    fixture_t *f = context;
    assert(t->space != BM_ADDRESS_IO && t->address + t->size <= 0x100000);
    assert(f->count < 32 && !t->wait_states);
    f->trace[f->count++] = *t;
    if (f->count == f->fail_at && !f->fail_after) return BM_STATUS_DEVICE_ERROR;
    if (t->operation != BM_BUS_WRITE) t->value = 0;
    for (unsigned i = 0; i < t->size; ++i) {
        if (t->operation == BM_BUS_WRITE)
            f->ram[(size_t)t->address + i] = (uint8_t)(t->value >> (8U * i));
        else t->value |= (uint64_t)f->ram[(size_t)t->address + i] << (8U * i);
    }
    if (f->count == f->fail_at) return BM_STATUS_DEVICE_ERROR;
    t->wait_states = 2;
    return BM_STATUS_OK;
}
static bm_status_t ack(void *context, unsigned phase, uint8_t *v, uint32_t *w)
{
    fixture_t *f = context;
    (void)phase; (void)v; (void)w;
    ++f->acknowledgements;
    return BM_STATUS_DEVICE_ERROR; /* Synchronous faults must not call INTA. */
}
static void lock_changed(void *context, int high)
{
    (void)context; (void)high;
    assert(0); /* No automatic INTA exclusion for synchronous faults. */
}
static bm_286_arch_state_t state(fixture_t *f)
{
    bm_286_arch_state_t s;
    assert(bm_286_get_arch_state(&f->cpu, &s) == BM_STATUS_OK);
    return s;
}
static void set(fixture_t *f, const bm_286_arch_state_t *s)
{
    assert(bm_286_set_arch_state(&f->cpu, s) == BM_STATUS_OK);
    f->count = 0;
}
static void same(const bm_286_arch_state_t *a, const bm_286_arch_state_t *b)
{
#define EQ(x) assert(a->x == b->x)
    EQ(size); EQ(version); EQ(ax); EQ(bx); EQ(cx); EQ(dx); EQ(sp); EQ(bp); EQ(si); EQ(di);
    EQ(ip); EQ(flags); EQ(msw); EQ(cpl); EQ(halted); EQ(shutdown);
    EQ(interrupt_shadow); EQ(trap_pending); EQ(nmi_pending); EQ(nmi_blocked);
    EQ(gdtr.base); EQ(gdtr.limit); EQ(idtr.base); EQ(idtr.limit);
#define SEG(x) EQ(x.selector); EQ(x.base); EQ(x.limit); EQ(x.valid); EQ(x.access)
    SEG(cs); SEG(ds); SEG(es); SEG(ss); SEG(ldtr); SEG(tr);
#undef SEG
#undef EQ
}
static void word(fixture_t *f, uint32_t address, uint16_t v)
{
    f->ram[address] = (uint8_t)v; f->ram[address + 1] = (uint8_t)(v >> 8);
}
static uint16_t read_word(fixture_t *f, uint32_t address)
{
    return (uint16_t)(f->ram[address] | (unsigned)f->ram[address + 1] << 8);
}
static void reg_set(bm_286_arch_state_t *s, unsigned reg, uint16_t value)
{
    switch (reg) {
    case 0: s->ax = value; break; case 1: s->cx = value; break;
    case 2: s->dx = value; break; case 3: s->bx = value; break;
    case 4: s->sp = value; break; case 5: s->bp = value; break;
    case 6: s->si = value; break; default: s->di = value; break;
    }
}
static bm_286_arch_state_t setup(fixture_t *f, const uint8_t *code, size_t length)
{
    bm_286_arch_state_t s;
    assert(f->cpu.ops.reset(f->cpu.context) == BM_STATUS_OK);
    f->count = f->fail_at = f->fail_after = f->acknowledgements = 0;
    s = state(f); s.ip = 0x100;
    s.cs.selector = 0x3000; s.cs.base = 0x30000;
    s.ss.selector = 0x1000; s.ss.base = 0x10000; s.sp = 0x800;
    s.es.selector = 0x2000; s.es.base = 0x20000;
    s.ax = 0x1234; s.cx = 0x5678; s.dx = 0x9abc; s.bx = 0xdef0;
    s.bp = 0x4321; s.si = 0x8765; s.di = 0xcba9;
    s.flags = 0xed7; /* CF/PF/AF/ZF/SF/IF/DF/OF, not TF. */
    memcpy(f->ram + 0x30100, code, length);
    f->ram[0x40200] = 0xcf;
    for (unsigned vector = 0; vector < 256; ++vector) {
        word(f, vector * 4U, 0x200); word(f, vector * 4U + 2, 0x4000);
    }
    return s;
}
static bm_286_boundary_t step(fixture_t *f)
{
    bm_286_boundary_t b; unsigned before = f->count;
    assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK);
    assert(b.timing == BM_286_TIMING_UNKNOWN);
    assert(b.bus_wait_cycles == (f->count - before) * 2U);
    assert(b.cpu_cycles == b.bus_wait_cycles && !f->acknowledgements);
    return b;
}
static void fault(fixture_t *f, const bm_286_arch_state_t *s,
                  bm_286_boundary_t b, unsigned vector)
{
    bm_286_arch_state_t e = *s, a = state(f);
    e.sp = (uint16_t)(e.sp - 6); e.flags &= 0xfcffU;
    e.cs.selector = 0x4000; e.cs.base = 0x40000; e.cs.limit = 0xffff;
    e.cs.access = 0; e.cs.valid = 1; e.ip = 0x200;
    e.interrupt_shadow = BM_286_SHADOW_NONE; e.trap_pending = 0;
    same(&a, &e);
    assert(b.kind == BM_286_BOUNDARY_EXCEPTION && b.has_vector && b.vector == vector);
    assert(b.instruction_ip == s->ip && b.instruction_address == s->cs.base + s->ip);
    assert(read_word(f, s->ss.base + (uint16_t)(s->sp - 2)) == s->flags);
    assert(read_word(f, s->ss.base + (uint16_t)(s->sp - 4)) == s->cs.selector);
    assert(read_word(f, s->ss.base + (uint16_t)(s->sp - 6)) == s->ip);
}
static void ranges(fixture_t *f)
{
    /* Sign-bit biased unsigned ordering: independent of production's signed
     * conversion. Includes inverted bounds and inclusive endpoints. */
    const uint16_t values[] = {0x8000,0x8001,0xffff,0,1,0x7ffe,0x7fff};
    for (unsigned reg = 0; reg < 8; ++reg) {
        uint8_t code[] = {0x62,(uint8_t)(6U | reg << 3),0,5};
        bm_286_arch_state_t base = setup(f, code, sizeof(code));
        for (unsigned lo = 0; lo < 7; ++lo) for (unsigned hi = 0; hi < 7; ++hi)
            for (unsigned n = 0; n < 7; ++n) {
                uint16_t index = reg == 4 ? (uint16_t)(0x800 + n * 2) : values[n];
                bm_286_arch_state_t s = base, e, a; bm_286_boundary_t b;
                reg_set(&s, reg, index);
                word(f, 0x500, values[lo]); word(f, 0x502, values[hi]);
                set(f, &s); b = step(f);
                if ((index ^ 0x8000U) < (values[lo] ^ 0x8000U) ||
                    (index ^ 0x8000U) > (values[hi] ^ 0x8000U)) fault(f, &s, b, 5);
                else {
                    e = s; e.ip += 4; a = state(f); same(&a, &e);
                    assert(b.kind == BM_286_BOUNDARY_INSTRUCTION && !b.has_vector);
                }
            }
    }
    { /* Every 16-bit index across a range straddling signed zero. */
        const uint8_t code[] = {0x62,6,0,5};
        bm_286_arch_state_t s = setup(f, code, sizeof(code));
        word(f, 0x500, 0xffff); word(f, 0x502, 1);
        for (unsigned index = 0; index < 65536; ++index) {
            bm_286_boundary_t b; s.ax = (uint16_t)index; set(f, &s); b = step(f);
            assert((b.kind == BM_286_BOUNDARY_EXCEPTION) == (index > 1 && index != 65535));
        }
    }
}
static void addressing(fixture_t *f)
{
    const uint8_t prefixes[] = {0,0x26,0x2e,0x36,0x3e};
    for (unsigned p = 0; p < 5; ++p) for (unsigned bp = 0; bp < 2; ++bp)
        for (unsigned odd = 0; odd < 2; ++odd) {
            uint8_t code[] = {prefixes[p],0x62,(uint8_t)(bp ? 0x46 : 0x40),0x10};
            bm_286_arch_state_t s = setup(f, code + (p == 0), sizeof(code) - (p == 0)), e, a;
            bm_286_boundary_t b; uint32_t base;
            s.ax = 10; s.bx = s.bp = (uint16_t)(0x400 + odd); s.si = 0xf0;
            uint16_t offset = (uint16_t)(bp ? s.bp + 0x10 : s.bx + s.si + 0x10);
            base = p == 1 ? s.es.base : p == 2 ? s.cs.base :
                   p == 3 || (p == 0 && bp) ? s.ss.base : s.ds.base;
            word(f, base + offset, 10); word(f, base + offset + 2, 10);
            set(f, &s); b = step(f); e = s; e.ip += (uint16_t)(p ? 4 : 3);
            a = state(f); same(&a, &e); assert(!b.has_vector);
            unsigned first = p ? 4 : 3;
            assert(f->count == first + (odd ? 4 : 2));
            assert(f->trace[first].address == base + offset);
        }
    { /* Last valid pair: FFFC..FFFF, no wrap. */
        const uint8_t code[] = {0x62,6,0xfc,0xff};
        bm_286_arch_state_t s = setup(f, code, sizeof(code));
        word(f, 0xfffc, 0x8000); word(f, 0xfffe, 0x7fff);
        set(f, &s); assert(!step(f).has_vector);
    }
}
static void invalid_register(fixture_t *f)
{
    for (unsigned modrm = 0xc0; modrm <= 0xff; ++modrm) {
        const uint8_t code[] = {0x26,0x62,(uint8_t)modrm};
        bm_286_arch_state_t s = setup(f, code, sizeof(code));
        s.es.valid = 0; /* Unused segment must not mask invalid encoding. */
        set(f, &s); fault(f, &s, step(f), 6);
        assert(f->count == 8); /* 3 fetches, 3 pushes, 2 vector words. */
    }
}
static void recovery(fixture_t *f)
{
    const uint8_t code[] = {0x26,0x62,6,0,5};
    { /* Repair an overrun EA after #13, then retry the same prefixed BOUND. */
        const uint8_t indirect[] = {0x3e,0x62,7}; /* BOUND AX,[BX] */
        bm_286_arch_state_t s = setup(f, indirect, sizeof(indirect)), a;
        s.bx = 0xfffe; s.ax = 0; set(f, &s);
        fault(f, &s, step(f), 13);
        a = state(f); a.bx = 0x500; set(f, &a);
        word(f, 0x500, 0xffff); word(f, 0x502, 1);
        step(f); a = state(f); s.bx = 0x500; same(&a, &s);
        assert(!step(f).has_vector && state(f).ip == s.ip + 3);
    }
    for (unsigned tf = 0; tf < 2; ++tf) {
        bm_286_arch_state_t s = setup(f, code, sizeof(code)), a;
        s.ax = 2; s.flags = (uint16_t)(0x202 | tf << 8);
        word(f, 0x20500, 0); word(f, 0x20502, 1); set(f, &s);
        fault(f, &s, step(f), 5);
        word(f, 0x20502, 2); /* Handler/external debugger repairs range. */
        step(f); a = state(f); same(&a, &s); /* IRET returns to first prefix. */
        bm_286_boundary_t b = step(f); a = state(f);
        assert(!b.has_vector && a.ip == s.ip + 5 && a.trap_pending == tf);
        if (tf) { b = step(f); assert(b.has_vector && b.vector == 1); }
    }
    { /* Synchronous fault is not blocked by STI/SS inhibition. */
        bm_286_arch_state_t s = setup(f, code, sizeof(code));
        s.ax = 2; s.flags = 0x302; s.trap_pending = 1;
        s.interrupt_shadow = BM_286_SHADOW_SS_LOAD;
        s.nmi_blocked = s.nmi_pending = 1;
        word(f, 0x20500, 0); word(f, 0x20502, 1); set(f, &s);
        assert(f->cpu.ops.signal(f->cpu.context, BM_286_SIGNAL_INTR, 1) == BM_STATUS_OK);
        fault(f, &s, step(f), 5); /* Discard deferred TF: explicit policy. */
    }
}
static void failures(fixture_t *f)
{
    uint8_t *expected = malloc(0x100000); assert(expected);
    for (unsigned invalid = 0; invalid < 3; ++invalid)
        for (unsigned odd = 0; odd < 2; ++odd) {
            uint8_t code[] = {0x62,(uint8_t)(invalid == 1 ? 0xc0 : 6),
                (uint8_t)(invalid == 2 ? 0xfd : odd),(uint8_t)(invalid == 2 ? 0xff : 5)};
            bm_286_arch_state_t s = setup(f, code, sizeof(code));
            bm_bus_transaction_t trace[32]; unsigned count;
            s.ax = 2; s.sp += (uint16_t)odd;
            word(f, 0x500 + odd, 0); word(f, 0x502 + odd, 1);
            set(f, &s); fault(f, &s, step(f), invalid == 2 ? 13 : invalid == 1 ? 6 : 5);
            count = f->count; memcpy(trace, f->trace, sizeof(trace));
            for (unsigned fail = 1; fail <= count; ++fail)
                for (unsigned after = 0; after < 2; ++after) {
                    bm_286_arch_state_t a; bm_286_boundary_t b;
                    memset(f->ram + 0x107f0, 0xa5, 32);
                    memcpy(expected, f->ram, 0x100000);
                    for (unsigned t = 0; t < fail - 1 + after; ++t)
                        if (trace[t].operation == BM_BUS_WRITE)
                            for (unsigned j = 0; j < trace[t].size; ++j)
                                expected[(size_t)trace[t].address + j] =
                                    (uint8_t)(trace[t].value >> (8U * j));
                    assert(f->cpu.ops.reset(f->cpu.context) == BM_STATUS_OK);
                    set(f, &s); f->fail_at = fail; f->fail_after = after;
                    assert(bm_286_step(&f->cpu, &b) == BM_STATUS_DEVICE_ERROR);
                    a = state(f); same(&a, &s);
                    assert(f->count == fail && !memcmp(expected, f->ram, 0x100000));
                    assert(bm_286_step(&f->cpu, &b) != BM_STATUS_OK);
                    assert(f->count == fail); /* No replay after partial frame. */
                }
        }
    free(expected);
}
static void gaps(fixture_t *f)
{
    for (unsigned bad = 0; bad < 10; ++bad) {
        uint8_t code[] = {0x62,6,0,5};
        bm_286_arch_state_t s = setup(f, code, sizeof(code)), a;
        bm_286_boundary_t b; s.ax = 2; word(f, 0x500, 0); word(f, 0x502, 1);
        if (bad < 3) { f->ram[0x30102] = (uint8_t)(0xfd + bad); f->ram[0x30103] = 0xff; }
        if (bad == 3) s.ds.valid = 0;
        if (bad == 4) s.ds.limit = 0x502;
        if (bad == 5) s.msw |= 1;
        if (bad == 6) s.idtr.limit = 22;
        if (bad == 7) s.sp = 5;
        if (bad == 8 || bad == 9) {
            f->ram[0x30100] = (uint8_t)(bad == 8 ? 0xf0 : 0xf3);
            memcpy(f->ram + 0x30101, code, sizeof(code));
        }
        set(f, &s);
        if (bad < 3 || bad == 4) {
            fault(f, &s, step(f), 13);
            assert(f->count == 9); /* Fetches and frame/IVT, no bounds read. */
            continue;
        }
        assert(bm_286_step(&f->cpu, &b) == BM_STATUS_UNSUPPORTED);
        a = state(f); same(&a, &s);
        for (unsigned i = 0; i < f->count; ++i) assert(f->trace[i].operation != BM_BUS_WRITE);
        if (bad < 5) assert(f->count == 4); /* No partial bounds read/wrap. */
    }
}
int main(void)
{
    fixture_t f = {0}; bm_286_config_t config = {0};
    bm_host_services_t host = bm_null_host_services();
    f.ram = calloc(0x100000, 1); assert(f.ram);
    config.size = sizeof(config); config.version = BM_286_CONTRACT_VERSION;
    config.access = access_bus; config.access_context = &f;
    config.interrupt_ack = ack; config.interrupt_context = &f;
    config.bus_lock = lock_changed; config.pin_context = &f;
    assert(bm_286_create(&host, &config, &f.cpu) == BM_STATUS_OK);
    ranges(&f); addressing(&f); invalid_register(&f); recovery(&f); failures(&f); gaps(&f);
    f.cpu.ops.destroy(f.cpu.context); free(f.ram); return 0;
}
