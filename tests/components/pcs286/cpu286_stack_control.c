/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Authored synthetic stack/control tests. No firmware or inherited test code.
 */
#include <blumach/components/cpu_80286.h>
#include <blumach/platforms/null_host.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

enum { CODE = 0x30000, DATA = 0x10000, STACK = 0x20000 };
typedef struct fixture {
    uint8_t bytes[0x40000];
    bm_bus_transaction_t trace[256];
    unsigned count, fail_at;
} fixture_t;

static bm_status_t access_bus(void *context, bm_bus_transaction_t *t)
{
    fixture_t *f = context;
    unsigned i;
    assert(f->count < sizeof(f->trace) / sizeof(f->trace[0]));
    assert(t->size == 1U || t->size == 2U);
    assert(t->address <= sizeof(f->bytes) - t->size);
    assert(t->endianness == BM_ENDIAN_LITTLE && t->wait_states == 0U);
    assert(t->alignment == t->size);
    assert(t->space == (t->operation == BM_BUS_FETCH ? BM_ADDRESS_PROGRAM : BM_ADDRESS_DATA));
    f->trace[f->count++] = *t;
    if (f->count == f->fail_at)
        return BM_STATUS_DEVICE_ERROR;
    if (t->operation != BM_BUS_WRITE)
        t->value = 0U;
    for (i = 0U; i < t->size; ++i) {
        if (t->operation == BM_BUS_WRITE)
            f->bytes[(size_t) t->address + i] = (uint8_t) (t->value >> (8U * i));
        else
            t->value |= (uint64_t) f->bytes[(size_t) t->address + i] << (8U * i);
    }
    t->wait_states = 2U;
    return BM_STATUS_OK;
}

static bm_cpu_t create_cpu(fixture_t *f)
{
    bm_host_services_t host = bm_null_host_services();
    bm_286_config_t config = {0};
    bm_cpu_t cpu;
    config.size = sizeof(config);
    config.version = BM_286_CONTRACT_VERSION;
    config.access = access_bus;
    config.access_context = f;
    assert(bm_286_create(&host, &config, &cpu) == BM_STATUS_OK);
    return cpu;
}

static bm_286_arch_state_t state_of(bm_cpu_t *cpu)
{
    bm_286_arch_state_t s;
    assert(bm_286_get_arch_state(cpu, &s) == BM_STATUS_OK);
    return s;
}

static void same_segment(const bm_286_segment_state_t *a, const bm_286_segment_state_t *b)
{
    assert(a->selector == b->selector && a->base == b->base && a->limit == b->limit);
    assert(a->access == b->access && a->valid == b->valid);
}

/* Native structs contain unspecified padding, not architectural state. Check
 * every field after a by-value return/import, including Release builds. */
static void same(const bm_286_arch_state_t *a, const bm_286_arch_state_t *b)
{
#define EQ(x) assert(a->x == b->x)
    EQ(size); EQ(version);
    EQ(ax); EQ(cx); EQ(dx); EQ(bx); EQ(sp); EQ(bp); EQ(si); EQ(di);
    EQ(ip); EQ(flags); EQ(msw); EQ(cpl); EQ(halted); EQ(shutdown);
    EQ(interrupt_shadow); EQ(trap_pending); EQ(nmi_pending); EQ(nmi_blocked);
    EQ(gdtr.base); EQ(gdtr.limit); EQ(idtr.base); EQ(idtr.limit);
#undef EQ
    same_segment(&a->cs, &b->cs); same_segment(&a->ds, &b->ds);
    same_segment(&a->ss, &b->ss); same_segment(&a->es, &b->es);
    same_segment(&a->ldtr, &b->ldtr); same_segment(&a->tr, &b->tr);
}

static bm_286_arch_state_t load(bm_cpu_t *cpu, fixture_t *f,
                                const uint8_t *code, size_t length)
{
    bm_286_arch_state_t s;
    assert(cpu->ops.reset(cpu->context) == BM_STATUS_OK);
    memset(f, 0, sizeof(*f));
    memcpy(f->bytes + CODE, code, length);
    s = state_of(cpu);
    s.cs.selector = 0x3000U; s.cs.base = CODE;
    s.ds.selector = 0x1000U; s.ds.base = DATA;
    s.ss.selector = 0x2000U; s.ss.base = STACK;
    s.es.selector = 0x0800U; s.es.base = 0x8000U;
    s.ip = 0U; s.sp = 0x200U; s.flags = 0x0ed7U; /* TF clear, others preserved. */
    s.ax = 0x1234U; s.cx = 0x5678U; s.dx = 0x9abcU; s.bx = 0x301U;
    s.bp = 0x3456U; s.si = 0x789aU; s.di = 0xbcdeU;
    return s;
}

static void word_at(fixture_t *f, unsigned address, uint16_t value)
{
    f->bytes[address] = (uint8_t) value;
    f->bytes[address + 1U] = (uint8_t) (value >> 8);
}

static uint16_t read_word(const fixture_t *f, unsigned address)
{
    return (uint16_t) (f->bytes[address] | ((uint16_t) f->bytes[address + 1U] << 8));
}

static uint16_t *reg_at(bm_286_arch_state_t *s, unsigned index)
{
    switch (index) {
    case 0: return &s->ax; case 1: return &s->cx; case 2: return &s->dx;
    case 3: return &s->bx; case 4: return &s->sp; case 5: return &s->bp;
    case 6: return &s->si; default: return &s->di;
    }
}

static void stack_fault(bm_cpu_t *cpu, fixture_t *f,
                        const bm_286_arch_state_t *s, int shutdown)
{
    bm_286_boundary_t b;
    bm_286_arch_state_t expected = *s, actual;
    word_at(f, 52, 0x100); word_at(f, 54, 0x0800);
    assert(bm_286_set_arch_state(cpu, s) == BM_STATUS_OK);
    assert(bm_286_step(cpu, &b) == BM_STATUS_OK);
    actual = state_of(cpu);
    if (shutdown) {
        expected.shutdown = 1;
        assert(b.kind == BM_286_BOUNDARY_SHUTDOWN && !b.has_vector);
        for (unsigned i = 0; i < f->count; ++i)
            assert(f->trace[i].operation == BM_BUS_FETCH);
    } else {
        expected.sp = (uint16_t)(s->sp-6);
        expected.flags &= 0xfcffU;
        expected.cs.selector = 0x0800; expected.cs.base = 0x8000;
        expected.cs.limit = 0xffff;
        expected.ip = 0x100;
        assert(b.kind == BM_286_BOUNDARY_EXCEPTION && b.has_vector && b.vector == 13);
        assert(read_word(f, STACK+expected.sp) == s->ip);
        assert(read_word(f, STACK+expected.sp+2) == s->cs.selector);
        assert(read_word(f, STACK+expected.sp+4) == s->flags);
    }
    same(&actual, &expected);
}

static bm_286_arch_state_t step(bm_cpu_t *cpu, fixture_t *f, const bm_286_arch_state_t *s)
{
    bm_286_boundary_t b;
    bm_286_arch_state_t after;
    unsigned before = f->count;
    assert(bm_286_set_arch_state(cpu, s) == BM_STATUS_OK);
    assert(bm_286_step(cpu, &b) == BM_STATUS_OK);
    after = state_of(cpu);
    assert(b.kind == BM_286_BOUNDARY_INSTRUCTION && b.timing == BM_286_TIMING_UNKNOWN);
    assert(b.instruction_ip == s->ip && b.instruction_address == s->cs.base + s->ip);
    assert(b.bus_wait_cycles == (uint64_t) (f->count - before) * 2U);
    assert(b.cpu_cycles == b.bus_wait_cycles);
    assert(after.flags == s->flags);
    same_segment(&after.cs, &s->cs);
    return after;
}

static void registers_and_forms(bm_cpu_t *cpu, fixture_t *f)
{
    unsigned reg, odd;
    for (reg = 0U; reg < 8U; ++reg) {
        for (odd = 0U; odd < 2U; ++odd) {
            uint8_t push[] = {(uint8_t) (0x50U + reg)};
            uint8_t pop[] = {(uint8_t) (0x58U + reg)};
            bm_286_arch_state_t s = load(cpu, f, push, sizeof(push)), a;
            uint16_t value;
            s.sp += (uint16_t) odd;
            value = *reg_at(&s, reg);
            a = step(cpu, f, &s);
            assert(a.sp == s.sp - 2U && a.ip == 1U);
            assert(read_word(f, STACK + a.sp) == value);
            assert(f->trace[1].address == (uint32_t) STACK + a.sp);
            assert(f->count == 2U + odd);
            s = load(cpu, f, pop, sizeof(pop)); s.sp += (uint16_t) odd;
            word_at(f, STACK + s.sp, 0xfaceU);
            a = step(cpu, f, &s);
            assert(*reg_at(&a, reg) == 0xfaceU && a.ip == 1U);
            assert(a.sp == (reg == 4U ? 0xfaceU : s.sp + 2U));
        }
    }
    {
        static const uint8_t codes[][3] = {{0x68,0xcd,0xab},{0x6a,0x80,0},{0xff,0xf4,0},
                                          {0x26,0xff,0x37},{0x8f,0xc4,0},{0x26,0x8f,0x07}};
        unsigned i;
        for (i = 0; i < 6U; ++i) {
            bm_286_arch_state_t s = load(cpu, f, codes[i], 3U), a;
            word_at(f, 0x8000U + s.bx, 0x1357U);
            word_at(f, STACK + s.sp, 0xbeefU);
            a = step(cpu, f, &s);
            if (i < 4U) {
                static const uint16_t expected[] = {0xabcd,0xff80,0x200,0x1357};
                assert(a.sp == 0x1feU && read_word(f, STACK + a.sp) == expected[i]);
            } else if (i == 4U)
                assert(a.sp == 0xbeefU);
            else {
                assert(a.sp == 0x202U);
                assert(read_word(f, 0x8000U + s.bx) == 0xbeefU);
            }
        }
    }
    {
        static const uint8_t pushes[] = {0x06,0x0e,0x16,0x1e};
        static const uint16_t values[] = {0x0800,0x3000,0x2000,0x1000};
        unsigned i;
        for (i = 0; i < 4U; ++i) {
            bm_286_arch_state_t s = load(cpu, f, &pushes[i], 1U);
            (void) step(cpu, f, &s);
            assert(read_word(f, STACK + 0x1feU) == values[i]);
        }
        for (i = 0; i < 2U; ++i) {
            uint8_t op = i ? 0x1fU : 0x07U;
            bm_286_arch_state_t s = load(cpu, f, &op, 1U), a;
            const bm_286_segment_state_t *seg;
            word_at(f, STACK + s.sp, 0x1234U);
            a = step(cpu, f, &s); seg = i ? &a.ds : &a.es;
            assert(seg->selector == 0x1234U && seg->base == 0x12340U);
            assert(seg->valid && seg->limit == 0xffffU && a.sp == s.sp + 2U);
        }
    }
}

static void calls_and_returns(bm_cpu_t *cpu, fixture_t *f)
{
    const uint8_t program[] = {0xe8,0x03,0x00,0xb8,0x11,0x11,0xb8,0x22,0x22,0xc3};
    bm_286_arch_state_t s = load(cpu, f, program, sizeof(program)), a;
    a = step(cpu, f, &s);
    assert(a.ip == 6U && a.sp == 0x1feU && read_word(f, STACK + a.sp) == 3U);
    a = step(cpu, f, &a); assert(a.ax == 0x2222U && a.ip == 9U);
    a = step(cpu, f, &a); assert(a.ip == 3U && a.sp == s.sp);
    a = step(cpu, f, &a); assert(a.ax == 0x1111U);
    {
        static const uint8_t codes[][4] = {{0xff,0xd4,0,0},{0xff,0x17,0,0},
            {0xff,0xe0,0,0},{0xff,0x27,0,0},{0xc2,0x10,0,0},
            {0xe9,0xfc,0xff,0},{0xeb,0xfe,0,0}};
        unsigned i;
        for (i = 0; i < 7U; ++i) {
            s = load(cpu, f, codes[i], 4U);
            word_at(f, DATA + s.bx, 0x4567U);
            word_at(f, STACK + s.sp, 0x6789U);
            a = step(cpu, f, &s);
            if (i < 2U) {
                assert(a.ip == (i ? 0x4567U : 0x200U));
                assert(a.sp == 0x1feU && read_word(f, STACK + a.sp) == 2U);
            } else {
                static const uint16_t targets[] = {0x1234,0x4567,0x6789,0xffff,0};
                assert(a.ip == targets[i - 2U]);
                assert(a.sp == (i == 4U ? 0x212U : 0x200U));
            }
        }
    }
}

static void condition_matrix(bm_cpu_t *cpu, fixture_t *f)
{
    unsigned bits, condition, direction;
    for (bits = 0U; bits < 32U; ++bits) {
        int cf = (bits & 1U) != 0U, pf = (bits & 2U) != 0U;
        int zf = (bits & 4U) != 0U, sf = (bits & 8U) != 0U, of = (bits & 16U) != 0U;
        int expected[16] = {of,!of,cf,!cf,zf,!zf,cf||zf,!cf&&!zf,
                           sf,!sf,pf,!pf,sf!=of,sf==of,zf||(sf!=of),!zf&&(sf==of)};
        for (condition = 0U; condition < 16U; ++condition) {
            for (direction = 0U; direction < 2U; ++direction) {
                uint8_t code[] = {(uint8_t) (0x70U + condition), direction ? 0xfcU : 5U};
                bm_286_arch_state_t s = load(cpu, f, code, sizeof(code)), a;
                s.flags = (uint16_t) (0x0402U | (cf ? 1U : 0U) | (pf ? 4U : 0U) |
                          (zf ? 0x40U : 0U) | (sf ? 0x80U : 0U) | (of ? 0x800U : 0U));
                a = step(cpu, f, &s);
                assert(a.ip == (expected[condition] ? (direction ? 0xfffeU : 7U) : 2U));
                assert(a.cx == s.cx && a.sp == s.sp && f->count == 2U);
            }
        }
    }
    {
        static const uint16_t counts[] = {0,1,2,0xffff};
        unsigned op, i, z;
        for (op = 0xe0U; op <= 0xe3U; ++op)
            for (i = 0; i < 4U; ++i)
                for (z = 0; z < 2U; ++z) {
                    uint8_t code[] = {(uint8_t) op, 0xfe};
                    bm_286_arch_state_t s = load(cpu, f, code, sizeof(code)), a;
                    uint16_t count = op == 0xe3U ? counts[i] : (uint16_t) (counts[i] - 1U);
                    int take = op == 0xe3U ? count == 0U : count != 0U &&
                               (op == 0xe2U || (op == 0xe1U ? z != 0U : z == 0U));
                    s.cx = counts[i]; s.flags = (uint16_t) (0x0402U | (z ? 0x40U : 0U));
                    a = step(cpu, f, &s);
                    assert(a.cx == count && a.ip == (take ? 0U : 2U));
                }
    }
}

static void aggregate_and_frames(bm_cpu_t *cpu, fixture_t *f)
{
    unsigned parity, level, i;
    for (parity = 0U; parity < 2U; ++parity) {
        const uint8_t code[] = {0x60,0x61};
        bm_286_arch_state_t s = load(cpu, f, code, sizeof(code)), a;
        s.sp = (uint16_t) (0x200U + parity);
        a = step(cpu, f, &s);
        assert(a.sp == s.sp - 16U && a.ip == 1U);
        for (i = 0U; i < 8U; ++i)
            assert(read_word(f, STACK + s.sp - 2U * (i + 1U)) == *reg_at(&s, i));
        /* POPA must ignore the saved SP, even if its contents change. */
        word_at(f, STACK + s.sp - 10U, 0xabcdU);
        for (i = 0U; i < 8U; ++i)
            if (i != 4U)
                *reg_at(&a, i) = 0U;
        a = step(cpu, f, &a);
        for (i = 0U; i < 8U; ++i)
            assert(*reg_at(&a, i) == *reg_at(&s, i));
        assert(a.ip == 2U);
    }
    /* All 256 encodings, two alignments: high nesting bits are ignored. */
    for (parity = 0U; parity < 2U; ++parity)
        for (level = 0U; level < 256U; ++level) {
            uint8_t code[] = {0xc8,0x23,0,0,0xc9};
            bm_286_arch_state_t s, a;
            unsigned effective = level & 31U;
            unsigned words = effective == 0U ? 1U : effective + 1U;
            code[3] = (uint8_t) level;
            s = load(cpu, f, code, sizeof(code));
            s.sp = (uint16_t) (0x200U + parity);
            s.bp = (uint16_t) (0x800U + parity);
            for (i = 1U; i < 32U; ++i)
                word_at(f, STACK + s.bp - 2U * i, (uint16_t) (0xa000U + i));
            a = step(cpu, f, &s);
            assert(a.bp == s.sp - 2U && a.sp == s.sp - 2U * words - 0x23U);
            assert(read_word(f, STACK + a.bp) == s.bp);
            for (i = 1U; i < effective; ++i)
                assert(read_word(f, STACK + s.sp - 2U * (i + 1U)) == 0xa000U + i);
            if (effective != 0U)
                assert(read_word(f, STACK + s.sp - 2U * words) == a.bp);
            a = step(cpu, f, &a);
            assert(a.sp == s.sp && a.bp == s.bp && a.ip == 5U);
        }
    {
        /* Interleaved reads see earlier pushes when the old/new frames overlap. */
        const uint8_t code[] = {0x3e,0xc8,0,0,3};
        bm_286_arch_state_t s = load(cpu, f, code, sizeof(code)), a;
        s.bp = s.sp;
        a = step(cpu, f, &s);
        assert(a.bp == 0x1feU && a.sp == 0x1f8U);
        assert(read_word(f, STACK + 0x1feU) == 0x200U);
        assert(read_word(f, STACK + 0x1fcU) == 0x200U);
        assert(read_word(f, STACK + 0x1faU) == 0x200U);
        assert(read_word(f, STACK + 0x1f8U) == 0x1feU);
        assert(read_word(f, DATA + 0x1feU) == 0U);
    }
    {
        const uint8_t code[] = {0xc8,0xff,0xff,0,0xc9};
        bm_286_arch_state_t s = load(cpu, f, code, sizeof(code)), a;
        a = step(cpu, f, &s);
        assert(a.bp == 0x1feU && a.sp == 0x1ffU); /* 16-bit allocation arithmetic */
        a = step(cpu, f, &a); assert(a.sp == s.sp && a.bp == s.bp);
    }
    for (i = 0U; i < 8U; ++i) {
        const uint8_t code[] = {0x60};
        bm_286_arch_state_t s = load(cpu, f, code, sizeof(code));
        s.sp = (uint16_t) (2U * i + 1U);
        stack_fault(cpu, f, &s, i < 3U); /* Intel PUSHA: SP 1/3/5 shutdown, 7..15 #13. */
    }
    for (i = 0U; i < 5U; ++i) {
        const uint8_t enter[] = {0xc8,0,0,3}, popa[] = {0x61}, leave[] = {0xc9};
        bm_286_arch_state_t s = load(cpu, f, i < 3U ? enter : i == 3U ? popa : leave,
                                    i < 3U ? 4U : 1U), a;
        bm_286_boundary_t b;
        if (i == 0U) s.sp = 3U;
        if (i == 1U) s.bp = 1U;
        if (i == 2U) s.ss.valid = 0U;
        if (i == 3U) s.sp = 0xfff9U; /* Discarded slot cannot hide a segment crossing. */
        if (i == 4U) s.bp = 0xffffU;
        if (i != 2U) {
            stack_fault(cpu, f, &s, i == 0U);
            continue;
        }
        assert(bm_286_set_arch_state(cpu, &s) == BM_STATUS_OK);
        assert(bm_286_step(cpu, &b) == BM_STATUS_UNSUPPORTED);
        a = state_of(cpu); same(&s, &a);
        for (unsigned j = 0U; j < f->count; ++j)
            assert(f->trace[j].operation == BM_BUS_FETCH);
    }
}

/* Fail every individual endpoint transfer of each instruction. The CPU state
 * must remain unchanged; only writes completed before the failure may persist. */
static void failures(bm_cpu_t *cpu, fixture_t *f)
{
    static const uint8_t codes[][4] = {{0x50,0,0,0},{0x68,0x34,0x12,0},
        {0x6a,0x80,0,0},{0xff,0x37,0,0},{0x58,0,0,0},{0x8f,0x07,0,0},
        {0xe8,3,0,0},{0xff,0x17,0,0},{0xc3,0,0,0},{0xc2,4,0,0},
        {0xeb,0xfe,0,0},{0x74,2,0,0},{0xe2,0xfe,0,0},{0x07,0,0,0},
        {0x60,0,0,0},{0x61,0,0,0},{0xc8,0x23,0,0},{0xc8,0x23,0,1},
        {0xc8,0x23,0,0xff},{0xc9,0,0,0}};
    unsigned op;
    uint8_t *expected = malloc(sizeof(f->bytes));
    assert(expected != NULL);
    for (op = 0U; op < sizeof(codes) / sizeof(codes[0]); ++op) {
        unsigned stop, transfers = 0U;
        for (stop = 0U; stop == 0U || stop <= transfers; ++stop) {
            bm_286_arch_state_t s = load(cpu, f, codes[op], 4U), after;
            bm_286_boundary_t b;
            s.sp = 0x201U;
            s.bp = 0x3457U;
            word_at(f, DATA + s.bx, 0x1234U);
            word_at(f, STACK + s.sp, 0x4567U);
            memcpy(expected, f->bytes, sizeof(f->bytes));
            f->fail_at = stop;
            assert(bm_286_set_arch_state(cpu, &s) == BM_STATUS_OK);
            if (stop == 0U) {
                assert(bm_286_step(cpu, &b) == BM_STATUS_OK);
                transfers = f->count;
            } else {
                unsigned i, j;
                assert(bm_286_step(cpu, &b) == BM_STATUS_DEVICE_ERROR);
                after = state_of(cpu);
                same(&after, &s); assert(f->count == stop);
                for (i = 0; i + 1U < stop; ++i) {
                    const bm_bus_transaction_t *t = &f->trace[i];
                    if (t->operation == BM_BUS_WRITE)
                        for (j = 0; j < t->size; ++j)
                            expected[(size_t) t->address + j] = (uint8_t) (t->value >> (8U * j));
                }
                assert(memcmp(expected, f->bytes, sizeof(f->bytes)) == 0);
                assert(bm_286_step(cpu, &b) == BM_STATUS_INVALID_STATE);
                assert(f->count == stop);
            }
        }
    }
    free(expected);
}

static void limits_and_gaps(bm_cpu_t *cpu, fixture_t *f)
{
    static const uint8_t gaps[][3] = {
        {0xff,0xd8,0},{0xff,0xe8,0},
        {0xff,0xf8,0},{0x8f,0xc8,0},{0xf0,0x50,0},{0xf3,0xc3,0}};
    unsigned i;
    for (i = 0; i < sizeof(gaps) / sizeof(gaps[0]); ++i) {
        bm_286_arch_state_t s = load(cpu, f, gaps[i], 3U), a;
        bm_286_boundary_t b;
        if (i < 2U) {
            word_at(f, 24, 0x100); word_at(f, 26, 0x0800);
            assert(bm_286_set_arch_state(cpu, &s) == BM_STATUS_OK);
            assert(bm_286_step(cpu, &b) == BM_STATUS_OK);
            a = state_of(cpu);
            assert(b.has_vector && b.vector == 6 && a.sp == (uint16_t)(s.sp-6));
            assert(read_word(f, STACK+a.sp) == s.ip);
            continue;
        }
        assert(bm_286_set_arch_state(cpu, &s) == BM_STATUS_OK);
        assert(bm_286_step(cpu, &b) == BM_STATUS_UNSUPPORTED);
        a = state_of(cpu); same(&a, &s);
        for (unsigned j = 0; j < f->count; ++j)
            assert(f->trace[j].operation == BM_BUS_FETCH);
    }
    for (i = 0; i < 7U; ++i) {
        const uint8_t push[] = {0x50}, pop[] = {0x58}, call[] = {0xe8,0x20,0};
        const uint8_t ret[] = {0xc3}, loop[] = {0xe2,0x20};
        bm_286_arch_state_t s = load(cpu, f, i < 2U ? push : i < 4U ? pop :
                                     i == 4U ? call : i == 5U ? ret : loop,
                                     i == 4U ? 3U : i == 6U ? 2U : 1U), a;
        bm_286_boundary_t b;
        if (i == 0U) s.sp = 1U;
        if (i == 1U) s.ss.valid = 0U;
        if (i == 2U) s.sp = 0xffffU;
        if (i == 3U) s.ss.limit = s.sp;
        if (i >= 4U) { s.cs.limit = 8U; word_at(f, STACK + s.sp, 0x20U); }
        if (i == 0U || i == 2U || i == 3U || i >= 4U) {
            stack_fault(cpu, f, &s, i == 0U);
            continue;
        }
        assert(bm_286_set_arch_state(cpu, &s) == BM_STATUS_OK);
        assert(bm_286_step(cpu, &b) == BM_STATUS_UNSUPPORTED);
        a = state_of(cpu); same(&a, &s);
        for (unsigned j = 0; j < f->count; ++j)
            assert(f->trace[j].operation != BM_BUS_WRITE);
    }
    {
        const uint8_t push_pop[] = {0x50,0x5b};
        bm_286_arch_state_t s = load(cpu, f, push_pop, sizeof(push_pop)), a;
        s.sp = 0U;
        a = step(cpu, f, &s); assert(a.sp == 0xfffeU);
        a = step(cpu, f, &a); assert(a.sp == 0U && a.bx == s.ax);
    }
    {
        const uint8_t blank[] = {0x90};
        bm_286_arch_state_t s = load(cpu, f, blank, sizeof(blank)), a;
        s.ip = 0xfffeU;
        f->bytes[CODE + 0xfffeU] = 0xeb; f->bytes[CODE + 0xffffU] = 0xfe;
        a = step(cpu, f, &s); assert(a.ip == 0xfffeU);
    }
    {
        const uint8_t not_taken[] = {0x74,0x7f};
        bm_286_arch_state_t s = load(cpu, f, not_taken, sizeof(not_taken)), a;
        s.flags = 2U; s.cs.limit = 4U;
        a = step(cpu, f, &s); assert(a.ip == 2U); /* Do not check unused target. */
    }
    {
        const uint8_t push_ds_override[] = {0x3e,0x50};
        bm_286_arch_state_t s = load(cpu, f, push_ds_override, sizeof(push_ds_override));
        (void) step(cpu, f, &s);
        assert(read_word(f, STACK + 0x1feU) == s.ax);
        assert(read_word(f, DATA + 0x1feU) == 0U);
    }
    {
        const uint8_t pop_bp_address[] = {0x8f,0x46,0x00};
        bm_286_arch_state_t s = load(cpu, f, pop_bp_address, sizeof(pop_bp_address)), a;
        word_at(f, STACK + s.sp, 0xabcdU);
        a = step(cpu, f, &s);
        assert(a.bp == s.bp && a.sp == s.sp + 2U);
        assert(read_word(f, STACK + s.bp) == 0xabcdU);
    }
    {
        const uint8_t ret_wrap[] = {0xc2,0xfe,0xff};
        bm_286_arch_state_t s = load(cpu, f, ret_wrap, sizeof(ret_wrap)), a;
        word_at(f, STACK + s.sp, 0x40U);
        a = step(cpu, f, &s);
        assert(a.sp == s.sp && a.ip == 0x40U);
    }
}

int main(void)
{
    fixture_t *f = calloc(1U, sizeof(*f));
    bm_cpu_t cpu;
    assert(f != NULL);
    cpu = create_cpu(f);
    registers_and_forms(&cpu, f);
    calls_and_returns(&cpu, f);
    condition_matrix(&cpu, f);
    aggregate_and_frames(&cpu, f);
    failures(&cpu, f);
    limits_and_gaps(&cpu, f);
    cpu.ops.destroy(cpu.context);
    free(f);
    return 0;
}
