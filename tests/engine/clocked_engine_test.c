/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/engine/engine.h>
#include <blumach/platforms/null_host.h>

#include <assert.h>
#include <stdint.h>
#include <string.h>

typedef struct trace {
    unsigned int values[32];
    size_t count;
} trace_t;

typedef struct synthetic_cpu {
    trace_t *trace;
    unsigned int id;
    unsigned int steps;
    unsigned int destroy_calls;
    int idle;
    int invalid_cycles;
    uint64_t cycles_per_step;
    bm_engine_t *engine;
    int schedule_event;
} synthetic_cpu_t;

typedef struct event_observation {
    trace_t *trace;
    synthetic_cpu_t *first;
    synthetic_cpu_t *second;
    unsigned int first_steps;
    unsigned int second_steps;
} event_observation_t;

static void observe_quarter_second(bm_engine_t *engine, void *context);

static void
record(trace_t *trace, unsigned int value)
{
    assert(trace->count < (sizeof(trace->values) / sizeof(trace->values[0])));
    trace->values[trace->count++] = value;
}

static bm_status_t
cpu_reset(void *context)
{
    synthetic_cpu_t *cpu = context;
    cpu->steps = 0U;
    return BM_STATUS_OK;
}

static bm_status_t
cpu_step(void *context, bm_tick_t start_ns, uint64_t *cycles)
{
    synthetic_cpu_t *cpu = context;

    if (cpu->idle) {
        *cycles = 0U;
        return BM_STATUS_IDLE;
    }
    if (cpu->invalid_cycles) {
        *cycles = 0U;
        return BM_STATUS_OK;
    }
    if (cpu->schedule_event) {
        assert(start_ns == 0U);
        assert(bm_engine_schedule_at(cpu->engine, UINT64_C(250000000),
                                     observe_quarter_second, cpu->trace) == BM_STATUS_OK);
        cpu->schedule_event = 0;
    }
    if (cpu->trace != NULL)
        record(cpu->trace, cpu->id);
    ++cpu->steps;
    *cycles = cpu->cycles_per_step != 0U ? cpu->cycles_per_step : 1U;
    return BM_STATUS_OK;
}

static bm_status_t
cpu_signal(void *context, uint32_t line, int asserted)
{
    synthetic_cpu_t *cpu = context;
    (void) line;
    if (asserted)
        cpu->idle = 0;
    return BM_STATUS_OK;
}

static void
cpu_destroy(void *context)
{
    synthetic_cpu_t *cpu = context;
    ++cpu->destroy_calls;
}

static bm_cpu_t
make_cpu(synthetic_cpu_t *context)
{
    bm_cpu_t cpu = { 0 };
    cpu.name = "clocked-synthetic";
    cpu.context = context;
    cpu.ops.reset = cpu_reset;
    cpu.ops.signal = cpu_signal;
    cpu.ops.destroy = cpu_destroy;
    return cpu;
}

static bm_engine_t *
make_engine(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_engine_config_t config = { 2U, 4U };
    bm_engine_t *engine = NULL;

    assert(bm_engine_create_clocked(&host, &config, &engine) == BM_STATUS_OK);
    assert(engine != NULL);
    return engine;
}

static void
observe_half_second(bm_engine_t *engine, void *context)
{
    event_observation_t *observation = context;

    assert(bm_engine_now(engine) == UINT64_C(500000000));
    observation->first_steps = observation->first->steps;
    observation->second_steps = observation->second->steps;
    record(observation->trace, 9U);
}

static void
observe_quarter_second(bm_engine_t *engine, void *context)
{
    trace_t *trace = context;
    assert(bm_engine_now(engine) == UINT64_C(250000000));
    record(trace, 9U);
}

static void
test_event_scheduled_during_cpu_step(void)
{
    bm_engine_t *engine = make_engine();
    trace_t trace = { 0 };
    synthetic_cpu_t context = { 0 };
    bm_cpu_t cpu;

    context.trace = &trace;
    context.id = 1U;
    context.engine = engine;
    context.schedule_event = 1;
    cpu = make_cpu(&context);
    assert(bm_engine_add_clocked_cpu(engine, &cpu, cpu_step, 2U, NULL) == BM_STATUS_OK);
    assert(bm_engine_run_for(engine, UINT64_C(500000000)) == BM_STATUS_OK);
    assert(trace.count == 2U);
    assert(trace.values[0] == 1U);
    assert(trace.values[1] == 9U);
    bm_engine_destroy(engine);
}

static void
run_two_domains(int split, trace_t *result)
{
    bm_engine_t *engine = make_engine();
    synthetic_cpu_t first = { 0 };
    synthetic_cpu_t second = { 0 };
    bm_cpu_t first_handle;
    bm_cpu_t second_handle;
    event_observation_t observation = { 0 };

    first.trace = result;
    first.id = 1U;
    second.trace = result;
    second.id = 2U;
    first_handle = make_cpu(&first);
    second_handle = make_cpu(&second);
    observation.trace = result;
    observation.first = &first;
    observation.second = &second;

    assert(bm_engine_add_cpu(engine, &first_handle, NULL) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_engine_add_clocked_cpu(engine, &first_handle, NULL, 2U, NULL) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_engine_add_clocked_cpu(engine, &first_handle, cpu_step, 0U, NULL) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_engine_add_clocked_cpu(engine, &first_handle, cpu_step, 2U, NULL) ==
           BM_STATUS_OK);
    assert(bm_engine_add_clocked_cpu(engine, &second_handle, cpu_step, 3U, NULL) ==
           BM_STATUS_OK);
    assert(bm_engine_schedule_at(engine, UINT64_C(500000000),
                                 observe_half_second, &observation) == BM_STATUS_OK);
    if (split) {
        assert(bm_engine_run_for(engine, UINT64_C(250000000)) == BM_STATUS_OK);
        assert(bm_engine_run_for(engine, UINT64_C(250000000)) == BM_STATUS_OK);
        assert(bm_engine_run_for(engine, UINT64_C(500000000)) == BM_STATUS_OK);
    } else {
        assert(bm_engine_run_for(engine, UINT64_C(1000000000)) == BM_STATUS_OK);
    }
    assert(first.steps == 2U);
    assert(second.steps == 3U);
    assert(bm_engine_now(engine) == UINT64_C(1000000000));
    /* The 3 Hz instruction crossing 0.5 s is visible before the event. This
     * is the declared instruction-boundary approximation, not cycle accuracy. */
    assert(observation.first_steps == 1U);
    assert(observation.second_steps == 2U);
    bm_engine_destroy(engine);
    assert(first.destroy_calls == 1U);
    assert(second.destroy_calls == 1U);
}

static void
wake_cpu(bm_engine_t *engine, void *context)
{
    bm_cpu_id_t *id = context;
    assert(bm_engine_now(engine) == 10U);
    assert(bm_engine_signal_cpu(engine, *id, 0U, 1) == BM_STATUS_OK);
}

static void
test_idle_wake_and_reset(void)
{
    bm_engine_t *engine = make_engine();
    trace_t trace = { 0 };
    synthetic_cpu_t context = { 0 };
    bm_cpu_t cpu;
    bm_cpu_id_t id = UINT32_MAX;

    context.trace = &trace;
    context.id = 1U;
    context.idle = 1;
    cpu = make_cpu(&context);
    assert(bm_engine_add_clocked_cpu(engine, &cpu, cpu_step, UINT64_C(1000000000),
                                     &id) == BM_STATUS_OK);
    assert(bm_engine_schedule_at(engine, 10U, wake_cpu, &id) == BM_STATUS_OK);
    assert(bm_engine_run_for(engine, 12U) == BM_STATUS_OK);
    assert(context.steps == 2U);
    assert(bm_engine_reset(engine) == BM_STATUS_OK);
    assert(bm_engine_now(engine) == 0U);
    assert(context.steps == 0U);
    bm_engine_destroy(engine);
    assert(context.destroy_calls == 1U);
}

static void
test_invalid_progress(void)
{
    bm_engine_t *engine = make_engine();
    trace_t trace = { 0 };
    synthetic_cpu_t context = { 0 };
    bm_cpu_t cpu;

    context.trace = &trace;
    context.invalid_cycles = 1;
    cpu = make_cpu(&context);
    assert(bm_engine_add_clocked_cpu(engine, &cpu, cpu_step, 1U, NULL) == BM_STATUS_OK);
    assert(bm_engine_run_for(engine, 1U) == BM_STATUS_DEVICE_ERROR);
    assert(bm_engine_now(engine) == 0U);
    bm_engine_destroy(engine);
}

static void
test_mixed_load_has_no_starvation(void)
{
    bm_engine_t *engine = make_engine();
    synthetic_cpu_t slow = { 0 };
    synthetic_cpu_t fast = { 0 };
    bm_cpu_t slow_handle = make_cpu(&slow);
    bm_cpu_t fast_handle = make_cpu(&fast);

    assert(bm_engine_add_clocked_cpu(engine, &slow_handle, cpu_step, 1U, NULL) ==
           BM_STATUS_OK);
    assert(bm_engine_add_clocked_cpu(engine, &fast_handle, cpu_step, 1000U, NULL) ==
           BM_STATUS_OK);
    assert(bm_engine_run_for(engine, UINT64_C(1000000000)) == BM_STATUS_OK);
    assert(slow.steps == 1U);
    assert(fast.steps == 1000U);
    bm_engine_destroy(engine);
}

static void
test_multi_cycle_boundaries(void)
{
    bm_engine_t *engine = make_engine();
    synthetic_cpu_t first = { 0 };
    synthetic_cpu_t second = { 0 };
    bm_cpu_t first_handle = make_cpu(&first);
    bm_cpu_t second_handle = make_cpu(&second);

    first.cycles_per_step = 3U;
    assert(bm_engine_add_clocked_cpu(engine, &first_handle, cpu_step, 10U, NULL) ==
           BM_STATUS_OK);
    assert(bm_engine_add_clocked_cpu(engine, &second_handle, cpu_step, 5U, NULL) ==
           BM_STATUS_OK);
    assert(bm_engine_run_for(engine, UINT64_C(600000000)) == BM_STATUS_OK);
    assert(first.steps == 2U);
    assert(second.steps == 3U);
    bm_engine_destroy(engine);
}

int
main(void)
{
    trace_t whole = { 0 };
    trace_t split = { 0 };

    run_two_domains(0, &whole);
    run_two_domains(1, &split);
    assert(whole.count == split.count);
    assert(memcmp(whole.values, split.values,
                  whole.count * sizeof(whole.values[0])) == 0);
    test_idle_wake_and_reset();
    test_event_scheduled_during_cpu_step();
    test_invalid_progress();
    test_mixed_load_has_no_starvation();
    test_multi_cycle_boundaries();
    return 0;
}
