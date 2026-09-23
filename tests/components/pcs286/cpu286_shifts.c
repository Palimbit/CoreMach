/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Authored functional oracle and fault-injection cases, not hardware vectors.
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
    unsigned count, fail_at;
} fixture_t;
static bm_status_t access_bus(void *context, bm_bus_transaction_t *t)
{
    fixture_t *f = context;
    assert(f->count < 32 && t->address + t->size <= 0x1000000U);
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
        fprintf(stderr, "shift status=%d bytes=%02x %02x %02x accesses=%u\n",
                (int)status, f->ram[0x30100], f->ram[0x30101], f->ram[0x30102], f->count);
    assert(status == BM_STATUS_OK);
    assert(b.kind == BM_286_BOUNDARY_INSTRUCTION && !b.has_vector);
    assert(b.timing == BM_286_TIMING_UNKNOWN && b.cpu_cycles == b.bus_wait_cycles);
    assert(b.bus_wait_cycles == 2U * f->count);
}

/* Independent closed-form oracle: rotates use a finite bit ring, shifts use
 * wide multiplication/division. No production-style per-bit execution loop. */
static uint16_t oracle(unsigned op, unsigned bits, uint16_t input, unsigned raw,
                       uint16_t *flags)
{
    unsigned n = raw & 31U, carry = *flags & CF;
    uint32_t mask = (1U << bits) - 1U, sign = 1U << (bits - 1U);
    uint32_t value = input & mask, result;
    if (!n) return (uint16_t)value;
    if (op < 4) {
        unsigned width = bits + (op >= 2), k = n % width;
        uint32_t ring_mask = (1U << width) - 1U;
        uint32_t ring = value | (op >= 2 ? carry << bits : 0U);
        uint32_t rotated = !k ? ring : (op & 1) ?
            ((ring >> k) | (ring << (width - k))) & ring_mask :
            ((ring << k) | (ring >> (width - k))) & ring_mask;
        result = rotated & mask;
        carry = op >= 2 ? rotated >> bits : op == 0 ? result & 1U : result >> (bits - 1U);
    } else if (op == 4) {
        uint64_t product = (uint64_t)value * (UINT64_C(1) << n);
        result = (uint32_t)(product & mask); carry = (unsigned)((product >> bits) & 1U);
    } else {
        uint64_t extended = value | (op == 7 && (value & sign) ? ~(uint64_t)mask : 0U);
        result = (uint32_t)((extended >> n) & mask);
        carry = (unsigned)((extended >> (n - 1U)) & 1U);
    }
    *flags = (uint16_t)((*flags & ~CF) | carry);
    if (op >= 4) {
        unsigned parity = result & 0xffU;
        parity ^= parity >> 4U; parity ^= parity >> 2U; parity ^= parity >> 1U;
        *flags &= (uint16_t)~(AF | SF | ZF | PF);
        if (!result) *flags |= ZF;
        if (result & sign) *flags |= SF;
        if (!(parity & 1U)) *flags |= PF;
    }
    if (n == 1) {
        unsigned overflow = op == 7 ? 0 : op == 5 ? !!(value & sign) :
            op == 1 || op == 3 ? !!(result & sign) ^ !!(result & (sign >> 1U)) :
            !!(result & sign) ^ carry;
        *flags = (uint16_t)((*flags & ~OF) | (overflow ? OF : 0));
    }
    return (uint16_t)result;
}
static void exhaustive(fixture_t *f)
{
    const uint16_t words[] = {0,1,2,0x7fff,0x8000,0x8001,0xffff,0x5555,0xaaaa};
    uint8_t code[] = {0xc0,0xc0,0};
    bm_286_arch_state_t base = setup(f, code, sizeof(code));
    for (unsigned size = 1; size <= 2; ++size)
        for (unsigned op = 0; op < 8; ++op) {
            if (op == 6) continue;
            for (unsigned n = 0; n < 256; ++n)
                for (unsigned v = 0; v < (size == 1 ? 256U : 9U); ++v)
                    for (unsigned background = 0; background < 2; ++background)
                        for (unsigned carry = 0; carry < 2; ++carry) {
                            bm_286_arch_state_t s = base, e, a;
                            uint16_t result;
                            s.ax = size == 1 ? (uint16_t)(0xa500 | v) : words[v];
                            s.flags = (uint16_t)((background ? 0xed6 : 2) | carry);
                            e = s; result = oracle(op, size * 8, s.ax, n, &e.flags);
                            e.ax = size == 1 ? (uint16_t)(0xa500 | result) : result; e.ip += 3;
                            f->ram[0x30100] = (uint8_t)(0xbf + size);
                            f->ram[0x30101] = (uint8_t)(0xc0 | (op << 3));
                            f->ram[0x30102] = (uint8_t)n;
                            f->count = 0; set(f, &s); step(f); a = state(f); same(&a, &e);
                            assert(f->count == 3);
                        }
        }
}
static void register_forms(fixture_t *f)
{
    const uint8_t counts[] = {0,1,8,9,16,17,31,32,33,255};
    for (unsigned form = 0; form < 6; ++form)
        for (unsigned op = 0; op < 8; ++op)
            for (unsigned r = 0; r < 8; ++r)
                for (unsigned c = 0; c < sizeof(counts); ++c) {
                    uint8_t opcode = (uint8_t)(form < 2 ? 0xc0 + form : 0xce + form);
                    uint8_t code[] = {opcode,(uint8_t)(0xc0 | (op << 3) | r),counts[c]};
                    unsigned size = (form & 1) + 1, length = form < 2 ? 3 : 2;
                    bm_286_arch_state_t s, e, a; uint16_t input, output;
                    if (op == 6) continue;
                    s = setup(f, code, length); s.cx = (uint16_t)(0x8100 | counts[c]); e = s;
                    input = *reg(&s, size == 2 ? r : r & 3);
                    if (size == 1) input = (uint8_t)(input >> (r >= 4 ? 8 : 0));
                    output = oracle(op, size * 8, input, form == 2 || form == 3 ? 1 : counts[c], &e.flags);
                    if (size == 2) *reg(&e, r) = output;
                    else {
                        uint16_t *dest = reg(&e, r & 3);
                        *dest = r >= 4 ? (uint16_t)((*dest & 0xff) | (output << 8)) :
                            (uint16_t)((*dest & 0xff00) | output);
                    }
                    e.ip += (uint16_t)length; set(f, &s); step(f); a = state(f); same(&a, &e);
                    assert(f->count == length);
                }
}
static void memory_forms(fixture_t *f)
{
    for (unsigned form = 0; form < 6; ++form)
        for (unsigned op = 0; op < 8; ++op)
            for (unsigned source = 0; source < 6; ++source)
                for (unsigned odd = 0; odd < 2; ++odd)
                    for (unsigned zero = 0; zero < 2; ++zero) {
                        uint8_t code[5]; unsigned length = 0, size = (form & 1) + 1;
                        unsigned selected = source == 0 ? 3 : source == 1 ? 2 : source - 2;
                        unsigned n = form == 2 || form == 3 ? 1 : zero ? 32 : 9;
                        uint32_t address; uint16_t result; bm_286_arch_state_t s, e, a;
                        if (op == 6) continue;
                        if (source >= 2) code[length++] = (uint8_t)(0x26 + 8U * selected);
                        code[length++] = (uint8_t)(form < 2 ? 0xc0 + form : 0xce + form);
                        code[length++] = (uint8_t)((op << 3) | (source == 1 ? 0x46 : 7));
                        if (source == 1) code[length++] = 0;
                        if (form < 2) code[length++] = (uint8_t)n;
                        s = setup(f, code, length); s.bx = s.bp = (uint16_t)(0x500 + odd); s.cx = (uint16_t)n;
                        address = seg(&s, selected)->base + s.bx;
                        f->ram[address] = 0x81; f->ram[address + 1] = 0x80;
                        e = s; result = oracle(op, size * 8, 0x8081, n, &e.flags); e.ip += (uint16_t)length;
                        set(f, &s); step(f); a = state(f); same(&a, &e);
                        assert(f->ram[address] == (uint8_t)result);
                        assert(f->ram[address + 1] == (size == 1 ? 0x80 : result >> 8));
                        assert(f->count == length + (size == 2 && odd ? 2 : 1) * ((n & 31) ? 2 : 1));
                        assert(f->trace[length].address == address && f->trace[length].operation == BM_BUS_READ);
                        if (n & 31) assert(f->trace[f->count - 1].operation == BM_BUS_WRITE);
                    }
}
static void failures(fixture_t *f)
{
    for (unsigned form = 0; form < 6; ++form)
        for (unsigned odd = 0; odd < 2; ++odd) {
            uint8_t code[] = {0x36,(uint8_t)(form < 2 ? 0xc0 + form : 0xce + form),0x27,1};
            unsigned length = form < 2 ? 4 : 3, size = (form & 1) + 1;
            unsigned accesses = length + (size == 2 && odd ? 4 : 2);
            for (unsigned fail = 1; fail <= accesses; ++fail) {
                bm_286_arch_state_t s = setup(f, code, length), a;
                bm_286_boundary_t b; uint32_t address = s.ss.base + 0x500 + odd;
                s.bx = (uint16_t)(0x500 + odd); s.cx = 1;
                f->ram[address] = 0x81; f->ram[address + 1] = 0x80;
                f->fail_at = fail; set(f, &s);
                assert(bm_286_step(&f->cpu, &b) == BM_STATUS_DEVICE_ERROR);
                a = state(f); same(&a, &s); assert(f->count == fail);
                assert(f->ram[address] == (size == 2 && odd && fail == accesses ? 2 : 0x81));
                assert(f->ram[address + 1] == 0x80); /* completed low write cannot be undone */
                assert(bm_286_step(&f->cpu, &b) == BM_STATUS_INVALID_STATE);
                assert(f->count == fail); /* no silent retry */
            }
        }
    for (unsigned bad = 0; bad < 8; ++bad) {
        uint8_t code[] = {0xc1,0x27,1}; bm_286_boundary_t b;
        bm_286_arch_state_t s, a;
        if (bad == 0) code[1] = 0x37; /* undocumented /6 */
        if (bad == 1) code[0] = 0xf0;
        if (bad == 2) code[0] = 0xf3;
        s = setup(f, code, sizeof(code));
        if (bad == 3) s.msw |= 1;
        if (bad == 4) s.ds.valid = 0;
        if (bad == 5) s.ds.limit = s.bx;
        if (bad == 6) s.bx = 0xffff;
        if (bad == 7) s.cs.limit = s.ip + 1;
        if (bad == 5 || bad == 6) {
            set(f, &s); assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK);
            a = state(f);
            assert(b.has_vector && b.vector == 13 && b.kind == BM_286_BOUNDARY_EXCEPTION);
            assert(a.ax == s.ax && a.sp == (uint16_t)(s.sp-6));
            continue;
        }
        set(f, &s); assert(bm_286_step(&f->cpu, &b) == BM_STATUS_UNSUPPORTED);
        a = state(f); same(&a, &s);
        for (unsigned i = 0; i < f->count; ++i) assert(f->trace[i].operation == BM_BUS_FETCH);
    }
    { /* Masked zero still reads: failure is not silently swallowed. */
        const uint8_t code[] = {0xc1,0x27,32}; bm_286_boundary_t b;
        bm_286_arch_state_t s = setup(f, code, sizeof(code)), a;
        f->fail_at = 4; set(f, &s);
        assert(bm_286_step(&f->cpu, &b) == BM_STATUS_DEVICE_ERROR);
        a = state(f); same(&a, &s); assert(f->count == 4);
    }
}
int main(void)
{
    fixture_t f = {0}; bm_286_config_t config = {0}; bm_host_services_t host = bm_null_host_services();
    f.ram = calloc(0x1000000, 1); assert(f.ram);
    config.size = sizeof(config); config.version = BM_286_CONTRACT_VERSION;
    config.access = access_bus; config.access_context = &f;
    assert(bm_286_create(&host, &config, &f.cpu) == BM_STATUS_OK);
    exhaustive(&f); register_forms(&f); memory_forms(&f); failures(&f);
    f.cpu.ops.destroy(f.cpu.context); free(f.ram); return 0;
}
