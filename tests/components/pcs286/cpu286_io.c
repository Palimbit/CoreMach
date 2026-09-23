/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Authored real-mode logical I/O tests, not physical 286/ISA bus timings.
 */
#include <blumach/components/cpu_80286.h>
#include <blumach/platforms/null_host.h>
#include <assert.h>
#include <string.h>

typedef struct fixture {
    uint8_t code[16], ports[65536];
    bm_bus_transaction_t trace[16];
    unsigned count, fail_at, io_reads, io_writes, hold_on_io;
    bm_status_t error;
    bm_cpu_t *cpu;
} fixture_t;

static bm_status_t access_bus(void *context, bm_bus_transaction_t *t)
{
    fixture_t *f = context;
    assert(f->count < 16U && !t->wait_states && !t->attributes);
    assert(t->endianness == BM_ENDIAN_LITTLE);
    f->trace[f->count++] = *t;
    if (f->count == f->fail_at) return f->error;
    if (t->operation == BM_BUS_FETCH) {
        assert(t->space == BM_ADDRESS_PROGRAM && t->size == 1);
        assert(t->address >= 0x30000U && t->address < 0x30010U);
        t->value = f->code[t->address - 0x30000U];
        t->wait_states = 2;
    } else {
        assert(t->space == BM_ADDRESS_IO && t->address < 65536U);
        assert(t->size == 1 || (t->size == 2 && !(t->address & 1U)));
        assert(t->alignment == t->size);
        if (f->hold_on_io)
            assert(f->cpu->ops.signal(f->cpu->context, BM_286_SIGNAL_HOLD, 1) == BM_STATUS_OK);
        if (t->operation == BM_BUS_WRITE) {
            ++f->io_writes;
            for (unsigned i = 0; i < t->size; ++i)
                f->ports[(size_t) t->address + i] = (uint8_t) (t->value >> (8U * i));
        } else {
            ++f->io_reads;
            t->value = 0;
            for (unsigned i = 0; i < t->size; ++i)
                t->value |= (uint64_t) f->ports[(size_t) t->address + i] << (8U * i);
        }
        t->wait_states = 3;
    }
    return BM_STATUS_OK;
}

static bm_286_arch_state_t state_of(bm_cpu_t *cpu)
{
    bm_286_arch_state_t s;
    assert(bm_286_get_arch_state(cpu, &s) == BM_STATUS_OK);
    return s;
}

static bm_286_arch_state_t setup(bm_cpu_t *cpu, fixture_t *f)
{
    bm_286_arch_state_t s;
    assert(cpu->ops.reset(cpu->context) == BM_STATUS_OK);
    memset(f, 0, sizeof(*f)); f->cpu = cpu; f->error = BM_STATUS_DEVICE_ERROR;
    s = state_of(cpu); s.cs.selector = 0x3000; s.cs.base = 0x30000; s.ip = 0;
    s.ax = 0xa65c; s.dx = 0xabcd; s.flags = 0x0cd7;
    /* I/O must not consult data segment validity or limits. */
    s.ds.valid = 0; s.es.valid = 0; s.ss.valid = 0;
    return s;
}

static void forms(bm_cpu_t *cpu, fixture_t *f)
{
    const uint8_t ops[] = {0xe4,0xe5,0xe6,0xe7,0xec,0xed,0xee,0xef};
    const uint8_t prefixes[] = {0,0x26,0x2e,0x36,0x3e};
    const uint16_t ports[] = {0,0x81,0xfffe,0xffff};
    for (unsigned op = 0; op < 8; ++op)
        for (unsigned pref = 0; pref < 5; ++pref)
            for (unsigned p = 0; p < 4; ++p) {
                bm_286_arch_state_t s = setup(cpu, f), a, expected;
                bm_286_boundary_t b;
                unsigned n = 0, size = (ops[op] & 1U) ? 2U : 1U, fragments;
                uint16_t port = op < 4 ? (uint8_t) ports[p] : ports[p];
                int output = (ops[op] & 2U) != 0U;
                if (pref) f->code[n++] = prefixes[pref];
                f->code[n++] = ops[op]; if (op < 4) f->code[n++] = (uint8_t) port;
                s.dx = ports[p];
                f->ports[port] = 0x39; f->ports[(uint16_t) (port + 1U)] = 0xd2;
                assert(bm_286_set_arch_state(cpu, &s) == BM_STATUS_OK);
                assert(bm_286_step(cpu, &b) == BM_STATUS_OK);
                expected = s; expected.ip = (uint16_t) n;
                if (!output) expected.ax = size == 2 ? 0xd239U : 0xa639U;
                a = state_of(cpu); assert(memcmp(&a, &expected, sizeof(a)) == 0);
                fragments = size == 2 && (port & 1U) ? 2U : 1U;
                assert(f->count == n + fragments);
                assert(f->trace[n].address == port);
                if (fragments == 2) assert(f->trace[n+1].address == (uint16_t) (port+1U));
                assert(f->io_writes == (output ? fragments : 0));
                assert(f->io_reads == (output ? 0 : fragments));
                assert(b.bus_wait_cycles == n * 2U + fragments * 3U);
                assert(b.timing == BM_286_TIMING_UNKNOWN && b.cpu_cycles == b.bus_wait_cycles);
                assert(f->ports[port] == (output ? 0x5c : 0x39));
                assert(f->ports[(uint16_t) (port+1U)] == (output && size == 2 ? 0xa6 : 0xd2));
            }
}

static void failures_and_hold(bm_cpu_t *cpu, fixture_t *f)
{
    const uint8_t ops[] = {0xe4,0xe5,0xe6,0xe7,0xec,0xed,0xee,0xef};
    for (unsigned op = 0; op < 8; ++op) {
        unsigned fetches = op < 4 ? 2U : 1U, fragments = (ops[op] & 1U) ? 2U : 1U;
        for (unsigned fail = 1; fail <= fetches + fragments; ++fail) {
            bm_286_arch_state_t s = setup(cpu, f), a;
            bm_286_boundary_t b;
            s.dx = 0xffff; f->code[0] = ops[op]; f->code[1] = 0xff;
            f->fail_at = fail;
            assert(bm_286_set_arch_state(cpu, &s) == BM_STATUS_OK);
            assert(bm_286_step(cpu, &b) == BM_STATUS_DEVICE_ERROR);
            a = state_of(cpu); assert(memcmp(&a, &s, sizeof(s)) == 0);
            assert(bm_286_step(cpu, &b) == BM_STATUS_INVALID_STATE && f->count == fail);
            if ((ops[op] & 3U) == 3U && fail == fetches + 2U) {
                uint16_t port = op < 4 ? 0xffU : 0xffffU;
                assert(f->ports[port] == 0x5c); /* No external rollback. */
                assert(f->ports[(uint16_t) (port+1U)] == 0);
            }
        }
    }
    for (unsigned mode = 0; mode < 4; ++mode) {
        bm_286_arch_state_t s = setup(cpu, f);
        bm_286_boundary_t b;
        f->code[0] = mode == 0 ? 0xf0 : mode == 1 ? 0xf3 : 0xed;
        f->code[1] = 0xed;
        if (mode == 2) s.msw |= 1;
        if (mode == 3) { f->fail_at = 2; f->error = BM_STATUS_UNMAPPED; }
        assert(bm_286_set_arch_state(cpu, &s) == BM_STATUS_OK);
        assert(bm_286_step(cpu, &b) == (mode == 3 ? BM_STATUS_UNMAPPED : BM_STATUS_UNSUPPORTED));
        assert(!f->io_reads && !f->io_writes);
    }
    {
        bm_286_arch_state_t s = setup(cpu, f);
        bm_286_boundary_t b;
        s.dx = 0xffff; f->code[0] = 0xed; f->hold_on_io = 1;
        assert(bm_286_set_arch_state(cpu, &s) == BM_STATUS_OK);
        assert(bm_286_step(cpu, &b) == BM_STATUS_OK && f->io_reads == 2);
        assert(bm_286_step(cpu, &b) == BM_STATUS_IDLE && f->count == 3);
        assert(b.kind == BM_286_BOUNDARY_HOLD);
    }
}

int main(void)
{
    fixture_t f; bm_cpu_t cpu; bm_286_config_t c = {0};
    bm_host_services_t host = bm_null_host_services();
    c.size = sizeof(c); c.version = BM_286_CONTRACT_VERSION;
    c.access = access_bus; c.access_context = &f;
    assert(bm_286_create(&host, &c, &cpu) == BM_STATUS_OK);
    forms(&cpu, &f); failures_and_hold(&cpu, &f);
    cpu.ops.destroy(cpu.context); return 0;
}
