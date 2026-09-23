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

/* Unsigned sign/magnitude oracle with binary long division. No signed / or %
 * and no production helper calls; no arithmetic is attempted for zero. */
static int oracle(unsigned bits, unsigned signed_op, uint32_t raw, uint16_t divisor,
                  uint16_t *quotient, uint16_t *remainder)
{
    uint64_t modulus = UINT64_C(1) << bits, full_modulus = modulus * modulus;
    uint64_t n = raw & (full_modulus - 1), d = divisor & (modulus - 1);
    uint64_t q = 0, r = 0;
    int negative_n = signed_op && (n & (full_modulus / 2));
    int negative_d = signed_op && (d & (modulus / 2));
    int negative_q = negative_n != negative_d;
    if (!d) return 1;
    if (negative_n) n = full_modulus - n;
    if (negative_d) d = modulus - d;
    for (unsigned i = 2 * bits; i > 0; --i) {
        r = r * 2 + ((n >> (i - 1)) & 1);
        if (r >= d) {r -= d; q |= UINT64_C(1) << (i - 1);}
    }
    if (q > (signed_op ? modulus / 2 - !negative_q : modulus - 1)) return 1;
    *quotient = (uint16_t)((negative_q ? modulus - q : q) & (modulus - 1));
    *remainder = (uint16_t)((negative_n ? modulus - r : r) & (modulus - 1));
    return 0;
}
static void fault_state(fixture_t *f, const bm_286_arch_state_t *before,
                        const bm_286_boundary_t *b)
{
    bm_286_arch_state_t e = *before, a = state(f);
    e.sp = (uint16_t)(e.sp - 6); e.flags &= 0xfcffU;
    e.cs.selector = 0x4000; e.cs.base = 0x40000; e.cs.limit = 0xffff;
    e.cs.valid = 1; e.cs.access = 0; e.ip = 0x200;
    e.interrupt_shadow = BM_286_SHADOW_NONE; e.trap_pending = 0;
    same(&a, &e);
    assert(b->kind == BM_286_BOUNDARY_EXCEPTION && b->has_vector && b->vector == 0);
    assert(b->instruction_ip == before->ip && b->instruction_address == before->cs.base + before->ip);
    assert(read_word(f, before->ss.base + (uint16_t)(before->sp - 2)) == before->flags);
    assert(read_word(f, before->ss.base + (uint16_t)(before->sp - 4)) == before->cs.selector);
    assert(read_word(f, before->ss.base + (uint16_t)(before->sp - 6)) == before->ip);
    assert(!f->acknowledgements);
}
static void scalar(fixture_t *f, const bm_286_arch_state_t *base, unsigned bits,
                   unsigned signed_op, uint32_t numerator, uint16_t divisor)
{
    bm_286_arch_state_t s = *base, e, a; bm_286_boundary_t b;
    uint16_t q = 0, r = 0; int fault = oracle(bits, signed_op, numerator, divisor, &q, &r);
    s.ax = (uint16_t)numerator; s.dx = bits == 16 ? (uint16_t)(numerator >> 16) : 0xbeef;
    s.bx = divisor; s.flags = (uint16_t)((numerator & 1) ? 0xed7 : 2); e = s;
    f->ram[0x30100] = (uint8_t)(bits == 8 ? 0xf6 : 0xf7);
    f->ram[0x30101] = (uint8_t)(signed_op ? 0xfb : 0xf3); /* BL/BX */
    f->count = 0; set(f, &s); b = step(f);
    if (fault) {
        fault_state(f, &s, &b); assert(f->count == 7);
    } else {
        if (bits == 8) e.ax = (uint16_t)(q | (r << 8));
        else {e.ax = q; e.dx = r;}
        e.ip += 2; a = state(f); same(&a, &e);
        assert(b.kind == BM_286_BOUNDARY_INSTRUCTION && !b.has_vector && f->count == 2);
    }
}
static void arithmetic(fixture_t *f)
{
    const uint8_t code[] = {0xf6,0xf3};
    const uint16_t divisors[] = {0,1,2,3,7,0x7f,0x80,0x81,0xaa,0xfe,0xff};
    const uint32_t numerators[] = {0,1,0x7fff,0x8000,0xffff,0x10000,
        0x7fffffff,0x80000000,0xffff0000,0xfffffffe,0xffffffff};
    bm_286_arch_state_t base = setup(f, code, sizeof(code));
    for (unsigned sign = 0; sign < 2; ++sign) {
        for (unsigned n = 0; n < 65536; ++n)
            for (unsigned d = 0; d < sizeof(divisors)/sizeof(divisors[0]); ++d)
                scalar(f, &base, 8, sign, n, divisors[d]);
        for (unsigned d = 0; d < 65536; ++d)
            for (unsigned n = 0; n < sizeof(numerators)/sizeof(numerators[0]); ++n)
                scalar(f, &base, 16, sign, numerators[n], (uint16_t)d);
        for (unsigned d = 0; d < 256; ++d)
            for (unsigned n = 0; n < sizeof(numerators)/sizeof(numerators[0]); ++n)
                scalar(f, &base, 8, sign, numerators[n] & 0xffff, (uint16_t)d);
    }
}
static uint16_t *reg(bm_286_arch_state_t *s, unsigned n)
{
    switch(n) {
    case 0: return &s->ax; case 1: return &s->cx; case 2: return &s->dx; case 3: return &s->bx;
    case 4: return &s->sp; case 5: return &s->bp; case 6: return &s->si; default: return &s->di;
    }
}
static void aliases(fixture_t *f)
{
    for (unsigned form = 0; form < 4; ++form)
        for (unsigned source = 0; source < 8; ++source) {
            uint8_t code[] = {(uint8_t)(form < 2 ? 0xf6 : 0xf7),
                (uint8_t)(0xf0 | ((form & 1) << 3) | source)};
            bm_286_arch_state_t s = setup(f, code, sizeof(code)), e, a;
            uint16_t d, q = 0, r = 0; unsigned bits = form < 2 ? 8 : 16; int fault;
            s.ax = 0x017f; s.dx = 0; s.cx = 0xff81; s.bx = 5; s.bp = 7; s.si = 2; s.di = 0xfffd;
            d = *reg(&s, bits == 8 ? source & 3 : source);
            if (bits == 8) d = (uint8_t)(d >> (source >= 4 ? 8 : 0));
            fault = oracle(bits, form & 1, s.ax, d, &q, &r);
            set(f, &s); bm_286_boundary_t b = step(f);
            if (fault) fault_state(f, &s, &b);
            else {
                e = s; e.ax = bits == 8 ? (uint16_t)(q | (r << 8)) : q;
                if (bits == 16) e.dx = r;
                e.ip += 2; a = state(f); same(&a, &e);
            }
        }
}
static void memory_and_failures(fixture_t *f)
{
    for (unsigned form = 0; form < 4; ++form)
        for (unsigned selection = 0; selection < 6; ++selection)
            for (unsigned odd = 0; odd < 2; ++odd)
                for (unsigned zero = 0; zero < 2; ++zero) {
                    uint8_t code[4]; unsigned length = 0, bits = form < 2 ? 8 : 16;
                    uint32_t bases[] = {0x20000,0x30000,0x10000,0};
                    unsigned segment = selection < 2 ? (selection ? 2 : 3) : selection - 2;
                    bm_286_arch_state_t s, e, a; bm_286_boundary_t b;
                    uint16_t q = 0, r = 0; uint32_t address; unsigned count;
                    if (selection >= 2) code[length++] = (uint8_t)(0x26 + 8U * segment);
                    code[length++] = (uint8_t)(bits == 8 ? 0xf6 : 0xf7);
                    code[length++] = (uint8_t)(0x30 | ((form & 1) << 3) | (selection == 1 ? 0x46 : 7));
                    if (selection == 1) code[length++] = 0;
                    s = setup(f, code, length); s.ax = 0x007f; s.dx = 0;
                    s.bx = s.bp = (uint16_t)(0x500 + odd); s.sp += (uint16_t)odd;
                    s.idtr.base = odd ? 0x901 : 0x900;
                    word(f, s.idtr.base, 0x200); word(f, s.idtr.base + 2, 0x4000);
                    address = bases[segment] + s.bx;
                    word(f, address, (uint16_t)(zero ? 0 : 3));
                    set(f, &s); b = step(f); count = f->count;
                    if (zero) fault_state(f, &s, &b);
                    else {
                        assert(!oracle(bits, form & 1, s.ax, 3, &q, &r)); e = s;
                        e.ax = bits == 8 ? (uint16_t)(q | (r << 8)) : q;
                        if (bits == 16) e.dx = r;
                        e.ip += (uint16_t)length; a = state(f); same(&a, &e);
                        for (unsigned i = 0; i < count; ++i) assert(f->trace[i].operation != BM_BUS_WRITE);
                    }
                    assert(f->trace[length].address == address);
                    for (unsigned fail = 1; fail <= count; ++fail) {
                        bm_bus_transaction_t completed[64]; unsigned written = 0;
                        (void)setup(f, code, length);
                        word(f, s.idtr.base, 0x200); word(f, s.idtr.base + 2, 0x4000);
                        memset(f->ram + s.ss.base + s.sp - 6, 0xcc, 6);
                        set(f, &s); f->fail_at = fail;
                        assert(bm_286_step(&f->cpu, &b) == BM_STATUS_DEVICE_ERROR);
                        a = state(f); same(&a, &s); assert(f->count == fail);
                        assert(!f->traced); /* Failed host delivery is not a completed #DE. */
                        for (unsigned i = 0; i + 1 < fail; ++i)
                            if (f->trace[i].operation == BM_BUS_WRITE) completed[written++] = f->trace[i];
                        for (unsigned i = 0; i < written; ++i)
                            for (unsigned j = 0; j < completed[i].size; ++j)
                                assert(f->ram[completed[i].address + j] == (uint8_t)(completed[i].value >> (8U*j)));
                        assert(bm_286_step(&f->cpu, &b) == BM_STATUS_INVALID_STATE);
                        assert(f->count == fail);
                    }
                }
}
static void recovery(fixture_t *f)
{
    /* Guest repairs divisor, IRET retries from the first override prefix. */
    const uint8_t code[] = {0x26,0x3e,0xf7,0xf3,0x90}; /* DIV BX */
    const uint8_t handler[] = {0xbb,2,0,0xcf}; /* MOV BX,2; IRET */
    for (unsigned tf = 0; tf < 2; ++tf) {
        bm_286_arch_state_t s = setup(f, code, sizeof(code)), a;
        bm_286_boundary_t b;
        s.ax = 10; s.dx = s.bx = 0; s.flags = (uint16_t)(0x202 | (tf ? 0x100 : 0));
        memcpy(f->ram + 0x40200, handler, sizeof(handler)); set(f, &s);
        b = step(f); fault_state(f, &s, &b);
        step(f); assert(state(f).bx == 2 && !state(f).trap_pending);
        step(f); a = state(f); assert(a.ip == s.ip && a.cs.selector == s.cs.selector);
        assert(a.flags == s.flags && a.sp == s.sp && !a.trap_pending);
        b = step(f); a = state(f);
        assert(b.kind == BM_286_BOUNDARY_INSTRUCTION && !b.has_vector);
        assert(a.ip == 0x104 && a.ax == 5 && !a.dx && a.trap_pending == tf);
        if (tf) {b = step(f); assert(b.kind == BM_286_BOUNDARY_EXCEPTION && b.vector == 1);}
    }
    { /* No INTA; IF/STI shadow cannot prevent a synchronous fault. */
        bm_286_arch_state_t s = setup(f, code, sizeof(code));
        bm_286_boundary_t b; s.bx = 0; s.flags = 0x302;
        s.interrupt_shadow = BM_286_SHADOW_INTR_ONLY; set(f, &s);
        assert(f->cpu.ops.signal(f->cpu.context, BM_286_SIGNAL_INTR, 1) == BM_STATUS_OK);
        b = step(f); fault_state(f, &s, &b);
    }
    { /* Explicit deferred-debug policy at an SS-shadow fault boundary. */
        bm_286_arch_state_t s = setup(f, code, sizeof(code));
        bm_286_boundary_t b; s.bx = 0; s.flags = 0x102;
        s.trap_pending = 1; s.interrupt_shadow = BM_286_SHADOW_SS_LOAD;
        s.nmi_blocked = 1; s.nmi_pending = 1; set(f, &s);
        b = step(f); fault_state(f, &s, &b);
        assert(state(f).nmi_blocked && state(f).nmi_pending && !state(f).trap_pending);
    }
    { /* Word frames wrap by offset; no extra error-code word for #DE. */
        bm_286_arch_state_t s = setup(f, code, sizeof(code));
        bm_286_boundary_t b; s.bx = 0; s.sp = 0; set(f, &s);
        b = step(f); fault_state(f, &s, &b);
        assert(state(f).sp == 0xfffa);
    }
}
static void rejected(fixture_t *f)
{
    for (unsigned bad = 0; bad < 8; ++bad) {
        uint8_t code[] = {0xf7,0x37}; /* DIV word [BX] */
        bm_286_arch_state_t s; bm_286_boundary_t b;
        if (bad == 0) code[0] = 0xf0;
        if (bad == 1) code[0] = 0xf3;
        s = setup(f, code, sizeof(code)); s.bx = 0x500; word(f, 0x500, 0);
        if (bad == 2) s.msw |= 1;
        if (bad == 3) s.ds.valid = 0;
        if (bad == 4) s.bx = 0xffff;
        if (bad == 5) s.idtr.limit = 2;
        if (bad == 6) s.ss.valid = 0;
        if (bad == 7) s.sp = 5; /* frame word would start at FFFF */
        set(f, &s); assert(bm_286_step(&f->cpu, &b) == BM_STATUS_UNSUPPORTED);
        bm_286_arch_state_t a = state(f); same(&a, &s);
        for (unsigned i = 0; i < f->count; ++i) assert(f->trace[i].operation != BM_BUS_WRITE);
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
    arithmetic(&f); aliases(&f); memory_and_failures(&f); recovery(&f); rejected(&f);
    f.cpu.ops.destroy(f.cpu.context); free(f.ram); return 0;
}
