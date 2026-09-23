/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Programming and idle gate only; no fictitious transfer/timing implementation.
 */
#include <blumach/components/at_dma.h>
#include <blumach/platforms/null_host.h>
#include <assert.h>

static bm_status_t forbidden_memory(void *context, bm_at_transfer_t *transfer)
{
    (void) context;
    (void) transfer;
    assert(!"DMA accessed memory without an eligible granted request");
    return BM_STATUS_INVALID_STATE;
}
static void write_port(bm_at_dma_t *dma, uint16_t port, uint8_t value)
{
    bm_bus_transaction_t t = {0};
    t.space = BM_ADDRESS_IO;
    t.operation = BM_BUS_WRITE;
    t.address = port;
    t.size = 1U;
    t.value = value;
    assert(bm_at_dma_io(dma, &t) == BM_STATUS_OK);
}
int main(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_at_dma_config_t c = {0};
    bm_at_dma_t *dma = NULL;
    bm_at_dma_channel_state_t s = {0};
    uint64_t cycles = 99U;
    c.memory = forbidden_memory;
    c.clock = (bm_clock_rate_t){4000000U, 1U}; /* synthetic */
    assert(bm_at_dma_create(&host, &c, &dma) == BM_STATUS_OK);
    bm_at_dma_reset(dma);
    assert(bm_at_dma_service(dma, &cycles) == BM_STATUS_IDLE && cycles == 0U);
    assert(bm_at_dma_channel_state(dma, 5U, &s) == BM_STATUS_OK && s.masked);
    write_port(dma, 0xd8U, 0U); /* upper-controller byte flip-flop */
    write_port(dma, 0xc4U, 0x34U); /* channel 5 word-address register */
    write_port(dma, 0xc4U, 0x12U);
    write_port(dma, 0xc6U, 0x01U); /* count=1 means two units, not bytes */
    write_port(dma, 0xc6U, 0x00U);
    assert(bm_at_dma_channel_state(dma, 5U, &s) == BM_STATUS_OK);
    assert(s.base_address == 0x1234U && s.current_address == 0x1234U);
    assert(s.base_count == 1U && s.current_count == 1U);
    cycles = 99U;
    assert(bm_at_dma_service(dma, &cycles) == BM_STATUS_IDLE && cycles == 0U);
    bm_at_dma_destroy(dma);
    bm_at_dma_destroy(NULL);
    return 0;
}
