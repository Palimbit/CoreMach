/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 */
#include "bus_fixture.h"
#include "clock_math.h"
#include <blumach/components/linear_memory.h>
#include <blumach/platforms/null_host.h>
#include <assert.h>

static bm_bus_transaction_t transfer(bm_address_space_t space, uint64_t address,
                                     uint32_t size, bm_endianness_t endian)
{
    bm_bus_transaction_t t = {0};
    t.space = space;
    t.address = address;
    t.size = size;
    t.endianness = endian;
    t.operation = BM_BUS_READ;
    t.alignment = 1U;
    return t;
}

static void test_non_pc_address_patterns(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_bus_t *bus = NULL;
    bm_linear_memory_t *memory = NULL;
    bm_linear_memory_config_t config = {0};
    bus_fixture_t io, program, data;
    bm_bus_transaction_t t;
    uint8_t byte;
    assert(bm_bus_create(&host, 4U, &bus) == BM_STATUS_OK);
    /* Real memory implementation, not merely checking our own byte fixture.
     * Address above 16 bits also detects accidental XT/20-bit masking. */
    config.space = BM_ADDRESS_MEMORY;
    config.base = 0x123400U;
    config.size = 256U;
    assert(bm_linear_memory_create(&host, bus, &config, &memory) == BM_STATUS_OK);
    t = transfer(BM_ADDRESS_MEMORY, 0x123400U, 2U, BM_ENDIAN_BIG);
    t.operation = BM_BUS_WRITE;
    t.value = 0x1234U;
    assert(bm_bus_transact(bus, &t) == BM_STATUS_OK);
    assert(bm_linear_memory_peek(memory, 0x123400U, &byte) == BM_STATUS_OK && byte == 0x12U);
    assert(bm_linear_memory_peek(memory, 0x123401U, &byte) == BM_STATUS_OK && byte == 0x34U);
    t.operation = BM_BUS_READ;
    t.endianness = BM_ENDIAN_LITTLE;
    assert(bm_bus_transact(bus, &t) == BM_STATUS_OK && t.value == 0x3412U);
    t.endianness = BM_ENDIAN_BIG;
    assert(bm_bus_transact(bus, &t) == BM_STATUS_OK && t.value == 0x1234U);

    fixture_init(&io, 0U);
    fixture_init(&program, 0U);
    fixture_init(&data, 0U);
    io.bytes[3] = 0x80U;
    program.bytes[3] = 0x65U;
    data.bytes[3] = 0x68U;
    assert(bm_bus_map(bus, BM_ADDRESS_IO, 0U, 255U, fixture_access, &io) == BM_STATUS_OK);
    assert(bm_bus_map(bus, BM_ADDRESS_PROGRAM, 0U, 255U, fixture_access, &program) == BM_STATUS_OK);
    assert(bm_bus_map(bus, BM_ADDRESS_DATA, 0U, 255U, fixture_access, &data) == BM_STATUS_OK);
    t = transfer(BM_ADDRESS_IO, 3U, 1U, BM_ENDIAN_LITTLE);
    assert(bm_bus_transact(bus, &t) == BM_STATUS_OK && t.value == 0x80U);
    t.space = BM_ADDRESS_PROGRAM;
    t.operation = BM_BUS_FETCH;
    assert(bm_bus_transact(bus, &t) == BM_STATUS_OK && t.value == 0x65U);
    t.space = BM_ADDRESS_DATA;
    t.operation = BM_BUS_READ;
    assert(bm_bus_transact(bus, &t) == BM_STATUS_OK && t.value == 0x68U);
    assert(io.reads == 1U && program.fetches == 1U && data.reads == 1U);
    bm_bus_destroy(bus); /* callbacks cannot outlive memory */
    bm_linear_memory_destroy(memory);
}

static void test_routing_fault_debug_and_isolation(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_bus_t *a = NULL, *b = NULL;
    bus_fixture_t left, right;
    bm_bus_transaction_t t;
    bm_bus_static_response_t open = {BM_STATUS_OK, BM_STATUS_OK, BM_STATUS_OK, 0xffU};
    fixture_init(&left, 0U);
    fixture_init(&right, 0U);
    left.bytes[0] = 7U;
    left.waits = 3U;
    right.bytes[0] = 9U;
    assert(bm_bus_create(&host, 1U, &a) == BM_STATUS_OK);
    assert(bm_bus_create(&host, 1U, &b) == BM_STATUS_OK);
    assert(bm_bus_map(a, BM_ADDRESS_MEMORY, 0U, 255U, fixture_access, &left) == BM_STATUS_OK);
    assert(bm_bus_map(b, BM_ADDRESS_MEMORY, 0U, 255U, fixture_access, &right) == BM_STATUS_OK);
    t = transfer(BM_ADDRESS_MEMORY, 0U, 1U, BM_ENDIAN_LITTLE);
    t.attributes = BM_BUS_TRANSACTION_DEBUG | BM_BUS_TRANSACTION_LOCKED;
    assert(bm_bus_transact(a, &t) == BM_STATUS_OK && t.value == 7U);
    assert(t.wait_states == 0U && left.reads == 0U);
    assert(left.trace[0].request.attributes == t.attributes);
    t.operation = BM_BUS_WRITE;
    t.value = 99U;
    assert(bm_bus_transact(a, &t) == BM_STATUS_UNSUPPORTED);
    assert(left.bytes[0] == 7U && left.writes == 0U);
    t = transfer(BM_ADDRESS_MEMORY, 0U, 1U, BM_ENDIAN_LITTLE);
    assert(bm_bus_transact(a, &t) == BM_STATUS_OK && t.wait_states == 3U);
    t.wait_states = 0U;
    assert(bm_bus_transact(b, &t) == BM_STATUS_OK && t.value == 9U);
    assert(left.reads == 1U && right.reads == 1U);

    /* No implicit split or PC-wide open-bus assumption. */
    t = transfer(BM_ADDRESS_MEMORY, 255U, 2U, BM_ENDIAN_LITTLE);
    assert(bm_bus_transact(a, &t) == BM_STATUS_UNMAPPED);
    assert(left.reads == 1U);
    assert(bm_bus_set_default_response(a, BM_ADDRESS_MEMORY, &open) == BM_STATUS_OK);
    t.address = 256U;
    assert(bm_bus_transact(a, &t) == BM_STATUS_OK && t.value == 0xffffU);
    assert(bm_bus_transact(b, &t) == BM_STATUS_UNMAPPED);
    /* Board-selected open bus must not swallow implementation failures. */
    left.result = BM_STATUS_UNSUPPORTED;
    t.address = 0U;
    assert(bm_bus_transact(a, &t) == BM_STATUS_UNSUPPORTED);
    left.result = BM_STATUS_READ_ONLY;
    assert(bm_bus_transact(a, &t) == BM_STATUS_READ_ONLY);
    left.result = BM_STATUS_UNMAPPED;
    assert(bm_bus_transact(a, &t) == BM_STATUS_OK && t.value == 0xffffU);
    bm_bus_destroy(a);
    bm_bus_destroy(b);
}

/* Oracle using existing exact clock math, NOT an implemented AT converter.
 * Rates below are synthetic, not PCS286 measured memory/ISA clock claims. */
static uint64_t ceiling_cycles(const bm_clock_position_t *duration,
                               const bm_clock_rate_t *requester)
{
    bm_clock_position_t edge;
    uint64_t cycles;
    assert(bm_clock_cycles_at_or_before(requester, duration, &cycles) == BM_STATUS_OK);
    assert(bm_clock_position_init(&edge, requester) == BM_STATUS_OK);
    assert(bm_clock_position_advance(&edge, cycles) == BM_STATUS_OK);
    return cycles + (bm_clock_position_compare(&edge, duration) < 0 ? 1U : 0U);
}

static void test_wait_rounding_oracle(void)
{
    const bm_clock_rate_t memory = {8000000U, 1U}, cpu = {12000000U, 1U};
    const bm_clock_rate_t fractional = {14318180U, 3U};
    const bm_clock_rate_t invalid = {1U, 0U};
    bm_clock_position_t pieces, bulk, saved;
    unsigned int i;
    assert(bm_clock_position_init(&pieces, &memory) == BM_STATUS_OK);
    assert(bm_clock_position_advance(&pieces, 1U) == BM_STATUS_OK);
    assert(ceiling_cycles(&pieces, &cpu) == 2U); /* 1.5 rounds UP */
    assert(bm_clock_position_advance(&pieces, 1U) == BM_STATUS_OK);
    assert(ceiling_cycles(&pieces, &cpu) == 3U); /* fragments: 3, not 2+2 */
    assert(bm_clock_position_init(&bulk, &memory) == BM_STATUS_OK);
    assert(bm_clock_position_advance(&bulk, 2U) == BM_STATUS_OK);
    assert(bm_clock_position_compare(&pieces, &bulk) == 0);
    assert(bm_clock_position_init(&pieces, &fractional) == BM_STATUS_OK);
    assert(bm_clock_position_init(&bulk, &fractional) == BM_STATUS_OK);
    for (i = 0U; i < 1000U; ++i)
        assert(bm_clock_position_advance(&pieces, 1U) == BM_STATUS_OK);
    assert(bm_clock_position_advance(&bulk, 1000U) == BM_STATUS_OK);
    assert(bm_clock_position_compare(&pieces, &bulk) == 0);
    assert(ceiling_cycles(&pieces, &cpu) == ceiling_cycles(&bulk, &cpu));
    saved = pieces;
    assert(bm_clock_position_advance(&pieces, UINT64_MAX) == BM_STATUS_CAPACITY_EXCEEDED);
    assert(bm_clock_position_compare(&pieces, &saved) == 0);
    assert(bm_clock_position_init(&pieces, &invalid) == BM_STATUS_INVALID_ARGUMENT);
}

int main(void)
{
    test_non_pc_address_patterns();
    test_routing_fault_debug_and_isolation();
    test_wait_rounding_oracle();
    return 0;
}
