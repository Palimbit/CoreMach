/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Authored storage and synthetic wiring checks, NOT PCS286 chipset conformance.
 */
#include <blumach/systems/pcs286_memory.h>
#include <blumach/components/at_bus.h>
#include <blumach/platforms/null_host.h>
#include "failure_injection_host.h"
#include <assert.h>
#include <string.h>

static bm_bus_transaction_t request(bm_bus_operation_t operation, uint32_t size)
{
    bm_bus_transaction_t t = {0};
    t.space = BM_ADDRESS_MEMORY;
    t.operation = operation;
    t.endianness = BM_ENDIAN_LITTLE;
    t.size = size;
    return t;
}

static void unchanged(bm_pcs286_memory_t *m, bm_pcs286_memory_region_t region,
                      uint32_t offset, bm_bus_transaction_t t, bm_status_t status)
{
    bm_bus_transaction_t before = t;
    assert(bm_pcs286_memory_access(m, region, offset, &t) == status);
    assert(memcmp(&before, &t, sizeof(t)) == 0);
}

static void storage_test(uint8_t *image)
{
    failure_injection_host_t failure;
    bm_host_services_t host;
    bm_pcs286_firmware_t firmware = {0};
    bm_pcs286_memory_t *a = NULL, *b = NULL;
    bm_bus_transaction_t t;
    size_t i;
    unsigned int size, endian;
    firmware.image[0].data = image;
    firmware.image[0].size = BM_PCS286_FIRMWARE_BYTES;
    for (i = 0; i < 3; ++i) {
        failure_injection_host_initialize(&failure);
        host = failure_injection_host_services(&failure);
        failure_injection_host_fail_on(&failure, i);
        a = (bm_pcs286_memory_t *)&failure; /* output must be cleared on failure */
        assert(bm_pcs286_memory_create(&host, 1024U, &firmware, &a) == BM_STATUS_OUT_OF_MEMORY);
        assert(a == NULL && failure.outstanding_allocations == 0);
    }
    failure_injection_host_initialize(&failure);
    host = failure_injection_host_services(&failure);
    assert(bm_pcs286_memory_create(&host, 1024U, &firmware, &a) == BM_STATUS_OK);
    assert(bm_pcs286_memory_create(&host, 1024U, &firmware, &b) == BM_STATUS_OK);
    assert(failure.outstanding_allocations == 6);
    for (endian = 0; endian < 2; ++endian) {
        for (size = 1; size <= 8; ++size) {
            uint64_t expected = UINT64_C(0x8192a3b4c5d6e7f8);
            uint64_t mask = size == 8 ? UINT64_MAX : (UINT64_C(1) << (size * 8U)) - 1U;
            t = request(BM_BUS_WRITE, size);
            t.endianness = (bm_endianness_t)endian;
            t.address = UINT64_MAX; /* resolved backing offset, no physical mask */
            t.value = expected;
            t.wait_states = 17U;
            t.attributes = BM_BUS_TRANSACTION_LOCKED;
            assert(bm_pcs286_memory_access(a, BM_PCS286_MEMORY_RAM, 3U, &t) == BM_STATUS_OK);
            t.operation = BM_BUS_READ;
            t.value = 0;
            assert(bm_pcs286_memory_access(a, BM_PCS286_MEMORY_RAM, 3U, &t) == BM_STATUS_OK);
            assert(t.value == (expected & mask) && t.wait_states == 17U && t.address == UINT64_MAX);
            /* Verify byte order independently, not merely an inverse roundtrip. */
            for (i = 0; i < size; ++i) {
                bm_bus_transaction_t byte = request(BM_BUS_READ, 1U);
                unsigned int shift = (endian == 0 ? (unsigned int)i : size - (unsigned int)i - 1U) * 8U;
                assert(bm_pcs286_memory_access(a, BM_PCS286_MEMORY_RAM, 3U + (uint32_t)i, &byte) == BM_STATUS_OK);
                assert(byte.value == ((expected >> shift) & 0xffU));
            }
            t.attributes = BM_BUS_TRANSACTION_DEBUG;
            assert(bm_pcs286_memory_access(b, BM_PCS286_MEMORY_RAM, 3U, &t) == BM_STATUS_OK);
            assert(t.value == 0 && t.wait_states == 17U);
        }
    }
    t = request(BM_BUS_WRITE, 1U);
    t.value = 0x5aU;
    assert(bm_pcs286_memory_access(a, BM_PCS286_MEMORY_RAM, 1023U, &t) == BM_STATUS_OK);
    t.size = 2U;
    unchanged(a, BM_PCS286_MEMORY_RAM, 1023U, t, BM_STATUS_UNMAPPED);
    unchanged(a, BM_PCS286_MEMORY_RAM, UINT32_MAX, t, BM_STATUS_UNMAPPED);
    t = request(BM_BUS_READ, 1U);
    assert(bm_pcs286_memory_access(a, BM_PCS286_MEMORY_RAM, 1023U, &t) == BM_STATUS_OK);
    assert(t.value == 0x5aU); /* no partial write */
    t.operation = BM_BUS_WRITE;
    unchanged(a, BM_PCS286_MEMORY_ROM, 0U, t, BM_STATUS_READ_ONLY);
    t.attributes = BM_BUS_TRANSACTION_DEBUG;
    unchanged(a, BM_PCS286_MEMORY_RAM, 3U, t, BM_STATUS_UNSUPPORTED);
    t = request(BM_BUS_FETCH, 1U);
    t.space = BM_ADDRESS_PROGRAM;
    image[0] = 0xa5U; /* helper owns a snapshot, not the caller's buffer */
    assert(bm_pcs286_memory_access(a, BM_PCS286_MEMORY_ROM, 0U, &t) == BM_STATUS_OK);
    assert(t.value == 0 && image[0] == 0xa5U);
    image[0] = 0;
    for (i = 0; i < BM_PCS286_FIRMWARE_BYTES; ++i) {
        assert(bm_pcs286_memory_access(a, BM_PCS286_MEMORY_ROM, (uint32_t)i, &t) == BM_STATUS_OK);
        assert(t.value == image[i]);
    }
    t.size = 2;
    unchanged(a, BM_PCS286_MEMORY_ROM, BM_PCS286_FIRMWARE_BYTES - 1U, t, BM_STATUS_UNMAPPED);
    unchanged(a, (bm_pcs286_memory_region_t)2, 0, t, BM_STATUS_INVALID_ARGUMENT);
    unchanged(NULL, BM_PCS286_MEMORY_RAM, 0, t, BM_STATUS_INVALID_ARGUMENT);
    assert(bm_pcs286_memory_access(a, BM_PCS286_MEMORY_RAM, 0, NULL) == BM_STATUS_INVALID_ARGUMENT);
    t.space = BM_ADDRESS_IO;
    unchanged(a, BM_PCS286_MEMORY_RAM, 0, t, BM_STATUS_INVALID_ARGUMENT);
    t.space = BM_ADDRESS_MEMORY;
    t.size = 0;
    unchanged(a, BM_PCS286_MEMORY_RAM, 0, t, BM_STATUS_INVALID_ARGUMENT);
    t.size = 9;
    unchanged(a, BM_PCS286_MEMORY_RAM, 0, t, BM_STATUS_INVALID_ARGUMENT);
    t.size = 1;
    t.operation = (bm_bus_operation_t)3;
    unchanged(a, BM_PCS286_MEMORY_RAM, 0, t, BM_STATUS_INVALID_ARGUMENT);
    t.operation = BM_BUS_READ;
    t.endianness = (bm_endianness_t)2;
    unchanged(a, BM_PCS286_MEMORY_RAM, 0, t, BM_STATUS_INVALID_ARGUMENT);
    t.endianness = BM_ENDIAN_LITTLE;
    t.attributes = 4U;
    unchanged(a, BM_PCS286_MEMORY_RAM, 0, t, BM_STATUS_INVALID_ARGUMENT);
    bm_pcs286_memory_destroy(a);
    bm_pcs286_memory_destroy(b);
    bm_pcs286_memory_destroy(NULL);
    assert(failure.outstanding_allocations == 0);

    /* Interleave the two authored lanes, and validate every resulting byte. */
    firmware.layout = BM_PCS286_FIRMWARE_LOW_HIGH;
    firmware.image[0].size /= 2U;
    firmware.image[1].data = image + BM_PCS286_FIRMWARE_BYTES / 2U;
    firmware.image[1].size = BM_PCS286_FIRMWARE_BYTES / 2U;
    assert(bm_pcs286_memory_create(&host, 4U * 1024U * 1024U, &firmware, &a) == BM_STATUS_OK);
    t = request(BM_BUS_READ, 1U);
    for (i = 0; i < BM_PCS286_FIRMWARE_BYTES; ++i) {
        assert(bm_pcs286_memory_access(a, BM_PCS286_MEMORY_ROM, (uint32_t)i, &t) == BM_STATUS_OK);
        assert(t.value == firmware.image[i & 1U].data[i / 2U]);
    }
    bm_pcs286_memory_destroy(a);
    assert(bm_pcs286_memory_create(&host, 0, &firmware, &a) == BM_STATUS_INVALID_ARGUMENT && a == NULL);
    assert(bm_pcs286_memory_create(&host, 4U * 1024U * 1024U + 1U, &firmware, &a) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_pcs286_memory_create(NULL, 1024, &firmware, &a) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_pcs286_memory_create(&host, 1024, NULL, &a) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_pcs286_memory_create(&host, 1024, &firmware, NULL) == BM_STATUS_INVALID_ARGUMENT);
    --firmware.image[1].size;
    assert(bm_pcs286_memory_create(&host, 1024, &firmware, &a) == BM_STATUS_INVALID_ARGUMENT);
    ++firmware.image[1].size;
    firmware.image[1].data = NULL;
    assert(bm_pcs286_memory_create(&host, 1024, &firmware, &a) == BM_STATUS_INVALID_ARGUMENT);
    firmware.layout = BM_PCS286_FIRMWARE_COMBINED;
    firmware.image[0].size = BM_PCS286_FIRMWARE_BYTES;
    assert(bm_pcs286_memory_create(&host, 1024, &firmware, &a) == BM_STATUS_INVALID_ARGUMENT);
    firmware.image[1].size = 0;
    firmware.image[0].data = NULL;
    assert(bm_pcs286_memory_create(&host, 1024, &firmware, &a) == BM_STATUS_INVALID_ARGUMENT);
    firmware.image[0].data = image;
    firmware.layout = (bm_pcs286_firmware_layout_t)2;
    assert(bm_pcs286_memory_create(&host, 1024, &firmware, &a) == BM_STATUS_INVALID_ARGUMENT);
    assert(failure.outstanding_allocations == 0);
}

typedef struct synthetic_board {
    bm_pcs286_memory_t *memory;
    bm_at_bus_t *bus;
    bm_cpu_t cpu;
    uint16_t output;
    unsigned int reads, writes, high_fetches;
} synthetic_board_t;

static bm_status_t synthetic_memory(void *context, bm_at_transfer_t *transfer)
{
    synthetic_board_t *board = context;
    bm_bus_transaction_t *t = &transfer->bus;
    uint64_t address = t->address;
    /* TEST-ONLY linear windows. Not Headland emulation or a PCS286 reset map.
     * No A20 assumptions, aliases, open-bus defaults or real wait claims. */
    if (address < 0x100000U && t->size <= 0x100000U - address)
        return bm_pcs286_memory_access(board->memory, BM_PCS286_MEMORY_RAM, (uint32_t)address, t);
    if (address >= 0xfe0000U && address <= 0xffffffU && t->size <= 0x1000000U - address) {
        ++board->high_fetches;
        return bm_pcs286_memory_access(board->memory, BM_PCS286_MEMORY_ROM,
                                      (uint32_t)(address - 0xfe0000U), t);
    }
    return BM_STATUS_UNMAPPED;
}

static bm_status_t synthetic_io(void *context, bm_at_transfer_t *transfer)
{
    synthetic_board_t *board = context;
    bm_bus_transaction_t *t = &transfer->bus;
    if (t->address != 0x80U || t->size != 2U)
        return BM_STATUS_UNMAPPED;
    if (t->operation == BM_BUS_WRITE) {
        ++board->writes;
        board->output = (uint16_t)t->value;
    } else if (t->operation == BM_BUS_READ) {
        ++board->reads;
        t->value = board->output;
    } else {
        return BM_STATUS_UNSUPPORTED;
    }
    return BM_STATUS_OK;
}

static void hold_changed(void *context, int asserted)
{
    synthetic_board_t *board = context;
    assert(board->cpu.ops.signal(board->cpu.context, BM_286_SIGNAL_HOLD, asserted) == BM_STATUS_OK);
}
static void ack_changed(void *context, int asserted)
{
    synthetic_board_t *board = context;
    assert(bm_at_bus_hold_ack(board->bus, asserted) == BM_STATUS_OK);
}

static void composition_test(uint8_t *image)
{
    bm_host_services_t host = bm_null_host_services();
    bm_pcs286_firmware_t firmware = {0};
    bm_at_bus_config_t bus = {0};
    bm_286_config_t cpu = {0};
    bm_286_arch_state_t state = {0};
    bm_286_boundary_t boundary = {0};
    synthetic_board_t board = {0};
    bm_bus_transaction_t t;
    unsigned int i;
    /* Authored reset trampoline into test RAM, NOT bytes from any BIOS. */
    static const uint8_t trampoline[] = {0xea, 0x00, 0x01, 0x00, 0x00};
    static const uint8_t program[] = {
        0xb8, 0x00, 0x20,       /* MOV AX,2000h */
        0x8e, 0xd0,             /* MOV SS,AX */
        0xbc, 0x00, 0x10,       /* MOV SP,1000h */
        0xb8, 0x34, 0x12,       /* MOV AX,1234h */
        0x50,                   /* PUSH AX at physical 20FFEh */
        0x5b,                   /* POP BX */
        0xe7, 0x80,             /* OUT 80h,AX: synthetic latch, not POST device */
        0xb8, 0x00, 0x00,       /* MOV AX,0 */
        0xe5, 0x80,             /* IN AX,80h */
        0x90                    /* NOP */
    };
    memset(image, 0, BM_PCS286_FIRMWARE_BYTES);
    memcpy(image + BM_PCS286_FIRMWARE_BYTES - 16U, trampoline, sizeof(trampoline));
    firmware.image[0].data = image;
    firmware.image[0].size = BM_PCS286_FIRMWARE_BYTES;
    assert(bm_pcs286_memory_create(&host, 0x100000U, &firmware, &board.memory) == BM_STATUS_OK);
    bus.cpu_clock = (bm_clock_rate_t){12000000U, 1U};
    bus.isa_clock = (bm_clock_rate_t){8000000U, 1U}; /* synthetic configuration */
    bus.memory = synthetic_memory;
    bus.io = synthetic_io;
    bus.decode_context = &board;
    bus.hold = hold_changed;
    bus.hold_context = &board;
    assert(bm_at_bus_create(&host, &bus, &board.bus) == BM_STATUS_OK);
    cpu.size = sizeof(cpu);
    cpu.version = BM_286_CONTRACT_VERSION;
    cpu.access = bm_at_bus_cpu_access;
    cpu.access_context = board.bus;
    cpu.hold_ack = ack_changed;
    cpu.pin_context = &board;
    assert(bm_286_create(&host, &cpu, &board.cpu) == BM_STATUS_OK);
    for (i = 0; i < sizeof(program); ++i) {
        t = request(BM_BUS_WRITE, 1U);
        t.value = program[i];
        assert(bm_pcs286_memory_access(board.memory, BM_PCS286_MEMORY_RAM, 0x100U + i, &t) == BM_STATUS_OK);
    }
    for (i = 0; i < 11; ++i) {
        assert(bm_286_step(&board.cpu, &boundary) == BM_STATUS_OK);
        assert(boundary.timing == BM_286_TIMING_UNKNOWN);
        if (i == 0) assert(boundary.instruction_address == 0xfffff0U);
    }
    assert(bm_286_get_arch_state(&board.cpu, &state) == BM_STATUS_OK);
    assert(state.ax == 0x1234U && state.bx == 0x1234U && state.sp == 0x1000U);
    assert(state.ss.base == 0x20000U && state.cs.base == 0 && state.ip == 0x100U + sizeof(program));
    assert(board.high_fetches > 0 && board.writes == 1U && board.reads == 1U);
    t = request(BM_BUS_READ, 2U);
    t.attributes = BM_BUS_TRANSACTION_DEBUG;
    t.address = 0x20ffeU;
    assert(bm_at_bus_cpu_access(board.bus, &t) == BM_STATUS_OK && t.value == 0x1234U);
    t.address = 0x100000U; /* hole must fail, not wrap to RAM or fabricate FF */
    assert(bm_at_bus_cpu_access(board.bus, &t) == BM_STATUS_UNMAPPED);
    assert(board.cpu.ops.reset(board.cpu.context) == BM_STATUS_OK);
    t.address = 0x20ffeU;
    assert(bm_at_bus_cpu_access(board.bus, &t) == BM_STATUS_OK && t.value == 0x1234U);
    assert(bm_286_step(&board.cpu, &boundary) == BM_STATUS_OK);
    assert(boundary.instruction_address == 0xfffff0U);
    bm_at_bus_destroy(board.bus);
    board.cpu.ops.destroy(board.cpu.context);
    bm_pcs286_memory_destroy(board.memory);
}

int main(void)
{
    bm_host_services_t host = bm_null_host_services();
    uint8_t *image = host.allocate(host.context, BM_PCS286_FIRMWARE_BYTES);
    size_t i;
    assert(image != NULL);
    for (i = 0; i < BM_PCS286_FIRMWARE_BYTES; ++i)
        image[i] = (uint8_t)(i ^ (i >> 8U) ^ (i >> 16U));
    storage_test(image);
    composition_test(image);
    host.release(host.context, image);
    return 0;
}
