/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Draft AT board interconnect contract; no ISA emulation implemented.
 */
#ifndef BLUMACH_COMPONENTS_AT_BUS_H
#define BLUMACH_COMPONENTS_AT_BUS_H
#include <blumach/components/bus.h>
#include <blumach/engine/engine.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct bm_at_bus bm_at_bus_t;
typedef void (*bm_at_line_fn)(void *context, int asserted);
typedef enum bm_at_master {
    BM_AT_MASTER_CPU = 0,
    BM_AT_MASTER_DMA8,
    BM_AT_MASTER_DMA16,
    BM_AT_MASTER_ISA
} bm_at_master_t;
typedef struct bm_at_transfer {
    bm_at_master_t master;
    bm_bus_transaction_t bus;
    bm_clock_rate_t requester_clock;
} bm_at_transfer_t;
typedef bm_status_t (*bm_at_access_fn)(void *context, bm_at_transfer_t *transfer);
typedef struct bm_at_bus_config {
    bm_clock_rate_t cpu_clock;
    bm_clock_rate_t isa_clock;
    bm_at_access_fn memory;
    bm_at_access_fn io;
    void *decode_context;
    bm_at_line_fn hold;
    void *hold_context;
} bm_at_bus_config_t;

/* Every returned wait_states value counts EXTRA requester clocks. Conversion
 * from ISA/memory clocks belongs to this interconnect; devices must not assume
 * equal CPU/ISA clocks. Debug cycles cannot acknowledge IRQs, consume FIFO
 * entries or advance clocks. Locked CPU windows defer DMA/ISA grants.
 * CPU reset/A20, DMA pages, PIC vectors and card inventory are board-owned. */
bm_status_t bm_at_bus_create(const bm_host_services_t *host,
                             const bm_at_bus_config_t *config,
                             bm_at_bus_t **out_bus);
void bm_at_bus_destroy(bm_at_bus_t *bus);
void bm_at_bus_reset(bm_at_bus_t *bus);
bm_status_t bm_at_bus_access(bm_at_bus_t *bus, bm_at_transfer_t *transfer);
/* Adapts the 286 access callback to master CPU and the configured CPU clock. */
bm_status_t bm_at_bus_cpu_access(void *context, bm_bus_transaction_t *transaction);
/* LOCK spans transactions; individual LOCKED attributes alone cannot express
 * the interval between a read and its paired write. Board/CPU integration
 * must supply this explicit level before requesting bus arbitration. */
bm_status_t bm_at_bus_set_lock(bm_at_bus_t *bus, int asserted);
bm_status_t bm_at_bus_request(bm_at_bus_t *bus, bm_at_master_t master, int asserted);
bm_status_t bm_at_bus_hold_ack(bm_at_bus_t *bus, int asserted);

/* Initial ISA cards are fixed during construction. No hotplug promise.
 * Full resource claims are checked before publication; failure retains none.
 * Physical slot count/width must come from board evidence, never from PCS86. */
typedef struct bm_isa_resource {
    bm_address_space_t space;
    uint32_t first;
    uint32_t last;
    uint8_t data_width;        /* 8 or 16 */
} bm_isa_resource_t;
typedef struct bm_isa_card {
    const char *id;
    const bm_isa_resource_t *resources;
    size_t resource_count;
    uint16_t irq_mask;
    uint8_t dma_mask;
    bm_bus_access_fn access;
    void *context;
} bm_isa_card_t;
typedef struct bm_isa_slot {
    uint32_t id;
    uint8_t data_width;        /* documented physical connector width */
    const bm_isa_card_t *card; /* NULL = empty; borrowed for machine lifetime */
} bm_isa_slot_t;
/* Board composition uses these records to build decode/routing; the generic
 * address bus is not modified to know about ISA cards or physical slots. */
#ifdef __cplusplus
}
#endif
#endif
