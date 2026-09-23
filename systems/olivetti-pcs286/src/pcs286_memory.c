/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 */
#include <blumach/systems/pcs286_memory.h>
#include <string.h>

struct bm_pcs286_memory {
    bm_host_services_t host;
    uint8_t *ram;
    uint8_t *rom;
    size_t ram_bytes;
};

static int firmware_valid(const bm_pcs286_firmware_t *firmware)
{
    if (firmware == NULL || firmware->image[0].data == NULL)
        return 0;
    if (firmware->layout == BM_PCS286_FIRMWARE_COMBINED)
        return firmware->image[0].size == BM_PCS286_FIRMWARE_BYTES &&
               firmware->image[1].data == NULL && firmware->image[1].size == 0;
    if (firmware->layout == BM_PCS286_FIRMWARE_LOW_HIGH)
        return firmware->image[0].size == BM_PCS286_FIRMWARE_BYTES / 2U &&
               firmware->image[1].data != NULL &&
               firmware->image[1].size == BM_PCS286_FIRMWARE_BYTES / 2U;
    return 0;
}

bm_status_t bm_pcs286_memory_create(const bm_host_services_t *host,
                                   size_t ram_bytes,
                                   const bm_pcs286_firmware_t *firmware,
                                   bm_pcs286_memory_t **out_memory)
{
    bm_pcs286_memory_t *memory;
    size_t index;
    if (out_memory == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    *out_memory = NULL;
    if (bm_host_services_validate(host) != BM_STATUS_OK ||
        ram_bytes == 0 || ram_bytes > BM_PCS286_ONBOARD_RAM_MAX_KIB * 1024U ||
        !firmware_valid(firmware))
        return BM_STATUS_INVALID_ARGUMENT;
    memory = host->allocate(host->context, sizeof(*memory));
    if (memory == NULL)
        return BM_STATUS_OUT_OF_MEMORY;
    memset(memory, 0, sizeof(*memory));
    memory->host = *host;
    memory->ram_bytes = ram_bytes;
    memory->ram = host->allocate(host->context, ram_bytes);
    if (memory->ram == NULL) {
        bm_pcs286_memory_destroy(memory);
        return BM_STATUS_OUT_OF_MEMORY;
    }
    memory->rom = host->allocate(host->context, BM_PCS286_FIRMWARE_BYTES);
    if (memory->rom == NULL) {
        bm_pcs286_memory_destroy(memory);
        return BM_STATUS_OUT_OF_MEMORY;
    }
    memset(memory->ram, 0, ram_bytes);
    if (firmware->layout == BM_PCS286_FIRMWARE_COMBINED) {
        memcpy(memory->rom, firmware->image[0].data, BM_PCS286_FIRMWARE_BYTES);
    } else {
        for (index = 0; index < BM_PCS286_FIRMWARE_BYTES / 2U; ++index) {
            memory->rom[index * 2U] = firmware->image[0].data[index];
            memory->rom[index * 2U + 1U] = firmware->image[1].data[index];
        }
    }
    *out_memory = memory;
    return BM_STATUS_OK;
}

void bm_pcs286_memory_destroy(bm_pcs286_memory_t *memory)
{
    bm_host_services_t host;
    if (memory == NULL)
        return;
    host = memory->host;
    if (memory->rom != NULL)
        host.release(host.context, memory->rom);
    if (memory->ram != NULL)
        host.release(host.context, memory->ram);
    host.release(host.context, memory);
}

bm_status_t bm_pcs286_memory_access(bm_pcs286_memory_t *memory,
                                   bm_pcs286_memory_region_t region,
                                   uint32_t offset,
                                   bm_bus_transaction_t *transaction)
{
    uint8_t *bytes;
    size_t length;
    uint32_t index;
    uint64_t value = 0;
    if (memory == NULL || transaction == NULL ||
        (region != BM_PCS286_MEMORY_RAM && region != BM_PCS286_MEMORY_ROM) ||
        (transaction->space != BM_ADDRESS_MEMORY &&
         transaction->space != BM_ADDRESS_PROGRAM && transaction->space != BM_ADDRESS_DATA) ||
        transaction->operation < BM_BUS_READ || transaction->operation > BM_BUS_FETCH ||
        transaction->size == 0 || transaction->size > 8U ||
        (transaction->endianness != BM_ENDIAN_LITTLE && transaction->endianness != BM_ENDIAN_BIG) ||
        (transaction->attributes & ~(uint32_t)(BM_BUS_TRANSACTION_DEBUG | BM_BUS_TRANSACTION_LOCKED)))
        return BM_STATUS_INVALID_ARGUMENT;
    if ((transaction->attributes & BM_BUS_TRANSACTION_DEBUG) &&
        transaction->operation == BM_BUS_WRITE)
        return BM_STATUS_UNSUPPORTED;
    bytes = region == BM_PCS286_MEMORY_RAM ? memory->ram : memory->rom;
    length = region == BM_PCS286_MEMORY_RAM ? memory->ram_bytes : BM_PCS286_FIRMWARE_BYTES;
    if (offset >= length || transaction->size > length - offset)
        return BM_STATUS_UNMAPPED;
    if (region == BM_PCS286_MEMORY_ROM && transaction->operation == BM_BUS_WRITE)
        return BM_STATUS_READ_ONLY;
    for (index = 0; index < transaction->size; ++index) {
        uint32_t shift = transaction->endianness == BM_ENDIAN_LITTLE ?
            index * 8U : (transaction->size - index - 1U) * 8U;
        if (transaction->operation == BM_BUS_WRITE)
            bytes[offset + index] = (uint8_t)(transaction->value >> shift);
        else
            value |= (uint64_t)bytes[offset + index] << shift;
    }
    if (transaction->operation != BM_BUS_WRITE)
        transaction->value = value;
    return BM_STATUS_OK;
}
