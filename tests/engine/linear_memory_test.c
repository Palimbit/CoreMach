/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/components/bus.h>
#include <blumach/components/linear_memory.h>
#include <blumach/platforms/null_host.h>

#include <assert.h>
#include <stdint.h>

static bm_status_t
transact(bm_bus_t *bus,
         bm_bus_operation_t operation,
         uint64_t address,
         uint32_t size,
         bm_endianness_t endianness,
         uint64_t *value)
{
    bm_bus_transaction_t transaction = {
        BM_ADDRESS_MEMORY, operation, address, *value, size, size, 0, endianness, 0
    };
    bm_status_t status = bm_bus_transact(bus, &transaction);
    *value = transaction.value;
    return status;
}

int
main(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_bus_t *bus = NULL;
    bm_linear_memory_t *ram = NULL;
    bm_linear_memory_t *rom = NULL;
    const uint8_t initial_rom[] = { 0x12, 0x34, 0x56, 0x78 };
    bm_linear_memory_config_t ram_config = {
        BM_ADDRESS_MEMORY, 0x1000, 16, 0, NULL, 0
    };
    bm_linear_memory_config_t rom_config = {
        BM_ADDRESS_MEMORY, 0x2000, sizeof(initial_rom), 1,
        initial_rom, sizeof(initial_rom)
    };
    uint64_t value = 0xa1b2;
    uint8_t byte = 0;

    assert(bm_bus_create(&host, 2, &bus) == BM_STATUS_OK);
    assert(bm_linear_memory_create(&host, bus, &ram_config, &ram) == BM_STATUS_OK);
    assert(bm_linear_memory_create(&host, bus, &rom_config, &rom) == BM_STATUS_OK);

    assert(transact(bus, BM_BUS_WRITE, 0x1002, 2, BM_ENDIAN_LITTLE, &value) == BM_STATUS_OK);
    value = 0;
    assert(transact(bus, BM_BUS_READ, 0x1002, 2, BM_ENDIAN_LITTLE, &value) == BM_STATUS_OK);
    assert(value == 0xa1b2);
    assert(bm_linear_memory_peek(ram, 0x1002, &byte) == BM_STATUS_OK && byte == 0xb2);

    value = 0;
    assert(transact(bus, BM_BUS_FETCH, 0x2000, 4, BM_ENDIAN_BIG, &value) == BM_STATUS_OK);
    assert(value == 0x12345678);
    value = 0xff;
    assert(transact(bus, BM_BUS_WRITE, 0x2000, 1, BM_ENDIAN_LITTLE, &value) == BM_STATUS_READ_ONLY);
    value = 0;
    assert(transact(bus, BM_BUS_READ, 0x3000, 1, BM_ENDIAN_LITTLE, &value) == BM_STATUS_UNMAPPED);

    bm_linear_memory_destroy(rom);
    bm_linear_memory_destroy(ram);
    bm_bus_destroy(bus);
    return 0;
}
