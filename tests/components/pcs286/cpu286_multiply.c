/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Authored multiplication oracle and fault-injection cases, not hardware vectors.
 */
#include <blumach/components/cpu_80286.h>
#include <blumach/platforms/null_host.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

enum {CF=1, PF=4, AF=16, ZF=64, SF=128, OF=2048};
typedef struct fixture {
    bm_cpu_t cpu;
    uint8_t *ram;
    bm_bus_transaction_t trace[32];
    unsigned count, fail_at, allow_fault_frame;
} fixture_t;
static bm_status_t access_bus(void *context, bm_bus_transaction_t *t)
{
    fixture_t *f = context;
    assert(f->count < 32 && t->address + t->size <= 0x1000000U);
    assert(t->operation != BM_BUS_WRITE || f->allow_fault_frame);
    assert(t->size == 1 || t->size == 2);
    assert(t->endianness == BM_ENDIAN_LITTLE && !t->wait_states);
    assert(t->space == (t->operation == BM_BUS_FETCH ? BM_ADDRESS_PROGRAM : BM_ADDRESS_DATA));
    f->trace[f->count++] = *t;
    if (f->fail_at == f->count) return BM_STATUS_DEVICE_ERROR;
    if (t->operation == BM_BUS_WRITE) {
        for (unsigned i = 0; i < t->size; ++i)
            f->ram[(size_t)t->address + i] = (uint8_t)(t->value >> (8U * i));
    } else {
        t->value = 0;
        for (unsigned i = 0; i < t->size; ++i)
            t->value |= (uint64_t)f->ram[(size_t)t->address + i] << (8U * i);
    }
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
static bm_286_arch_state_t setup(fixture_t *f, const uint8_t *code, size_t length)
{
    bm_286_arch_state_t s;
    assert(f->cpu.ops.reset(f->cpu.context) == BM_STATUS_OK);
    f->count = f->fail_at = 0; s = state(f); s.ip = 0x100;
    s.cs.selector = 0x3000; s.cs.base = 0x30000;
    s.ds.selector = 0x1000; s.ds.base = 0x10000;
    s.ss.selector = 0x2000; s.ss.base = 0x20000;
    s.es.selector = 0x4000; s.es.base = 0x40000;
    s.ax = 0xa580; s.cx = 0x1209; s.dx = 0x5678; s.bx = 0x501;
    s.si = 0x321; s.di = 0x432; s.bp = 0x501; s.sp = 0x800;
    s.flags = 0x0ed7; s.nmi_blocked = 1;
    memcpy(f->ram + 0x30100, code, length); return s;
}
static void step(fixture_t *f)
{
    bm_286_boundary_t b;
    bm_status_t status = bm_286_step(&f->cpu, &b);
    if (status != BM_STATUS_OK)
        fprintf(stderr, "multiply status=%d bytes=%02x %02x %02x accesses=%u\n",
                (int)status, f->ram[0x30100], f->ram[0x30101], f->ram[0x30102], f->count);
    assert(status == BM_STATUS_OK);
    assert(b.kind == BM_286_BOUNDARY_INSTRUCTION && !b.has_vector);
    assert(b.timing == BM_286_TIMING_UNKNOWN && b.cpu_cycles == b.bus_wait_cycles);
    assert(b.bus_wait_cycles == 2U * f->count);
}


/* Independent sign/magnitude oracle: unsigned 64-bit multiplication, followed
 * by two's-complement encoding and a discarded-half sign-extension check. */
static uint32_t product(unsigned bits, unsigned signed_op, uint16_t a, uint16_t b, int *overflow)
{
    uint64_t modulus = UINT64_C(1) << bits, mask = modulus - 1;
    uint64_t x = a & mask, y = b & mask, magnitude, encoded, high, expected;
    int negative_x = signed_op && (x & (modulus / 2));
    int negative_y = signed_op && (y & (modulus / 2));
    if (negative_x) x = modulus - x;
    if (negative_y) y = modulus - y;
    magnitude = x * y;
    encoded = negative_x != negative_y ? (modulus * modulus - magnitude) : magnitude;
    encoded &= modulus * modulus - 1;
    high = encoded >> bits;
    expected = signed_op && (encoded & (modulus / 2)) ? mask : 0;
    *overflow = high != expected;
    return (uint32_t)encoded;
}
static void flags(bm_286_arch_state_t *s, int overflow)
{
    s->flags = (uint16_t)((s->flags & ~(CF | OF)) | (overflow ? CF | OF : 0));
}
static void scalar(fixture_t *f, const bm_286_arch_state_t *base,
                   unsigned size, unsigned signed_op, uint16_t a, uint16_t source, unsigned background)
{
    bm_286_arch_state_t s = *base, e, actual; int overflow;
    uint32_t p = product(size * 8, signed_op, a, source, &overflow);
    s.ax = size == 1 ? (uint16_t)(0xa500 | a) : a;
    s.bx = source; s.flags = (uint16_t)(background ? 0xed7 : 2); e = s;
    e.ax = (uint16_t)p; if (size == 2) e.dx = (uint16_t)(p >> 16);
    flags(&e, overflow); e.ip += 2;
    f->ram[0x30100] = (uint8_t)(size == 1 ? 0xf6 : 0xf7);
    f->ram[0x30101] = (uint8_t)(0xe3 | (signed_op << 3)); /* BL/BX */
    f->count = 0; set(f, &s); step(f); actual = state(f); same(&actual, &e);
    assert(f->count == 2);
}
static void arithmetic(fixture_t *f)
{
    const uint16_t edge[] = {0,1,2,0x7f,0x80,0xff,0x100,0x7fff,0x8000,0xfffe,0xffff};
    const uint8_t code[] = {0xf6,0xe3};
    bm_286_arch_state_t base = setup(f, code, sizeof(code));
    for (unsigned sign = 0; sign < 2; ++sign)
        for (unsigned bg = 0; bg < 2; ++bg) {
            for (unsigned a = 0; a < 256; ++a)
                for (unsigned b = 0; b < 256; ++b)
                    scalar(f, &base, 1, sign, (uint16_t)a, (uint16_t)b, bg);
            for (unsigned a = 0; a < 65536; ++a)
                for (unsigned b = 0; b < sizeof(edge)/sizeof(edge[0]); ++b)
                    scalar(f, &base, 2, sign, (uint16_t)a, edge[b], bg);
        }
}
static void immediate(fixture_t *f)
{
    const uint16_t words[] = {0,1,2,0x7fff,0x8000,0x8001,0xffff,0x5555,0xaaaa};
    const uint8_t bytes[] = {0,1,0x7f,0x80,0xff};
    const uint8_t code[] = {0x69,0xd3,0,0}; /* DX <- BX * imm */
    bm_286_arch_state_t base = setup(f, code, sizeof(code));
    for (unsigned form = 0; form < 3; ++form)
        for (unsigned n = 0; n < (form == 1 ? 256U : 65536U); ++n)
            for (unsigned j = 0; j < (form == 2 ? 5U : 9U); ++j) {
                unsigned imm = form == 2 ? bytes[j] : n;
                uint16_t source = form == 2 ? (uint16_t)n : words[j];
                uint16_t multiplier = form == 0 || imm < 128 ? (uint16_t)imm : (uint16_t)(0xff00 | imm);
                bm_286_arch_state_t s = base, e, a; int overflow;
                uint32_t p = product(16, 1, source, multiplier, &overflow);
                s.bx = source; e = s; e.dx = (uint16_t)p;
                flags(&e, overflow); e.ip += (uint16_t)(form == 0 ? 4 : 3);
                f->ram[0x30100] = (uint8_t)(form == 0 ? 0x69 : 0x6b);
                f->ram[0x30102] = (uint8_t)imm; f->ram[0x30103] = (uint8_t)(imm >> 8);
                f->count = 0; set(f, &s); step(f); a = state(f); same(&a, &e);
                assert(f->count == (form == 0 ? 4U : 3U));
            }
}
static void conversions(fixture_t *f)
{
    const uint8_t code[] = {0x98};
    bm_286_arch_state_t base = setup(f, code, sizeof(code));
    for (unsigned op = 0; op < 2; ++op)
        for (unsigned v = 0; v < 65536; ++v)
            for (unsigned bg = 0; bg < 2; ++bg) {
                bm_286_arch_state_t s = base, e, a;
                s.ax = (uint16_t)v; s.flags = (uint16_t)(bg ? 0xed7 : 2); e = s;
                if (op) e.dx = v >= 32768 ? 0xffff : 0;
                else e.ax = (uint16_t)((v % 256) + (v % 256 >= 128 ? 0xff00 : 0));
                e.ip++; f->ram[0x30100] = (uint8_t)(0x98 + op);
                f->count = 0; set(f, &s); step(f); a = state(f); same(&a, &e);
                assert(f->count == 1);
            }
}
static void register_aliases(fixture_t *f)
{
    for (unsigned form = 0; form < 6; ++form)
        for (unsigned r = 0; r < 8; ++r)
            for (unsigned dest = 0; dest < 8; ++dest) {
                unsigned size = form < 2 ? 1 : 2, signed_op = (form & 1) || form >= 4;
                uint8_t code[] = {(uint8_t)(form < 2 ? 0xf6 : form < 4 ? 0xf7 : form == 4 ? 0x69 : 0x6b),
                    (uint8_t)(0xc0 | ((form >= 4 ? dest : 4 + signed_op) << 3) | r),0x80,0xff};
                unsigned length = form < 4 ? 2 : form == 4 ? 4 : 3;
                bm_286_arch_state_t s = setup(f, code, length), e = s, a;
                uint16_t source = *reg(&s, size == 1 ? r & 3 : r); uint32_t p; int overflow;
                if (size == 1) source = (uint8_t)(source >> (r >= 4 ? 8 : 0));
                p = product(size * 8, signed_op, source, form >= 4 ? 0xff80 : s.ax, &overflow);
                if (form >= 4) *reg(&e, dest) = (uint16_t)p;
                else {e.ax = (uint16_t)p; if (size == 2) e.dx = (uint16_t)(p >> 16);}
                flags(&e, overflow); e.ip += (uint16_t)length;
                set(f, &s); step(f); a = state(f); same(&a, &e); assert(f->count == length);
            }
}
static void memory_cases(fixture_t *f)
{
    for (unsigned form = 0; form < 6; ++form)
        for (unsigned selected = 0; selected < 6; ++selected)
            for (unsigned odd = 0; odd < 2; ++odd) {
                unsigned size = form < 2 ? 1 : 2, signed_op = (form & 1) || form >= 4;
                unsigned segment = selected < 2 ? (selected == 0 ? 3 : 2) : selected - 2;
                uint8_t code[6]; unsigned length = 0;
                bm_286_arch_state_t s, e, a; uint32_t address, p; int overflow;
                if (selected >= 2) code[length++] = (uint8_t)(0x26 + 8U * segment);
                code[length++] = (uint8_t)(form < 2 ? 0xf6 : form < 4 ? 0xf7 : form == 4 ? 0x69 : 0x6b);
                /* Immediate destination BX aliases the default address register. */
                code[length++] = (uint8_t)(((form >= 4 ? 3 : 4 + signed_op) << 3) |
                    (selected == 1 ? 0x46 : 7));
                if (selected == 1) code[length++] = 0;
                if (form >= 4) code[length++] = 0x80;
                if (form == 4) code[length++] = 0xff;
                s = setup(f, code, length); s.bx = s.bp = (uint16_t)(0x500 + odd); e = s;
                address = seg(&s, segment)->base + s.bx;
                f->ram[address] = 0x81; f->ram[address + 1] = 0x80;
                p = product(size * 8, signed_op, 0x8081, form >= 4 ? 0xff80 : s.ax, &overflow);
                if (form >= 4) e.bx = (uint16_t)p;
                else {e.ax = (uint16_t)p; if (size == 2) e.dx = (uint16_t)(p >> 16);}
                flags(&e, overflow); e.ip += (uint16_t)length;
                set(f, &s); step(f); a = state(f); same(&a, &e);
                assert(f->count == length + (size == 2 && odd ? 2 : 1));
                assert(f->trace[length].address == address && f->trace[length].operation == BM_BUS_READ);
                assert(f->ram[address] == 0x81 && f->ram[address + 1] == 0x80);
                for (unsigned fail = 1; fail <= length + (size == 2 && odd ? 2U : 1U); ++fail) {
                    bm_286_boundary_t b; bm_286_arch_state_t original = s;
                    (void)setup(f, code, length); f->fail_at = fail; set(f, &original);
                    assert(bm_286_step(&f->cpu, &b) == BM_STATUS_DEVICE_ERROR);
                    a = state(f); same(&a, &original); assert(f->count == fail);
                    assert(bm_286_step(&f->cpu, &b) == BM_STATUS_INVALID_STATE);
                    assert(f->count == fail);
                }
            }
}
static void rejected(fixture_t *f)
{
    for (unsigned bad = 0; bad < 10; ++bad) {
        uint8_t code[] = {0x69,0x07,0xff,0xff}; bm_286_boundary_t b;
        bm_286_arch_state_t s, a;
        if (bad == 0) code[0] = 0xf0;
        if (bad == 1) code[0] = 0xf2;
        if (bad == 2) code[0] = 0xf3;
        if (bad == 7) {code[0] = 0xf6; code[1] = 0xc8;} /* Undefined /1 byte */
        if (bad == 8) {code[0] = 0x0f; code[1] = 0xaf;} /* 386 IMUL not a 286 form */
        if (bad == 9) {code[0] = 0xf7; code[1] = 0xc8;} /* Undefined /1 word */
        s = setup(f, code, sizeof(code));
        if (bad == 3) s.msw |= 1;
        if (bad == 4) s.ds.valid = 0;
        if (bad == 5) s.bx = 0xffff;
        if (bad == 6) s.cs.limit = s.ip + 2;
        if (bad == 5 || bad == 6) {
            f->allow_fault_frame = 1; set(f, &s);
            assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK);
            f->allow_fault_frame = 0; a = state(f);
            assert(b.has_vector && b.vector == 13 && b.kind == BM_286_BOUNDARY_EXCEPTION);
            assert(a.ax == s.ax && a.dx == s.dx && a.sp == (uint16_t)(s.sp-6));
            continue;
        }
        set(f, &s); assert(bm_286_step(&f->cpu, &b) == BM_STATUS_UNSUPPORTED);
        a = state(f); same(&s, &a);
        for (unsigned i = 0; i < f->count; ++i) assert(f->trace[i].operation == BM_BUS_FETCH);
    }
    for (unsigned op = 0; op < 2; ++op) {
        uint8_t code[] = {(uint8_t)(0x98 + op)};
        bm_286_arch_state_t s = setup(f, code, sizeof(code)), a; bm_286_boundary_t b;
        f->fail_at = 1; set(f, &s); assert(bm_286_step(&f->cpu, &b) == BM_STATUS_DEVICE_ERROR);
        a = state(f); same(&a, &s);
    }
}
int main(void)
{
    fixture_t f = {0}; bm_286_config_t config = {0}; bm_host_services_t host = bm_null_host_services();
    f.ram = calloc(0x1000000, 1); assert(f.ram);
    config.size = sizeof(config); config.version = BM_286_CONTRACT_VERSION;
    config.access = access_bus; config.access_context = &f;
    assert(bm_286_create(&host, &config, &f.cpu) == BM_STATUS_OK);
    arithmetic(&f); immediate(&f); conversions(&f); register_aliases(&f); memory_cases(&f); rejected(&f);
    f.cpu.ops.destroy(f.cpu.context); free(f.ram); return 0;
}
