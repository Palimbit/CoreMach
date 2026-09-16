/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/engine/engine.h>
#include <blumach/platforms/null_host.h>

#include <assert.h>
#include <stdint.h>
#include <string.h>

enum test_run_mode {
    TEST_RUN_NORMAL,
    TEST_RUN_IDLE,
    TEST_RUN_ZERO_PROGRESS,
    TEST_RUN_EXCESS_PROGRESS,
    TEST_RUN_ERROR
};

typedef struct test_log {
    unsigned int values[32];
    size_t count;
} test_log_t;

typedef struct test_cpu {
    unsigned int id;
    enum test_run_mode run_mode;
    bm_status_t reset_status;
    bm_status_t signal_status;
    bm_tick_t ticks;
    unsigned int reset_calls;
    unsigned int run_calls;
    unsigned int signal_calls;
    unsigned int destroy_calls;
    uint32_t last_line;
    int last_asserted;
    test_log_t *run_log;
} test_cpu_t;

typedef struct test_event {
    test_log_t *log;
    unsigned int value;
    struct test_event *followup;
} test_event_t;

static void
log_value(test_log_t *log, unsigned int value)
{
    assert(log != NULL);
    assert(log->count < (sizeof(log->values) / sizeof(log->values[0])));
    log->values[log->count++] = value;
}

static bm_status_t
test_cpu_reset(void *context)
{
    test_cpu_t *cpu = context;

    ++cpu->reset_calls;
    cpu->ticks = 0U;
    return cpu->reset_status;
}

static bm_status_t
test_cpu_run(void *context, bm_tick_t budget, bm_tick_t *consumed)
{
    test_cpu_t *cpu = context;

    assert(budget == 1U);
    assert(consumed != NULL);
    ++cpu->run_calls;
    if (cpu->run_log != NULL)
        log_value(cpu->run_log, cpu->id);

    switch (cpu->run_mode) {
        case TEST_RUN_NORMAL:
            *consumed = 1U;
            ++cpu->ticks;
            return BM_STATUS_OK;
        case TEST_RUN_IDLE:
            *consumed = 0U;
            return BM_STATUS_IDLE;
        case TEST_RUN_ZERO_PROGRESS:
            *consumed = 0U;
            return BM_STATUS_OK;
        case TEST_RUN_EXCESS_PROGRESS:
            *consumed = budget + 1U;
            return BM_STATUS_OK;
        case TEST_RUN_ERROR:
            *consumed = 0U;
            return BM_STATUS_DEVICE_ERROR;
    }
    assert(0);
    return BM_STATUS_DEVICE_ERROR;
}

static bm_status_t
test_cpu_signal(void *context, uint32_t line, int asserted)
{
    test_cpu_t *cpu = context;

    ++cpu->signal_calls;
    cpu->last_line = line;
    cpu->last_asserted = asserted;
    return cpu->signal_status;
}

static bm_status_t
test_cpu_inspect(const void *context, const char *name, uint64_t *value)
{
    const test_cpu_t *cpu = context;

    if (strcmp(name, "ticks") != 0)
        return BM_STATUS_INVALID_ARGUMENT;
    *value = cpu->ticks;
    return BM_STATUS_OK;
}

static void
test_cpu_destroy(void *context)
{
    test_cpu_t *cpu = context;
    ++cpu->destroy_calls;
}

static bm_cpu_t
make_cpu(test_cpu_t *context)
{
    bm_cpu_t cpu = {
        "scheduler-contract-cpu",
        context,
        {
            test_cpu_reset,
            test_cpu_run,
            test_cpu_signal,
            test_cpu_inspect,
            test_cpu_destroy
        }
    };
    return cpu;
}

static bm_engine_t *
make_engine(size_t max_cpus, size_t max_events)
{
    bm_host_services_t host = bm_null_host_services();
    bm_engine_config_t config = { max_cpus, max_events };
    bm_engine_t *engine = NULL;

    assert(bm_engine_create(&host, &config, &engine) == BM_STATUS_OK);
    assert(engine != NULL);
    return engine;
}

static void
record_event(bm_engine_t *engine, void *context)
{
    test_event_t *event = context;

    log_value(event->log, event->value);
    if (event->followup != NULL) {
        assert(bm_engine_schedule_at(engine, bm_engine_now(engine), record_event,
                                     event->followup) == BM_STATUS_OK);
    }
}

static void
test_engine_validation_and_cpu_ownership(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_host_services_t invalid_host = host;
    bm_engine_config_t config = { 1U, 1U };
    bm_engine_config_t invalid_config = { 0U, 1U };
    bm_engine_t *engine = NULL;
    test_cpu_t first_context = { 0 };
    test_cpu_t rejected_context = { 0 };
    bm_cpu_t first = make_cpu(&first_context);
    bm_cpu_t rejected = make_cpu(&rejected_context);
    bm_cpu_t invalid = first;
    bm_cpu_id_t id = UINT32_MAX;
    uint64_t value = UINT64_MAX;

    assert(bm_host_services_validate(NULL) == BM_STATUS_INVALID_ARGUMENT);
    invalid_host.log = NULL;
    assert(bm_host_services_validate(&invalid_host) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_engine_create(NULL, &config, &engine) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_engine_create(&host, NULL, &engine) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_engine_create(&host, &invalid_config, &engine) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_engine_create(&host, &config, NULL) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_engine_create(&host, &config, &engine) == BM_STATUS_OK);

    invalid.ops.run = NULL;
    assert(bm_engine_add_cpu(engine, &invalid, NULL) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_engine_add_cpu(engine, &first, &id) == BM_STATUS_OK);
    assert(id == 0U);
    assert(bm_engine_add_cpu(engine, &rejected, NULL) ==
           BM_STATUS_CAPACITY_EXCEEDED);
    assert(bm_engine_signal_cpu(engine, id, 7U, 1) == BM_STATUS_OK);
    assert(first_context.signal_calls == 1U);
    assert(first_context.last_line == 7U);
    assert(first_context.last_asserted == 1);
    assert(bm_engine_signal_cpu(engine, 1U, 0U, 0) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_engine_inspect_cpu(engine, id, "ticks", &value) == BM_STATUS_OK);
    assert(value == 0U);
    assert(bm_engine_inspect_cpu(engine, id, "unknown", &value) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_engine_inspect_cpu(engine, id, NULL, &value) ==
           BM_STATUS_INVALID_ARGUMENT);

    bm_engine_destroy(engine);
    assert(first_context.destroy_calls == 1U);
    assert(rejected_context.destroy_calls == 0U);
    bm_engine_destroy(NULL);
}

static void
test_event_order_boundary_and_reuse(void)
{
    bm_engine_t *engine = make_engine(1U, 4U);
    test_log_t log = { { 0U }, 0U };
    test_cpu_t cpu_context = { 0 };
    bm_cpu_t cpu = make_cpu(&cpu_context);
    test_event_t followup = { &log, 40U, NULL };
    test_event_t first = { &log, 20U, &followup };
    test_event_t second = { &log, 30U, NULL };
    test_event_t earlier = { &log, 10U, NULL };
    test_event_t immediate = { &log, 50U, NULL };

    assert(bm_engine_add_cpu(engine, &cpu, NULL) == BM_STATUS_OK);
    assert(bm_engine_schedule_at(engine, 2U, record_event, &first) ==
           BM_STATUS_OK);
    assert(bm_engine_schedule_at(engine, 2U, record_event, &second) ==
           BM_STATUS_OK);
    assert(bm_engine_schedule_at(engine, 1U, record_event, &earlier) ==
           BM_STATUS_OK);
    assert(bm_engine_run_for(engine, 2U) == BM_STATUS_OK);
    assert(bm_engine_now(engine) == 2U);
    assert(cpu_context.ticks == 2U);
    assert(log.count == 4U);
    assert(log.values[0] == 10U);
    assert(log.values[1] == 20U);
    assert(log.values[2] == 30U);
    assert(log.values[3] == 40U);

    assert(bm_engine_schedule_at(engine, 1U, record_event, &immediate) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_engine_schedule_at(engine, 2U, record_event, &immediate) ==
           BM_STATUS_OK);
    assert(bm_engine_run_for(engine, 1U) == BM_STATUS_OK);
    assert(log.count == 5U);
    assert(log.values[4] == 50U);
    assert(cpu_context.ticks == 3U);

    assert(bm_engine_schedule_at(NULL, 0U, record_event, &immediate) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_engine_schedule_at(engine, 3U, NULL, &immediate) ==
           BM_STATUS_INVALID_ARGUMENT);
    bm_engine_destroy(engine);
}

static void
test_event_capacity(void)
{
    bm_engine_t *engine = make_engine(1U, 2U);
    test_log_t log = { { 0U }, 0U };
    test_event_t first = { &log, 1U, NULL };
    test_event_t second = { &log, 2U, NULL };
    test_event_t third = { &log, 3U, NULL };

    assert(bm_engine_schedule_at(engine, 1U, record_event, &first) ==
           BM_STATUS_OK);
    assert(bm_engine_schedule_at(engine, 2U, record_event, &second) ==
           BM_STATUS_OK);
    assert(bm_engine_schedule_at(engine, 3U, record_event, &third) ==
           BM_STATUS_CAPACITY_EXCEEDED);
    assert(bm_engine_run_for(engine, 1U) == BM_STATUS_OK);
    assert(log.count == 1U);
    assert(bm_engine_schedule_at(engine, 3U, record_event, &third) ==
           BM_STATUS_OK);
    assert(bm_engine_run_for(engine, 2U) == BM_STATUS_OK);
    assert(log.count == 3U);
    assert(log.values[0] == 1U);
    assert(log.values[1] == 2U);
    assert(log.values[2] == 3U);
    bm_engine_destroy(engine);
}

static void
test_multiple_cpu_order_and_idle(void)
{
    bm_engine_t *engine = make_engine(2U, 1U);
    test_log_t log = { { 0U }, 0U };
    test_cpu_t first_context = { 0 };
    test_cpu_t second_context = { 0 };
    bm_cpu_t first;
    bm_cpu_t second;
    size_t index;

    first_context.id = 1U;
    first_context.run_log = &log;
    second_context.id = 2U;
    second_context.run_log = &log;
    first = make_cpu(&first_context);
    second = make_cpu(&second_context);
    assert(bm_engine_add_cpu(engine, &first, NULL) == BM_STATUS_OK);
    assert(bm_engine_add_cpu(engine, &second, NULL) == BM_STATUS_OK);
    assert(bm_engine_run_for(engine, 3U) == BM_STATUS_OK);
    assert(log.count == 6U);
    for (index = 0U; index < log.count; ++index)
        assert(log.values[index] == ((index % 2U) == 0U ? 1U : 2U));
    assert(first_context.ticks == 3U);
    assert(second_context.ticks == 3U);
    bm_engine_destroy(engine);

    engine = make_engine(2U, 1U);
    memset(&log, 0, sizeof(log));
    memset(&first_context, 0, sizeof(first_context));
    memset(&second_context, 0, sizeof(second_context));
    first_context.id = 1U;
    first_context.run_mode = TEST_RUN_IDLE;
    first_context.run_log = &log;
    second_context.id = 2U;
    second_context.run_log = &log;
    first = make_cpu(&first_context);
    second = make_cpu(&second_context);
    assert(bm_engine_add_cpu(engine, &first, NULL) == BM_STATUS_OK);
    assert(bm_engine_add_cpu(engine, &second, NULL) == BM_STATUS_OK);
    assert(bm_engine_run_for(engine, 3U) == BM_STATUS_OK);
    assert(first_context.run_calls == 1U);
    assert(second_context.run_calls == 3U);
    assert(second_context.ticks == 3U);
    bm_engine_destroy(engine);
}

static void
test_cpu_run_contract_errors(void)
{
    enum test_run_mode modes[] = {
        TEST_RUN_ZERO_PROGRESS,
        TEST_RUN_EXCESS_PROGRESS,
        TEST_RUN_ERROR
    };
    bm_status_t expected[] = {
        BM_STATUS_DEVICE_ERROR,
        BM_STATUS_DEVICE_ERROR,
        BM_STATUS_DEVICE_ERROR
    };
    size_t index;

    for (index = 0U; index < sizeof(modes) / sizeof(modes[0]); ++index) {
        bm_engine_t *engine = make_engine(1U, 1U);
        test_cpu_t context = { 0 };
        bm_cpu_t cpu;

        context.run_mode = modes[index];
        cpu = make_cpu(&context);
        assert(bm_engine_add_cpu(engine, &cpu, NULL) == BM_STATUS_OK);
        assert(bm_engine_run_for(engine, 1U) == expected[index]);
        assert(bm_engine_now(engine) == 0U);
        bm_engine_destroy(engine);
    }
}

static void
test_reset_and_time_overflow(void)
{
    bm_engine_t *engine = make_engine(1U, 1U);
    test_cpu_t cpu_context = { 0 };
    bm_cpu_t cpu = make_cpu(&cpu_context);
    test_log_t log = { { 0U }, 0U };
    test_event_t event = { &log, 1U, NULL };

    assert(bm_engine_add_cpu(engine, &cpu, NULL) == BM_STATUS_OK);
    assert(bm_engine_schedule_at(engine, 4U, record_event, &event) ==
           BM_STATUS_OK);
    assert(bm_engine_run_for(engine, 2U) == BM_STATUS_OK);
    assert(cpu_context.ticks == 2U);
    assert(bm_engine_reset(engine) == BM_STATUS_OK);
    assert(cpu_context.reset_calls == 1U);
    assert(cpu_context.ticks == 0U);
    assert(bm_engine_now(engine) == 0U);
    assert(bm_engine_run_for(engine, 4U) == BM_STATUS_OK);
    assert(log.count == 0U);
    bm_engine_destroy(engine);

    engine = make_engine(1U, 1U);
    assert(bm_engine_run_for(engine, UINT64_MAX) == BM_STATUS_OK);
    assert(bm_engine_now(engine) == UINT64_MAX);
    assert(bm_engine_run_for(engine, 1U) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_engine_run_for(NULL, 1U) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_engine_now(NULL) == 0U);
    bm_engine_destroy(engine);
}

int
main(void)
{
    test_engine_validation_and_cpu_ownership();
    test_event_order_boundary_and_reuse();
    test_event_capacity();
    test_multiple_cpu_order_and_idle();
    test_cpu_run_contract_errors();
    test_reset_and_time_overflow();
    return 0;
}
