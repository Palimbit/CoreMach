/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_COMPONENTS_CPU_808X_H
#define BLUMACH_COMPONENTS_CPU_808X_H

#include <stdint.h>
#include <blumach/components/bus.h>
#include <blumach/engine/cpu.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum bm_808x_model {
    BM_808X_NEC_V30 = 0
} bm_808x_model_t;

typedef struct bm_808x_trace {
    uint16_t cs;
    uint16_t ip;
    uint32_t physical_address;
    uint8_t opcode;
    uint8_t effective_opcode;
    uint8_t prefix_count;
} bm_808x_trace_t;

typedef void (*bm_808x_trace_fn)(void *context, const bm_808x_trace_t *trace);
typedef bm_status_t (*bm_808x_interrupt_ack_fn)(void *context, uint8_t *vector);

typedef struct bm_808x_config {
    bm_808x_model_t model;
    uint32_t frequency_hz;
    bm_bus_t *bus;
    bm_808x_trace_fn trace;
    void *trace_context;
    bm_808x_interrupt_ack_fn interrupt_ack;
    void *interrupt_context;
} bm_808x_config_t;

bm_status_t bm_808x_create(const bm_host_services_t *host,
                           const bm_808x_config_t *config,
                           bm_cpu_t *out_cpu);

#ifdef __cplusplus
}
#endif

#endif
