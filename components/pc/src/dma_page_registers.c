/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2008-2020 Sarah Walker
 * Copyright 2016-2020 Miran Grca
 * Copyright 2017-2020 Fred N. van Kempen
 * Copyright 2026 BluMach contributors
 *
 * Derived rewrite of the inherited PC/XT DMA page-register block. The page
 * latches are external to the 8237 and explicitly drive its four upper-address
 * inputs through bm_dma8237_set_page().
 */
#include <blumach/components/dma_page_registers.h>

#include <string.h>

struct bm_dma_page_registers {
    bm_host_services_t host;
    bm_dma8237_t *dma;
    uint16_t io_base;
    uint8_t page_mask;
    uint8_t latch[8];
};

static const int8_t channel_for_offset[8] = {
    -1, 2, 3, 1, -1, -1, -1, 0
};

static bm_status_t
page_access(void *context, bm_bus_transaction_t *transaction)
{
    bm_dma_page_registers_t *registers = context;
    unsigned int offset;
    int channel;

    if ((transaction->size != 1) || (transaction->operation == BM_BUS_FETCH))
        return BM_STATUS_UNSUPPORTED;
    offset = (unsigned int) (transaction->address - registers->io_base);
    if (transaction->operation == BM_BUS_READ) {
        transaction->value = registers->latch[offset];
        return BM_STATUS_OK;
    }

    registers->latch[offset] =
        (uint8_t) transaction->value & registers->page_mask;
    channel = channel_for_offset[offset];
    if (channel >= 0)
        return bm_dma8237_set_page(registers->dma, (unsigned int) channel,
                                   registers->latch[offset]);
    return BM_STATUS_OK;
}

bm_status_t
bm_dma_page_registers_create(const bm_host_services_t *host,
                             bm_bus_t *bus,
                             const bm_dma_page_registers_config_t *config,
                             bm_dma_page_registers_t **out_registers)
{
    bm_dma_page_registers_t *registers;
    bm_status_t status;
    uint32_t end;

    if ((bm_host_services_validate(host) != BM_STATUS_OK) || (bus == NULL) ||
        (config == NULL) || (config->dma == NULL) || (out_registers == NULL) ||
        (config->page_mask == 0))
        return BM_STATUS_INVALID_ARGUMENT;
    end = (uint32_t) config->io_base + 7U;
    if (end > UINT16_MAX)
        return BM_STATUS_INVALID_ARGUMENT;
    *out_registers = NULL;
    registers = host->allocate(host->context, sizeof(*registers));
    if (registers == NULL)
        return BM_STATUS_OUT_OF_MEMORY;
    memset(registers, 0, sizeof(*registers));
    registers->host = *host;
    registers->dma = config->dma;
    registers->io_base = config->io_base;
    registers->page_mask = config->page_mask;
    bm_dma_page_registers_reset(registers);
    status = bm_bus_map(bus, BM_ADDRESS_IO, config->io_base, end,
                        page_access, registers);
    if (status != BM_STATUS_OK) {
        host->release(host->context, registers);
        return status;
    }
    *out_registers = registers;
    return BM_STATUS_OK;
}

void
bm_dma_page_registers_destroy(bm_dma_page_registers_t *registers)
{
    if (registers != NULL)
        registers->host.release(registers->host.context, registers);
}

void
bm_dma_page_registers_reset(bm_dma_page_registers_t *registers)
{
    unsigned int channel;
    if (registers == NULL)
        return;
    memset(registers->latch, 0, sizeof(registers->latch));
    for (channel = 0; channel < 4; ++channel)
        (void) bm_dma8237_set_page(registers->dma, channel, 0);
}
