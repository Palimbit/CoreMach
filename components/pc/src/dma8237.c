/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2008-2020 Sarah Walker
 * Copyright 2016-2020 Miran Grca
 * Copyright 2017-2020 Fred N. van Kempen
 * Copyright 2026 BluMach contributors
 *
 * Derived rewrite of the inherited PC/XT 8237 path. This stage models the
 * controller's programming interface, request state and synchronous
 * peripheral DACK transfers. Host scheduling and HOLD/HLDA timing remain the
 * responsibility of the machine adapter.
 */
#include <blumach/components/dma8237.h>

#include <string.h>

typedef struct bm_dma8237_channel {
    uint16_t base_address;
    uint16_t current_address;
    uint16_t base_count;
    uint16_t current_count;
    uint8_t mode;
} bm_dma8237_channel_t;

struct bm_dma8237 {
    bm_host_services_t host;
    bm_bus_t *bus;
    uint16_t io_base;
    bm_dma8237_channel_t channel[4];
    uint8_t command;
    uint8_t software_request;
    uint8_t dreq;
    uint8_t terminal_count;
    uint8_t mask;
    uint8_t temporary;
    uint8_t high_byte;
    uint8_t page[4];
};

static uint8_t
request_bits(const bm_dma8237_t *dma)
{
    return (uint8_t) ((dma->software_request | dma->dreq) & 0x0fU);
}

static void
write_word_byte(uint16_t *value, uint8_t byte, int high_byte)
{
    if (high_byte)
        *value = (uint16_t) ((*value & 0x00ffU) | ((uint16_t) byte << 8U));
    else
        *value = (uint16_t) ((*value & 0xff00U) | byte);
}

static uint8_t
read_word_byte(uint16_t value, int high_byte)
{
    return high_byte ? (uint8_t) (value >> 8U) : (uint8_t) value;
}

static void
master_clear(bm_dma8237_t *dma)
{
    dma->command = 0;
    dma->software_request = 0;
    dma->terminal_count = 0;
    dma->mask = 0x0fU;
    dma->temporary = 0;
    dma->high_byte = 0;
}

static bm_status_t
dma_access(void *context, bm_bus_transaction_t *transaction)
{
    bm_dma8237_t *dma = context;
    uint8_t offset;
    uint8_t value;
    unsigned int channel;
    uint8_t bit;

    if ((transaction->size != 1) || (transaction->operation == BM_BUS_FETCH))
        return BM_STATUS_UNSUPPORTED;
    offset = (uint8_t) (transaction->address - dma->io_base);

    if (transaction->operation == BM_BUS_READ) {
        if (offset < 8U) {
            channel = offset >> 1U;
            value = read_word_byte((offset & 1U) ?
                dma->channel[channel].current_count :
                dma->channel[channel].current_address, dma->high_byte);
            dma->high_byte ^= 1U;
            transaction->value = value;
            return BM_STATUS_OK;
        }
        if (offset == 8U) {
            transaction->value = (uint8_t) ((request_bits(dma) << 4U) |
                                            dma->terminal_count);
            dma->terminal_count = 0;
            return BM_STATUS_OK;
        }
        if (offset == 13U) {
            transaction->value = dma->temporary;
            return BM_STATUS_OK;
        }
        return BM_STATUS_UNSUPPORTED;
    }

    value = (uint8_t) transaction->value;
    if (offset < 8U) {
        uint16_t *base;
        uint16_t *current;
        channel = offset >> 1U;
        base = (offset & 1U) ? &dma->channel[channel].base_count :
                              &dma->channel[channel].base_address;
        current = (offset & 1U) ? &dma->channel[channel].current_count :
                                 &dma->channel[channel].current_address;
        write_word_byte(base, value, dma->high_byte);
        write_word_byte(current, value, dma->high_byte);
        dma->high_byte ^= 1U;
        return BM_STATUS_OK;
    }

    channel = value & 3U;
    bit = (uint8_t) (1U << channel);
    switch (offset) {
        case 8U:
            dma->command = value;
            break;
        case 9U:
            if ((value & 4U) != 0)
                dma->software_request |= bit;
            else
                dma->software_request &= (uint8_t) ~bit;
            break;
        case 10U:
            if ((value & 4U) != 0)
                dma->mask |= bit;
            else
                dma->mask &= (uint8_t) ~bit;
            break;
        case 11U:
            dma->channel[channel].mode = value;
            break;
        case 12U:
            dma->high_byte = 0;
            break;
        case 13U:
            master_clear(dma);
            break;
        case 14U:
            dma->mask = 0;
            break;
        case 15U:
            dma->mask = value & 0x0fU;
            break;
        default:
            return BM_STATUS_UNSUPPORTED;
    }
    return BM_STATUS_OK;
}

bm_status_t
bm_dma8237_create(const bm_host_services_t *host,
                  bm_bus_t *bus,
                  const bm_dma8237_config_t *config,
                  bm_dma8237_t **out_dma)
{
    bm_dma8237_t *dma;
    bm_status_t status;
    uint32_t end;

    if ((bm_host_services_validate(host) != BM_STATUS_OK) || (bus == NULL) ||
        (config == NULL) || (out_dma == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    end = (uint32_t) config->io_base + 15U;
    if (end > UINT16_MAX)
        return BM_STATUS_INVALID_ARGUMENT;
    *out_dma = NULL;
    dma = host->allocate(host->context, sizeof(*dma));
    if (dma == NULL)
        return BM_STATUS_OUT_OF_MEMORY;
    memset(dma, 0, sizeof(*dma));
    dma->host = *host;
    dma->bus = bus;
    dma->io_base = config->io_base;
    bm_dma8237_reset(dma);
    status = bm_bus_map(bus, BM_ADDRESS_IO, config->io_base, end,
                        dma_access, dma);
    if (status != BM_STATUS_OK) {
        host->release(host->context, dma);
        return status;
    }
    *out_dma = dma;
    return BM_STATUS_OK;
}

void
bm_dma8237_destroy(bm_dma8237_t *dma)
{
    if (dma != NULL)
        dma->host.release(dma->host.context, dma);
}

void
bm_dma8237_reset(bm_dma8237_t *dma)
{
    uint8_t dreq;
    if (dma == NULL)
        return;
    dreq = dma->dreq;
    memset(dma->channel, 0, sizeof(dma->channel));
    master_clear(dma);
    dma->dreq = dreq;
}

bm_status_t
bm_dma8237_set_dreq(bm_dma8237_t *dma, unsigned int channel, int asserted)
{
    uint8_t bit;
    if ((dma == NULL) || (channel >= 4))
        return BM_STATUS_INVALID_ARGUMENT;
    bit = (uint8_t) (1U << channel);
    if (asserted)
        dma->dreq |= bit;
    else
        dma->dreq &= (uint8_t) ~bit;
    return BM_STATUS_OK;
}

bm_status_t
bm_dma8237_set_page(bm_dma8237_t *dma, unsigned int channel, uint8_t page)
{
    if ((dma == NULL) || (channel >= 4))
        return BM_STATUS_INVALID_ARGUMENT;
    dma->page[channel] = page;
    return BM_STATUS_OK;
}

bm_status_t
bm_dma8237_channel_state(const bm_dma8237_t *dma,
                         unsigned int channel,
                         bm_dma8237_channel_state_t *out_state)
{
    uint8_t bit;
    if ((dma == NULL) || (channel >= 4) || (out_state == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    bit = (uint8_t) (1U << channel);
    out_state->base_address = dma->channel[channel].base_address;
    out_state->current_address = dma->channel[channel].current_address;
    out_state->base_count = dma->channel[channel].base_count;
    out_state->current_count = dma->channel[channel].current_count;
    out_state->mode = dma->channel[channel].mode;
    out_state->page = dma->page[channel];
    out_state->current_physical_address =
        ((uint32_t) dma->page[channel] << 16U) |
        dma->channel[channel].current_address;
    out_state->masked = (dma->mask & bit) != 0;
    out_state->requested = (request_bits(dma) & bit) != 0;
    out_state->terminal_count = (dma->terminal_count & bit) != 0;
    return BM_STATUS_OK;
}

static bm_status_t
transfer_byte(bm_dma8237_t *dma,
              unsigned int channel,
              uint8_t *value,
              int device_to_memory,
              int *terminal_count)
{
    bm_dma8237_channel_t *state;
    bm_bus_transaction_t transaction;
    uint8_t bit;
    uint8_t transfer_type;
    int terminal;
    bm_status_t status;

    if ((dma == NULL) || (channel >= 4U) || (value == NULL) ||
        (terminal_count == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    *terminal_count = 0;
    bit = (uint8_t) (1U << channel);
    if (((dma->command & 0x04U) != 0U) || ((dma->mask & bit) != 0U) ||
        ((request_bits(dma) & bit) == 0U))
        return BM_STATUS_INVALID_STATE;

    state = &dma->channel[channel];
    transfer_type = state->mode & 0x0cU;
    if ((transfer_type != 0x00U) &&
        (transfer_type != (device_to_memory ? 0x04U : 0x08U)))
        return BM_STATUS_INVALID_STATE;

    if (transfer_type != 0x00U) {
        transaction = (bm_bus_transaction_t) {
            BM_ADDRESS_MEMORY,
            device_to_memory ? BM_BUS_WRITE : BM_BUS_READ,
            ((uint64_t) dma->page[channel] << 16U) | state->current_address,
            device_to_memory ? *value : 0U,
            1U,
            1U,
            0U,
            BM_ENDIAN_LITTLE,
            0
        };
        status = bm_bus_transact(dma->bus, &transaction);
        if (status != BM_STATUS_OK)
            return status;
        if (!device_to_memory)
            *value = (uint8_t) transaction.value;
    }
    dma->temporary = *value;

    if ((state->mode & 0x20U) != 0U)
        --state->current_address;
    else
        ++state->current_address;

    terminal = state->current_count == 0U;
    --state->current_count;
    if (terminal) {
        dma->terminal_count |= bit;
        dma->software_request &= (uint8_t) ~bit;
        if ((state->mode & 0x10U) != 0U) {
            state->current_address = state->base_address;
            state->current_count = state->base_count;
        }
    }
    *terminal_count = terminal;
    return BM_STATUS_OK;
}

bm_status_t
bm_dma8237_device_write(bm_dma8237_t *dma,
                        unsigned int channel,
                        uint8_t value,
                        int *terminal_count)
{
    return transfer_byte(dma, channel, &value, 1, terminal_count);
}

bm_status_t
bm_dma8237_device_read(bm_dma8237_t *dma,
                       unsigned int channel,
                       uint8_t *value,
                       int *terminal_count)
{
    return transfer_byte(dma, channel, value, 0, terminal_count);
}

uint8_t
bm_dma8237_command(const bm_dma8237_t *dma)
{
    return dma != NULL ? dma->command : 0;
}

uint8_t
bm_dma8237_mask(const bm_dma8237_t *dma)
{
    return dma != NULL ? dma->mask : 0x0fU;
}
