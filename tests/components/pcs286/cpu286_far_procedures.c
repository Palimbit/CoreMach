/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Authored functional tests, not firmware or measured physical bus timings.
 */
#include <blumach/components/cpu_80286.h>
#include <blumach/platforms/null_host.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct fixture {
    bm_cpu_t cpu;
    uint8_t *ram;
    bm_bus_transaction_t trace[64];
    unsigned count, fail_at;
} fixture_t;

static bm_status_t access_bus(void *context, bm_bus_transaction_t *t)
{
    fixture_t *f = context;
    assert(t->address + t->size <= 0x1000000U && f->count < 64);
    assert(t->size == 1 || t->size == 2);
    assert(t->endianness == BM_ENDIAN_LITTLE && !t->wait_states);
    assert(t->space == (t->operation == BM_BUS_FETCH ? BM_ADDRESS_PROGRAM : BM_ADDRESS_DATA));
    f->trace[f->count++] = *t;
    if (f->count == f->fail_at) return BM_STATUS_DEVICE_ERROR;
    if (t->operation != BM_BUS_WRITE) t->value = 0;
    for (unsigned i = 0; i < t->size; ++i) {
        if (t->operation == BM_BUS_WRITE)
            f->ram[(size_t)t->address + i] = (uint8_t)(t->value >> (8U * i));
        else t->value |= (uint64_t)f->ram[(size_t)t->address + i] << (8U * i);
    }
    t->wait_states = 3;
    return BM_STATUS_OK;
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
}
static void same(const bm_286_arch_state_t *a, const bm_286_arch_state_t *b)
{
#define EQ(x) assert(a->x == b->x)
    EQ(size); EQ(version); EQ(ax); EQ(bx); EQ(cx); EQ(dx); EQ(si); EQ(di); EQ(bp); EQ(sp);
    EQ(ip); EQ(flags); EQ(msw); EQ(cpl); EQ(halted); EQ(shutdown);
    EQ(interrupt_shadow); EQ(nmi_blocked); EQ(nmi_pending); EQ(trap_pending);
    EQ(gdtr.base); EQ(gdtr.limit); EQ(idtr.base); EQ(idtr.limit);
#define SEG(x) EQ(x.selector); EQ(x.base); EQ(x.limit); EQ(x.access); EQ(x.valid)
    SEG(es); SEG(cs); SEG(ss); SEG(ds); SEG(ldtr); SEG(tr);
#undef SEG
#undef EQ
}
static void word(fixture_t *f, uint32_t address, uint16_t v)
{
    f->ram[address] = (uint8_t)v; f->ram[address + 1] = (uint8_t)(v >> 8);
}
static uint16_t read_word(fixture_t *f, uint32_t address)
{
    return (uint16_t)(f->ram[address] | ((unsigned)f->ram[address + 1] << 8));
}
static bm_286_arch_state_t setup(fixture_t *f, const uint8_t *code, size_t length)
{
    bm_286_arch_state_t s;
    assert(f->cpu.ops.reset(f->cpu.context) == BM_STATUS_OK);
    f->count = f->fail_at = 0; s = state(f);
    s.cs.selector = 0x3000; s.cs.base = 0x30000; s.ip = 0x100;
    s.ds.selector = 0x1000; s.ds.base = 0x10000;
    s.ss.selector = 0x2000; s.ss.base = 0x20000; s.sp = 0x800;
    s.es.selector = 0x4000; s.es.base = 0x40000;
    s.bx = s.bp = 0x500; s.ax = 0xaaaa; s.dx = 0xbbbb; s.cx = 0xcccc;
    s.si = 0xdddd; s.di = 0xeeee; s.flags = 0x0ed7; s.nmi_blocked = 1;
    memcpy(f->ram + 0x30100, code, length);
    return s;
}
static bm_286_boundary_t step(fixture_t *f)
{
    bm_286_boundary_t b; unsigned before = f->count;
    assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK);
    assert(b.kind == BM_286_BOUNDARY_INSTRUCTION && !b.has_vector);
    assert(b.timing == BM_286_TIMING_UNKNOWN);
    assert(b.cpu_cycles == b.bus_wait_cycles && b.bus_wait_cycles == 3U * (f->count - before));
    return b;
}
static void target(bm_286_arch_state_t *s, uint16_t ip, uint16_t cs, uint16_t sp)
{
    s->ip = ip; s->sp = sp; s->cs.selector = cs; s->cs.base = (uint32_t)cs << 4;
    s->cs.limit = 0xffff; s->cs.access = 0x82; s->cs.valid = 1;
}
static void calls(fixture_t *f)
{
    for (unsigned form = 0; form < 7; ++form)
        for (unsigned alignment = 0; alignment < 4; ++alignment) {
            uint8_t code[6] = {0x9a,0x34,0x12,0xff,0xff,0};
            unsigned length = form == 0 ? 5 : form == 1 ? 2 : 3;
            bm_286_arch_state_t s, expected, actual;
            uint32_t base;
            if (form) { code[0] = 0xff; code[1] = 0x1f; }
            if (form == 2) { code[1] = 0x5e; code[2] = 0; } /* SS:[BP+0] */
            if (form >= 3) { code[0] = (uint8_t)(0x26 + 8U * (form - 3)); code[1] = 0xff; code[2] = 0x1f; }
            s = setup(f, code, length); s.sp += (uint16_t)(alignment & 1U);
            s.bx += (uint16_t)(alignment >> 1); s.bp = s.bx;
            base = form == 2 || form == 5 ? s.ss.base : form == 3 ? s.es.base :
                form == 4 ? s.cs.base : s.ds.base;
            word(f, base + s.bx, 0x1234); word(f, base + s.bx + 2, 0xffff);
            set(f, &s); step(f); actual = state(f); expected = s;
            target(&expected, 0x1234, 0xffff, (uint16_t)(s.sp - 4)); same(&actual, &expected);
            assert(read_word(f, s.ss.base + s.sp - 4) == s.ip + length);
            assert(read_word(f, s.ss.base + s.sp - 2) == s.cs.selector);
            /* Fetch above 1 MiB: no CPU-owned A20 masking; return reloads CS. */
            f->ram[0x101224] = 0xcb; step(f); actual = state(f); expected = s;
            expected.ip = (uint16_t)(s.ip + length); same(&actual, &expected);
        }
    {
        /* Read both pointer words before pushes overwrite the same SS slots. */
        const uint8_t code[] = {0xff,0x5e,0};
        bm_286_arch_state_t s = setup(f, code, sizeof(code));
        s.bp = (uint16_t)(s.sp - 4); word(f, s.ss.base + s.bp, 0x2468);
        word(f, s.ss.base + s.bp + 2, 0x1234); set(f, &s); step(f);
        assert(state(f).cs.selector == 0x1234 && state(f).ip == 0x2468);
        assert(f->trace[3].operation == BM_BUS_READ && f->trace[4].operation == BM_BUS_READ);
        assert(f->trace[5].operation == BM_BUS_WRITE && f->trace[5].value == 0x3000);
        assert(f->trace[6].value == 0x103);
    }
    {
        const uint8_t code[] = {0xff,0x1f};
        bm_286_arch_state_t s = setup(f, code, sizeof(code));
        s.bx = 0xfffe; s.sp = 2;
        word(f, s.ds.base + 0xfffe, 0x2468); word(f, s.ds.base, 0x1234);
        set(f, &s); step(f); assert(state(f).sp == 0xfffe);
        assert(f->trace[2].address == s.ds.base + 0xfffe && f->trace[3].address == s.ds.base);
        assert(read_word(f, s.ss.base) == s.cs.selector && read_word(f, s.ss.base + 0xfffe) == 0x102);
        f->ram[0x147a8] = 0xcb; step(f); assert(state(f).sp == 2 && state(f).ip == 0x102);
    }
    {
        const uint8_t code[] = {0x9a,0,1,0,0xf0};
        bm_286_arch_state_t s = setup(f, code, sizeof(code));
        s.cs.selector = 0xf000; s.cs.base = 0xff0000; s.ip = 0xfff0;
        memcpy(f->ram + 0xfffff0, code, sizeof(code)); set(f, &s);
        assert(step(f).instruction_address == 0xfffff0 && state(f).cs.base == 0xf0000);
        f->ram[0xf0100] = 0xcb; step(f);
        assert(state(f).ip == 0xfff5 && state(f).cs.base == 0xf0000);
        f->ram[0xffff5] = 0x90; assert(step(f).instruction_address == 0xffff5);
    }
    {
        const uint8_t code[] = {0x26,0x9a,0,1,0,0xf0};
        bm_286_arch_state_t s = setup(f, code, sizeof(code));
        s.ip = 0xfffa; memcpy(f->ram + 0x3fffa, code, sizeof(code)); set(f, &s);
        step(f); assert(read_word(f, 0x207fc) == 0); /* prefix-inclusive next IP */
    }
}
static void returns(fixture_t *f)
{
    const uint8_t code[] = {0x26,0xca,0,0};
    bm_286_arch_state_t s = setup(f, code, sizeof(code));
    s.sp = 0xfffc; s.cs.limit = 0x103; /* target limit is the reloaded CS limit */
    word(f, 0x2fffc, 0xffff); word(f, 0x2fffe, 0);
    for (unsigned discard = 0; discard < 65536; ++discard) {
        bm_286_arch_state_t expected = s, actual;
        f->count = 0; word(f, 0x30102, (uint16_t)discard); set(f, &s); step(f);
        actual = state(f); target(&expected, 0xffff, 0, (uint16_t)discard);
        same(&actual, &expected); assert(f->count == 6); /* no reads of discarded bytes */
    }
    {
        const uint8_t call[] = {0x9a,0,1,0,0xf0};
        s = setup(f, call, sizeof(call)); s.flags |= 0x100; set(f, &s); step(f);
        assert(state(f).trap_pending); /* CALL is single stepped, unlike INT. */
        s = setup(f, (const uint8_t[]){0xcb}, 1); s.flags |= 0x100;
        word(f, 0x20800, 0x1234); word(f, 0x20802, 0x5678); set(f, &s); step(f);
        assert(state(f).trap_pending && state(f).nmi_blocked);
    }
}
static void failures(fixture_t *f)
{
    const uint8_t codes[][6] = {{0x9a,0x34,0x12,0xff,0xff,0}, {0x26,0xff,0x1f,0,0,0},
        {0xcb,0,0,0,0,0}, {0x36,0xca,0xff,0xff,0,0}};
    for (unsigned form = 0; form < 4; ++form)
        for (unsigned odd = 0; odd < 2; ++odd) {
            unsigned transfers = 0;
            for (unsigned fail = 0; fail <= transfers; ++fail) {
                bm_286_arch_state_t s = setup(f, codes[form], 6), a;
                bm_286_boundary_t b;
                s.sp += (uint16_t)odd; s.bx += (uint16_t)odd;
                word(f, s.es.base + s.bx, 0x1234); word(f, s.es.base + s.bx + 2, 0xffff);
                word(f, s.ss.base + s.sp, 0x1234); word(f, s.ss.base + s.sp + 2, 0xffff);
                memset(f->ram + s.ss.base + s.sp - 4, 0xa5, 4);
                set(f, &s); f->fail_at = fail;
                if (!fail) { step(f); transfers = f->count; continue; }
                assert(bm_286_step(&f->cpu, &b) == BM_STATUS_DEVICE_ERROR);
                a = state(f); same(&s, &a);
                for (unsigned j = 0; j + 1 < fail; ++j)
                    if (f->trace[j].operation == BM_BUS_WRITE)
                        for (unsigned k = 0; k < f->trace[j].size; ++k)
                            assert(f->ram[(size_t)f->trace[j].address + k] ==
                                (uint8_t)(f->trace[j].value >> (8U * k)));
                assert(bm_286_step(&f->cpu, &b) == BM_STATUS_INVALID_STATE && f->count == fail);
            }
        }
    for (unsigned form = 0; form < 4; ++form)
        for (unsigned bad = 0; bad < 6; ++bad) {
            bm_286_arch_state_t s = setup(f, codes[form], 6), a;
            bm_286_boundary_t b;
            if (bad == 0) s.ss.valid = 0;
            if (bad == 1) s.ss.limit = 0x100;
            if (bad == 2) s.sp = (uint16_t)(form < 2 ? 3 : 0xfffd);
            /* Imported PE refusal now tests strict clocks; functional PE is enabled. */
            uint64_t gate_cycles = 99;
            if (bad == 3) s.msw |= 1;
            if (bad == 4) f->ram[0x30100] = 0xf0;
            if (bad == 5) f->ram[0x30100] = 0xf3;
            if (bad == 1 || bad == 2) {
                set(f, &s); assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK);
                a = state(f);
                if (bad == 1 || form < 2) {
                    s.shutdown = 1; same(&s, &a);
                    assert(b.kind == BM_286_BOUNDARY_SHUTDOWN && !b.has_vector);
                    for (unsigned j = 0; j < f->count; ++j) assert(f->trace[j].operation != BM_BUS_WRITE);
                } else {
                    assert(b.kind == BM_286_BOUNDARY_EXCEPTION && b.has_vector && b.vector == 13);
                    assert(a.sp == (uint16_t)(s.sp-6) && a.nmi_blocked == s.nmi_blocked);
                    assert(f->ram[s.ss.base+a.sp] == (uint8_t)s.ip);
                }
                continue;
            }
            set(f, &s); assert((s.msw & 1U ? bm_286_step_clocked(f->cpu.context, 0, &gate_cycles) : bm_286_step(&f->cpu, &b)) == BM_STATUS_UNSUPPORTED);
            if (s.msw & 1U) assert(gate_cycles == 0);
            a = state(f); same(&s, &a);
            for (unsigned j = 0; j < f->count; ++j) assert(f->trace[j].operation != BM_BUS_WRITE);
        }
    for (unsigned bad = 0; bad < 3; ++bad) {
        const uint8_t code[] = {0xff,0x1f};
        bm_286_arch_state_t s = setup(f, code, sizeof(code)), a; bm_286_boundary_t b;
        if (bad == 0) f->ram[0x30101] = 0xd8; /* invalid register far CALL */
        if (bad == 1) s.ds.valid = 0;
        if (bad == 2) s.bx = 0xffff;
        if (bad == 0 || bad == 2) {
            set(f, &s); assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK);
            a = state(f);
            assert(b.kind == BM_286_BOUNDARY_EXCEPTION && b.has_vector && b.vector == (bad == 0 ? 6 : 13));
            assert(a.sp == (uint16_t)(s.sp-6) && f->count == 7);
            assert(f->ram[s.ss.base+a.sp] == (uint8_t)s.ip);
            continue;
        }
        set(f, &s); assert(bm_286_step(&f->cpu, &b) == BM_STATUS_UNSUPPORTED);
        a = state(f); same(&s, &a);
        assert(f->count == 2 && f->trace[1].operation == BM_BUS_FETCH);
    }
}
int main(void)
{
    fixture_t f = {0}; bm_286_config_t config = {0};
    bm_host_services_t host = bm_null_host_services();
    f.ram = calloc(0x1000000, 1); assert(f.ram);
    config.size = sizeof(config); config.version = BM_286_CONTRACT_VERSION;
    config.access = access_bus; config.access_context = &f;
    assert(bm_286_create(&host, &config, &f.cpu) == BM_STATUS_OK);
    calls(&f); returns(&f); failures(&f);
    f.cpu.ops.destroy(f.cpu.context); free(f.ram); return 0;
}
