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
    bm_cpu_t cpu;
    uint8_t *ram;
    bm_bus_transaction_t trace[64];
    unsigned count, fail_at, acknowledgements, traced;
    bm_286_boundary_t last_boundary;
} fixture_t;
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
static void trace_boundary(void *context, const bm_286_boundary_t *b)
{
    fixture_t *f = context;
    f->last_boundary = *b; ++f->traced;
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
    f->count = f->fail_at = f->acknowledgements = f->traced = 0;
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
    unsigned traced = f->traced;
    assert(bm_286_step(&f->cpu, &b) == BM_STATUS_OK);
    assert(b.timing == BM_286_TIMING_UNKNOWN && b.bus_wait_cycles == (f->count - before) * 2U);
    assert(b.cpu_cycles == b.bus_wait_cycles);
    assert(f->traced == traced + 1 && f->last_boundary.kind == b.kind);
    assert(f->last_boundary.has_vector == b.has_vector && f->last_boundary.vector == b.vector);
    return b;
}

/* Independent arithmetic oracle: split decimal corrections and explicit
 * carry/borrow between AL and AH. Flags defined by the ISA are checked;
 * undefined flags test our documented preservation policy only. */
static uint16_t szp(unsigned value)
{
    unsigned parity = 0;
    for (unsigned bit = 0; bit < 8; ++bit) parity ^= (value >> bit) & 1U;
    return (uint16_t)((!value ? 0x40 : 0) | (value >= 128 ? 0x80 : 0) |
                      (!parity ? 4 : 0));
}
static void oracle(bm_286_arch_state_t *e, unsigned op, unsigned radix)
{
    unsigned lo = e->ax % 256, hi = e->ax / 256;
    int adjust = lo % 16 >= 10 || (e->flags & 16);
    if (op == 0x37 || op == 0x3f) {
        if (adjust) {
            int low = (int)lo + (op == 0x37 ? 6 : -6);
            int high = (int)hi + (op == 0x37 ? 1 : -1);
            if (low >= 256) ++high;
            if (low < 0) --high;
            lo = (unsigned)low & 255; hi = (unsigned)high & 255;
        }
        e->ax = (uint16_t)(hi * 256 + lo % 16);
        e->flags = (uint16_t)((e->flags & 0xffeeU) | (adjust ? 17 : 0));
    } else {
        if (op == 0x27 || op == 0x2f) {
            int high = lo >= 154 || (e->flags & 1);
            int carry = high || (op == 0x2f && adjust && lo < 6);
            int delta = 6 * adjust + 96 * high;
            lo = (unsigned)((int)lo + (op == 0x27 ? delta : -delta)) & 255;
            e->ax = (uint16_t)(hi * 256 + lo);
            e->flags = (uint16_t)((e->flags & 0xffeeU) | (adjust ? 16 : 0) | carry);
        } else if (op == 0xd4) {
            assert(radix);
            hi = 0;
            while (lo >= radix) { lo -= radix; ++hi; }
            e->ax = (uint16_t)(hi * 256 + lo);
        } else {
            unsigned total = lo;
            for (unsigned i = 0; i < 8; ++i)
                if (hi & (1U << i)) total += radix << i;
            lo = total & 255; e->ax = (uint16_t)lo;
        }
        e->flags = (uint16_t)((e->flags & 0xff3bU) | szp(lo));
    }
    e->ip += (uint16_t)(op >= 0xd4 ? 2 : 1);
}
static void scalar(fixture_t *f, const bm_286_arch_state_t *base,
                   unsigned op, unsigned ax, unsigned flags, unsigned radix)
{
    bm_286_arch_state_t s = *base, e, a; bm_286_boundary_t b;
    s.ax = (uint16_t)ax; s.flags = (uint16_t)flags; e = s;
    oracle(&e, op, radix);
    f->ram[0x30100] = (uint8_t)op; f->ram[0x30101] = (uint8_t)radix;
    f->count = 0; set(f, &s); b = step(f); a = state(f); same(&a, &e);
    assert(b.kind == BM_286_BOUNDARY_INSTRUCTION && !b.has_vector);
    assert(f->count == (op >= 0xd4 ? 2U : 1U));
    for (unsigned i = 0; i < f->count; ++i) assert(f->trace[i].operation == BM_BUS_FETCH);
}
static void exhaustive(fixture_t *f)
{
    const uint8_t code[] = {0x27,0};
    const unsigned bases[] = {0,1,2,3,8,10,16,128,255};
    bm_286_arch_state_t s = setup(f, code, sizeof(code));
    /* DAA/DAS: every AL and all six arithmetic flag combinations, two AHs. */
    for (unsigned op = 0x27; op <= 0x2f; op += 8)
        for (unsigned al = 0; al < 256; ++al)
            for (unsigned bits = 0; bits < 64; ++bits)
                for (unsigned ah = 0; ah < 2; ++ah) {
                    unsigned flags = 0x602 | (bits & 1) | ((bits & 2) << 1) |
                        ((bits & 4) << 2) | ((bits & 8) << 3) |
                        ((bits & 16) << 3) | ((bits & 32) << 6);
                    scalar(f, &s, op, al + (ah ? 0xff00 : 0), flags, 10);
                }
    /* AAA/AAS: every AX, AF and background for all other arithmetic flags. */
    for (unsigned op = 0x37; op <= 0x3f; op += 8)
        for (unsigned ax = 0; ax < 65536; ++ax)
            for (unsigned bits = 0; bits < 4; ++bits)
                scalar(f, &s, op, ax, 2 | ((bits & 1) ? 16 : 0) |
                       ((bits & 2) ? 0xec5 : 0), 10);
    /* AAM: every AL/radix (nonzero), varied discarded AH and flag background. */
    for (unsigned al = 0; al < 256; ++al)
        for (unsigned radix = 1; radix < 256; ++radix)
            for (unsigned bg = 0; bg < 2; ++bg)
                scalar(f, &s, 0xd4, al + (bg ? 0xff00 : 0),
                       bg ? 0xed7 : 2, radix);
    /* AAD: every AX in nine bases, including base zero and modulo overflow. */
    for (unsigned ax = 0; ax < 65536; ++ax)
        for (unsigned i = 0; i < sizeof(bases)/sizeof(bases[0]); ++i)
            scalar(f, &s, 0xd5, ax, (ax & 1) ? 0xed7 : 2, bases[i]);
    /* Every remaining radix against an AL/AH boundary cross-product. */
    for (unsigned radix = 0; radix < 256; ++radix)
        for (unsigned al = 0; al < 256; al += 51)
            for (unsigned ah = 0; ah < 256; ah += 51)
                scalar(f, &s, 0xd5, al + ah * 256, 0xed7, radix);
}
static unsigned bcd(unsigned decimal) { return (decimal / 10) * 16 + decimal % 10; }
static void decimal_pairs(fixture_t *f)
{
    /* Validate packed decimal by decimal mathematics, not correction formulas:
     * all valid pairs, both carry/borrow inputs, following actual ADC/SBB. */
    for (unsigned subtract = 0; subtract < 2; ++subtract)
        for (unsigned left = 0; left < 100; ++left)
            for (unsigned right = 0; right < 100; ++right)
                for (unsigned carry = 0; carry < 2; ++carry) {
                    uint8_t code[] = {(uint8_t)(subtract ? 0x1c : 0x14),
                        (uint8_t)bcd(right), (uint8_t)(subtract ? 0x2f : 0x27)};
                    bm_286_arch_state_t s = setup(f, code, sizeof(code)), a;
                    int total = (int)left + (subtract ? -1 : 1) * (int)(right + carry);
                    int overflow = total < 0 || total >= 100;
                    s.ax = (uint16_t)(0x5a00 | bcd(left)); s.flags = (uint16_t)(2 | carry);
                    set(f, &s); step(f); step(f); a = state(f);
                    assert(a.ax == (0x5a00 | bcd((unsigned)(total + 100) % 100)));
                    assert((a.flags & 1U) == (unsigned)overflow);
                }
}
static void fault_state(fixture_t *f, const bm_286_arch_state_t *s,
                        const bm_286_boundary_t *b)
{
    bm_286_arch_state_t e = *s, a = state(f);
    e.ip = 0x200; e.cs.selector = 0x4000; e.cs.base = 0x40000;
    e.cs.limit = 0xffff; e.cs.valid = 1; e.cs.access = 0;
    e.sp = (uint16_t)(s->sp - 6); e.flags &= 0xfcffU;
    e.trap_pending = 0; e.interrupt_shadow = BM_286_SHADOW_NONE;
    same(&e, &a);
    assert(b->kind == BM_286_BOUNDARY_EXCEPTION && b->has_vector && !b->vector);
    assert(b->instruction_ip == s->ip);
    assert(read_word(f, s->ss.base + (uint16_t)(s->sp - 2)) == s->flags);
    assert(read_word(f, s->ss.base + (uint16_t)(s->sp - 4)) == s->cs.selector);
    assert(read_word(f, s->ss.base + (uint16_t)(s->sp - 6)) == s->ip);
    assert(!f->acknowledgements);
}
static void prefixes_and_failures(fixture_t *f)
{
    const uint8_t ops[] = {0x27,0x2f,0x37,0x3f,0xd4,0xd5};
    for (unsigned k = 0; k < sizeof(ops); ++k)
        for (unsigned seg = 0; seg < 4; ++seg)
            for (unsigned zero = 0; zero < 2; ++zero)
                for (unsigned odd = 0; odd < 2; ++odd) {
                    uint8_t code[] = {(uint8_t)(0x26 + 8*seg), ops[k], (uint8_t)(zero ? 0 : 10)};
                    size_t length = ops[k] >= 0xd4 ? 3 : 2;
                    bm_286_arch_state_t s = setup(f, code, length), e, a;
                    bm_286_boundary_t b; unsigned count;
                    s.ax = 0xffff; s.flags = 0xed7; s.sp += (uint16_t)odd;
                    s.idtr.base = 0x900 + odd;
                    word(f, s.idtr.base, 0x200); word(f, s.idtr.base + 2, 0x4000);
                    set(f, &s); b = step(f); count = f->count;
                    if (ops[k] == 0xd4 && zero) fault_state(f, &s, &b);
                    else { e = s; oracle(&e, ops[k], code[2]); ++e.ip; a = state(f); same(&e, &a); }
                    for (unsigned fail = 1; fail <= count; ++fail) {
                        assert(f->cpu.ops.reset(f->cpu.context) == BM_STATUS_OK);
                        f->count = f->traced = 0; f->fail_at = fail; set(f, &s);
                        memset(f->ram + 0x10700, 0xa5, 0x110);
                        assert(bm_286_step(&f->cpu, &b) == BM_STATUS_DEVICE_ERROR);
                        a = state(f); same(&s, &a); assert(!f->traced && f->count == fail);
                        for (unsigned i = 0; i + 1 < fail; ++i)
                            if (f->trace[i].operation == BM_BUS_WRITE)
                                for (unsigned j = 0; j < f->trace[i].size; ++j)
                                    assert(f->ram[f->trace[i].address+j] ==
                                        (uint8_t)(f->trace[i].value >> (8*j)));
                        assert(bm_286_step(&f->cpu, &b) == BM_STATUS_INVALID_STATE);
                        assert(f->count == fail);
                    }
                }
    { /* Guest patches AAM's radix in CS, IRET restarts at its first prefix. */
        const uint8_t code[] = {0x26,0xd4,0,0x90};
        const uint8_t handler[] = {0xc6,0x06,0x02,0x01,10,0xcf}; /* MOV byte [0102],10 */
        bm_286_arch_state_t s = setup(f, code, sizeof(code)), a;
        s.ax = 99; s.flags = 0x302; s.ds = s.cs;
        memcpy(f->ram + 0x40200, handler, sizeof(handler));
        set(f, &s); bm_286_boundary_t b = step(f); fault_state(f, &s, &b);
        step(f); step(f); a = state(f);
        assert(a.ip == s.ip && a.flags == s.flags && a.ax == s.ax && a.sp == s.sp);
        b = step(f); a = state(f);
        assert(b.kind == BM_286_BOUNDARY_INSTRUCTION && a.ax == 0x0909 && a.ip == 0x103);
        assert(a.trap_pending); b = step(f);
        assert(b.kind == BM_286_BOUNDARY_EXCEPTION && b.vector == 1);
    }
    for (unsigned ax = 0; ax < 256; ++ax) {
        const uint8_t code[] = {0xd4,0};
        bm_286_arch_state_t s = setup(f, code, sizeof(code));
        s.ax = (uint16_t)(0xff00 | ax); set(f, &s);
        bm_286_boundary_t b = step(f); fault_state(f, &s, &b);
    }
    for (unsigned bad = 0; bad < 4; ++bad) {
        uint8_t code[] = {0xf0,0xd4,10};
        if (bad == 1) code[0] = 0xf2;
        if (bad == 2) code[0] = 0xf3;
        if (bad == 3) code[0] = 0xd4;
        bm_286_arch_state_t s = setup(f, code, sizeof(code)), a;
        bm_286_boundary_t b; if (bad == 3) s.msw |= 1;
        set(f, &s); assert(bm_286_step(&f->cpu, &b) == BM_STATUS_UNSUPPORTED);
        a = state(f); same(&a, &s); assert(f->count == (bad == 3 ? 0U : 1U));
    }
}
int main(void)
{
    fixture_t f = {0}; bm_host_services_t host = bm_null_host_services(); bm_286_config_t config = {0};
    f.ram = calloc(0x100000, 1); assert(f.ram);
    config.size = sizeof(config); config.version = BM_286_CONTRACT_VERSION;
    config.access = access_bus; config.access_context = &f;
    config.interrupt_ack = ack; config.interrupt_context = &f;
    config.trace = trace_boundary; config.trace_context = &f;
    assert(bm_286_create(&host, &config, &f.cpu) == BM_STATUS_OK);
    exhaustive(&f); decimal_pairs(&f); prefixes_and_failures(&f);
    f.cpu.ops.destroy(f.cpu.context); free(f.ram); return 0;
}
