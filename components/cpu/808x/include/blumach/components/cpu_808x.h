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

#define BM_808X_ARCH_STATE_VERSION 2U

typedef struct bm_808x_arch_state {
    uint32_t size;
    uint32_t version;
    bm_808x_model_t model;
    uint16_t ax;
    uint16_t cx;
    uint16_t dx;
    uint16_t bx;
    uint16_t sp;
    uint16_t bp;
    uint16_t si;
    uint16_t di;
    uint16_t es;
    uint16_t cs;
    uint16_t ss;
    uint16_t ds;
    uint16_t ip;
    uint16_t flags;
    uint8_t halted;
    /* Remaining completed instruction boundaries before a maskable interrupt
     * may be accepted. This is observable state after EI and segment-register
     * transfers, so snapshots must preserve it. */
    uint8_t interrupt_inhibit;
} bm_808x_arch_state_t;

bm_status_t bm_808x_create(const bm_host_services_t *host,
                           const bm_808x_config_t *config,
                           bm_cpu_t *out_cpu);

/* Architectural state transfer is defined only at an instruction boundary.
 * It deliberately excludes bus pins, trace bookkeeping and host callbacks,
 * but includes the architecturally observable maskable-interrupt inhibit. */
bm_status_t bm_808x_get_arch_state(const bm_cpu_t *cpu,
                                   bm_808x_arch_state_t *out_state);
bm_status_t bm_808x_set_arch_state(bm_cpu_t *cpu,
                                   const bm_808x_arch_state_t *state);

/* Execute one architectural boundary. A pending accepted interrupt consumes
 * the boundary instead of an opcode, matching bm_cpu_ops.run with budget 1. */
bm_status_t bm_808x_step(bm_cpu_t *cpu, bm_tick_t *consumed);

#ifdef __cplusplus
}
#endif

#endif
