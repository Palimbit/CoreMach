/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors */
#include "fetch_supply_286.h"
#include <blumach/components/cpu_80286.h>
#include <blumach/platforms/null_host.h>
#include <assert.h>
#include <string.h>

typedef struct memory { uint8_t bytes[1024]; unsigned accesses; uint32_t waits; bool fail; } memory_t;
static bm_status_t access_memory(void *context, bm_bus_transaction_t *t)
{
    memory_t *m = context;
    unsigned i;
    if (m->fail) return BM_STATUS_DEVICE_ERROR;
    if (t->address >= sizeof(m->bytes) || t->size > sizeof(m->bytes) - t->address)
        return BM_STATUS_UNMAPPED;
    ++m->accesses;
    if (t->operation != BM_BUS_WRITE) t->value = 0;
    for (i = 0; i < t->size; ++i) {
        if (t->operation == BM_BUS_WRITE) m->bytes[t->address + i] = (uint8_t)(t->value >> (8 * i));
        else t->value |= (uint64_t)m->bytes[t->address + i] << (8 * i);
    }
    t->wait_states = m->waits;
    return BM_STATUS_OK;
}

static void execute(unsigned mode, bool adapted, bm_286_arch_state_t *result, memory_t *mem)
{
    const uint8_t code[] = {0xb8,0x34,0x12, 0x05,1,0, 0xa3,0,2, 0xeb,0, 0xf4};
    bm_host_services_t host = bm_null_host_services();
    bm_286_config_t config = {0};
    bm_286_fetch_supply_t supply = {0};
    bm_286_boundary_t boundary;
    bm_cpu_t cpu;
    unsigned i;
    memcpy(mem->bytes + 0x100, code, sizeof(code)); mem->waits = 3;
    supply.source = access_memory; supply.context = mem;
    assert(bm_286_fetch_supply_redirect(&supply, 0x100, 0x3ff, mode != 0) == BM_STATUS_OK);
    config.size = sizeof(config); config.version = BM_286_CONTRACT_VERSION;
    config.access = adapted ? bm_286_fetch_supply_access : access_memory;
    config.access_context = adapted ? (void *)&supply : (void *)mem;
    assert(bm_286_create(&host, &config, &cpu) == BM_STATUS_OK);
    assert(bm_286_get_arch_state(&cpu, result) == BM_STATUS_OK);
    result->cs.base = 0; result->cs.selector = 8; result->cs.limit = 0x3ff;
    result->cs.access = 0x9b; result->ip = 0x100;
    result->ds.access = result->ss.access = result->es.access = 0x93;
    result->ds.selector = result->ss.selector = result->es.selector = 16;
    result->msw |= (uint16_t)mode;
    assert(bm_286_set_arch_state(&cpu, result) == BM_STATUS_OK);
    if (adapted) {
        for (i = 0; i < 3; ++i) assert(bm_286_fetch_supply_refill(&supply) == BM_STATUS_OK);
        assert(supply.bus.clock == 15 && supply.queue.count == 6);
    }
    for (i = 0; i < 5; ++i) {
        assert(bm_286_step(&cpu, &boundary) == BM_STATUS_OK);
        assert(boundary.timing == BM_286_TIMING_UNKNOWN);
        assert(bm_286_get_arch_state(&cpu, result) == BM_STATUS_OK);
        if (adapted && i == 3) {
            /* JMP+0 must flush even though the new IP equals next fetch. */
            assert(bm_286_fetch_supply_redirect(&supply, result->ip, 0x3ff, mode != 0) == BM_STATUS_OK);
        }
    }
    assert(result->ax == 0x1235 && result->halted);
    assert(mem->bytes[0x200] == 0x35 && mem->bytes[0x201] == 0x12);
    if (adapted) assert(supply.bus.clock == supply.transfers * 5);
    cpu.ops.destroy(cpu.context);
}

int main(void)
{
    unsigned mode;
    for (mode = 0; mode <= 1; ++mode) {
        bm_286_arch_state_t baseline, adapted;
        memory_t a = {0}, b = {0};
        execute(mode, false, &baseline, &a); execute(mode, true, &adapted, &b);
        assert(!memcmp(&baseline, &adapted, sizeof(baseline)));
        assert(!memcmp(a.bytes, b.bytes, sizeof(a.bytes)));
    }
    {
        memory_t memory = {0};
        bm_286_fetch_supply_t s = {0};
        bm_bus_transaction_t t = {0};
        s.source = access_memory; s.context = &memory;
        assert(bm_286_fetch_supply_redirect(&s, 0, 2, true) == BM_STATUS_OK);
        memory.bytes[2] = 0x90; memory.bytes[3] = 0xcc;
        assert(bm_286_fetch_supply_refill(&s) == BM_STATUS_OK);
        assert(bm_286_fetch_supply_refill(&s) == BM_STATUS_OK && s.queue.count == 3);
        t.operation = BM_BUS_FETCH; t.space = BM_ADDRESS_PROGRAM; t.size = 1;
        t.address = 1;
        assert(bm_286_fetch_supply_access(&s, &t) == BM_STATUS_UNSUPPORTED); /* No guessed redirect. */
        t.address = 0; t.attributes = BM_BUS_TRANSACTION_DEBUG;
        assert(bm_286_fetch_supply_access(&s, &t) == BM_STATUS_OK && s.consume == 0 && s.bus.clock == 4);
        memory.fail = true;
        assert(bm_286_fetch_supply_redirect(&s, 10, 30, false) == BM_STATUS_OK);
        assert(bm_286_fetch_supply_refill(&s) == BM_STATUS_DEVICE_ERROR && s.stopped);
        assert(bm_286_fetch_supply_refill(&s) == BM_STATUS_INVALID_STATE);
        memory.fail = false;
        assert(bm_286_fetch_supply_redirect(&s, 10, 30, false) == BM_STATUS_OK);
        assert(bm_286_fetch_supply_refill(&s) == BM_STATUS_OK);
    }
    return 0;
}
