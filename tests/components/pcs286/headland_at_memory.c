/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Authored composition/failure tests, never firmware or hardware captures.
 */
#include "headland_at_memory.h"
#include "board_io.h"
#include <blumach/components/cpu_80286.h>
#include <blumach/platforms/null_host.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct event { uint32_t address, offset, size; int external; } event_t;
typedef struct fixture {
    bm_gc103_memory_t routes;
    bm_headland_at_memory_t adapter;
    bm_pcs286_memory_t *bytes;
    uint8_t external[256]; /* Authored test endpoint, not an ISA card profile. */
    event_t events[128];
    unsigned calls, fail_at, after, effects, reenter;
    uint32_t waits;
    bm_status_t failure;
    bm_at_bus_t *bus;
    bm_cpu_t cpu;
    bm_pcs286_io_t io;
    bm_ioc02_legacy_registers_t ioc;
    bm_at_pic_t *pic;
    bm_at_dma_t *dma;
    int hold;
} fixture_t;
static unsigned long matrix_cases, write_cases, failure_cases, clock_cases;
static bm_at_transfer_t request(uint32_t address, unsigned size, bm_bus_operation_t op)
{
    bm_at_transfer_t t = {0};
    t.master = BM_AT_MASTER_CPU;
    t.requester_clock = (bm_clock_rate_t){12000000U, 1U};
    t.bus.space = BM_ADDRESS_MEMORY; t.bus.operation = op;
    t.bus.address = address; t.bus.size = size; t.bus.alignment = 1;
    t.bus.value = UINT64_C(0x8877665544332211);
    return t;
}
static void trace_reset(fixture_t *f)
{
    f->calls = f->effects = f->fail_at = f->after = f->reenter = f->waits = 0;
    f->failure = BM_STATUS_DEVICE_ERROR;
}
static void same_transfer(const bm_at_transfer_t *a, const bm_at_transfer_t *b)
{
    /* Compare every contract field, never unspecified C structure padding. */
    assert(a->master == b->master);
    assert(a->requester_clock.cycles_per_second_numerator == b->requester_clock.cycles_per_second_numerator);
    assert(a->requester_clock.cycles_per_second_denominator == b->requester_clock.cycles_per_second_denominator);
    assert(a->bus.space == b->bus.space && a->bus.operation == b->bus.operation);
    assert(a->bus.address == b->bus.address && a->bus.value == b->bus.value);
    assert(a->bus.size == b->bus.size && a->bus.alignment == b->bus.alignment);
    assert(a->bus.wait_states == b->bus.wait_states && a->bus.endianness == b->bus.endianness);
    assert(a->bus.attributes == b->bus.attributes);
}
static bm_status_t endpoint(fixture_t *f, int external, bm_pcs286_memory_region_t region,
                             uint32_t offset, bm_bus_transaction_t *t)
{
    bm_status_t status;
    unsigned i;
    assert(f->calls < 128U && (t->size == 1U || t->size == 2U));
    assert(t->size != 2U || !(t->address & 1U));
    assert(t->alignment == t->size && t->wait_states == 0U);
    f->events[f->calls++] = (event_t){(uint32_t)t->address, offset, t->size, external};
    if (f->reenter) {
        bm_at_transfer_t nested = request(0, 1, BM_BUS_READ), before;
        memcpy(&before, &nested, sizeof(before));
        assert(bm_headland_at_memory_access(&f->adapter, &nested) == BM_STATUS_INVALID_STATE);
        same_transfer(&nested, &before);
        assert(bm_headland_at_memory_a20(&f->adapter, 0) == BM_STATUS_INVALID_STATE);
    }
    if (f->calls == f->fail_at && !f->after) {
        t->value = 0; t->wait_states = UINT32_MAX;
        return f->failure;
    }
    if (!external) {
        status = bm_headland_at_memory_backing(f->bytes, region, offset, t);
        if (status != BM_STATUS_OK) return status;
    } else {
        uint64_t value = 0;
        for (i = 0; i < t->size; ++i) {
            unsigned shift = (t->endianness == BM_ENDIAN_LITTLE ? i : t->size - i - 1U) * 8U;
            if (t->operation == BM_BUS_WRITE)
                f->external[(t->address + i) & 255U] = (uint8_t)(t->value >> shift);
            else value |= (uint64_t)f->external[(t->address + i) & 255U] << shift;
        }
        if (t->operation != BM_BUS_WRITE) t->value = value;
    }
    ++f->effects;
    t->wait_states = f->waits;
    if (f->calls == f->fail_at) {
        t->value = 0; t->wait_states = UINT32_MAX;
        return f->failure;
    }
    return BM_STATUS_OK;
}
static bm_status_t backing(void *context, bm_pcs286_memory_region_t region,
                            uint32_t offset, bm_bus_transaction_t *t)
{ return endpoint(context, 0, region, offset, t); }
static bm_status_t external(void *context, bm_bus_transaction_t *t)
{ return endpoint(context, 1, BM_PCS286_MEMORY_RAM, 0U, t); }
static void start(fixture_t *f, unsigned mib, const uint8_t *image)
{
    bm_host_services_t host = bm_null_host_services();
    bm_pcs286_firmware_t firmware = {0};
    bm_headland_at_config_t c = {0};
    uint32_t i;
    memset(f, 0, sizeof(*f));
    firmware.image[0].data = image; firmware.image[0].size = BM_PCS286_FIRMWARE_BYTES;
    assert(bm_pcs286_memory_create(&host, mib * 0x100000U, &firmware, &f->bytes) == BM_STATUS_OK);
    assert(bm_gc103_memory_initialize(&f->routes, mib * 0x100000U) == BM_STATUS_OK);
    c.profile = BM_HEADLAND_AT_LEGACY_GC103; c.routes = &f->routes;
    c.backing = backing; c.backing_context = f;
    c.external = external; c.external_context = f; c.external_width = 1;
    c.holes = BM_HEADLAND_AT_HOLES_FF; c.protected_writes = BM_HEADLAND_AT_PROTECTED_IGNORE;
    c.timing = BM_HEADLAND_AT_PROVISIONAL_SERVICE_CLOCK;
    c.service_clock = (bm_clock_rate_t){8000000U, 1U}; c.cpu_a20 = 1;
    for (i = 0; i < 4U; ++i) c.extra_clocks[i] = 1;
    assert(bm_headland_at_memory_initialize(&f->adapter, &c) == BM_STATUS_OK);
    for (i = 0; i < 256U; ++i) f->external[i] = (uint8_t)(i ^ 0x5aU);
    /* Distinguish backing offsets, including relocated/shadow/EMS pages. */
    for (i = 0; i < mib * 0x100000U; i += 8U) {
        bm_at_transfer_t t = request(0, 8, BM_BUS_WRITE);
        unsigned b;
        t.bus.value = 0;
        for (b = 0; b < 8U; ++b)
            t.bus.value |= (uint64_t)(uint8_t)((i + b) ^ ((i + b) >> 8) ^ ((i + b) >> 16)) << (8U * b);
        assert(bm_pcs286_memory_access(f->bytes, BM_PCS286_MEMORY_RAM, i, &t.bus) == BM_STATUS_OK);
    }
    trace_reset(f);
}
static void port(fixture_t *f, uint16_t address, unsigned width, uint16_t value)
{ assert(bm_gc103_memory_io(&f->routes, address, width, BM_BUS_WRITE, 0, &value) == BM_STATUS_OK); }
static uint8_t peek(fixture_t *f, bm_gc10x_route_t route)
{
    bm_at_transfer_t t = request(0, 1, BM_BUS_READ);
    if (route.target == BM_GC10X_OPEN_BUS) return 0xffU;
    if (route.target == BM_GC10X_EXTERNAL) return f->external[route.offset & 255U];
    assert(bm_pcs286_memory_access(f->bytes, route.target == BM_GC10X_RAM ?
        BM_PCS286_MEMORY_RAM : BM_PCS286_MEMORY_ROM, route.offset, &t.bus) == BM_STATUS_OK);
    return (uint8_t)t.bus.value;
}
static uint64_t oracle(fixture_t *f, bm_at_transfer_t t)
{
    uint64_t value = 0;
    unsigned i;
    for (i = 0; i < t.bus.size; ++i) {
        bm_gc10x_route_t route;
        unsigned shift = (t.bus.endianness == BM_ENDIAN_LITTLE ? i : t.bus.size - i - 1U) * 8U;
        assert(bm_gc103_memory_resolve(&f->routes, t.master == BM_AT_MASTER_CPU ? BM_GC10X_CPU :
            (t.master == BM_AT_MASTER_ISA ? BM_GC10X_ISA_MASTER : BM_GC10X_DMA),
            f->adapter.config.cpu_a20, (uint32_t)t.bus.address + i, t.bus.operation, &route) == BM_STATUS_OK);
        value |= (uint64_t)peek(f, route) << shift;
    }
    return value;
}
static void matrices(const uint8_t *image)
{
    static const uint32_t addresses[] = {0, 1, 0x3ffd, 0x3ffff, 0x43ffd,
        0x5ffff, 0x7ffff, 0x9fffd, 0xbfffd, 0xc3fff, 0xdfffd, 0xefffd,
        0xffffd, 0x13fffd, 0x15fffd, 0x1fffff, 0x25fffd, 0x3fffff,
        0x45fffd, 0xfdfffd, 0xfefffd, 0xfffff8};
    static const uint8_t controls[] = {0, 2, 3, 4, 0x0c, 0x14, 0x1c, 0x1e};
    fixture_t f;
    unsigned mib, c, who, a20, endian, index, size;
    for (mib = 1; mib <= 4U; ++mib) {
        start(&f, mib, image);
        /* Adjacent windows deliberately have unrelated backing pages. */
        port(&f, 0x1eeU, 1, 0); port(&f, 0x1ecU, 2, 0x288U);
        port(&f, 0x1eeU, 1, 1); port(&f, 0x1ecU, 2, 0x201U);
        port(&f, 0x1eeU, 1, 24); port(&f, 0x1ecU, 2, 0x3ffU);
        for (c = 0; c < sizeof(controls); ++c) {
            port(&f, 0x1efU, 1, controls[c]);
            for (who = 0; who < 4U; ++who) for (a20 = 0; a20 < 2U; ++a20)
            for (endian = 0; endian < 2U; ++endian)
            for (index = 0; index < sizeof(addresses) / sizeof(addresses[0]); ++index)
            for (size = 1; size <= 8U; ++size) {
                bm_at_transfer_t t = request(addresses[index], size, index & 1U ? BM_BUS_FETCH : BM_BUS_READ);
                bm_headland_at_progress_t last;
                uint64_t expected;
                memcpy(&last, &f.adapter.last, sizeof(last));
                t.master = (bm_at_master_t)who; t.bus.endianness = (bm_endianness_t)endian;
                t.bus.attributes = BM_BUS_TRANSACTION_DEBUG | BM_BUS_TRANSACTION_LOCKED;
                assert(bm_headland_at_memory_a20(&f.adapter, (int)a20) == BM_STATUS_OK);
                expected = oracle(&f, t);
                trace_reset(&f);
                assert(bm_headland_at_memory_access(&f.adapter, &t) == BM_STATUS_OK);
                assert(t.bus.value == expected && t.bus.wait_states == 0U);
                assert(memcmp(&last, &f.adapter.last, sizeof(last)) == 0);
                ++matrix_cases;
            }
        }
        bm_pcs286_memory_destroy(f.bytes);
    }
}
static void writes(const uint8_t *image)
{
    static const uint32_t addresses[] = {0x3fffdU, 0x43fffU, 0x9ffffU, 0xfffffU,
        0x13ffffU, 0xdffffU, 0xfeffffU, 0xfffff8U};
    static const uint8_t controls[] = {0, 2, 0x1c};
    fixture_t f;
    unsigned mib, c, a, a20, who, endian, size, i;
    for (mib = 1; mib <= 4U; ++mib) {
        start(&f, mib, image);
        port(&f, 0x1eeU, 1, 0); port(&f, 0x1ecU, 2, 0x288U);
        port(&f, 0x1eeU, 1, 1); port(&f, 0x1ecU, 2, 0x203U);
        for (c = 0; c < sizeof(controls); ++c) {
            port(&f, 0x1efU, 1, controls[c]);
            for (a = 0; a < sizeof(addresses) / sizeof(addresses[0]); ++a)
            for (a20 = 0; a20 < 2U; ++a20) for (who = 0; who < 4U; ++who)
            for (endian = 0; endian < 2U; ++endian) for (size = 1; size <= 8U; ++size) {
                bm_at_transfer_t t = request(addresses[a], size, BM_BUS_WRITE);
                bm_gc10x_route_t routes[8];
                uint8_t expected[8];
                t.master = (bm_at_master_t)who; t.bus.endianness = (bm_endianness_t)endian;
                t.bus.space = BM_ADDRESS_DATA;
                f.adapter.config.cpu_a20 = (int)a20;
                for (i = 0; i < size; ++i) {
                    unsigned shift = (endian == 0U ? i : size - i - 1U) * 8U;
                    assert(bm_gc103_memory_resolve(&f.routes, who == 0U ? BM_GC10X_CPU :
                        (who == 3U ? BM_GC10X_ISA_MASTER : BM_GC10X_DMA), (int)a20,
                        addresses[a] + i, BM_BUS_WRITE, &routes[i]) == BM_STATUS_OK);
                    expected[i] = routes[i].writable ? (uint8_t)(t.bus.value >> shift) : peek(&f, routes[i]);
                }
                trace_reset(&f);
                assert(bm_headland_at_memory_access(&f.adapter, &t) == BM_STATUS_OK);
                assert(t.bus.value == UINT64_C(0x8877665544332211));
                for (i = 0; i < size; ++i) assert(peek(&f, routes[i]) == expected[i]);
                assert(f.adapter.last.completed_bytes == size && f.adapter.last.attempted_bytes == size);
                ++write_cases;
            }
        }
        bm_pcs286_memory_destroy(f.bytes);
    }
}
static void unchanged(fixture_t *f, bm_at_transfer_t t, bm_status_t status)
{
    bm_at_transfer_t before = t;
    assert(bm_headland_at_memory_access(&f->adapter, &t) == status);
    same_transfer(&before, &t);
}
static void policies(const uint8_t *image)
{
    fixture_t f;
    bm_at_transfer_t t;
    bm_gc103_memory_t before;
    start(&f, 1, image);
    memcpy(&before, &f.routes, sizeof(before));
    f.adapter.config.timing = BM_HEADLAND_AT_STRICT;
    t = request(0x9ffffU, 4, BM_BUS_WRITE);
    unchanged(&f, t, BM_STATUS_UNSUPPORTED);
    assert(f.calls == 0U && f.adapter.last.attempted_bytes == 0U);
    t.bus.operation = BM_BUS_READ; t.bus.attributes = BM_BUS_TRANSACTION_DEBUG;
    assert(bm_headland_at_memory_access(&f.adapter, &t) == BM_STATUS_OK && t.bus.wait_states == 0U);
    t.bus.operation = BM_BUS_WRITE;
    unchanged(&f, t, BM_STATUS_UNSUPPORTED);
    assert(memcmp(&before, &f.routes, sizeof(before)) == 0);
    f.adapter.config.timing = BM_HEADLAND_AT_PROVISIONAL_SERVICE_CLOCK;
    f.adapter.config.protected_writes = BM_HEADLAND_AT_PROTECTED_REJECT;
    trace_reset(&f);
    t = request(0xdffffU, 2, BM_BUS_WRITE); /* external then ROM: preflight stops BOTH */
    unchanged(&f, t, BM_STATUS_READ_ONLY);
    assert(f.calls == 0U);
    f.adapter.config.protected_writes = BM_HEADLAND_AT_PROTECTED_IGNORE;
    assert(bm_headland_at_memory_access(&f.adapter, &t) == BM_STATUS_OK);
    assert(f.calls == 1U && f.events[0].address == 0xdffffU && f.external[255] == 0x11U);
    trace_reset(&f);
    f.adapter.config.external = NULL; f.adapter.config.holes = BM_HEADLAND_AT_HOLES_REJECT;
    unchanged(&f, request(0x9ffffU, 2, BM_BUS_WRITE), BM_STATUS_UNMAPPED);
    assert(f.calls == 0U);
    f.adapter.config.holes = BM_HEADLAND_AT_HOLES_FF;
    t = request(0xa0000U, 8, BM_BUS_READ);
    assert(bm_headland_at_memory_access(&f.adapter, &t) == BM_STATUS_OK && t.bus.value == UINT64_MAX);
    assert(f.calls == 0U);
    /* OPEN_BUS inherited from an EMS page beyond installed backing. */
    port(&f, 0x1eeU, 1, 0); port(&f, 0x1ecU, 2, 0x3ffU); port(&f, 0x1efU, 1, 2);
    t = request(0x40000U, 2, BM_BUS_FETCH);
    assert(bm_headland_at_memory_access(&f.adapter, &t) == BM_STATUS_OK && t.bus.value == 0xffffU);
    f.adapter.config.holes = BM_HEADLAND_AT_HOLES_REJECT;
    unchanged(&f, request(0x3ffffU, 2, BM_BUS_WRITE), BM_STATUS_UNMAPPED);
    assert(f.calls == 0U);
    /* A registered endpoint's errors are never open-bus/ignored-write success. */
    f.adapter.config.external = external; f.adapter.config.holes = BM_HEADLAND_AT_HOLES_FF;
    trace_reset(&f); f.fail_at = 1; f.failure = BM_STATUS_UNMAPPED;
    unchanged(&f, request(0xa0000U, 1, BM_BUS_READ), BM_STATUS_UNMAPPED);
    assert(f.calls == 1U && f.effects == 0U);
    trace_reset(&f); f.fail_at = 1; f.failure = BM_STATUS_READ_ONLY;
    unchanged(&f, request(0U, 1, BM_BUS_WRITE), BM_STATUS_READ_ONLY);
    trace_reset(&f); f.reenter = 1;
    t = request(0U, 8, BM_BUS_READ);
    assert(bm_headland_at_memory_access(&f.adapter, &t) == BM_STATUS_OK && f.calls == 4U);
    assert(f.adapter.config.cpu_a20 == 1 && !f.adapter.busy);
    /* A backing capacity mismatch is an error, not an explicit OPEN_BUS route. */
    assert(bm_gc103_memory_initialize(&f.routes, 0x200000U) == BM_STATUS_OK);
    trace_reset(&f);
    unchanged(&f, request(0x1a0000U, 2, BM_BUS_READ), BM_STATUS_UNMAPPED);
    assert(f.calls == 1U && f.effects == 0U);
    bm_pcs286_memory_destroy(f.bytes);
}
static void failures(const uint8_t *image)
{
    static const bm_status_t statuses[] = {BM_STATUS_DEVICE_ERROR, BM_STATUS_UNMAPPED,
        BM_STATUS_READ_ONLY, BM_STATUS_INVALID_STATE, BM_STATUS_CAPACITY_EXCEEDED,
        BM_STATUS_UNSUPPORTED, BM_STATUS_OUT_OF_MEMORY, BM_STATUS_IDLE};
    static const uint32_t addresses[] = {0x3fffdU, 0x9fffdU, 0xa0001U};
    static const unsigned sizes[][8] = {{1, 2, 2, 2, 1}, {1, 2, 1, 1, 1, 1, 1}, {1, 1, 1, 1, 1, 1, 1, 1}};
    static const unsigned counts[] = {5, 7, 8};
    fixture_t f;
    unsigned pattern, op, endian, fail, after, error, i;
    start(&f, 1, image);
    for (pattern = 0; pattern < 3U; ++pattern)
    for (op = 0; op < 3U; ++op) for (endian = 0; endian < 2U; ++endian)
    for (fail = 1; fail <= counts[pattern]; ++fail) for (after = 0; after < 2U; ++after)
    for (error = 0; error < sizeof(statuses) / sizeof(statuses[0]); ++error) {
        bm_at_transfer_t t = request(addresses[pattern], 8, (bm_bus_operation_t)op);
        bm_bus_transaction_t seed = t.bus;
        unsigned completed = 0, affected;
        seed.operation = BM_BUS_WRITE; seed.value = 0;
        assert(bm_pcs286_memory_access(f.bytes, BM_PCS286_MEMORY_RAM, addresses[pattern], &seed) == BM_STATUS_OK);
        memset(f.external, 0, sizeof(f.external));
        trace_reset(&f); f.fail_at = fail; f.after = after; f.failure = statuses[error];
        t.bus.endianness = (bm_endianness_t)endian;
        unchanged(&f, t, statuses[error]);
        for (i = 0; i + 1U < fail; ++i) completed += sizes[pattern][i];
        affected = completed + (after ? sizes[pattern][fail - 1U] : 0U);
        assert(f.calls == fail && f.effects == fail - 1U + after);
        assert(f.adapter.last.completed_bytes == completed);
        assert(f.adapter.last.attempted_bytes == completed + sizes[pattern][fail - 1U]);
        assert(f.adapter.last.status == statuses[error] && !f.adapter.busy);
        for (i = 0; i < 8U; ++i) {
            unsigned shift = (endian == 0U ? i : 7U - i) * 8U;
            bm_gc10x_route_t route;
            assert(bm_gc103_memory_resolve(&f.routes, BM_GC10X_CPU, 1, addresses[pattern] + i, BM_BUS_READ, &route) == BM_STATUS_OK);
            assert(peek(&f, route) == (op == BM_BUS_WRITE && i < affected ? ((t.bus.value >> shift) & 255U) : 0U));
        }
        ++failure_cases;
    }
    bm_pcs286_memory_destroy(f.bytes);
}
static void clocks(const uint8_t *image)
{
    fixture_t f;
    unsigned sn, sd, rn, rd, size;
    start(&f, 1, image);
    for (sn = 1; sn <= 5U; ++sn) for (sd = 1; sd <= 5U; ++sd)
    for (rn = 1; rn <= 5U; ++rn) for (rd = 1; rd <= 5U; ++rd)
    for (size = 1; size <= 8U; ++size) {
        bm_at_transfer_t t = request(0x9ffffU, size, BM_BUS_READ);
        uint64_t n, d, expected;
        trace_reset(&f); f.waits = 2U;
        f.adapter.config.service_clock = (bm_clock_rate_t){sn, sd};
        t.requester_clock = (bm_clock_rate_t){rn, rd};
        /* One RAM byte and size-1 external bytes, each with 1+2 service clocks. */
        n = (uint64_t)size * 3U * sd * rn; d = (uint64_t)sn * rd;
        expected = n / d + (n % d != 0U);
        assert(bm_headland_at_memory_access(&f.adapter, &t) == BM_STATUS_OK);
        assert(t.bus.wait_states == expected && f.calls == size);
        assert(f.adapter.last.timing == BM_GC10X_WAIT_PROVISIONAL);
        ++clock_cases;
    }
    /* No per-fragment ceil or cross-transfer credit; reduced 64-bit rates. */
    f.adapter.config.service_clock = (bm_clock_rate_t){3, 1};
    for (size = 1; size <= 2U; ++size) {
        bm_at_transfer_t t = request(0xa0000U, size, BM_BUS_READ);
        trace_reset(&f); t.requester_clock = (bm_clock_rate_t){1, 1};
        assert(bm_headland_at_memory_access(&f.adapter, &t) == BM_STATUS_OK && t.bus.wait_states == 1U);
    }
    {
        bm_at_transfer_t t = request(0x9ffffU, 4, BM_BUS_READ);
        trace_reset(&f); t.requester_clock = (bm_clock_rate_t){2, 1};
        f.adapter.config.extra_clocks[BM_GC10X_EXTERNAL] = 2U;
        f.adapter.config.extra_clocks[BM_GC10X_FIRMWARE] = 3U;
        f.adapter.config.extra_clocks[BM_GC10X_OPEN_BUS] = 4U;
        assert(bm_headland_at_memory_access(&f.adapter, &t) == BM_STATUS_OK && t.bus.wait_states == 5U);
        trace_reset(&f); t = request(0xdffffU, 4, BM_BUS_READ); t.requester_clock = (bm_clock_rate_t){2, 1};
        assert(bm_headland_at_memory_access(&f.adapter, &t) == BM_STATUS_OK && t.bus.wait_states == 6U);
        port(&f, 0x1eeU, 1, 0); port(&f, 0x1ecU, 2, 0x3ffU); port(&f, 0x1efU, 1, 2);
        trace_reset(&f); t = request(0x40000U, 2, BM_BUS_READ); t.requester_clock = (bm_clock_rate_t){2, 1};
        assert(bm_headland_at_memory_access(&f.adapter, &t) == BM_STATUS_OK && t.bus.wait_states == 3U && f.calls == 0U);
    }
    f.adapter.config.service_clock = (bm_clock_rate_t){UINT64_MAX, UINT64_MAX};
    {
        bm_at_transfer_t t = request(0U, 1, BM_BUS_READ);
        trace_reset(&f); t.requester_clock = f.adapter.config.service_clock;
        assert(bm_headland_at_memory_access(&f.adapter, &t) == BM_STATUS_OK && t.bus.wait_states == 1U);
    }
    f.adapter.config.service_clock = (bm_clock_rate_t){1, 1};
    f.adapter.config.extra_clocks[BM_GC10X_RAM] = UINT32_MAX;
    trace_reset(&f);
    unchanged(&f, request(1U, 2, BM_BUS_WRITE), BM_STATUS_CAPACITY_EXCEEDED);
    assert(f.calls == 0U);
    f.adapter.config.extra_clocks[BM_GC10X_RAM] = 0U;
    {
        bm_at_transfer_t t = request(0U, 4, BM_BUS_WRITE);
        trace_reset(&f); f.waits = UINT32_MAX;
        t.requester_clock = (bm_clock_rate_t){1, 1};
        unchanged(&f, t, BM_STATUS_CAPACITY_EXCEEDED);
        assert(f.effects == 2U && f.adapter.last.completed_bytes == 4U);
        trace_reset(&f); f.waits = UINT32_MAX; f.fail_at = 1; f.after = 1;
        unchanged(&f, t, BM_STATUS_DEVICE_ERROR); /* Host failure wins over its bogus waits. */
        assert(f.effects == 1U && f.adapter.last.completed_bytes == 0U);
        trace_reset(&f); f.waits = UINT32_MAX;
        t.bus.size = 1U;
        assert(bm_headland_at_memory_access(&f.adapter, &t) == BM_STATUS_OK && t.bus.wait_states == UINT32_MAX);
        trace_reset(&f); f.waits = UINT32_MAX;
        t.bus.wait_states = 0U; t.bus.operation = BM_BUS_READ;
        t.bus.attributes = BM_BUS_TRANSACTION_DEBUG;
        f.adapter.config.timing = BM_HEADLAND_AT_STRICT;
        assert(bm_headland_at_memory_access(&f.adapter, &t) == BM_STATUS_OK && t.bus.wait_states == 0U);
    }
    bm_pcs286_memory_destroy(f.bytes);
}
static bm_status_t at_memory(void *context, bm_at_transfer_t *t)
{ return bm_headland_at_memory_access(&((fixture_t *)context)->adapter, t); }
static bm_status_t at_io(void *context, bm_at_transfer_t *t)
{ return bm_pcs286_io_access(&((fixture_t *)context)->io, t); }
static void hold(void *context, int asserted)
{
    fixture_t *f = context;
    f->hold = asserted;
    if (f->cpu.context) assert(f->cpu.ops.signal(f->cpu.context, BM_286_SIGNAL_HOLD, asserted) == BM_STATUS_OK);
}
static void hlda(void *context, int asserted)
{ assert(bm_at_bus_hold_ack(((fixture_t *)context)->bus, asserted) == BM_STATUS_OK); }
static void make_bus(fixture_t *f)
{
    bm_host_services_t host = bm_null_host_services();
    bm_at_bus_config_t c = {0};
    bm_at_pic_config_t p = {0};
    bm_at_dma_config_t d = {0};
    bm_pcs286_io_config_t io = {0};
    p.master_base = 0x20; p.slave_base = 0xa0; p.cascade_line = 2;
    assert(bm_at_pic_create(&host, &p, &f->pic) == BM_STATUS_OK);
    d.clock = (bm_clock_rate_t){4000000, 1}; d.memory = at_memory; d.memory_context = f;
    assert(bm_at_dma_create(&host, &d, &f->dma) == BM_STATUS_OK);
    assert(bm_ioc02_legacy_initialize(&f->ioc) == BM_STATUS_OK);
    io.profile = BM_PCS286_IO_LEGACY_GC103_AT; io.headland = &f->routes;
    io.ioc02 = &f->ioc; io.pic = f->pic; io.dma = f->dma;
    io.timing = BM_PCS286_IO_PROVISIONAL; io.service_clock = (bm_clock_rate_t){8000000, 1};
    assert(bm_pcs286_io_initialize(&f->io, &io) == BM_STATUS_OK);
    c.cpu_clock = (bm_clock_rate_t){12000000, 1}; c.isa_clock = (bm_clock_rate_t){8000000, 1};
    c.memory = at_memory; c.io = at_io; c.decode_context = f;
    c.hold = hold; c.hold_context = f;
    assert(bm_at_bus_create(&host, &c, &f->bus) == BM_STATUS_OK);
}
static void arbitration(const uint8_t *image)
{
    fixture_t f;
    unsigned master;
    start(&f, 1, image); make_bus(&f);
    assert(bm_headland_at_memory_a20(&f.adapter, 0) == BM_STATUS_OK);
    for (master = BM_AT_MASTER_DMA8; master <= BM_AT_MASTER_ISA; ++master) {
        bm_at_transfer_t t = request(0x100000U, 2, BM_BUS_WRITE), before;
        trace_reset(&f); t.master = (bm_at_master_t)master;
        memcpy(&before, &t, sizeof(before));
        assert(bm_at_bus_access(f.bus, &t) == BM_STATUS_IDLE && f.calls == 0U);
        same_transfer(&before, &t);
        assert(bm_at_bus_set_lock(f.bus, 1) == BM_STATUS_OK);
        assert(bm_at_bus_request(f.bus, t.master, 1) == BM_STATUS_OK && !f.hold);
        assert(bm_at_bus_access(f.bus, &t) == BM_STATUS_IDLE && f.calls == 0U);
        assert(bm_at_bus_set_lock(f.bus, 0) == BM_STATUS_OK && f.hold);
        assert(bm_at_bus_hold_ack(f.bus, 1) == BM_STATUS_OK);
        assert(bm_at_bus_access(f.bus, &t) == BM_STATUS_OK);
        assert(f.calls == 1U && f.events[0].address == 0x100000U && f.events[0].offset == 0xa0000U);
        trace_reset(&f); t = request(0x100000U, 2, BM_BUS_READ);
        assert(bm_at_bus_access(f.bus, &t) == BM_STATUS_IDLE && f.calls == 0U);
        t.bus.attributes = BM_BUS_TRANSACTION_DEBUG;
        assert(bm_at_bus_access(f.bus, &t) == BM_STATUS_OK && t.bus.wait_states == 0U);
        assert(f.events[0].address == 0U && f.events[0].offset == 0U);
        assert(bm_at_bus_request(f.bus, (bm_at_master_t)master, 0) == BM_STATUS_OK && !f.hold);
        assert(bm_at_bus_hold_ack(f.bus, 0) == BM_STATUS_OK);
    }
    bm_at_bus_destroy(f.bus); bm_at_pic_destroy(f.pic); bm_at_dma_destroy(f.dma);
    bm_pcs286_memory_destroy(f.bytes);
}
static void cpu_program(uint8_t *image)
{
    static const uint8_t trampoline[] = {0xea, 0, 1, 0, 0};
    static const uint8_t program[] = {
        0xb8,0xff,0xff, 0x8e,0xd8, 0xbb,0x10,0, /* DS:BX -> 100000h */
        0xc7,0x07,0xef,0xbe,                    /* relocated RAM write */
        0xba,0xef,1, 0xb0,2, 0xee,              /* CR0: enable EMS */
        0xba,0xee,1, 0xb0,0, 0xee,              /* MAR: slot0 */
        0xba,0xec,1, 0xb8,0x88,2, 0xef,         /* EMS -> A0000 backing */
        0xb8,0,0x40, 0x8e,0xd8, 0xbb,0,0,
        0x8b,0x0f, 0xf4                        /* CX=[EMS], HLT */
    };
    fixture_t f;
    bm_host_services_t host = bm_null_host_services();
    bm_286_config_t c = {0};
    bm_286_arch_state_t state = {0};
    bm_286_boundary_t boundary = {0};
    unsigned i;
    memcpy(image + BM_PCS286_FIRMWARE_BYTES - 16U, trampoline, sizeof(trampoline));
    start(&f, 1, image); make_bus(&f);
    for (i = 0; i < sizeof(program); ++i) {
        bm_bus_transaction_t t = request(0, 1, BM_BUS_WRITE).bus;
        t.value = program[i];
        assert(bm_pcs286_memory_access(f.bytes, BM_PCS286_MEMORY_RAM, 0x100U + i, &t) == BM_STATUS_OK);
    }
    c.size = sizeof(c); c.version = BM_286_CONTRACT_VERSION;
    c.access = bm_at_bus_cpu_access; c.access_context = f.bus;
    c.hold_ack = hlda; c.pin_context = &f;
    assert(bm_286_create(&host, &c, &f.cpu) == BM_STATUS_OK);
    for (i = 0; i < 30U; ++i) {
        assert(bm_286_step(&f.cpu, &boundary) == BM_STATUS_OK);
        assert(boundary.timing == BM_286_TIMING_UNKNOWN);
        if (i == 0) assert(boundary.instruction_address == 0xfffff0U);
        assert(bm_286_get_arch_state(&f.cpu, &state) == BM_STATUS_OK);
        if (state.halted) break;
    }
    assert(i < 30U && state.cx == 0xbeefU);
    assert(f.routes.registers.ems[0] == 0x288U);
    assert(f.cpu.ops.reset(f.cpu.context) == BM_STATUS_OK);
    trace_reset(&f); f.fail_at = 1;
    assert(bm_286_step(&f.cpu, &boundary) == BM_STATUS_DEVICE_ERROR);
    assert(bm_286_get_arch_state(&f.cpu, &state) == BM_STATUS_OK);
    assert(state.ip == 0xfff0U && !state.shutdown); /* Host stop, no guest delivery. */
    assert(bm_286_step(&f.cpu, &boundary) == BM_STATUS_INVALID_STATE && f.calls == 1U);
    f.cpu.ops.destroy(f.cpu.context); f.cpu.context = NULL;
    bm_at_bus_destroy(f.bus); bm_at_pic_destroy(f.pic); bm_at_dma_destroy(f.dma);
    bm_pcs286_memory_destroy(f.bytes);
}
static void invalid(const uint8_t *image)
{
    fixture_t f;
    bm_headland_at_memory_t before;
    bm_headland_at_config_t c;
    bm_at_transfer_t t;
    unsigned i;
    start(&f, 1, image);
    memcpy(&before, &f.adapter, sizeof(before));
    assert(bm_headland_at_memory_initialize(NULL, &before.config) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_headland_at_memory_initialize(&f.adapter, NULL) == BM_STATUS_INVALID_ARGUMENT);
    for (i = 0; i < 9U; ++i) {
        c = before.config;
        switch (i) {
        case 0: c.profile = 0; break;
        case 1: c.routes = NULL; break;
        case 2: c.backing = NULL; break;
        case 3: c.external_width = 4; break;
        case 4: c.holes = (bm_headland_at_holes_t)3; break;
        case 5: c.protected_writes = (bm_headland_at_protected_t)3; break;
        case 6: c.timing = (bm_headland_at_timing_t)3; break;
        case 7: c.cpu_a20 = 2; break;
        default: c.service_clock.cycles_per_second_denominator = 0; break;
        }
        assert(bm_headland_at_memory_initialize(&f.adapter, &c) == BM_STATUS_INVALID_ARGUMENT);
        assert(memcmp(&before, &f.adapter, sizeof(before)) == 0);
    }
    assert(bm_headland_at_memory_access(NULL, NULL) == BM_STATUS_INVALID_ARGUMENT);
    c = before.config; c.service_clock = (bm_clock_rate_t){1, UINT64_MAX};
    assert(bm_headland_at_memory_initialize(&f.adapter, &c) == BM_STATUS_CAPACITY_EXCEEDED);
    assert(memcmp(&before, &f.adapter, sizeof(before)) == 0);
    assert(bm_headland_at_memory_access(&f.adapter, NULL) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_headland_at_memory_a20(NULL, 0) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_headland_at_memory_a20(&f.adapter, -1) == BM_STATUS_INVALID_ARGUMENT);
    for (i = 0; i < 13U; ++i) {
        t = request(0, 1, BM_BUS_READ);
        switch (i) {
        case 0: t.bus.size = 0; break;
        case 1: t.bus.size = 9; break;
        case 2: t.bus.address = 0x1000000U; break;
        case 3: t.bus.address = 0xffffffU; t.bus.size = 2; break;
        case 4: t.bus.operation = (bm_bus_operation_t)99; break;
        case 5: t.bus.endianness = (bm_endianness_t)99; break;
        case 6: t.bus.attributes = 4; break;
        case 7: t.bus.wait_states = 1; break;
        case 8: t.master = (bm_at_master_t)99; break;
        case 9: t.requester_clock.cycles_per_second_numerator = 0; break;
        case 10: t.requester_clock.cycles_per_second_denominator = 0; break;
        case 11: t.bus.space = (bm_address_space_t)99; break;
        default: t.bus.address = UINT64_MAX; break;
        }
        unchanged(&f, t, BM_STATUS_INVALID_ARGUMENT);
        assert(f.calls == 0U);
    }
    t = request(0, 1, BM_BUS_READ); t.bus.space = BM_ADDRESS_IO;
    unchanged(&f, t, BM_STATUS_UNMAPPED);
    assert(memcmp(&before, &f.adapter, sizeof(before)) == 0);
    bm_pcs286_memory_destroy(f.bytes);
}
static void isolation(const uint8_t *image)
{
    fixture_t a, b;
    bm_at_transfer_t t;
    bm_headland_at_config_t copy;
    uint64_t before;
    unsigned width, offset;
    start(&a, 1, image); start(&b, 1, image);
    t = request(0x80000U, 2, BM_BUS_READ); before = oracle(&a, t);
    t = request(0x60000U, 2, BM_BUS_WRITE); t.bus.value = 0x1234U;
    assert(bm_headland_at_memory_access(&a.adapter, &t) == BM_STATUS_OK);
    t = request(0x80000U, 2, BM_BUS_READ);
    assert(bm_headland_at_memory_access(&a.adapter, &t) == BM_STATUS_OK && t.bus.value == before);
    t = request(0x60000U, 2, BM_BUS_READ); before = oracle(&b, t);
    assert(bm_headland_at_memory_access(&b.adapter, &t) == BM_STATUS_OK && t.bus.value == before && before != 0x1234U);
    copy = a.adapter.config;
    assert(bm_headland_at_memory_initialize(&a.adapter, &copy) == BM_STATUS_OK);
    copy.cpu_a20 = 0; copy.extra_clocks[0] = 99; /* Config fields were copied. */
    assert(a.adapter.config.cpu_a20 == 1 && a.adapter.config.extra_clocks[0] == 1U);
    assert(bm_gc103_memory_initialize(&a.routes, 0x100000U) == BM_STATUS_OK);
    t = request(0x60000U, 2, BM_BUS_READ);
    assert(bm_headland_at_memory_access(&a.adapter, &t) == BM_STATUS_OK && t.bus.value == 0x1234U);
    for (width = 1; width <= 2U; ++width) for (offset = 0; offset < 2U; ++offset) {
        trace_reset(&a); a.adapter.config.external_width = width;
        t = request(0xa0000U + offset, 8, BM_BUS_WRITE); t.bus.endianness = BM_ENDIAN_BIG;
        assert(bm_headland_at_memory_access(&a.adapter, &t) == BM_STATUS_OK);
        assert(a.calls == (width == 1U ? 8U : (offset ? 5U : 4U)));
        trace_reset(&a); t = request(0xa0000U + offset, 8, BM_BUS_READ); t.bus.endianness = BM_ENDIAN_BIG;
        assert(bm_headland_at_memory_access(&a.adapter, &t) == BM_STATUS_OK && t.bus.value == UINT64_C(0x8877665544332211));
    }
    bm_pcs286_memory_destroy(a.bytes); bm_pcs286_memory_destroy(b.bytes);
}
int main(void)
{
    bm_host_services_t host = bm_null_host_services();
    uint8_t *image = host.allocate(host.context, BM_PCS286_FIRMWARE_BYTES);
    unsigned i;
    assert(image != NULL);
    for (i = 0; i < BM_PCS286_FIRMWARE_BYTES; ++i) image[i] = (uint8_t)(i ^ (i >> 7) ^ 0xa5U);
    matrices(image); writes(image); policies(image); failures(image); clocks(image); arbitration(image);
    invalid(image); isolation(image); cpu_program(image);
    host.release(host.context, image);
    printf("Headland AT: %lu route/backing reads, %lu writes, %lu endpoint failures, %lu rational waits; policy/ownership/CPU checks passed\n",
           matrix_cases, write_cases, failure_cases, clock_cases);
    return 0;
}
