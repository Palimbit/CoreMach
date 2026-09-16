/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_TESTS_CPU_808X_TEST_HARNESS_H
#define BLUMACH_TESTS_CPU_808X_TEST_HARNESS_H

#include <blumach/components/cpu_808x.h>
#include <blumach/components/linear_memory.h>
#include <blumach/engine/engine.h>

#include <stddef.h>
#include <stdint.h>

#define CPU_808X_TEST_IMAGE_SIZE 0x100000U

typedef struct cpu_808x_test_config {
    size_t bus_capacity;
    bm_808x_trace_fn trace;
    void *trace_context;
    bm_808x_interrupt_ack_fn interrupt_ack;
    void *interrupt_context;
} cpu_808x_test_config_t;

typedef struct cpu_808x_test_machine {
    bm_host_services_t host;
    bm_engine_t *engine;
    bm_bus_t *bus;
    bm_linear_memory_t *memory;
    bm_cpu_t cpu;
    uint8_t *image;
} cpu_808x_test_machine_t;

void cpu_808x_test_machine_create(cpu_808x_test_machine_t *machine,
                                  const cpu_808x_test_config_t *config,
                                  const uint8_t *program,
                                  size_t program_size);
void cpu_808x_test_machine_destroy(cpu_808x_test_machine_t *machine);
bm_status_t cpu_808x_test_run(cpu_808x_test_machine_t *machine,
                              uint64_t ticks);
uint64_t cpu_808x_test_inspect(const cpu_808x_test_machine_t *machine,
                               const char *name);
uint8_t cpu_808x_test_peek(const cpu_808x_test_machine_t *machine,
                           uint64_t address);
void cpu_808x_test_write(cpu_808x_test_machine_t *machine,
                         uint64_t address,
                         const uint8_t *data,
                         size_t size);
void cpu_808x_test_poke(cpu_808x_test_machine_t *machine,
                        uint64_t address,
                        uint8_t value);

#endif
