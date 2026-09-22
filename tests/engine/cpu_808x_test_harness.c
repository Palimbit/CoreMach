/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "cpu_808x_test_harness.h"

#include <blumach/components/bus.h>
#include <blumach/platforms/null_host.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

void
cpu_808x_test_machine_create(cpu_808x_test_machine_t *machine,
                             const cpu_808x_test_config_t *config,
                             const uint8_t *program,
                             size_t program_size)
{
    bm_engine_config_t engine_config = { 1U, 1U, 0U };
    bm_linear_memory_config_t memory_config;
    bm_808x_config_t cpu_config;
    size_t bus_capacity = 1U;

    assert(machine != NULL);
    assert(program != NULL || program_size == 0U);
    assert(program_size <= 0x0fff0U);
    memset(machine, 0, sizeof(*machine));
    if ((config != NULL) && (config->bus_capacity != 0U))
        bus_capacity = config->bus_capacity;
    machine->host = bm_null_host_services();
    machine->image = calloc(1U, CPU_808X_TEST_IMAGE_SIZE);
    assert(machine->image != NULL);
    if (program_size != 0U)
        memcpy(machine->image + 0xf0000U, program, program_size);
    machine->image[0xffff0U] = 0xeaU;
    machine->image[0xffff1U] = 0x00U;
    machine->image[0xffff2U] = 0x00U;
    machine->image[0xffff3U] = 0x00U;
    machine->image[0xffff4U] = 0xf0U;

    assert(bm_bus_create(&machine->host, bus_capacity, &machine->bus) ==
           BM_STATUS_OK);
    memory_config = (bm_linear_memory_config_t) {
        BM_ADDRESS_MEMORY, 0U, CPU_808X_TEST_IMAGE_SIZE,
        BM_LINEAR_MEMORY_WRITABLE,
        machine->image, CPU_808X_TEST_IMAGE_SIZE
    };
    assert(bm_linear_memory_create(&machine->host, machine->bus,
                                   &memory_config, &machine->memory) ==
           BM_STATUS_OK);
    assert(bm_engine_create(&machine->host, &engine_config, &machine->engine) ==
           BM_STATUS_OK);
    cpu_config = (bm_808x_config_t) {
        .model = config != NULL ? config->model : BM_808X_NEC_V30,
        .frequency_hz = (config != NULL &&
                         config->model == BM_808X_INTEL_8088) ?
                        4772727U : 10000000U,
        .bus = machine->bus,
        .trace = config != NULL ? config->trace : NULL,
        .trace_context = config != NULL ? config->trace_context : NULL,
        .interrupt_ack = config != NULL ? config->interrupt_ack : NULL,
        .interrupt_context = config != NULL ? config->interrupt_context : NULL,
        .fpo = config != NULL ? config->fpo : NULL,
        .poll = config != NULL ? config->poll : NULL,
        .coprocessor_context =
            config != NULL ? config->coprocessor_context : NULL,
        .timing = config != NULL ? config->timing : NULL,
        .timing_context = config != NULL ? config->timing_context : NULL,
        .bus_phase = config != NULL ? config->bus_phase : NULL,
        .bus_phase_context = config != NULL ?
            config->bus_phase_context : NULL,
        .intel_queue_event = config != NULL ?
            config->intel_queue_event : NULL,
        .intel_queue_event_context = config != NULL ?
            config->intel_queue_event_context : NULL
    };
    assert(bm_808x_create(&machine->host, &cpu_config, &machine->cpu) ==
           BM_STATUS_OK);
    assert(bm_engine_add_cpu(machine->engine, &machine->cpu, NULL) ==
           BM_STATUS_OK);
    assert(bm_engine_reset(machine->engine) == BM_STATUS_OK);
}

void
cpu_808x_test_machine_destroy(cpu_808x_test_machine_t *machine)
{
    if (machine == NULL)
        return;
    bm_engine_destroy(machine->engine);
    bm_linear_memory_destroy(machine->memory);
    bm_bus_destroy(machine->bus);
    free(machine->image);
    memset(machine, 0, sizeof(*machine));
}

bm_status_t
cpu_808x_test_run(cpu_808x_test_machine_t *machine, uint64_t ticks)
{
    assert(machine != NULL);
    return bm_engine_run_for(machine->engine, ticks);
}

uint64_t
cpu_808x_test_inspect(const cpu_808x_test_machine_t *machine, const char *name)
{
    uint64_t value = UINT64_MAX;
    assert(machine != NULL);
    assert(bm_engine_inspect_cpu(machine->engine, 0U, name, &value) ==
           BM_STATUS_OK);
    return value;
}

uint8_t
cpu_808x_test_peek(const cpu_808x_test_machine_t *machine, uint64_t address)
{
    uint8_t value = 0U;
    assert(machine != NULL);
    assert(bm_linear_memory_peek(machine->memory, address, &value) ==
           BM_STATUS_OK);
    return value;
}

void
cpu_808x_test_write(cpu_808x_test_machine_t *machine,
                    uint64_t address,
                    const uint8_t *data,
                    size_t size)
{
    size_t offset;

    assert(machine != NULL);
    assert(data != NULL || size == 0U);
    assert(address <= CPU_808X_TEST_IMAGE_SIZE);
    assert(size <= CPU_808X_TEST_IMAGE_SIZE - address);
    for (offset = 0U; offset < size; ++offset) {
        bm_bus_transaction_t transaction = {
            BM_ADDRESS_MEMORY, BM_BUS_WRITE, address + offset, data[offset],
            1U, 1U, 0U, BM_ENDIAN_LITTLE, 0
        };
        assert(bm_bus_transact(machine->bus, &transaction) == BM_STATUS_OK);
    }
}

void
cpu_808x_test_poke(cpu_808x_test_machine_t *machine,
                   uint64_t address,
                   uint8_t value)
{
    cpu_808x_test_write(machine, address, &value, 1U);
}

bm_808x_arch_state_t
cpu_808x_test_get_state(const cpu_808x_test_machine_t *machine)
{
    bm_808x_arch_state_t state;
    assert(machine != NULL);
    assert(bm_808x_get_arch_state(&machine->cpu, &state) == BM_STATUS_OK);
    return state;
}

void
cpu_808x_test_set_state(cpu_808x_test_machine_t *machine,
                        const bm_808x_arch_state_t *state)
{
    assert(machine != NULL);
    assert(bm_808x_set_arch_state(&machine->cpu, state) == BM_STATUS_OK);
}

bm_status_t
cpu_808x_test_step(cpu_808x_test_machine_t *machine, bm_tick_t *consumed)
{
    assert(machine != NULL);
    return bm_808x_step(&machine->cpu, consumed);
}
