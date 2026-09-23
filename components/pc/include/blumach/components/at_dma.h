/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Draft AT dual-8237/page-register contract.
 */
#ifndef BLUMACH_COMPONENTS_AT_DMA_H
#define BLUMACH_COMPONENTS_AT_DMA_H
#include <blumach/components/at_bus.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct bm_at_dma bm_at_dma_t;
/* value carries one device data unit: byte on 0-3, word on 5-7.
 * channel 4 is cascade, never a usable transfer channel. */
typedef bm_status_t (*bm_at_dma_read_fn)(void *context, uint16_t *value);
typedef bm_status_t (*bm_at_dma_write_fn)(void *context, uint16_t value);
typedef struct bm_at_dma_endpoint {
    void *context;
    bm_at_dma_read_fn read;
    bm_at_dma_write_fn write;
    bm_at_line_fn dack;
    bm_at_line_fn terminal_count;
} bm_at_dma_endpoint_t;
typedef struct bm_at_dma_config {
    bm_at_access_fn memory;
    void *memory_context;
    bm_at_line_fn bus_request;
    void *bus_context;
    bm_clock_rate_t clock;
    bm_at_dma_endpoint_t endpoints[8];
} bm_at_dma_config_t;
typedef struct bm_at_dma_channel_state {
    uint16_t base_address, current_address, base_count, current_count;
    uint8_t page, mode;
    int masked, requested, terminal_count;
} bm_at_dma_channel_state_t;
bm_status_t bm_at_dma_create(const bm_host_services_t *host,
                             const bm_at_dma_config_t *config,
                             bm_at_dma_t **out_dma);
void bm_at_dma_destroy(bm_at_dma_t *dma);
void bm_at_dma_reset(bm_at_dma_t *dma);
bm_status_t bm_at_dma_io(void *context, bm_bus_transaction_t *transaction);
bm_status_t bm_at_dma_set_dreq(bm_at_dma_t *dma, unsigned int channel, int level);
bm_status_t bm_at_dma_set_bus_grant(bm_at_dma_t *dma, int level);
/* Only execute a transfer after arbitration grants the bus. Native DMA clocks
 * include memory waits returned in this domain. IDLE returns zero clocks when
 * no transfer is eligible. Terminal-count pulses follow the final complete
 * transfer, never a partial sector/word. 16-bit page/address/count semantics,
 * controller cascade and masks require their own conformance tests. */
bm_status_t bm_at_dma_service(bm_at_dma_t *dma, uint64_t *consumed_cycles);
bm_status_t bm_at_dma_channel_state(const bm_at_dma_t *dma, unsigned int channel,
                                     bm_at_dma_channel_state_t *out_state);
#ifdef __cplusplus
}
#endif
#endif
