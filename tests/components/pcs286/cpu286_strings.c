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
    unsigned count, fail_at, acknowledgements, traced, hold_at;
    bm_286_boundary_t last_boundary;
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
    assert(t->space != BM_ADDRESS_IO && t->address + t->size <= 0x1000000);
    assert(!t->wait_states && f->count < 64);
    assert(t->endianness == BM_ENDIAN_LITTLE);
    assert(t->space == (t->operation == BM_BUS_FETCH ? BM_ADDRESS_PROGRAM : BM_ADDRESS_DATA));
    f->trace[f->count++] = *t;
    if (f->hold_at == f->count)
        assert(f->cpu.ops.signal(f->cpu.context, BM_286_SIGNAL_HOLD, 1) == BM_STATUS_OK);
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
    f->count = f->fail_at = f->acknowledgements = f->traced = f->hold_at = 0;
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

static bm_286_segment_state_t *segment(bm_286_arch_state_t *s, unsigned index)
{
    switch (index) {case 0:return &s->es; case 1:return &s->cs;
    case 2:return &s->ss; default:return &s->ds;}
}
static uint16_t comparison(unsigned left, unsigned right, unsigned size, uint16_t flags)
{
    unsigned modulus = size == 1 ? 256 : 65536, sign = modulus / 2;
    unsigned result = (left + modulus - right) % modulus, parity = 0;
    int sl = (int)left - (left >= sign ? (int)modulus : 0);
    int sr = (int)right - (right >= sign ? (int)modulus : 0);
    int difference = sl - sr;
    flags &= 0xf72aU;
    if (left < right) flags |= 1;
    if (left % 16 < right % 16) flags |= 16;
    if (result == 0) flags |= 64;
    if (result >= sign) flags |= 128;
    if (difference < -(int)sign || difference >= (int)sign) flags |= 2048;
    for (unsigned bit = 0; bit < 8; ++bit) parity += (result >> bit) & 1U;
    if (!(parity % 2)) flags |= 4;
    return flags;
}
static void expected(bm_286_arch_state_t *e, unsigned op, unsigned length,
                     unsigned source, unsigned destination)
{
    unsigned kind = op & 0xfe, size = (op & 1) + 1;
    int delta = (e->flags & 0x400) ? -(int)size : (int)size;
    if (kind == 0xa6 || kind == 0xae)
        e->flags = comparison(kind == 0xae ? e->ax & (size == 1 ? 255U : 65535U) : source,
                              destination, size, e->flags);
    if (kind == 0xac)
        e->ax = (uint16_t)(size == 1 ? (e->ax & 0xff00U) | source : source);
    if (kind == 0xa4 || kind == 0xa6 || kind == 0xac) e->si = (uint16_t)(e->si + delta);
    if (kind != 0xac) e->di = (uint16_t)(e->di + delta);
    e->ip += (uint16_t)length;
}
static void endpoint(fixture_t *f, unsigned *index, unsigned address, unsigned size, int write)
{
    unsigned fragments = size == 2 && (address & 1) ? 2 : 1;
    for (unsigned i = 0; i < fragments; ++i) {
        bm_bus_transaction_t *t = &f->trace[(*index)++];
        assert(t->address == ((address + i) & 0xffffffU));
        assert(t->operation == (write ? BM_BUS_WRITE : BM_BUS_READ));
        assert(t->size == (fragments == 2 ? 1 : size));
        assert(t->alignment == t->size);
    }
}
static void matrix(fixture_t *f)
{
    const uint8_t ops[] = {0xa4,0xa5,0xa6,0xa7,0xaa,0xab,0xac,0xad,0xae,0xaf};
    const uint16_t counts[] = {0,1,0xffff}, patterns[] = {0,0x807f,0xffff};
    for (unsigned k = 0; k < sizeof(ops); ++k)
        for (unsigned prefix = 0; prefix < 5; ++prefix)
            for (unsigned df = 0; df < 2; ++df)
                for (unsigned parity = 0; parity < 4; ++parity)
                    for (unsigned cx = 0; cx < 3; ++cx)
                        for (unsigned v = 0; v < 3; ++v) {
                            uint8_t code[2]; unsigned length = 0, op = ops[k], kind = op & 0xfe;
                            unsigned size = (op & 1) + 1, mask = size == 1 ? 255 : 65535;
                            unsigned src, dst, n, source = patterns[v] & mask;
                            unsigned destination = patterns[(v+1)%3] & mask;
                            bm_286_arch_state_t s, e, a; bm_286_boundary_t b;
                            if (prefix) code[length++] = (uint8_t)(0x26 + 8*(prefix-1));
                            code[length++] = (uint8_t)op; s = setup(f, code, length);
                            s.si = (uint16_t)(0x500 + (parity & 1));
                            s.di = (uint16_t)(0x600 + (parity >> 1));
                            s.cx = counts[cx]; s.flags = (uint16_t)(0xad7 | (df ? 0x400 : 0));
                            src = segment(&s, prefix ? prefix-1 : 3)->base + s.si;
                            dst = s.es.base + s.di; word(f, src, patterns[v]); word(f, dst, patterns[(v+1)%3]);
                            e = s; expected(&e, op, length, source, destination);
                            set(f, &s); b = step(f); a = state(f); same(&e, &a);
                            assert(b.kind == BM_286_BOUNDARY_INSTRUCTION && !b.has_vector);
                            n = length;
                            if (kind == 0xa6 || kind == 0xae) endpoint(f, &n, dst, size, 0);
                            if (kind == 0xa4 || kind == 0xa6 || kind == 0xac) endpoint(f, &n, src, size, 0);
                            if (kind == 0xa4 || kind == 0xaa) {
                                endpoint(f, &n, dst, size, 1);
                                unsigned written = kind == 0xaa ? s.ax & mask : source;
                                assert((read_word(f, dst) & mask) == written);
                                if (size == 1) assert(f->ram[dst+1] == (patterns[(v+1)%3] >> 8));
                            } else assert(read_word(f, dst) == patterns[(v+1)%3]);
                            assert(n == f->count);
                        }
}
static void compare_case(fixture_t *f, const bm_286_arch_state_t *base,
                        unsigned op, unsigned left, unsigned right, unsigned bg)
{
    bm_286_arch_state_t s = *base, e, a;
    s.flags = (uint16_t)(bg ? 0xed7 : 2); s.ax = (uint16_t)left;
    f->ram[0x30100] = (uint8_t)op; word(f, s.si, (uint16_t)left);
    word(f, s.es.base+s.di, (uint16_t)right); e = s;
    expected(&e, op, 1, left, right); f->count = 0; set(f, &s); step(f); a = state(f); same(&e, &a);
}
static void comparisons(fixture_t *f)
{
    const uint8_t code[] = {0xa6};
    const unsigned edges[] = {0,1,15,16,127,128,255,256,0x7fff,0x8000,0xffff};
    bm_286_arch_state_t s = setup(f, code, sizeof(code)); s.si = 0x500; s.di = 0x600;
    for (unsigned kind = 0; kind < 2; ++kind)
        for (unsigned bg = 0; bg < 2; ++bg) {
            for (unsigned a = 0; a < 256; ++a)
                for (unsigned b = 0; b < 256; ++b)
                    compare_case(f, &s, kind ? 0xae : 0xa6, a, b, bg);
            for (unsigned a = 0; a < 65536; ++a)
                for (unsigned i = 0; i < sizeof(edges)/sizeof(edges[0]); ++i)
                    compare_case(f, &s, kind ? 0xaf : 0xa7, a, edges[i], bg);
        }
}
static void failures(fixture_t *f)
{
    const uint8_t ops[] = {0xa4,0xa5,0xa6,0xa7,0xaa,0xab,0xac,0xad,0xae,0xaf};
    for (unsigned k = 0; k < sizeof(ops); ++k)
        for (unsigned odd = 0; odd < 4; ++odd) {
            uint8_t code[] = {0x36,ops[k]};
            bm_286_arch_state_t s = setup(f, code, sizeof(code)), a;
            bm_286_boundary_t b; unsigned count;
            s.si = (uint16_t)(0x500 + (odd & 1)); s.di = (uint16_t)(0x600 + (odd >> 1));
            s.flags = 0xed7; word(f, s.ss.base+s.si, 0x7654); word(f, s.es.base+s.di, 0xfedc);
            set(f, &s); step(f); count = f->count;
            for (unsigned fail = 1; fail <= count; ++fail) {
                assert(f->cpu.ops.reset(f->cpu.context) == BM_STATUS_OK);
                f->count = f->traced = 0; f->fail_at = fail; set(f, &s);
                word(f, s.es.base+s.di, 0xfedc);
                assert(bm_286_step(&f->cpu, &b) == BM_STATUS_DEVICE_ERROR);
                a = state(f); same(&a, &s);
                assert(!f->traced && f->count == fail);
                for (unsigned j = 0; j < 2; ++j) {
                    uint8_t want = (uint8_t)(0xfedcU >> (8*j));
                    for (unsigned i = 0; i+1 < fail; ++i)
                        if (f->trace[i].operation == BM_BUS_WRITE &&
                            s.es.base+s.di+j >= f->trace[i].address &&
                            s.es.base+s.di+j < f->trace[i].address+f->trace[i].size)
                            want = (uint8_t)(f->trace[i].value >> (8*(s.es.base+s.di+j-f->trace[i].address)));
                    assert(f->ram[s.es.base+s.di+j] == want);
                }
                assert(bm_286_step(&f->cpu, &b) == BM_STATUS_INVALID_STATE && f->count == fail);
            }
        }
}
static void edges(fixture_t *f)
{
    const uint8_t ops[] = {0xa4,0xa5,0xa6,0xa7,0xaa,0xab,0xac,0xad,0xae,0xaf};
    const uint16_t offsets[] = {0,1,0xfffd,0xfffe,0xffff};
    for (unsigned k = 0; k < sizeof(ops); ++k)
        for (unsigned df = 0; df < 2; ++df)
            for (unsigned i = 0; i < sizeof(offsets)/sizeof(offsets[0]); ++i) {
                uint8_t code[] = {ops[k]};
                bm_286_arch_state_t s = setup(f, code, sizeof(code)), e, a;
                bm_286_boundary_t b; unsigned size = (ops[k]&1)+1;
                s.ds = s.ss; /* Avoid IVT; offsets can be zero. */
                s.si = s.di = offsets[i]; s.flags = (uint16_t)(0x202 | (df ? 0x400 : 0));
                word(f, s.ds.base+s.si, 0x807f); word(f, s.es.base+s.di, 0x7f80);
                set(f, &s);
                if (size == 2 && offsets[i] == 0xffff) {
                    assert(bm_286_step(&f->cpu, &b) == BM_STATUS_UNSUPPORTED);
                    a = state(f); same(&a, &s); assert(f->count == 1);
                } else {
                    step(f); a = state(f); e = s;
                    expected(&e, ops[k], 1, size == 1 ? 0x7f : 0x807f, size == 1 ? 0x80 : 0x7f80);
                    same(&e, &a);
                }
            }
    { /* Physical 24-bit wrap inside an odd source word, not offset overflow. */
        const uint8_t code[] = {0xad};
        bm_286_arch_state_t s = setup(f, code, sizeof(code));
        s.ds.base = 0xfffff0; s.si = 15; f->ram[0xffffff] = 0x34; f->ram[0] = 0x12;
        set(f, &s); step(f); assert(state(f).ax == 0x1234 && state(f).si == 17);
        assert(f->trace[1].address == 0xffffff && f->trace[2].address == 0);
    }
    { /* Overlapping MOVSW must capture the whole source before writing. */
        const uint8_t code[] = {0x26,0xa5};
        for (unsigned direction = 0; direction < 3; ++direction) {
            bm_286_arch_state_t s = setup(f, code, sizeof(code));
            s.si = 0x501; s.di = (uint16_t)(0x500+direction);
            word(f, s.es.base+s.si, 0x1234); set(f, &s); step(f);
            assert(read_word(f, s.es.base+s.di) == 0x1234);
        }
    }
    { /* The last override selects the source, ES remains destination. */
        const uint8_t code[] = {0x26,0x36,0xa4};
        bm_286_arch_state_t s = setup(f, code, sizeof(code));
        s.si = 0x500; s.di = 0x600; f->ram[s.ss.base+s.si] = 0x7e;
        set(f, &s); step(f); assert(f->ram[s.es.base+s.di] == 0x7e);
    }
}
static void irrelevant_segments_and_rejection(fixture_t *f)
{
    const uint8_t ops[] = {0xaa,0xab,0xac,0xad,0xae,0xaf};
    for (unsigned i = 0; i < sizeof(ops); ++i) {
        uint8_t code[] = {0x36,ops[i]};
        bm_286_arch_state_t s = setup(f, code, sizeof(code));
        s.si = 0x500; s.di = 0x600;
        if ((ops[i]&0xfe) == 0xac) {s.es.valid = 0; s.ds.valid = 0;}
        else {s.ss.valid = 0; s.ds.valid = 0;}
        set(f, &s); step(f); /* Unused segments must not be validated. */
    }
    for (unsigned bad = 0; bad < 8; ++bad) {
        uint8_t code[] = {0xa5,0xa5};
        bm_286_arch_state_t s; bm_286_boundary_t b;
        if (bad < 3) code[0] = (uint8_t)(bad == 0 ? 0xf0 : bad == 1 ? 0xf2 : 0xf3);
        if (bad == 0) code[1] = 0xab; /* LOCK STOS remains outside this tranche. */
        if (bad == 1 || bad == 2) code[1] = 0x9b; /* REP WAIT remains unsupported. */
        s = setup(f, code, sizeof(code)); s.si = 0x500; s.di = 0x600;
        /* Imported PE refusal now tests strict clocks; functional PE is enabled. */
        uint64_t gate_cycles = 99;
        if (bad == 3) s.msw |= 1;
        if (bad == 4) s.ds.valid = 0;
        if (bad == 5) s.es.valid = 0;
        if (bad == 6) s.ds.limit = s.si;
        if (bad == 7) s.es.limit = s.di;
        set(f, &s); assert((s.msw & 1U ? bm_286_step_clocked(f->cpu.context, 0, &gate_cycles) : bm_286_step(&f->cpu, &b)) == BM_STATUS_UNSUPPORTED);
        if (s.msw & 1U) assert(gate_cycles == 0);
        bm_286_arch_state_t a = state(f); same(&a, &s);
        for (unsigned i = 0; i < f->count; ++i) assert(f->trace[i].operation != BM_BUS_WRITE);
    }
    { /* TF trap is sampled only after the complete string element. */
        const uint8_t code[] = {0xa5};
        bm_286_arch_state_t s = setup(f, code, sizeof(code));
        s.si = 0x500; s.di = 0x600; s.flags = 0x302; word(f, s.si, 0x789a);
        set(f, &s); step(f); assert(state(f).trap_pending && read_word(f, s.es.base+s.di) == 0x789a);
        bm_286_boundary_t b = step(f);
        assert(b.kind == BM_286_BOUNDARY_EXCEPTION && b.vector == 1);
        assert(read_word(f, s.ss.base+s.sp-6) == s.ip+1);
    }
    { /* HOLD requested during the source read cannot split an element. */
        const uint8_t code[] = {0xa5,0x90};
        bm_286_arch_state_t s = setup(f, code, sizeof(code)), a;
        bm_286_boundary_t b; unsigned count;
        s.si = 0x501; s.di = 0x601; word(f, s.si, 0xcafe);
        f->hold_at = 2; set(f, &s); step(f); count = f->count; a = state(f);
        assert(a.si == 0x503 && a.di == 0x603 && read_word(f, s.es.base+s.di) == 0xcafe);
        assert(bm_286_step(&f->cpu, &b) == BM_STATUS_IDLE);
        assert(b.kind == BM_286_BOUNDARY_HOLD && f->count == count);
        bm_286_arch_state_t held = state(f); same(&a, &held);
        assert(f->cpu.ops.signal(f->cpu.context, BM_286_SIGNAL_HOLD, 0) == BM_STATUS_OK);
        step(f); assert(state(f).ip == s.ip+2);
    }
}
int main(void)
{
    fixture_t f = {0}; bm_host_services_t host = bm_null_host_services(); bm_286_config_t config = {0};
    f.ram = calloc(0x1000000, 1); assert(f.ram);
    config.size = sizeof(config); config.version = BM_286_CONTRACT_VERSION;
    config.access = access_bus; config.access_context = &f;
    config.interrupt_ack = ack; config.interrupt_context = &f;
    config.bus_lock = lock_changed; config.pin_context = &f;
    config.trace = trace_boundary; config.trace_context = &f;
    assert(bm_286_create(&host, &config, &f.cpu) == BM_STATUS_OK);
    matrix(&f); comparisons(&f); failures(&f); edges(&f); irrelevant_segments_and_rejection(&f);
    f.cpu.ops.destroy(f.cpu.context); free(f.ram); return 0;
}
