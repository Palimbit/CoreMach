/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors */
#include "dma_coordinator.h"
#include <blumach/platforms/null_host.h>

#include <assert.h>
#include <string.h>

typedef struct fixture {
    bm_at_bus_t *bus;
    bm_at_dma_t *dma;
    bm_pcs286_dma_coordinator_t coordinator;
    uint8_t memory[0x10000], device[4];
    unsigned device_index, holds;
    int hold;
} fixture_t;

static void hold(void *context, int level)
{
    fixture_t *f = context;
    assert(f->hold != !!level);
    f->hold = !!level;
    ++f->holds;
}

static bm_status_t memory(void *context, bm_at_transfer_t *transfer)
{
    fixture_t *f = context;
    if (transfer->master != BM_AT_MASTER_DMA8 || transfer->bus.size != 1U ||
        transfer->bus.address >= sizeof(f->memory)) return BM_STATUS_INVALID_ARGUMENT;
    if (transfer->bus.operation == BM_BUS_WRITE)
        f->memory[transfer->bus.address] = (uint8_t)transfer->bus.value;
    else if (transfer->bus.operation == BM_BUS_READ)
        transfer->bus.value = f->memory[transfer->bus.address];
    else return BM_STATUS_UNSUPPORTED;
    return BM_STATUS_OK;
}

static bm_status_t dma_memory(void *context, bm_at_transfer_t *transfer)
{
    return bm_at_bus_access(((fixture_t *)context)->bus, transfer);
}

static bm_status_t device_read(void *context, uint16_t *value)
{
    fixture_t *f = context;
    assert(f->device_index < sizeof(f->device));
    *value = f->device[f->device_index++];
    return BM_STATUS_OK;
}

static void dma_write(fixture_t *f, uint16_t port, uint8_t value)
{
    bm_bus_transaction_t transaction = {
        BM_ADDRESS_IO, BM_BUS_WRITE, port, value, 1U, 1U, 0U,
        BM_ENDIAN_LITTLE, 0U
    };
    assert(bm_at_dma_io(f->dma, &transaction) == BM_STATUS_OK);
}

static void program_channel2(fixture_t *f, uint16_t address, uint16_t count)
{
    dma_write(f, 0x0cU, 0U);
    dma_write(f, 0x04U, (uint8_t)address);
    dma_write(f, 0x04U, (uint8_t)(address >> 8U));
    dma_write(f, 0x05U, (uint8_t)count);
    dma_write(f, 0x05U, (uint8_t)(count >> 8U));
    dma_write(f, 0x0bU, 0x46U); /* Single, device-to-memory, channel 2. */
    dma_write(f, 0x0aU, 0x02U);
    dma_write(f, 0xd6U, 0xc0U); /* Upper channel 4 cascade. */
    dma_write(f, 0xd4U, 0U);
    dma_write(f, 0x81U, 0U);
}

static void start(fixture_t *f)
{
    bm_host_services_t host = bm_null_host_services();
    bm_at_bus_config_t bus = {0};
    bm_at_dma_config_t dma = {0};
    bm_pcs286_dma_coordinator_config_t coordinator;
    memset(f, 0, sizeof(*f));
    bus.memory = memory; bus.io = memory; bus.decode_context = f;
    bus.cpu_clock = (bm_clock_rate_t){12000000U, 1U};
    bus.isa_clock = (bm_clock_rate_t){8000000U, 1U};
    bus.hold = hold; bus.hold_context = f;
    assert(bm_at_bus_create(&host, &bus, &f->bus) == BM_STATUS_OK);
    dma.memory = dma_memory; dma.memory_context = f;
    dma.clock = (bm_clock_rate_t){4000000U, 1U};
    dma.endpoints[2].context = f; dma.endpoints[2].read = device_read;
    assert(bm_at_dma_create(&host, &dma, &f->dma) == BM_STATUS_OK);
    coordinator = (bm_pcs286_dma_coordinator_config_t){f->bus, f->dma};
    assert(bm_pcs286_dma_coordinator_initialize(&f->coordinator, &coordinator) == BM_STATUS_OK);
}

static void stop(fixture_t *f)
{
    bm_at_dma_destroy(f->dma);
    bm_at_bus_destroy(f->bus);
}

int main(void)
{
    fixture_t f;
    bm_pcs286_dma_coordinator_state_t state;
    bm_at_bus_arbitration_t bus;
    uint64_t clocks = 99U;
    start(&f);
    f.device[0] = 0x5aU; f.device[1] = 0xa5U;
    assert(bm_pcs286_dma_coordinator_step(&f.coordinator, &clocks) == BM_STATUS_IDLE && !clocks);
    program_channel2(&f, 0x0100U, 1U);
    assert(bm_at_dma_set_dreq(f.dma, 2U, 1) == BM_STATUS_OK);
    assert(bm_pcs286_dma_coordinator_step(&f.coordinator, &clocks) == BM_STATUS_OK && !clocks);
    assert(f.hold && f.holds == 1U);
    assert(bm_pcs286_dma_coordinator_step(&f.coordinator, &clocks) == BM_STATUS_IDLE && !clocks);
    assert(bm_at_bus_hold_ack(f.bus, 1) == BM_STATUS_OK);
    assert(bm_pcs286_dma_coordinator_step(&f.coordinator, &clocks) == BM_STATUS_OK && clocks == 4U);
    assert(f.memory[0x100U] == 0x5aU && !f.hold && f.holds == 2U);

    /* A held DREQ may request the next single unit only after stale HLDA falls. */
    assert(bm_pcs286_dma_coordinator_step(&f.coordinator, &clocks) == BM_STATUS_IDLE && !clocks);
    assert(!f.hold);
    assert(bm_at_bus_hold_ack(f.bus, 0) == BM_STATUS_OK);
    assert(bm_pcs286_dma_coordinator_step(&f.coordinator, &clocks) == BM_STATUS_OK && !clocks);
    assert(f.hold && f.holds == 3U);
    assert(bm_at_bus_hold_ack(f.bus, 1) == BM_STATUS_OK);
    assert(bm_pcs286_dma_coordinator_step(&f.coordinator, &clocks) == BM_STATUS_OK && clocks == 4U);
    assert(f.memory[0x101U] == 0xa5U && !f.hold && f.holds == 4U);
    assert(bm_at_dma_set_dreq(f.dma, 2U, 0) == BM_STATUS_OK);
    assert(bm_at_bus_hold_ack(f.bus, 0) == BM_STATUS_OK);
    assert(bm_pcs286_dma_coordinator_step(&f.coordinator, &clocks) == BM_STATUS_IDLE && !clocks);
    assert(bm_pcs286_dma_coordinator_state(&f.coordinator, &state) == BM_STATUS_OK);
    assert(state.master == BM_AT_MASTER_CPU && !state.requested);
    assert(state.completed_units == 2U && state.completed_clocks == 8U);
    assert(state.failure == BM_STATUS_OK);
    assert(bm_at_bus_arbitration(f.bus, &bus) == BM_STATUS_OK);
    assert(!bus.requested && !bus.hold && !bus.hlda);
    stop(&f);
    return 0;
}
