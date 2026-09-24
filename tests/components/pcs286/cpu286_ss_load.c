/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Authored SS reload / boundary-inhibition tests; no physical timing claim.
 */
#include <blumach/components/cpu_80286.h>
#include <blumach/platforms/null_host.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct fixture {
    uint8_t *ram;
    bm_bus_transaction_t trace[32];
    unsigned count, fail_at;
} fixture_t;

static bm_status_t access_bus(void *context, bm_bus_transaction_t *t)
{
    fixture_t *f = context;
    assert(f->count < 32U && t->address + t->size <= 0x200000U);
    assert(t->wait_states == 0U);
    f->trace[f->count++] = *t;
    if (f->count == f->fail_at) return BM_STATUS_DEVICE_ERROR;
    if (t->operation != BM_BUS_WRITE) t->value = 0;
    for (unsigned i = 0; i < t->size; ++i) {
        if (t->operation == BM_BUS_WRITE)
            f->ram[(size_t) t->address + i] = (uint8_t) (t->value >> (i * 8U));
        else t->value |= (uint64_t) f->ram[(size_t) t->address + i] << (i * 8U);
    }
    t->wait_states = 2U;
    return BM_STATUS_OK;
}

static bm_286_arch_state_t state_of(bm_cpu_t *cpu)
{
    bm_286_arch_state_t s;
    assert(bm_286_get_arch_state(cpu, &s) == BM_STATUS_OK);
    return s;
}

/* Compare architectural fields, not unspecified C structure padding. */
static void same(const bm_286_arch_state_t *a, const bm_286_arch_state_t *b)
{
#define EQ(x) assert(a->x == b->x)
    EQ(size); EQ(version);
    EQ(ax); EQ(cx); EQ(dx); EQ(bx); EQ(sp); EQ(bp); EQ(si); EQ(di);
    EQ(ip); EQ(flags); EQ(msw); EQ(cpl); EQ(halted); EQ(shutdown);
    EQ(interrupt_shadow); EQ(trap_pending); EQ(nmi_pending); EQ(nmi_blocked);
    EQ(gdtr.base); EQ(gdtr.limit); EQ(idtr.base); EQ(idtr.limit);
#define SEG(x) EQ(x.selector); EQ(x.base); EQ(x.limit); EQ(x.valid); EQ(x.access)
    SEG(cs); SEG(ds); SEG(ss); SEG(es); SEG(ldtr); SEG(tr);
#undef SEG
#undef EQ
}

static bm_286_arch_state_t setup(bm_cpu_t *cpu, fixture_t *f,
                                 const uint8_t *code, size_t size)
{
    bm_286_arch_state_t s;
    assert(cpu->ops.reset(cpu->context) == BM_STATUS_OK);
    memset(f->ram, 0, 0x200000U); f->count = f->fail_at = 0;
    s = state_of(cpu);
    s.cs.selector = 0x3000U; s.cs.base = 0x30000U; s.ip = 0;
    s.ss.selector = 0x1000U; s.ss.base = 0x10000U;
    s.ds.selector = 0x2000U; s.ds.base = 0x20000U;
    s.ax = 0x4321U; s.bx = 0x100U; s.bp = 0x200U; s.sp = 0x400U;
    s.flags = 0x0ed7U; /* IF=1, TF=0; preserve all flags. */
    memcpy(f->ram + s.cs.base, code, size);
    return s;
}

static void word(fixture_t *f, uint32_t address, uint16_t value)
{
    f->ram[address] = (uint8_t) value;
    f->ram[address + 1U] = (uint8_t) (value >> 8);
}

static void reloads(bm_cpu_t *cpu, fixture_t *f)
{
    const uint8_t codes[][4] = {{0x8e,0xd0,0,0}, {0x8e,0x17,0,0},
        {0x36,0x8e,0x17,0}, {0x8e,0x56,0,0}, {0x3e,0x17,0,0}};
    const unsigned lengths[] = {2,2,3,3,2};
    for (unsigned form = 0; form < 5; ++form)
        for (unsigned odd = 0; odd < 2; ++odd) {
            bm_286_arch_state_t s = setup(cpu, f, codes[form], lengths[form]), a, expected;
            bm_286_boundary_t b;
            uint32_t address;
            s.bx += (uint16_t) odd; s.bp += (uint16_t) odd; s.sp += (uint16_t) odd;
            address = form == 1 ? s.ds.base + s.bx : form == 2 ? s.ss.base + s.bx :
                form == 3 ? s.ss.base + s.bp : s.ss.base + s.sp;
            word(f, address, s.ax);
            assert(bm_286_set_arch_state(cpu, &s) == BM_STATUS_OK);
            assert(bm_286_step(cpu, &b) == BM_STATUS_OK);
            a = state_of(cpu); expected = s;
            expected.ss.selector = s.ax; expected.ss.base = (uint32_t) s.ax << 4;
            expected.ss.limit = 0xffffU; expected.ss.valid = 1; expected.ss.access = 0;
            expected.ip = (uint16_t) lengths[form];
            expected.interrupt_shadow = BM_286_SHADOW_SS_LOAD;
            if (form == 4) expected.sp = (uint16_t) (s.sp + 2U);
            same(&a, &expected);
            if (form) assert(f->trace[lengths[form]].address == address);
            assert(b.timing == BM_286_TIMING_UNKNOWN && b.bus_wait_cycles == 2U * f->count);
        }
    {
        /* POP SS reads OLD SS:SP; following POP SP reads NEW SS:SP+2. */
        const uint8_t code[] = {0x17,0x5c};
        bm_286_arch_state_t s = setup(cpu, f, code, sizeof(code));
        bm_286_boundary_t b;
        word(f, s.ss.base + s.sp, 0x4321U); word(f, 0x43210U + s.sp + 2U, 0x7654U);
        assert(bm_286_set_arch_state(cpu, &s) == BM_STATUS_OK);
        assert(bm_286_step(cpu, &b) == BM_STATUS_OK);
        assert(bm_286_step(cpu, &b) == BM_STATUS_OK);
        assert(state_of(cpu).sp == 0x7654U && state_of(cpu).interrupt_shadow == 0);
    }
}

static void boundaries(bm_cpu_t *cpu, fixture_t *f)
{
    const uint8_t code[] = {0x8e,0xd0,0xbc,0x00,0x80,0x90};
    for (unsigned event = 0; event < 4; ++event) {
        bm_286_arch_state_t s = setup(cpu, f, code, sizeof(code)), a;
        bm_286_boundary_t b;
        if (event == 2) s.flags |= 0x100U;
        assert(bm_286_set_arch_state(cpu, &s) == BM_STATUS_OK);
        assert(bm_286_step(cpu, &b) == BM_STATUS_OK);
        a = state_of(cpu);
        assert(a.interrupt_shadow == BM_286_SHADOW_SS_LOAD && !a.trap_pending);
        if (event < 2)
            assert(cpu->ops.signal(cpu->context, event == 0 ? BM_286_SIGNAL_INTR : BM_286_SIGNAL_NMI, 1) == BM_STATUS_OK);
        if (event == 3) {
            a.trap_pending = 1; /* Imported deferred trap with TF clear survives. */
            assert(bm_286_set_arch_state(cpu, &a) == BM_STATUS_OK);
        }
        assert(cpu->ops.signal(cpu->context, BM_286_SIGNAL_HOLD, 1) == BM_STATUS_OK);
        assert(bm_286_step(cpu, &b) == BM_STATUS_IDLE && f->count == 2);
        assert(state_of(cpu).interrupt_shadow == BM_286_SHADOW_SS_LOAD);
        assert(cpu->ops.signal(cpu->context, BM_286_SIGNAL_HOLD, 0) == BM_STATUS_OK);
        assert(bm_286_step(cpu, &b) == BM_STATUS_OK);
        a = state_of(cpu);
        assert(a.sp == 0x8000U && a.ip == 5 && !a.interrupt_shadow);
        if (event >= 2) assert(a.trap_pending);
        if (event == 1) assert(a.nmi_pending);
        if (event == 0) {
            assert(bm_286_step(cpu, &b) == BM_STATUS_UNSUPPORTED); /* No INTA callback. */
            assert(f->count == 5);
        } else {
            assert(bm_286_step(cpu, &b) == BM_STATUS_OK);
            assert(b.has_vector && b.vector == (event == 1 ? 2 : 1));
            assert(state_of(cpu).sp == 0x7ffaU && state_of(cpu).ip == 0);
            assert(f->count == 10); /* Frame + IVT, no instruction fetch. */
        }
    }
    /* An INTR-only shadow must NOT mask NMI or an already sampled #1. */
    for (unsigned trap = 0; trap < 2; ++trap) {
        bm_286_arch_state_t s = setup(cpu, f, code, sizeof(code));
        bm_286_boundary_t b;
        s.interrupt_shadow = BM_286_SHADOW_INTR_ONLY;
        s.trap_pending = (uint8_t) trap; s.nmi_pending = (uint8_t) !trap;
        assert(bm_286_set_arch_state(cpu, &s) == BM_STATUS_OK);
        assert(bm_286_step(cpu, &b) == BM_STATUS_OK && f->count == 5);
        assert(b.has_vector && b.vector == (trap ? 1 : 2));
    }
    /* Other segment loads do not acquire SS's inhibition. */
    for (unsigned ds = 0; ds < 2; ++ds) {
        uint8_t other[] = {0x8e, (uint8_t) (ds ? 0xd8 : 0xc0), 0x90};
        bm_286_arch_state_t s = setup(cpu, f, other, sizeof(other));
        bm_286_boundary_t b;
        s.flags |= 0x100U;
        assert(bm_286_set_arch_state(cpu, &s) == BM_STATUS_OK);
        assert(bm_286_step(cpu, &b) == BM_STATUS_OK);
        assert(!state_of(cpu).interrupt_shadow && state_of(cpu).trap_pending);
    }
    {
        /* Each successful SS reload starts a new inhibition interval. */
        const uint8_t twice[] = {0x8e,0xd0,0x8e,0xd0,0x90};
        bm_286_arch_state_t s = setup(cpu, f, twice, sizeof(twice));
        bm_286_boundary_t b;
        s.ax = 0; /* Null is legal for SS in real mode. */
        assert(bm_286_set_arch_state(cpu, &s) == BM_STATUS_OK);
        assert(bm_286_step(cpu, &b) == BM_STATUS_OK);
        assert(cpu->ops.signal(cpu->context, BM_286_SIGNAL_NMI, 1) == BM_STATUS_OK);
        assert(bm_286_step(cpu, &b) == BM_STATUS_OK);
        assert(state_of(cpu).interrupt_shadow == BM_286_SHADOW_SS_LOAD);
        assert(state_of(cpu).ss.valid && state_of(cpu).ss.base == 0);
        assert(bm_286_step(cpu, &b) == BM_STATUS_OK);
        assert(!state_of(cpu).interrupt_shadow && state_of(cpu).nmi_pending);
        assert(bm_286_step(cpu, &b) == BM_STATUS_OK);
        assert(b.has_vector && b.vector == 2 && state_of(cpu).nmi_blocked);
        assert(cpu->ops.reset(cpu->context) == BM_STATUS_OK);
        assert(!state_of(cpu).interrupt_shadow && !state_of(cpu).nmi_pending);
    }
}

static void errors_and_import(bm_cpu_t *cpu, fixture_t *f)
{
    const uint8_t codes[][3] = {{0x8e,0x17,0}, {0x17,0,0}, {0x8e,0xd0,0}};
    for (unsigned form = 0; form < 3; ++form)
        for (unsigned fail = 1; fail <= (form == 0 ? 4U : form == 1 ? 3U : 2U); ++fail) {
            bm_286_arch_state_t s = setup(cpu, f, codes[form], 3), a;
            bm_286_boundary_t b;
            s.bx |= 1U; s.sp |= 1U;
            s.interrupt_shadow = BM_286_SHADOW_SS_LOAD; /* Failure must not consume it. */
            word(f, s.ds.base + s.bx, 0x4321U); word(f, s.ss.base + s.sp, 0x4321U);
            f->fail_at = fail;
            assert(bm_286_set_arch_state(cpu, &s) == BM_STATUS_OK);
            assert(bm_286_step(cpu, &b) == BM_STATUS_DEVICE_ERROR);
            a = state_of(cpu); same(&s, &a);
            assert(bm_286_step(cpu, &b) == BM_STATUS_INVALID_STATE && f->count == fail);
        }
    {
        bm_286_arch_state_t s = setup(cpu, f, codes[2], 2), bad, a;
        assert(bm_286_set_arch_state(cpu, &s) == BM_STATUS_OK);
        bad = s; bad.version = 2;
        assert(bm_286_set_arch_state(cpu, &bad) == BM_STATUS_INVALID_ARGUMENT);
        bad = s; bad.interrupt_shadow = 3;
        assert(bm_286_set_arch_state(cpu, &bad) == BM_STATUS_INVALID_ARGUMENT);
        a = state_of(cpu); same(&s, &a);
    }
    for (unsigned form = 0; form < 3; ++form) {
        bm_286_arch_state_t s = setup(cpu, f, codes[form], 3), a;
        bm_286_boundary_t b;
        if (form == 0) s.bx = 0xffff;
        if (form == 1) s.sp = 0xffff;
        if (form == 2) s.msw |= 1;
        s.interrupt_shadow = BM_286_SHADOW_SS_LOAD;
        assert(bm_286_set_arch_state(cpu, &s) == BM_STATUS_OK);
        if (form < 2) {
            assert(bm_286_step(cpu, &b) == BM_STATUS_OK);
            a = state_of(cpu);
            assert(b.has_vector && b.vector == 13 && b.kind == BM_286_BOUNDARY_EXCEPTION);
            assert(a.ss.selector == s.ss.selector && a.ss.base == s.ss.base && !a.interrupt_shadow);
            assert(a.sp == (uint16_t)(s.sp-6));
            continue;
        }
        assert(bm_286_step(cpu, &b) == BM_STATUS_UNSUPPORTED);
        a = state_of(cpu); same(&s, &a);
        for (unsigned i = 0; i < f->count; ++i) assert(f->trace[i].operation == BM_BUS_FETCH);
    }
}

int main(void)
{
    fixture_t f = {0}; bm_cpu_t cpu; bm_286_config_t c = {0};
    bm_host_services_t host = bm_null_host_services();
    f.ram = calloc(0x200000U, 1); assert(f.ram);
    c.size = sizeof(c); c.version = BM_286_CONTRACT_VERSION;
    c.access = access_bus; c.access_context = &f;
    assert(bm_286_create(&host, &c, &cpu) == BM_STATUS_OK);
    reloads(&cpu, &f); boundaries(&cpu, &f); errors_and_import(&cpu, &f);
    cpu.ops.destroy(cpu.context); free(f.ram); return 0;
}
