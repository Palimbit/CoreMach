/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Authored real-mode jump/reset tests; not a PCS286 motherboard or firmware.
 */
#include <blumach/components/cpu_80286.h>
#include <blumach/platforms/null_host.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct fixture {
    uint8_t *ram;
    bm_bus_transaction_t trace[32];
    unsigned count, fail_at, allow_frame;
} fixture_t;

static bm_status_t access_bus(void *context, bm_bus_transaction_t *t)
{
    fixture_t *f = context;
    unsigned i;
    assert(f->count < 32U && (t->size == 1U || t->size == 2U));
    assert(t->address <= 0x1000000U - t->size);
    assert(t->operation != BM_BUS_WRITE || f->allow_frame);
    assert(t->wait_states == 0U && t->endianness == BM_ENDIAN_LITTLE);
    assert(t->space == (t->operation == BM_BUS_FETCH ? BM_ADDRESS_PROGRAM : BM_ADDRESS_DATA));
    f->trace[f->count++] = *t;
    if (f->count == f->fail_at) return BM_STATUS_DEVICE_ERROR;
    if (t->operation != BM_BUS_WRITE) t->value = 0U;
    for (i = 0U; i < t->size; ++i)
        if (t->operation == BM_BUS_WRITE)
            f->ram[(size_t)t->address+i] = (uint8_t)(t->value >> (8U*i));
        else
            t->value |= (uint64_t) f->ram[(size_t) t->address + i] << (8U * i);
    t->wait_states = 3U;
    return BM_STATUS_OK;
}

static bm_286_arch_state_t setup(bm_cpu_t *cpu, fixture_t *f,
                                 const uint8_t *code, size_t length, int reset)
{
    bm_286_arch_state_t s;
    assert(cpu->ops.reset(cpu->context) == BM_STATUS_OK);
    assert(bm_286_get_arch_state(cpu, &s) == BM_STATUS_OK);
    memset(f->ram, 0, 0x1000000U);
    f->count = f->fail_at = f->allow_frame = 0U;
    if (!reset) {
        s.cs.selector = 0x3000U; s.cs.base = 0x30000U; s.ip = 0U;
    }
    s.ds.selector = 0x1000U; s.ds.base = 0x10000U;
    s.ss.selector = 0x2000U; s.ss.base = 0x20000U;
    s.bx = 0x400U; s.bp = 0x500U; s.sp = 0x800U;
    s.flags = 0x0cd7U; /* No pending IF/TF event. */
    memcpy(f->ram + s.cs.base + s.ip, code, length);
    return s;
}

static void pointer(fixture_t *f, unsigned address, uint16_t ip, uint16_t cs)
{
    f->ram[address] = (uint8_t) ip; f->ram[address + 1U] = (uint8_t) (ip >> 8);
    f->ram[address + 2U] = (uint8_t) cs; f->ram[address + 3U] = (uint8_t) (cs >> 8);
}

static bm_286_arch_state_t jump(bm_cpu_t *cpu, fixture_t *f, bm_286_arch_state_t s,
                                uint16_t ip, uint16_t cs)
{
    bm_286_boundary_t b;
    bm_286_arch_state_t a, expected = s;
    unsigned before = f->count;
    assert(bm_286_set_arch_state(cpu, &s) == BM_STATUS_OK);
    assert(bm_286_step(cpu, &b) == BM_STATUS_OK);
    assert(bm_286_get_arch_state(cpu, &a) == BM_STATUS_OK);
    expected.cs.selector = cs; expected.cs.base = (uint32_t) cs << 4;
    expected.cs.limit = 0xffffU; expected.cs.access = 0x82U; expected.cs.valid = 1U;
    expected.ip = ip;
    assert(memcmp(&a, &expected, sizeof(a)) == 0);
    assert(b.kind == BM_286_BOUNDARY_INSTRUCTION && b.timing == BM_286_TIMING_UNKNOWN);
    assert(b.instruction_address == s.cs.base + s.ip && b.instruction_ip == s.ip);
    assert(b.bus_wait_cycles == (uint64_t) (f->count - before) * 3U);
    return a;
}

static void forms(bm_cpu_t *cpu, fixture_t *f)
{
    const uint8_t reset_code[] = {0xea,0x00,0x01,0x00,0xf0};
    bm_286_arch_state_t s = setup(cpu, f, reset_code, sizeof(reset_code), 1), a;
    bm_286_boundary_t b;
    a = jump(cpu, f, s, 0x100U, 0xf000U);
    assert(f->trace[0].address == 0xfffff0U && a.cs.base == 0xf0000U);
    f->ram[0xf0100U] = 0x90U;
    assert(bm_286_step(cpu, &b) == BM_STATUS_OK);
    assert(f->trace[5].address == 0xf0100U && b.instruction_address == 0xf0100U);
    for (unsigned odd = 0U; odd < 2U; ++odd) {
        const uint8_t indirect[] = {0xff,0x2f}; /* DS:[BX] */
        s = setup(cpu, f, indirect, sizeof(indirect), 0);
        s.bx += (uint16_t) odd;
        pointer(f, s.ds.base + s.bx, 0x40U, 0xffffU);
        a = jump(cpu, f, s, 0x40U, 0xffffU);
        assert(f->count == 4U + 2U * odd);
        assert(f->trace[2].address == s.ds.base + s.bx);
        f->ram[0x100030U] = 0x90U; /* A20 must not be masked in the CPU. */
        assert(bm_286_step(cpu, &b) == BM_STATUS_OK);
        assert(b.instruction_address == 0x100030U);
        (void) a;
    }
    {
        const uint8_t override[] = {0x36,0xff,0x2f};
        s = setup(cpu, f, override, sizeof(override), 0);
        pointer(f, s.ss.base + s.bx, 0xffffU, 0U);
        (void) jump(cpu, f, s, 0xffffU, 0U);
        assert(f->trace[3].address == s.ss.base + s.bx);
    }
    {
        const uint8_t bp[] = {0xff,0x6e,0};
        s = setup(cpu, f, bp, sizeof(bp), 0);
        pointer(f, s.ss.base + s.bp, 0x100U, 0x1234U);
        (void) jump(cpu, f, s, 0x100U, 0x1234U);
        assert(f->trace[3].address == s.ss.base + s.bp);
    }
    {
        /* Authored regression for the boundary revealed by Harris captures;
         * these values are not copied hardware vectors. */
        const uint8_t indirect[] = {0xff,0x2f};
        s = setup(cpu, f, indirect, sizeof(indirect), 0);
        s.bx = 0xfffeU;
        f->ram[s.ds.base + 0xfffeU] = 0x34U;
        f->ram[s.ds.base + 0xffffU] = 0x12U;
        f->ram[s.ds.base] = 0x78U; f->ram[s.ds.base + 1U] = 0x56U;
        (void) jump(cpu, f, s, 0x1234U, 0x5678U);
        assert(f->trace[2].address == s.ds.base + 0xfffeU);
        assert(f->trace[3].address == s.ds.base);
    }
}

static void failures_and_limits(bm_cpu_t *cpu, fixture_t *f)
{
    const uint8_t direct[] = {0xea,0x34,0x12,0x00,0xf0}, indirect[] = {0xff,0x2f};
    for (unsigned form = 0U; form < 3U; ++form)
        for (unsigned stop = 1U; stop <= (form == 0U ? 5U : form == 1U ? 4U : 6U); ++stop) {
            bm_286_arch_state_t s = setup(cpu, f, form ? indirect : direct, form ? 2U : 5U, 0), a;
            bm_286_boundary_t b;
            s.bx += (uint16_t) (form == 2U);
            pointer(f, s.ds.base + s.bx, 0x1234U, 0xf000U);
            f->fail_at = stop;
            assert(bm_286_set_arch_state(cpu, &s) == BM_STATUS_OK);
            assert(bm_286_step(cpu, &b) == BM_STATUS_DEVICE_ERROR);
            assert(bm_286_get_arch_state(cpu, &a) == BM_STATUS_OK);
            assert(memcmp(&s, &a, sizeof(s)) == 0 && f->count == stop);
            assert(bm_286_step(cpu, &b) == BM_STATUS_INVALID_STATE && f->count == stop);
        }
    for (unsigned which = 0U; which < 7U; ++which) {
        const uint8_t invalid[] = {0xff,0xe8};
        bm_286_arch_state_t s = setup(cpu, f, which == 3U ? invalid : which < 3U ? indirect : direct,
                                       which <= 3U ? 2U : 5U, 0), a;
        bm_286_boundary_t b;
        if (which < 2U) s.bx = which == 0U ? 0xfffdU : 0xffffU;
        if (which == 2U) s.ds.valid = 0U;
        if (which == 4U) s.cs.limit = 3U;
        /* Imported PE refusal now tests strict clocks; functional PE is enabled. */
        uint64_t gate_cycles = 99;
        if (which == 5U) s.msw |= 1U;
        if (which == 6U) s.cs.valid = 0U;
        assert(bm_286_set_arch_state(cpu, &s) == BM_STATUS_OK);
        if (which < 2U || which == 3U || which == 4U) {
            f->allow_frame = 1;
            assert(bm_286_step(cpu, &b) == BM_STATUS_OK);
            assert(bm_286_get_arch_state(cpu, &a) == BM_STATUS_OK);
            assert(b.kind == BM_286_BOUNDARY_EXCEPTION && b.has_vector && b.vector == (which == 3U ? 6 : 13));
            assert(a.sp == (uint16_t)(s.sp-6) && f->count == (which == 4U ? 9U : 7U));
            assert(f->ram[s.ss.base+a.sp] == (uint8_t)s.ip);
            continue;
        }
        assert((s.msw & 1U ? bm_286_step_clocked(cpu->context, 0, &gate_cycles) : bm_286_step(cpu, &b)) == BM_STATUS_UNSUPPORTED);
        if (s.msw & 1U) assert(gate_cycles == 0);
        assert(bm_286_get_arch_state(cpu, &a) == BM_STATUS_OK);
        assert(memcmp(&s, &a, sizeof(s)) == 0);
        for (unsigned i = 0U; i < f->count; ++i)
            assert(f->trace[i].operation == BM_BUS_FETCH);
    }
}

int main(void)
{
    fixture_t f = {0};
    bm_host_services_t host = bm_null_host_services();
    bm_286_config_t config = {0};
    bm_cpu_t cpu;
    f.ram = calloc(0x1000000U, 1U); assert(f.ram != NULL);
    config.size = sizeof(config); config.version = BM_286_CONTRACT_VERSION;
    config.access = access_bus; config.access_context = &f;
    assert(bm_286_create(&host, &config, &cpu) == BM_STATUS_OK);
    forms(&cpu, &f);
    failures_and_limits(&cpu, &f);
    cpu.ops.destroy(cpu.context); free(f.ram);
    return 0;
}
