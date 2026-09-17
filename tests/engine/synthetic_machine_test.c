/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/components/bus.h>
#include <blumach/platforms/null_host.h>
#include <blumach/runtime/runtime.h>

#include "failure_injection_host.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

enum synthetic_event_kind {
    SYNTHETIC_IO_WRITE = 1,
    SYNTHETIC_TIMER = 2
};

typedef struct synthetic_event {
    int kind;
    uint8_t value;
} synthetic_event_t;

typedef struct synthetic_sink {
    synthetic_event_t events[8];
    size_t count;
    bm_input_event_t last_input;
    int received_input;
} synthetic_sink_t;

typedef struct synthetic_config {
    synthetic_sink_t *sink;
} synthetic_config_t;

typedef struct synthetic_machine {
    bm_host_services_t host;
    bm_engine_t *engine;
    bm_bus_t *bus;
    bm_cpu_id_t cpu_id;
    synthetic_sink_t *sink;
    uint8_t memory[32];
    uint8_t registers[2];
    uint8_t pc;
    int halted;
} synthetic_machine_t;

static void
test_partial_initialization_cleanup(void)
{
    size_t failure;
    bm_engine_config_t configuration = { 1, 1 };

    for (failure = 0; failure < 3; ++failure) {
        failure_injection_host_t tracker;
        bm_host_services_t host;
        bm_engine_t *engine = NULL;

        failure_injection_host_initialize(&tracker);
        failure_injection_host_fail_on(&tracker, failure);
        host = failure_injection_host_services(&tracker);
        assert(bm_engine_create(&host, &configuration, &engine) == BM_STATUS_OUT_OF_MEMORY);
        assert(engine == NULL);
        assert(tracker.outstanding_allocations == 0U);
    }
}

static bm_status_t
memory_access(void *context, bm_bus_transaction_t *transaction)
{
    synthetic_machine_t *machine = context;

    if ((transaction->size != 1) || (transaction->address >= sizeof(machine->memory)))
        return BM_STATUS_DEVICE_ERROR;
    if ((transaction->operation == BM_BUS_READ) || (transaction->operation == BM_BUS_FETCH))
        transaction->value = machine->memory[transaction->address];
    else
        machine->memory[transaction->address] = (uint8_t) transaction->value;
    return BM_STATUS_OK;
}

static bm_status_t
io_access(void *context, bm_bus_transaction_t *transaction)
{
    synthetic_machine_t *machine = context;

    if ((transaction->operation != BM_BUS_WRITE) || (transaction->size != 1) ||
        (machine->sink->count >= (sizeof(machine->sink->events) / sizeof(machine->sink->events[0]))))
        return BM_STATUS_DEVICE_ERROR;
    machine->sink->events[machine->sink->count++] =
        (synthetic_event_t) { SYNTHETIC_IO_WRITE, (uint8_t) transaction->value };
    return BM_STATUS_OK;
}

static bm_status_t
fetch_byte(synthetic_machine_t *machine, uint8_t *value)
{
    bm_bus_transaction_t transaction = {
        BM_ADDRESS_PROGRAM,
        BM_BUS_FETCH,
        machine->pc,
        0,
        1,
        1,
        0,
        BM_ENDIAN_LITTLE,
        0
    };
    bm_status_t status = bm_bus_transact(machine->bus, &transaction);
    if (status == BM_STATUS_OK) {
        *value = (uint8_t) transaction.value;
        ++machine->pc;
    }
    return status;
}

static bm_status_t
synthetic_cpu_run(void *context, bm_tick_t budget, bm_tick_t *consumed)
{
    synthetic_machine_t *machine = context;

    *consumed = 0;
    while ((*consumed < budget) && !machine->halted) {
        uint8_t opcode;
        uint8_t operand;
        bm_status_t status = fetch_byte(machine, &opcode);
        if (status != BM_STATUS_OK)
            return status;

        switch (opcode) {
            case 0x00: /* NOP */
                break;
            case 0x10: /* INC register */
                if ((fetch_byte(machine, &operand) != BM_STATUS_OK) || (operand >= 2))
                    return BM_STATUS_DEVICE_ERROR;
                ++machine->registers[operand];
                break;
            case 0x20: { /* OUT port, register */
                uint8_t port;
                bm_bus_transaction_t transaction;
                if ((fetch_byte(machine, &port) != BM_STATUS_OK) ||
                    (fetch_byte(machine, &operand) != BM_STATUS_OK) || (operand >= 2))
                    return BM_STATUS_DEVICE_ERROR;
                transaction = (bm_bus_transaction_t) {
                    BM_ADDRESS_IO,
                    BM_BUS_WRITE,
                    port,
                    machine->registers[operand],
                    1,
                    1,
                    0,
                    BM_ENDIAN_LITTLE,
                    0
                };
                if (bm_bus_transact(machine->bus, &transaction) != BM_STATUS_OK)
                    return BM_STATUS_DEVICE_ERROR;
                break;
            }
            case 0xff: /* HALT */
                machine->halted = 1;
                break;
            default:
                return BM_STATUS_DEVICE_ERROR;
        }
        ++*consumed;
    }
    return machine->halted ? BM_STATUS_IDLE : BM_STATUS_OK;
}

static void
synthetic_timer(bm_engine_t *engine, void *context)
{
    synthetic_machine_t *machine = context;
    machine->sink->events[machine->sink->count++] = (synthetic_event_t) { SYNTHETIC_TIMER, 0 };
    assert(bm_engine_signal_cpu(engine, machine->cpu_id, 1, 1) == BM_STATUS_OK);
}

static bm_status_t
synthetic_cpu_reset(void *context)
{
    synthetic_machine_t *machine = context;
    machine->registers[0] = 0;
    machine->registers[1] = 0;
    machine->pc = 0;
    machine->halted = 0;
    machine->sink->count = 0;
    return bm_engine_schedule_at(machine->engine, 6, synthetic_timer, machine);
}

static bm_status_t
synthetic_cpu_signal(void *context, uint32_t line, int asserted)
{
    synthetic_machine_t *machine = context;
    if ((line != 1) || !asserted)
        return BM_STATUS_OK;
    ++machine->registers[1];
    machine->halted = 0;
    return BM_STATUS_OK;
}

static bm_status_t
synthetic_cpu_inspect(const void *context, const char *name, uint64_t *value)
{
    const synthetic_machine_t *machine = context;
    if (strcmp(name, "r0") == 0)
        *value = machine->registers[0];
    else if (strcmp(name, "r1") == 0)
        *value = machine->registers[1];
    else if (strcmp(name, "pc") == 0)
        *value = machine->pc;
    else if (strcmp(name, "halted") == 0)
        *value = (uint64_t) machine->halted;
    else
        return BM_STATUS_INVALID_ARGUMENT;
    return BM_STATUS_OK;
}

static bm_status_t
synthetic_validate(const bm_configuration_view_t *configuration)
{
    const synthetic_config_t *config;

    if ((configuration == NULL) || (configuration->type == NULL) ||
        (strcmp(configuration->type, "test.synthetic-config") != 0) ||
        (configuration->version != 1U) ||
        (configuration->size != sizeof(synthetic_config_t)) ||
        (configuration->data == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    config = configuration->data;
    return (config->sink != NULL) ? BM_STATUS_OK : BM_STATUS_INVALID_ARGUMENT;
}

static bm_status_t
synthetic_create(bm_engine_t *engine,
                 const bm_host_services_t *host,
                 const bm_configuration_view_t *configuration,
                 void **out_machine)
{
    const synthetic_config_t *config;
    synthetic_machine_t *machine;
    bm_cpu_t cpu;
    bm_status_t status;
    static const uint8_t program[] = {
        0x10, 0x00,
        0x20, 0x7f, 0x00,
        0xff,
        0x10, 0x00,
        0x20, 0x7f, 0x00,
        0xff
    };

    *out_machine = NULL;
    if (synthetic_validate(configuration) != BM_STATUS_OK)
        return BM_STATUS_INVALID_ARGUMENT;
    config = configuration->data;
    machine = host->allocate(host->context, sizeof(*machine));
    if (machine == NULL)
        return BM_STATUS_OUT_OF_MEMORY;
    memset(machine, 0, sizeof(*machine));
    machine->host = *host;
    machine->engine = engine;
    machine->sink = config->sink;
    memcpy(machine->memory, program, sizeof(program));

    status = bm_bus_create(host, 2, &machine->bus);
    if (status == BM_STATUS_OK)
        status = bm_bus_map(machine->bus, BM_ADDRESS_PROGRAM, 0, sizeof(machine->memory) - 1,
                            memory_access, machine);
    if (status == BM_STATUS_OK)
        status = bm_bus_map(machine->bus, BM_ADDRESS_IO, 0x7f, 0x7f, io_access, machine);
    cpu = (bm_cpu_t) {
        "synthetic-4",
        machine,
        {
            synthetic_cpu_reset,
            synthetic_cpu_run,
            synthetic_cpu_signal,
            synthetic_cpu_inspect,
            NULL
        }
    };
    if (status == BM_STATUS_OK)
        status = bm_engine_add_cpu(engine, &cpu, &machine->cpu_id);
    if (status != BM_STATUS_OK) {
        bm_bus_destroy(machine->bus);
        host->release(host->context, machine);
        return status;
    }
    *out_machine = machine;
    return BM_STATUS_OK;
}

static void
synthetic_destroy(void *context)
{
    synthetic_machine_t *machine = context;
    bm_bus_destroy(machine->bus);
    machine->host.release(machine->host.context, machine);
}

static bm_status_t
synthetic_input(void *context, const bm_input_event_t *event)
{
    synthetic_machine_t *machine = context;
    if ((machine == NULL) || (event == NULL) ||
        (event->kind != BM_INPUT_KEY))
        return BM_STATUS_INVALID_ARGUMENT;
    machine->sink->last_input = *event;
    machine->sink->received_input = 1;
    return BM_STATUS_OK;
}

static bm_machine_config_t
machine_config(const synthetic_config_t *configuration)
{
    static const bm_machine_definition_t definition = {
        .id = "test.synthetic-four-instruction",
        .configuration = { "test.synthetic-config", 1U,
                           sizeof(synthetic_config_t) },
        .ops = { synthetic_validate, synthetic_create, synthetic_destroy,
                 NULL, NULL, NULL, NULL, synthetic_input, NULL, NULL },
        .engine = { 1U, 4U }
    };
    bm_machine_config_t result = {
        .definition = &definition,
        .configuration = { "test.synthetic-config", 1U,
                           sizeof(*configuration), configuration }
    };
    return result;
}

static uint64_t
inspect(bm_session_t *session, const char *name)
{
    uint64_t value = UINT64_MAX;
    assert(bm_session_inspect_cpu(session, 0, name, &value) == BM_STATUS_OK);
    return value;
}

int
main(void)
{
    bm_host_services_t host = bm_null_host_services();
    synthetic_sink_t first_sink = { 0 };
    synthetic_sink_t second_sink = { 0 };
    synthetic_config_t first_config = { &first_sink };
    synthetic_config_t second_config = { &second_sink };
    bm_machine_config_t first_machine = machine_config(&first_config);
    bm_machine_config_t second_machine = machine_config(&second_config);
    bm_session_t *first = NULL;
    bm_session_t *second = NULL;

    test_partial_initialization_cleanup();

    assert(bm_session_create(&host, &first) == BM_STATUS_OK);
    assert(bm_session_create(&host, &second) == BM_STATUS_OK);
    assert(bm_session_configure(first, &first_machine) == BM_STATUS_OK);
    assert(bm_session_configure(second, &second_machine) == BM_STATUS_OK);
    assert(bm_session_start(first) == BM_STATUS_OK);
    assert(bm_session_start(second) == BM_STATUS_OK);

    {
        bm_input_event_t event = { BM_INPUT_KEY, BM_KEY_A, 1, 0 };
        assert(bm_session_send_input(first, &event) == BM_STATUS_OK);
        assert(first_sink.received_input);
        assert(first_sink.last_input.key == BM_KEY_A);
        assert(first_sink.last_input.pressed);
        assert(!second_sink.received_input);
    }

    assert(bm_session_run_for(first, 12) == BM_STATUS_OK);
    assert(inspect(first, "r0") == 2);
    assert(inspect(first, "r1") == 1);
    assert(inspect(first, "halted") == 1);
    assert(bm_session_time(first) == 12);
    assert(first_sink.count == 3);
    assert(first_sink.events[0].kind == SYNTHETIC_IO_WRITE && first_sink.events[0].value == 1);
    assert(first_sink.events[1].kind == SYNTHETIC_TIMER);
    assert(first_sink.events[2].kind == SYNTHETIC_IO_WRITE && first_sink.events[2].value == 2);

    assert(bm_session_run_for(second, 3) == BM_STATUS_OK);
    assert(inspect(second, "r0") == 1);
    assert(inspect(second, "r1") == 0);
    assert(second_sink.count == 1);
    assert(inspect(first, "r0") == 2); /* The sessions share no mutable state. */

    assert(bm_session_pause(first) == BM_STATUS_OK);
    assert(bm_session_run_for(first, 1) == BM_STATUS_INVALID_STATE);
    assert(bm_session_resume(first) == BM_STATUS_OK);
    assert(bm_session_reset(first) == BM_STATUS_OK);
    assert(inspect(first, "r0") == 0);
    assert(bm_session_time(first) == 0);

    assert(bm_session_stop(first) == BM_STATUS_OK);
    assert(bm_session_stop(second) == BM_STATUS_OK);
    assert(bm_session_state(first) == BM_SESSION_STOPPED);
    bm_session_destroy(first);
    bm_session_destroy(second);
    return 0;
}
