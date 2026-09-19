/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/components/linear_memory.h>

#include <limits.h>
#include <string.h>

struct bm_linear_memory {
    bm_host_services_t host;
    uint8_t *bytes;
    uint64_t base;
    size_t size;
    bm_linear_memory_write_policy_t write_policy;
};

static bm_status_t
linear_access(void *context, bm_bus_transaction_t *transaction)
{
    bm_linear_memory_t *memory = context;
    uint64_t offset;
    uint32_t index;

    if ((transaction == NULL) || (transaction->address < memory->base) ||
        (transaction->size == 0) || (transaction->size > sizeof(transaction->value)))
        return BM_STATUS_INVALID_ARGUMENT;
    offset = transaction->address - memory->base;
    if ((offset >= memory->size) || (transaction->size > memory->size - (size_t) offset))
        return BM_STATUS_UNMAPPED;
    if (transaction->operation == BM_BUS_WRITE) {
        if (memory->write_policy == BM_LINEAR_MEMORY_WRITE_REJECT)
            return BM_STATUS_READ_ONLY;
        if (memory->write_policy == BM_LINEAR_MEMORY_WRITE_IGNORE)
            return BM_STATUS_OK;
        for (index = 0; index < transaction->size; ++index) {
            uint32_t shift = (transaction->endianness == BM_ENDIAN_LITTLE)
                                 ? index * 8U
                                 : (transaction->size - index - 1U) * 8U;
            memory->bytes[offset + index] = (uint8_t) (transaction->value >> shift);
        }
        return BM_STATUS_OK;
    }
    if ((transaction->operation != BM_BUS_READ) && (transaction->operation != BM_BUS_FETCH))
        return BM_STATUS_INVALID_ARGUMENT;
    transaction->value = 0;
    for (index = 0; index < transaction->size; ++index) {
        uint32_t shift = (transaction->endianness == BM_ENDIAN_LITTLE)
                             ? index * 8U
                             : (transaction->size - index - 1U) * 8U;
        transaction->value |= (uint64_t) memory->bytes[offset + index] << shift;
    }
    return BM_STATUS_OK;
}

bm_status_t
bm_linear_memory_create(const bm_host_services_t *host,
                        bm_bus_t *bus,
                        const bm_linear_memory_config_t *config,
                        bm_linear_memory_t **out_memory)
{
    bm_linear_memory_t *memory;
    bm_status_t status;

    if ((bm_host_services_validate(host) != BM_STATUS_OK) || (bus == NULL) ||
        (config == NULL) || (out_memory == NULL) || (config->size == 0) ||
        (config->initial_data_size > config->size) ||
        (config->write_policy < BM_LINEAR_MEMORY_WRITABLE) ||
        (config->write_policy > BM_LINEAR_MEMORY_WRITE_IGNORE) ||
        ((config->initial_data_size != 0) && (config->initial_data == NULL)) ||
        (config->base > UINT64_MAX - (config->size - 1U)))
        return BM_STATUS_INVALID_ARGUMENT;
    *out_memory = NULL;
    memory = host->allocate(host->context, sizeof(*memory));
    if (memory == NULL)
        return BM_STATUS_OUT_OF_MEMORY;
    memset(memory, 0, sizeof(*memory));
    memory->host = *host;
    memory->base = config->base;
    memory->size = config->size;
    memory->write_policy = config->write_policy;
    memory->bytes = host->allocate(host->context, config->size);
    if (memory->bytes == NULL) {
        bm_linear_memory_destroy(memory);
        return BM_STATUS_OUT_OF_MEMORY;
    }
    memset(memory->bytes, 0, config->size);
    if (config->initial_data_size != 0)
        memcpy(memory->bytes, config->initial_data, config->initial_data_size);
    status = bm_bus_map(bus, config->space, config->base,
                        config->base + config->size - 1U, linear_access, memory);
    if (status != BM_STATUS_OK) {
        bm_linear_memory_destroy(memory);
        return status;
    }
    *out_memory = memory;
    return BM_STATUS_OK;
}

void
bm_linear_memory_destroy(bm_linear_memory_t *memory)
{
    if (memory == NULL)
        return;
    if (memory->bytes != NULL)
        memory->host.release(memory->host.context, memory->bytes);
    memory->host.release(memory->host.context, memory);
}

bm_status_t
bm_linear_memory_peek(const bm_linear_memory_t *memory, uint64_t address, uint8_t *value)
{
    if ((memory == NULL) || (value == NULL) || (address < memory->base) ||
        ((address - memory->base) >= memory->size))
        return BM_STATUS_INVALID_ARGUMENT;
    *value = memory->bytes[address - memory->base];
    return BM_STATUS_OK;
}
