/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "cpu_i8085_test_harness.h"

#include <blumach/components/bus.h>
#include <blumach/platforms/null_host.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void
log_access(i8085_test_machine_t *machine, const bm_bus_transaction_t *txn)
{
    if (machine->log_count < I8085_TEST_LOG_CAPACITY) {
        machine->log[machine->log_count] = (i8085_test_access_t) {
            txn->space, txn->operation, (uint16_t) txn->address,
            (uint8_t) txn->value
        };
    }
    ++machine->log_count;
}

static bm_status_t
memory_access(void *context, bm_bus_transaction_t *txn)
{
    i8085_test_machine_t *machine = context;

    assert(txn->size == 1U);
    assert(txn->address < I8085_TEST_MEMORY_SIZE);
    if (txn->operation == BM_BUS_WRITE)
        machine->memory[txn->address] = (uint8_t) txn->value;
    else
        txn->value = machine->memory[txn->address];
    txn->wait_states = machine->memory_wait_states;
    log_access(machine, txn);
    return BM_STATUS_OK;
}

static bm_status_t
io_access(void *context, bm_bus_transaction_t *txn)
{
    i8085_test_machine_t *machine = context;

    assert(txn->size == 1U);
    assert(txn->address <= 0xffU);
    if (txn->operation == BM_BUS_WRITE)
        machine->io_output[txn->address] = (uint8_t) txn->value;
    else
        txn->value = machine->io_input[txn->address];
    txn->wait_states = machine->io_wait_states;
    log_access(machine, txn);
    return BM_STATUS_OK;
}

void
i8085_test_create(i8085_test_machine_t *machine,
                  const i8085_test_config_t *config)
{
    bm_i8085_config_t cpu_config;

    assert(machine != NULL);
    memset(machine, 0, sizeof(*machine));
    machine->host = bm_null_host_services();
    if ((config != NULL) && (config->tolerance != NULL)) {
        machine->tolerance_capability = (bm_host_capability_t) {
            BM_HOST_CAPABILITY_TOLERANCE, BM_HOST_TOLERANCE_VERSION,
            sizeof(*config->tolerance), config->tolerance
        };
        machine->host.capabilities = &machine->tolerance_capability;
        machine->host.capability_count = 1U;
    }
    machine->memory = calloc(1U, I8085_TEST_MEMORY_SIZE);
    assert(machine->memory != NULL);
    assert(bm_bus_create(&machine->host, 2U, &machine->bus) == BM_STATUS_OK);
    assert(bm_bus_map(machine->bus, BM_ADDRESS_MEMORY, 0U, 0xffffU,
                      memory_access, machine) == BM_STATUS_OK);
    assert(bm_bus_map(machine->bus, BM_ADDRESS_IO, 0U, 0xffU,
                      io_access, machine) == BM_STATUS_OK);
    cpu_config = (bm_i8085_config_t) {
        .model = config != NULL ? config->model : BM_I8085_INTEL_8085A,
        .frequency_hz = 2457600U,
        .bus = machine->bus,
        .interrupt_ack = config != NULL ? config->interrupt_ack : NULL,
        .interrupt_context = config != NULL ? config->interrupt_context : NULL,
        .sod = config != NULL ? config->sod : NULL,
        .sod_context = config != NULL ? config->sod_context : NULL,
        .timing = config != NULL ? config->timing : NULL,
        .timing_context = config != NULL ? config->timing_context : NULL
    };
    assert(bm_i8085_create(&machine->host, &cpu_config, &machine->cpu) ==
           BM_STATUS_OK);
}

void
i8085_test_destroy(i8085_test_machine_t *machine)
{
    if (machine == NULL)
        return;
    if (machine->cpu.ops.destroy != NULL)
        machine->cpu.ops.destroy(machine->cpu.context);
    bm_bus_destroy(machine->bus);
    free(machine->memory);
    memset(machine, 0, sizeof(*machine));
}

void
i8085_test_load(i8085_test_machine_t *machine, uint16_t address,
                const uint8_t *bytes, size_t size)
{
    size_t index;

    assert(machine != NULL && (bytes != NULL || size == 0U));
    for (index = 0U; index < size; ++index)
        machine->memory[(uint16_t) (address + index)] = bytes[index];
}

bm_i8085_arch_state_t
i8085_test_state(const i8085_test_machine_t *machine)
{
    bm_i8085_arch_state_t state;

    assert(bm_i8085_get_arch_state(&machine->cpu, &state) == BM_STATUS_OK);
    return state;
}

void
i8085_test_set_state(i8085_test_machine_t *machine,
                     const bm_i8085_arch_state_t *state)
{
    assert(bm_i8085_set_arch_state(&machine->cpu, state) == BM_STATUS_OK);
}

void
i8085_test_step(i8085_test_machine_t *machine)
{
    bm_tick_t consumed = 0U;

    assert(bm_i8085_step(&machine->cpu, &consumed) == BM_STATUS_OK);
    assert(consumed == 1U);
}

void
i8085_test_signal(i8085_test_machine_t *machine, bm_i8085_signal_t line,
                  int asserted)
{
    assert(machine->cpu.ops.signal(machine->cpu.context, (uint32_t) line,
                                   asserted) == BM_STATUS_OK);
}

void
i8085_test_clear_log(i8085_test_machine_t *machine)
{
    machine->log_count = 0U;
}
