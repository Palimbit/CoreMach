/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors */
#include <blumach/components/cpu_80286_timing.h>
#include <blumach/platforms/null_host.h>
#include <assert.h>
#include <string.h>
#include <stdbool.h>

typedef struct memory { uint8_t bytes[4096]; unsigned accesses; uint32_t waits; bool fail; } memory_t;
static bm_status_t access_memory(void *context, bm_bus_transaction_t *t)
{
    memory_t *m = context;
    unsigned i;
    if (m->fail) return BM_STATUS_DEVICE_ERROR;
    if (t->address >= sizeof(m->bytes) || t->size > sizeof(m->bytes) - t->address) return BM_STATUS_UNMAPPED;
    ++m->accesses;
    if (t->operation != BM_BUS_WRITE) t->value = 0;
    for (i = 0; i < t->size; ++i)
        if (t->operation == BM_BUS_WRITE) m->bytes[t->address + i] = (uint8_t)(t->value >> (8*i));
        else t->value |= (uint64_t)m->bytes[t->address + i] << (8*i);
    t->wait_states = m->waits;
    return BM_STATUS_OK;
}

static void initial(bm_cpu_t *cpu, unsigned mode, unsigned bx)
{
    bm_286_arch_state_t a;
    assert(cpu->ops.reset(cpu->context) == BM_STATUS_OK);
    assert(bm_286_get_arch_state(cpu, &a) == BM_STATUS_OK);
    a.cs.base = 0; a.cs.selector = 8; a.cs.limit = 4095; a.cs.access = 0x9b;
    a.ds.selector = a.ss.selector = a.es.selector = 16;
    a.ds.access = a.ss.access = a.es.access = 0x93;
    a.msw |= (uint16_t)mode; a.ip = 0x100; a.ax = 1; a.cx = 1; a.bx = (uint16_t)bx;
    assert(bm_286_set_arch_state(cpu, &a) == BM_STATUS_OK);
}

int main(void)
{
    const uint8_t code[][4] = {{0x90}, {0xb8,0x34,0x12}, {0xd3,0xe1}, {0x01,0x07}, {0x26,0x90}, {0xf4}};
    const unsigned lengths[] = {1,3,2,2,2,1};
    const uint64_t bases[] = {3,2,6,7,0,0};
    bm_host_services_t host = bm_null_host_services();
    bm_286_config_t config = {0};
    memory_t mem = {0};
    bm_cpu_t cpu;
    unsigned mode, i;
    config.size = sizeof(config); config.version = BM_286_CONTRACT_VERSION;
    config.access = access_memory; config.access_context = &mem;
    assert(bm_286_create(&host, &config, &cpu) == BM_STATUS_OK);
    mem.waits = 1;
    for (mode = 0; mode < 2; ++mode) for (i = 0; i < 6; ++i) {
        bm_286_arch_state_t plain, estimated;
        bm_286_boundary_t b;
        bm_286_timing_estimate_t e;
        unsigned accesses;
        memcpy(mem.bytes + 0x100, code[i], 4);
        initial(&cpu, mode, 0x200); mem.accesses = 0; mem.bytes[0x200] = 2;
        assert(bm_286_step(&cpu, &b) == BM_STATUS_OK);
        assert(bm_286_get_arch_state(&cpu, &plain) == BM_STATUS_OK);
        accesses = mem.accesses;
        initial(&cpu, mode, 0x200); mem.accesses = 0; mem.bytes[0x200] = 2;
        assert(bm_286_step_provisional(&cpu, 12, &b, &e) == BM_STATUS_OK);
        assert(bm_286_get_arch_state(&cpu, &estimated) == BM_STATUS_OK);
        assert(!memcmp(&plain, &estimated, sizeof(plain)));
        assert(mem.accesses == accesses); /* No timing rereads. */
        assert(e.length == lengths[i] && !memcmp(e.bytes, code[i], lengths[i]));
        assert(e.quality == BM_286_TIMING_PROVISIONAL && b.timing == BM_286_TIMING_UNKNOWN);
        assert(e.source == (bases[i] ? BM_286_ESTIMATE_MANUAL : BM_286_ESTIMATE_DIAGNOSTIC_FALLBACK));
        assert(e.base_clocks == (bases[i] ? bases[i] : 12));
        assert(e.estimated_clocks == e.base_clocks + b.bus_wait_cycles);
        assert(e.assumptions & BM_286_ESTIMATE_NO_PIPELINE);
    }
    {
        bm_286_boundary_t b;
        bm_286_timing_estimate_t e;
        bm_286_arch_state_t a;
        memcpy(mem.bytes + 0x100, code[4], 4);
        initial(&cpu, 0, 0x200);
        assert(bm_286_step_provisional(&cpu, 0, &b, &e) == BM_STATUS_OK);
        assert(e.source == BM_286_ESTIMATE_PENDING && !e.estimated_clocks && e.wait_clocks == 2);
        assert(bm_286_get_arch_state(&cpu, &a) == BM_STATUS_OK && a.ip == 0x102);
        initial(&cpu, 0, 0x200);
        assert(bm_286_step_provisional(&cpu, UINT64_MAX, &b, &e) == BM_STATUS_OK);
        assert(e.source == BM_286_ESTIMATE_PENDING && !e.estimated_clocks);
        initial(&cpu, 0, 0x200); mem.fail = true;
        assert(bm_286_step_provisional(&cpu, 12, &b, &e) == BM_STATUS_DEVICE_ERROR);
        assert(e.source == BM_286_ESTIMATE_PENDING);
        mem.fail = false; initial(&cpu, 0, 0x200);
        assert(bm_286_step(&cpu, &b) == BM_STATUS_OK); /* Original callback restored after error. */
        assert(bm_286_step_provisional(&cpu, 12, NULL, &e) == BM_STATUS_INVALID_ARGUMENT);
    }
    cpu.ops.destroy(cpu.context);
    return 0;
}
