/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Authored functional cases; no firmware, hardware vectors or cycle claims.
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
    unsigned count, fail_at, allow_frame;
} fixture_t;
static bm_status_t access_bus(void *context, bm_bus_transaction_t *t)
{
    fixture_t *f = context;
    assert(f->count < 32 && t->address + t->size <= 0x1000000U);
    assert(t->operation != BM_BUS_WRITE || f->allow_frame);
    assert(t->size == 1 || t->size == 2);
    assert(t->endianness == BM_ENDIAN_LITTLE && !t->wait_states);
    assert(t->space == (t->operation == BM_BUS_FETCH ? BM_ADDRESS_PROGRAM : BM_ADDRESS_DATA));
    f->trace[f->count++] = *t;
    if (f->fail_at == f->count) return BM_STATUS_DEVICE_ERROR;
    if (t->operation != BM_BUS_WRITE) t->value = 0;
    for (unsigned i = 0; i < t->size; ++i)
        if (t->operation == BM_BUS_WRITE)
            f->ram[(size_t)t->address+i] = (uint8_t)(t->value >> (8U*i));
        else
            t->value |= (uint64_t)f->ram[(size_t)t->address + i] << (8U * i);
    t->wait_states = 2; return BM_STATUS_OK;
}
static bm_286_arch_state_t state(fixture_t *f)
{
    bm_286_arch_state_t s;
    assert(bm_286_get_arch_state(&f->cpu, &s) == BM_STATUS_OK); return s;
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
static uint16_t *reg(bm_286_arch_state_t *s, unsigned n)
{
    switch (n) {
    case 0: return &s->ax; case 1: return &s->cx; case 2: return &s->dx; case 3: return &s->bx;
    case 4: return &s->sp; case 5: return &s->bp; case 6: return &s->si; default: return &s->di;
    }
}
static bm_286_segment_state_t *seg(bm_286_arch_state_t *s, unsigned n)
{
    switch (n) {case 0: return &s->es; case 1: return &s->cs; case 2: return &s->ss; default: return &s->ds;}
}
static bm_286_arch_state_t setup(fixture_t *f, const uint8_t *code, size_t size)
{
    bm_286_arch_state_t s;
    assert(f->cpu.ops.reset(f->cpu.context) == BM_STATUS_OK);
    f->count = f->fail_at = f->allow_frame = 0; s = state(f); s.ip = 0x100;
    s.cs.selector = 0x3000; s.cs.base = 0x30000;
    s.ds.selector = 0x1000; s.ds.base = 0x10000;
    s.ss.selector = 0x2000; s.ss.base = 0x20000;
    s.es.selector = 0x4000; s.es.base = 0x40000;
    s.ax = 0xa580; s.cx = 0x1234; s.dx = 0x5678; s.bx = 0xff80;
    s.si = 0x321; s.di = 0x432; s.bp = 0xfedc; s.sp = 0x800;
    s.flags = 0x0ed7; s.nmi_blocked = 1;
    memcpy(f->ram + 0x30100, code, size); return s;
}
static void pointer(fixture_t *f, uint32_t base, uint16_t offset, uint16_t value, uint16_t selector)
{
    f->ram[base + offset] = (uint8_t)value;
    f->ram[base + offset + 1U] = (uint8_t)(value >> 8);
    offset = (uint16_t)(offset + 2U);
    f->ram[base + offset] = (uint8_t)selector;
    f->ram[base + offset + 1U] = (uint8_t)(selector >> 8);
}
static void step(fixture_t *f)
{
    bm_286_boundary_t b; unsigned before = f->count;
    assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK);
    assert(b.kind == BM_286_BOUNDARY_INSTRUCTION && !b.has_vector);
    assert(b.timing == BM_286_TIMING_UNKNOWN && b.cpu_cycles == b.bus_wait_cycles);
    assert(b.bus_wait_cycles == 2U * (f->count - before));
}
static void lea_matrix(fixture_t *f)
{
    for (unsigned prefix = 0; prefix < 5; ++prefix)
        for (unsigned modrm = 0; modrm < 192; ++modrm)
            for (unsigned variant = 0; variant < 2; ++variant) {
                uint8_t code[5]; unsigned length = 0, mode = modrm >> 6, rm = modrm & 7;
                uint16_t disp = variant ? 0xff80 : 0x7f;
                bm_286_arch_state_t s, expected, actual;
                uint32_t bases[8], offset;
                if (prefix) code[length++] = (uint8_t)(0x26 + 8U * (prefix - 1));
                code[length++] = 0x8d; code[length++] = (uint8_t)modrm;
                if (mode || rm == 6) code[length++] = (uint8_t)disp;
                if (mode == 2 || (mode == 0 && rm == 6)) code[length++] = (uint8_t)(disp >> 8);
                s = setup(f, code, length);
                bases[0] = s.bx + s.si; bases[1] = s.bx + s.di;
                bases[2] = s.bp + s.si; bases[3] = s.bp + s.di;
                bases[4] = s.si; bases[5] = s.di; bases[6] = s.bp; bases[7] = s.bx;
                offset = mode == 0 && rm == 6 ? disp : bases[rm] + (mode ? disp : 0);
                s.ds.valid = s.es.valid = s.ss.valid = 0;
                s.ds.limit = s.es.limit = s.ss.limit = 0;
                s.cs.limit = (uint16_t)(s.ip + length - 1); /* fetch allowed, data EA irrelevant */
                set(f, &s); step(f); actual = state(f); expected = s;
                *reg(&expected, (modrm >> 3) & 7) = (uint16_t)offset;
                expected.ip += (uint16_t)length; same(&actual, &expected);
                assert(f->count == length);
                for (unsigned i = 0; i < f->count; ++i) assert(f->trace[i].operation == BM_BUS_FETCH);
            }
}
static void far_loads(fixture_t *f)
{
    for (unsigned les = 0; les < 2; ++les)
        for (unsigned dest = 0; dest < 8; ++dest)
            for (unsigned source = 0; source < 6; ++source)
                for (unsigned odd = 0; odd < 2; ++odd) {
                    uint8_t code[3]; unsigned length = 0;
                    bm_286_arch_state_t s, expected, actual;
                    bm_286_segment_state_t *ds;
                    uint16_t offset = (uint16_t)(0x500 + odd), selector = odd ? 0xffff : 0;
                    unsigned source_seg = source == 0 ? 3 : source == 1 ? 2 : source - 2;
                    if (source >= 2) code[length++] = (uint8_t)(0x26 + 8U * source_seg);
                    code[length++] = (uint8_t)(les ? 0xc4 : 0xc5);
                    code[length++] = (uint8_t)((dest << 3) | (source == 1 ? 0x46 : 7));
                    if (source == 1) code[length++] = 0; /* BP + disp8 defaults to SS */
                    s = setup(f, code, length); s.bx = s.bp = offset;
                    pointer(f, seg(&s, source_seg)->base, offset, 0x7654, selector);
                    set(f, &s); step(f); actual = state(f); expected = s;
                    *reg(&expected, dest) = 0x7654; expected.ip += (uint16_t)length;
                    ds = les ? &expected.es : &expected.ds;
                    ds->selector = selector; ds->base = (uint32_t)selector << 4;
                    ds->limit = 0xffff; ds->access = 0x82; ds->valid = 1;
                    same(&actual, &expected); assert(!actual.interrupt_shadow);
                    assert(f->count == length + 2 + 2 * odd);
                    assert(f->trace[length].address == seg(&s, source_seg)->base + offset);
                }
    for (unsigned les = 0; les < 2; ++les) {
        uint8_t code[] = {(uint8_t)(les ? 0xc4 : 0xc5),0x1f}; /* dest BX aliases EA */
        bm_286_arch_state_t s = setup(f, code, sizeof(code));
        s.bx = 0xfffe; pointer(f, s.ds.base, s.bx, 0x1234, 0x5678);
        set(f, &s); step(f); assert(state(f).bx == 0x1234);
        assert(f->trace[2].address == 0x1fffe && f->trace[3].address == 0x10000);
    }
    for (unsigned les = 0; les < 2; ++les) {
        uint8_t code[] = {0x36,(uint8_t)(les ? 0xc4 : 0xc5),0x07,
            (uint8_t)(les ? 0x26 : 0x3e),0xa0,0,2};
        bm_286_arch_state_t s = setup(f, code, sizeof(code));
        bm_286_segment_state_t *dest = les ? &s.es : &s.ds;
        dest->valid = 0; dest->limit = 0; dest->access = 0xff; dest->base = 0xab0000;
        pointer(f, s.ss.base, s.bx, 0x7654, 0x1234);
        f->ram[0x12540] = 0x42; set(f, &s); step(f); step(f);
        s = state(f); dest = les ? &s.es : &s.ds;
        assert(s.ax == 0x7642 && dest->base == 0x12340 && dest->limit == 0xffff);
        assert(dest->valid && dest->access == 0x82 && !s.interrupt_shadow);
    }
}
static void xlat(fixture_t *f)
{
    for (unsigned prefix = 0; prefix < 5; ++prefix)
        for (unsigned index = 0; index < 256; ++index) {
            uint8_t code[2]; unsigned length = 0, selected = prefix ? prefix - 1 : 3;
            bm_286_arch_state_t s, expected, actual; uint32_t address;
            if (prefix) code[length++] = (uint8_t)(0x26 + 8U * selected);
            code[length++] = 0xd7; s = setup(f, code, length);
            s.ax = (uint16_t)(0xa500 | index);
            address = seg(&s, selected)->base + (uint16_t)(s.bx + index);
            f->ram[address] = (uint8_t)(index ^ 0x5a); set(f, &s); step(f);
            actual = state(f); expected = s; expected.ax = (uint16_t)(0xa500 | (index ^ 0x5a));
            expected.ip += (uint16_t)length; same(&actual, &expected);
            assert(f->count == length + 1 && f->trace[length].address == address && f->trace[length].size == 1);
        }
    {
        const uint8_t code[] = {0xd7}; bm_286_arch_state_t s = setup(f, code, 1);
        s.ds.selector = 0xffff; s.ds.base = 0xffff0; s.bx = 0x200;
        f->ram[0x100270] = 0x42; set(f, &s); step(f);
        assert(state(f).ax == 0xa542 && f->trace[1].address == 0x100270);
    }
}
static void failures(fixture_t *f)
{
    const uint8_t codes[][5] = {{0x26,0x8d,0x86,0x34,0x12}, {0xc4,0x1f,0,0,0},
        {0xc5,0x1f,0,0,0}, {0x36,0xd7,0,0,0}};
    for (unsigned form = 0; form < 4; ++form)
        for (unsigned odd = 0; odd < 2; ++odd) {
            unsigned transfers = 0;
            for (unsigned fail = 0; fail <= transfers; ++fail) {
                bm_286_arch_state_t s = setup(f, codes[form], 5), a; bm_286_boundary_t b;
                s.bx = (uint16_t)(0x500 + odd); pointer(f, s.ds.base, s.bx, 0x1234, 0x5678);
                set(f, &s); f->fail_at = fail;
                if (!fail) {step(f); transfers = f->count; continue;}
                assert(bm_286_step(&f->cpu, &b) == BM_STATUS_DEVICE_ERROR);
                a = state(f); same(&s, &a);
                assert(bm_286_step(&f->cpu, &b) == BM_STATUS_INVALID_STATE && f->count == fail);
            }
        }
    for (unsigned op = 0; op < 3; ++op)
        for (unsigned mr = 192; mr < 256; ++mr) {
            uint8_t code[] = {(uint8_t)(op == 0 ? 0x8d : op == 1 ? 0xc4 : 0xc5),(uint8_t)mr};
            bm_286_arch_state_t s = setup(f, code, 2), a; bm_286_boundary_t b;
            f->allow_frame = 1;
            set(f, &s); assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK);
            a = state(f);
            assert(b.kind == BM_286_BOUNDARY_EXCEPTION && b.has_vector && b.vector == 6);
            s.sp -= 6; s.flags &= 0xfcffU; s.trap_pending = 0;
            s.cs.selector = 0; s.cs.base = 0; s.cs.access = 0x82; s.ip = 0;
            same(&s, &a); assert(f->count == 7);
        }
    for (unsigned form = 0; form < 4; ++form)
        for (unsigned bad = 0; bad < 6; ++bad) {
            bm_286_arch_state_t s = setup(f, codes[form], 5), a; bm_286_boundary_t b;
            s.bx = 0xffff; /* word access invalid; XLAT wraps to 007F */
            /* Imported PE refusal now tests strict clocks; functional PE is enabled. */
            uint64_t gate_cycles = 99;
            if (bad == 0) s.msw |= 1;
            if (bad == 1) f->ram[0x30100] = 0xf0;
            if (bad == 2) f->ram[0x30100] = 0xf3;
            if (bad == 3) s.cs.limit = 0x100;
            if (bad == 4) s.ds.valid = s.ss.valid = 0;
            if (bad == 5) s.ds.limit = s.ss.limit = 1;
            if (bad == 3) {
                f->allow_frame = 1;
                set(f, &s); assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK);
                a = state(f);
                assert(b.kind == BM_286_BOUNDARY_EXCEPTION && b.has_vector && b.vector == 13);
                assert(a.ax == s.ax && a.sp == (uint16_t)(s.sp-6));
                continue;
            }
            if (form == 0 && bad >= 4) continue; /* LEA needs no data segment. */
            if (form != 0 && bad == 5) { /* Operand #13 cannot build its stack frame. */
                set(f, &s); assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK);
                a = state(f); s.shutdown = 1; same(&s, &a);
                assert(b.kind == BM_286_BOUNDARY_SHUTDOWN && !b.has_vector);
                for (unsigned i = 0; i < f->count; ++i) assert(f->trace[i].operation == BM_BUS_FETCH);
                continue;
            }
            set(f, &s); assert((s.msw & 1U ? bm_286_step_clocked(f->cpu.context, 0, &gate_cycles) : bm_286_step(&f->cpu, &b)) == BM_STATUS_UNSUPPORTED);
            if (s.msw & 1U) assert(gate_cycles == 0);
            a = state(f); same(&s, &a);
            for (unsigned i = 0; i < f->count; ++i) assert(f->trace[i].operation == BM_BUS_FETCH);
        }
    for (unsigned les = 0; les < 2; ++les)
        for (unsigned bad = 0; bad < 3; ++bad) {
            uint8_t code[] = {(uint8_t)(les ? 0xc4 : 0xc5),0x07};
            bm_286_arch_state_t s = setup(f, code, 2), a; bm_286_boundary_t b;
            s.bx = (uint16_t)(bad == 0 ? 0xfffd : bad == 1 ? 0xffff : 0x500);
            if (bad == 2) s.ds.limit = 0x502; /* second word crosses limit */
            f->allow_frame = 1;
            set(f, &s); assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK);
            a = state(f);
            assert(b.kind == BM_286_BOUNDARY_EXCEPTION && b.has_vector && b.vector == 13);
            assert(a.ax == s.ax && a.ds.base == s.ds.base && a.es.base == s.es.base);
            assert(a.sp == (uint16_t)(s.sp-6) && f->count == 7);
            assert(f->ram[s.ss.base+a.sp] == (uint8_t)s.ip);
        }
}
int main(void)
{
    fixture_t f = {0}; bm_286_config_t config = {0}; bm_host_services_t host = bm_null_host_services();
    f.ram = calloc(0x1000000, 1); assert(f.ram);
    config.size = sizeof(config); config.version = BM_286_CONTRACT_VERSION;
    config.access = access_bus; config.access_context = &f;
    assert(bm_286_create(&host, &config, &f.cpu) == BM_STATUS_OK);
    lea_matrix(&f); far_loads(&f); xlat(&f); failures(&f);
    f.cpu.ops.destroy(f.cpu.context); free(f.ram); return 0;
}
