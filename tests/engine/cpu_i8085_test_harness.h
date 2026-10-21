/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_TESTS_CPU_I8085_TEST_HARNESS_H
#define BLUMACH_TESTS_CPU_I8085_TEST_HARNESS_H

#include <blumach/components/cpu_i8085.h>

#include <stddef.h>
#include <stdint.h>

#define I8085_TEST_MEMORY_SIZE 0x10000U
#define I8085_TEST_LOG_CAPACITY 64U

typedef struct i8085_test_config {
    bm_i8085_model_t model;
    bm_i8085_interrupt_ack_fn interrupt_ack;
    void *interrupt_context;
    bm_i8085_sod_fn sod;
    void *sod_context;
    bm_i8085_timing_fn timing;
    void *timing_context;
} i8085_test_config_t;

typedef struct i8085_test_access {
    bm_address_space_t space;
    bm_bus_operation_t operation;
    uint16_t address;
    uint8_t value;
} i8085_test_access_t;

/* Synthetic 64 KiB memory and 256-port I/O space. Every access is logged in
 * order and reports the configured wait states in CLK periods. */
typedef struct i8085_test_machine {
    bm_host_services_t host;
    bm_bus_t *bus;
    bm_cpu_t cpu;
    uint8_t *memory;
    uint8_t io_input[256];
    uint8_t io_output[256];
    uint32_t memory_wait_states;
    uint32_t io_wait_states;
    size_t log_count;
    i8085_test_access_t log[I8085_TEST_LOG_CAPACITY];
} i8085_test_machine_t;

void i8085_test_create(i8085_test_machine_t *machine,
                       const i8085_test_config_t *config);
void i8085_test_destroy(i8085_test_machine_t *machine);
void i8085_test_load(i8085_test_machine_t *machine, uint16_t address,
                     const uint8_t *bytes, size_t size);
bm_i8085_arch_state_t i8085_test_state(const i8085_test_machine_t *machine);
void i8085_test_set_state(i8085_test_machine_t *machine,
                          const bm_i8085_arch_state_t *state);
/* Execute one boundary and require BM_STATUS_OK. */
void i8085_test_step(i8085_test_machine_t *machine);
void i8085_test_signal(i8085_test_machine_t *machine, bm_i8085_signal_t line,
                       int asserted);
void i8085_test_clear_log(i8085_test_machine_t *machine);

#endif
