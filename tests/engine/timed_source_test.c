/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/engine/engine.h>
#include <blumach/platforms/null_host.h>
#include "failure_injection_host.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

typedef struct trace {
    unsigned int values[32];
    size_t count;
} trace_t;

typedef struct periodic_source {
    trace_t *trace;
    bm_time_point_t observed[4];
    size_t count;
    size_t stop_after;
    unsigned int trace_value;
} periodic_source_t;

typedef struct ordered_source {
    trace_t *trace;
    unsigned int value;
    int schedule_followup;
} ordered_source_t;

typedef struct sleeping_cpu {
    int idle;
    unsigned int completed_steps;
} sleeping_cpu_t;

typedef struct wake_source {
    bm_cpu_id_t cpu_id;
} wake_source_t;

typedef struct controlled_source {
    bm_timed_source_id_t id;
    bm_status_t control_status;
    bm_status_t cursor_status;
    bm_time_point_t fired_at;
    size_t fire_count;
    uint64_t fired_cycle_count;
    uint64_t delay_cycles;
} controlled_source_t;

typedef struct arming_cpu {
    bm_engine_t *engine;
    bm_timed_source_id_t source_id;
    size_t step_count;
    bm_status_t arm_status;
    bm_status_t cursor_status;
    uint64_t observed_source_cycles;
} arming_cpu_t;

static void
must_not_run(bm_engine_t *engine, void *context)
{
    (void) engine;
    (void) context;
    assert(0);
}

static const bm_clock_rate_t rate_zero = { 0U, 1U };
static const bm_clock_rate_t rate_1_hz = { 1U, 1U };
static const bm_clock_rate_t rate_2_hz = { 2U, 1U };
static const bm_clock_rate_t rate_3_hz = { 3U, 1U };
static const bm_clock_rate_t rate_1_ghz = { UINT64_C(1000000000), 1U };

static void
record(trace_t *trace, unsigned int value)
{
    assert(trace->count < (sizeof(trace->values) / sizeof(trace->values[0])));
    trace->values[trace->count++] = value;
}

static bm_engine_t *
make_clocked_engine(size_t max_sources)
{
    bm_host_services_t host = bm_null_host_services();
    bm_engine_config_t config = { 1U, 8U, max_sources };
    bm_engine_t *engine = NULL;

    assert(bm_engine_create_clocked(&host, &config, &engine) == BM_STATUS_OK);
    assert(engine != NULL);
    return engine;
}

static bm_status_t
fire_periodic(bm_engine_t *engine, void *context,
              const bm_time_point_t *when, uint64_t *cycles_until_next)
{
    periodic_source_t *source = context;
    bm_time_point_t now;

    assert(source->count < (sizeof(source->observed) /
                            sizeof(source->observed[0])));
    assert(bm_engine_now_exact(engine, &now) == BM_STATUS_OK);
    assert(memcmp(&now, when, sizeof(now)) == 0);
    source->observed[source->count++] = *when;
    if (source->trace != NULL)
        record(source->trace, source->trace_value);
    if (source->count == source->stop_after) {
        *cycles_until_next = 0U;
        return BM_STATUS_IDLE;
    }
    *cycles_until_next = 1U;
    return BM_STATUS_OK;
}

static bm_status_t
fire_once(bm_engine_t *engine, void *context,
          const bm_time_point_t *when, uint64_t *cycles_until_next)
{
    controlled_source_t *source = context;

    source->cursor_status = bm_engine_timed_source_cycle_count(
        engine, source->id, &source->fired_cycle_count);
    source->fired_at = *when;
    ++source->fire_count;
    *cycles_until_next = 0U;
    return BM_STATUS_IDLE;
}

static void
arm_source_after_event(bm_engine_t *engine, void *context)
{
    controlled_source_t *source = context;

    source->control_status = bm_engine_arm_timed_source(
        engine, source->id, source->delay_cycles);
}

static void
disarm_source_after_event(bm_engine_t *engine, void *context)
{
    controlled_source_t *source = context;

    source->control_status = bm_engine_disarm_timed_source(engine,
                                                           source->id);
}

static void
test_initially_disarmed_source_can_be_armed_exactly(void)
{
    bm_engine_t *engine = make_clocked_engine(1U);
    controlled_source_t source = { 0 };

    source.delay_cycles = 1U;
    assert(bm_engine_add_timed_source(engine, fire_once, &source,
                                      &rate_3_hz, 0U, &source.id) ==
           BM_STATUS_OK);
    assert(bm_engine_schedule_at(engine, UINT64_C(500000000),
                                 arm_source_after_event, &source) ==
           BM_STATUS_OK);
    assert(bm_engine_run_for(engine, UINT64_C(666666666)) == BM_STATUS_OK);
    assert(source.control_status == BM_STATUS_OK);
    assert(source.fire_count == 0U);
    assert(bm_engine_run_for(engine, 1U) == BM_STATUS_OK);
    assert(source.fire_count == 1U);
    assert(source.fired_at.nanoseconds == UINT64_C(666666666));
    assert(source.fired_at.subnanosecond_numerator == 2U);
    assert(source.fired_at.subnanosecond_denominator == 3U);
    assert(source.cursor_status == BM_STATUS_OK);
    assert(source.fired_cycle_count == 2U);
    bm_engine_destroy(engine);
}

static void
test_reprogram_disarm_and_reset_restore_registration(void)
{
    bm_engine_t *engine = make_clocked_engine(1U);
    controlled_source_t source = { 0 };

    source.delay_cycles = 2U;
    assert(bm_engine_add_timed_source(engine, fire_once, &source,
                                      &rate_2_hz, 1U, &source.id) ==
           BM_STATUS_OK);
    assert(bm_engine_schedule_at(engine, UINT64_C(250000000),
                                 arm_source_after_event, &source) ==
           BM_STATUS_OK);
    assert(bm_engine_run_for(engine, UINT64_C(500000000)) == BM_STATUS_OK);
    assert(source.fire_count == 0U);
    assert(bm_engine_run_for(engine, UINT64_C(500000000)) == BM_STATUS_OK);
    assert(source.fire_count == 1U);
    assert(source.fired_at.nanoseconds == UINT64_C(1000000000));

    source.fire_count = 0U;
    assert(bm_engine_reset(engine) == BM_STATUS_OK);
    assert(bm_engine_schedule_at(engine, UINT64_C(250000000),
                                 disarm_source_after_event, &source) ==
           BM_STATUS_OK);
    assert(bm_engine_run_for(engine, UINT64_C(1000000000)) == BM_STATUS_OK);
    assert(source.control_status == BM_STATUS_OK);
    assert(source.fire_count == 0U);

    assert(bm_engine_reset(engine) == BM_STATUS_OK);
    assert(bm_engine_run_for(engine, UINT64_C(500000000)) == BM_STATUS_OK);
    assert(source.fire_count == 1U);
    bm_engine_destroy(engine);
}

static void
test_same_boundary_event_can_cancel_source(void)
{
    bm_engine_t *engine = make_clocked_engine(1U);
    controlled_source_t source = { 0 };

    assert(bm_engine_add_timed_source(engine, fire_once, &source,
                                      &rate_2_hz, 1U, &source.id) ==
           BM_STATUS_OK);
    assert(bm_engine_schedule_at(engine, UINT64_C(500000000),
                                 disarm_source_after_event, &source) ==
           BM_STATUS_OK);
    assert(bm_engine_run_for(engine, UINT64_C(500000000)) == BM_STATUS_OK);
    assert(source.control_status == BM_STATUS_OK);
    assert(source.fire_count == 0U);
    bm_engine_destroy(engine);
}

static void
test_exact_fractional_periods_and_reset(void)
{
    bm_engine_t *engine = make_clocked_engine(1U);
    bm_engine_t *whole_engine;
    periodic_source_t source = { 0 };
    periodic_source_t whole_source = { 0 };
    bm_time_point_t now;
    bm_timed_source_id_t id = UINT32_MAX;

    source.stop_after = 3U;
    assert(bm_engine_add_timed_source(engine, fire_periodic, &source,
                                      &rate_3_hz, 1U, &id) == BM_STATUS_OK);
    assert(id == 0U);
    assert(bm_engine_run_for(engine, UINT64_C(500000000)) == BM_STATUS_OK);
    assert(source.count == 1U);
    assert(bm_engine_run_for(engine, UINT64_C(500000000)) == BM_STATUS_OK);
    assert(source.count == 3U);
    assert(source.observed[0].nanoseconds == UINT64_C(333333333));
    assert(source.observed[0].subnanosecond_numerator == 1U);
    assert(source.observed[0].subnanosecond_denominator == 3U);
    assert(source.observed[1].nanoseconds == UINT64_C(666666666));
    assert(source.observed[1].subnanosecond_numerator == 2U);
    assert(source.observed[1].subnanosecond_denominator == 3U);
    assert(source.observed[2].nanoseconds == UINT64_C(1000000000));
    assert(source.observed[2].subnanosecond_numerator == 0U);
    assert(source.observed[2].subnanosecond_denominator == 1U);
    assert(bm_engine_now_exact(engine, &now) == BM_STATUS_OK);
    assert(now.nanoseconds == UINT64_C(1000000000));
    assert(now.subnanosecond_numerator == 0U);

    whole_engine = make_clocked_engine(1U);
    whole_source.stop_after = 3U;
    assert(bm_engine_add_timed_source(whole_engine, fire_periodic,
                                      &whole_source, &rate_3_hz, 1U,
                                      NULL) == BM_STATUS_OK);
    assert(bm_engine_run_for(whole_engine, UINT64_C(1000000000)) ==
           BM_STATUS_OK);
    assert(whole_source.count == source.count);
    assert(memcmp(whole_source.observed, source.observed,
                  source.count * sizeof(source.observed[0])) == 0);
    bm_engine_destroy(whole_engine);

    source.count = 0U;
    assert(bm_engine_reset(engine) == BM_STATUS_OK);
    assert(bm_engine_run_for(engine, UINT64_C(333333334)) == BM_STATUS_OK);
    assert(source.count == 1U);
    bm_engine_destroy(engine);
}

static void
record_followup(bm_engine_t *engine, void *context)
{
    trace_t *trace = context;

    assert(bm_engine_now(engine) == UINT64_C(500000000));
    record(trace, 4U);
}

static void
record_initial_event(bm_engine_t *engine, void *context)
{
    trace_t *trace = context;

    assert(bm_engine_now(engine) == UINT64_C(500000000));
    record(trace, 1U);
}

static bm_status_t
fire_ordered(bm_engine_t *engine, void *context,
             const bm_time_point_t *when, uint64_t *cycles_until_next)
{
    ordered_source_t *source = context;

    assert(when->nanoseconds == UINT64_C(500000000));
    assert(when->subnanosecond_numerator == 0U);
    record(source->trace, source->value);
    if (source->schedule_followup)
        assert(bm_engine_schedule_at(engine, when->nanoseconds,
                                     record_followup, source->trace) == BM_STATUS_OK);
    *cycles_until_next = 0U;
    return BM_STATUS_IDLE;
}

static void
test_same_time_order_is_stable(void)
{
    bm_engine_t *engine = make_clocked_engine(2U);
    trace_t trace = { 0 };
    ordered_source_t first = { &trace, 2U, 1 };
    ordered_source_t second = { &trace, 3U, 0 };

    assert(bm_engine_schedule_at(engine, UINT64_C(500000000),
                                 record_initial_event, &trace) == BM_STATUS_OK);
    assert(bm_engine_add_timed_source(engine, fire_ordered, &first,
                                      &rate_2_hz, 1U, NULL) == BM_STATUS_OK);
    assert(bm_engine_add_timed_source(engine, fire_ordered, &second,
                                      &rate_2_hz, 1U, NULL) == BM_STATUS_OK);
    assert(bm_engine_run_for(engine, UINT64_C(500000000)) == BM_STATUS_OK);
    assert(trace.count == 4U);
    assert(trace.values[0] == 1U);
    assert(trace.values[1] == 2U);
    assert(trace.values[2] == 3U);
    assert(trace.values[3] == 4U);
    bm_engine_destroy(engine);
}

static bm_status_t
sleeping_cpu_reset(void *context)
{
    sleeping_cpu_t *cpu = context;

    cpu->idle = 1;
    cpu->completed_steps = 0U;
    return BM_STATUS_OK;
}

static bm_status_t
sleeping_cpu_step(void *context, bm_tick_t start_ns, uint64_t *cycles)
{
    sleeping_cpu_t *cpu = context;
    (void) start_ns;

    if (cpu->idle) {
        *cycles = 0U;
        return BM_STATUS_IDLE;
    }
    ++cpu->completed_steps;
    *cycles = 1U;
    return BM_STATUS_OK;
}

static bm_status_t
sleeping_cpu_signal(void *context, uint32_t line, int asserted)
{
    sleeping_cpu_t *cpu = context;
    (void) line;

    if (asserted)
        cpu->idle = 0;
    return BM_STATUS_OK;
}

static bm_status_t
arming_cpu_reset(void *context)
{
    arming_cpu_t *cpu = context;

    cpu->step_count = 0U;
    cpu->arm_status = BM_STATUS_INVALID_STATE;
    cpu->cursor_status = BM_STATUS_INVALID_STATE;
    cpu->observed_source_cycles = UINT64_MAX;
    return BM_STATUS_OK;
}

static bm_status_t
arming_cpu_step(void *context, bm_tick_t start_ns, uint64_t *cycles)
{
    arming_cpu_t *cpu = context;

    ++cpu->step_count;
    if (cpu->step_count == 2U) {
        /* The public callback only exposes the integer floor, but the engine
         * must arm from this CPU domain's exact fractional boundary. */
        assert(start_ns == UINT64_C(333333333));
        cpu->cursor_status = bm_engine_timed_source_cycle_count(
            cpu->engine, cpu->source_id, &cpu->observed_source_cycles);
        cpu->arm_status = bm_engine_arm_timed_source(cpu->engine,
                                                     cpu->source_id, 1U);
    }
    *cycles = 1U;
    return BM_STATUS_OK;
}

static bm_status_t
arming_cpu_signal(void *context, uint32_t line, int asserted)
{
    (void) context;
    (void) line;
    (void) asserted;
    return BM_STATUS_OK;
}

static void
test_cpu_control_uses_exact_instruction_start_boundary(void)
{
    bm_engine_t *engine = make_clocked_engine(1U);
    controlled_source_t source = { 0 };
    arming_cpu_t cpu_context = { engine, UINT32_MAX, 0U,
                                 BM_STATUS_INVALID_STATE,
                                 BM_STATUS_INVALID_STATE, UINT64_MAX };
    bm_cpu_t cpu = { 0 };

    assert(bm_engine_add_timed_source(engine, fire_once, &source,
                                      &rate_3_hz, 0U, &source.id) ==
           BM_STATUS_OK);
    cpu_context.source_id = source.id;
    cpu.name = "arming-synthetic";
    cpu.context = &cpu_context;
    cpu.ops.reset = arming_cpu_reset;
    cpu.ops.signal = arming_cpu_signal;
    assert(bm_engine_add_clocked_cpu(engine, &cpu, arming_cpu_step,
                                     &rate_3_hz, NULL) == BM_STATUS_OK);
    assert(bm_engine_run_for(engine, UINT64_C(666666667)) == BM_STATUS_OK);
    assert(cpu_context.arm_status == BM_STATUS_OK);
    assert(cpu_context.cursor_status == BM_STATUS_OK);
    assert(cpu_context.observed_source_cycles == 1U);
    assert(source.fire_count == 1U);
    assert(source.fired_at.nanoseconds == UINT64_C(666666666));
    assert(source.fired_at.subnanosecond_numerator == 2U);
    assert(source.fired_at.subnanosecond_denominator == 3U);
    bm_engine_destroy(engine);
}

static void
test_source_cycle_cursor_tracks_domain_while_disarmed(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_engine_config_t legacy_config = { 1U, 1U, 1U };
    bm_engine_t *legacy = NULL;
    bm_engine_t *engine = make_clocked_engine(1U);
    controlled_source_t source = { 0 };
    uint64_t cycles = UINT64_MAX;

    assert(bm_engine_create(&host, &legacy_config, &legacy) == BM_STATUS_OK);
    assert(bm_engine_timed_source_cycle_count(legacy, 0U, &cycles) ==
           BM_STATUS_INVALID_ARGUMENT);
    bm_engine_destroy(legacy);

    assert(bm_engine_add_timed_source(engine, fire_once, &source,
                                      &rate_3_hz, 0U, &source.id) ==
           BM_STATUS_OK);
    assert(bm_engine_timed_source_cycle_count(engine, source.id, &cycles) ==
           BM_STATUS_OK);
    assert(cycles == 0U);
    assert(bm_engine_run_for(engine, UINT64_C(333333333)) == BM_STATUS_OK);
    assert(bm_engine_timed_source_cycle_count(engine, source.id, &cycles) ==
           BM_STATUS_OK);
    assert(cycles == 0U);
    assert(bm_engine_run_for(engine, 1U) == BM_STATUS_OK);
    assert(bm_engine_timed_source_cycle_count(engine, source.id, &cycles) ==
           BM_STATUS_OK);
    assert(cycles == 1U);
    assert(bm_engine_run_for(engine, UINT64_C(333333333)) == BM_STATUS_OK);
    assert(bm_engine_timed_source_cycle_count(engine, source.id, &cycles) ==
           BM_STATUS_OK);
    assert(cycles == 2U);
    assert(bm_engine_reset(engine) == BM_STATUS_OK);
    assert(bm_engine_timed_source_cycle_count(engine, source.id, &cycles) ==
           BM_STATUS_OK);
    assert(cycles == 0U);
    assert(bm_engine_timed_source_cycle_count(engine, source.id, NULL) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_engine_timed_source_cycle_count(engine, source.id + 1U,
                                               &cycles) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_engine_timed_source_cycle_count(NULL, source.id, &cycles) ==
           BM_STATUS_INVALID_ARGUMENT);
    bm_engine_destroy(engine);
}

static bm_status_t
fire_wake(bm_engine_t *engine, void *context,
          const bm_time_point_t *when, uint64_t *cycles_until_next)
{
    wake_source_t *wake = context;

    assert(when->nanoseconds == UINT64_C(333333333));
    assert(when->subnanosecond_numerator == 1U);
    assert(bm_engine_schedule_at(engine, when->nanoseconds,
                                 must_not_run, NULL) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_engine_signal_cpu(engine, wake->cpu_id, 0U, 1) == BM_STATUS_OK);
    *cycles_until_next = 0U;
    return BM_STATUS_IDLE;
}

static void
test_fractional_source_wakes_idle_cpu_without_time_travel(void)
{
    bm_engine_t *engine = make_clocked_engine(1U);
    sleeping_cpu_t context = { 1, 0U };
    wake_source_t wake = { UINT32_MAX };
    bm_cpu_t cpu = { 0 };

    cpu.name = "sleeping-synthetic";
    cpu.context = &context;
    cpu.ops.reset = sleeping_cpu_reset;
    cpu.ops.signal = sleeping_cpu_signal;
    assert(bm_engine_add_clocked_cpu(engine, &cpu, sleeping_cpu_step,
                                     &rate_1_ghz, &wake.cpu_id) == BM_STATUS_OK);
    assert(bm_engine_add_timed_source(engine, fire_wake, &wake,
                                      &rate_3_hz, 1U, NULL) == BM_STATUS_OK);
    assert(bm_engine_run_for(engine, UINT64_C(333333335)) == BM_STATUS_OK);
    assert(context.completed_steps == 1U);
    bm_engine_destroy(engine);
}

static bm_status_t
fire_zero_progress(bm_engine_t *engine, void *context,
                   const bm_time_point_t *when, uint64_t *cycles_until_next)
{
    (void) engine;
    (void) context;
    (void) when;
    *cycles_until_next = 0U;
    return BM_STATUS_OK;
}

static bm_status_t
fire_invalid_idle(bm_engine_t *engine, void *context,
                  const bm_time_point_t *when, uint64_t *cycles_until_next)
{
    (void) engine;
    (void) context;
    (void) when;
    *cycles_until_next = 1U;
    return BM_STATUS_IDLE;
}

static bm_status_t
fire_reject_self_control(bm_engine_t *engine, void *context,
                         const bm_time_point_t *when,
                         uint64_t *cycles_until_next)
{
    controlled_source_t *source = context;

    (void) when;
    assert(bm_engine_disarm_timed_source(engine, source->id) ==
           BM_STATUS_INVALID_STATE);
    assert(bm_engine_arm_timed_source(engine, source->id, 1U) ==
           BM_STATUS_INVALID_STATE);
    ++source->fire_count;
    *cycles_until_next = 0U;
    return BM_STATUS_IDLE;
}

static void
test_control_validation_and_overflow_are_atomic(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_engine_config_t legacy_config = { 1U, 1U, 1U };
    bm_engine_t *legacy = NULL;
    bm_engine_t *engine = make_clocked_engine(1U);
    controlled_source_t source = { 0 };

    assert(bm_engine_create(&host, &legacy_config, &legacy) == BM_STATUS_OK);
    assert(bm_engine_arm_timed_source(legacy, 0U, 1U) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_engine_disarm_timed_source(legacy, 0U) ==
           BM_STATUS_INVALID_ARGUMENT);
    bm_engine_destroy(legacy);

    assert(bm_engine_add_timed_source(engine, fire_once, &source,
                                      &rate_1_hz, 1U, &source.id) ==
           BM_STATUS_OK);
    assert(bm_engine_arm_timed_source(engine, source.id, 0U) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_engine_arm_timed_source(engine, source.id + 1U, 1U) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_engine_disarm_timed_source(engine, source.id + 1U) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_engine_arm_timed_source(engine, source.id, UINT64_MAX) ==
           BM_STATUS_CAPACITY_EXCEEDED);
    assert(bm_engine_run_for(engine, UINT64_C(1000000000)) == BM_STATUS_OK);
    assert(source.fire_count == 1U);
    bm_engine_destroy(engine);

    engine = make_clocked_engine(1U);
    memset(&source, 0, sizeof(source));
    assert(bm_engine_add_timed_source(engine, fire_reject_self_control,
                                      &source, &rate_1_hz, 1U, &source.id) ==
           BM_STATUS_OK);
    assert(bm_engine_run_for(engine, UINT64_C(1000000000)) == BM_STATUS_OK);
    assert(source.fire_count == 1U);
    bm_engine_destroy(engine);
}

static void
test_validation_and_invalid_progress(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_engine_config_t legacy_config = { 1U, 1U, 1U };
    bm_engine_t *legacy = NULL;
    bm_engine_t *engine = make_clocked_engine(1U);
    bm_time_point_t now;
    bm_timed_source_id_t id = UINT32_MAX;

    assert(bm_engine_create(&host, &legacy_config, &legacy) == BM_STATUS_OK);
    assert(bm_engine_add_timed_source(legacy, fire_zero_progress, NULL,
                                      &rate_1_hz, 1U, NULL) ==
           BM_STATUS_INVALID_ARGUMENT);
    bm_engine_destroy(legacy);

    assert(bm_engine_now_exact(NULL, &now) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_engine_now_exact(engine, NULL) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_engine_run_for(engine, 1U) == BM_STATUS_OK);
    assert(bm_engine_add_timed_source(engine, fire_zero_progress, NULL,
                                      &rate_1_hz, 1U, NULL) ==
           BM_STATUS_INVALID_ARGUMENT);
    bm_engine_destroy(engine);

    engine = make_clocked_engine(1U);
    assert(bm_engine_add_timed_source(engine, NULL, NULL, &rate_1_hz,
                                      1U, NULL) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_engine_add_timed_source(engine, fire_zero_progress, NULL,
                                      &rate_zero, 1U, NULL) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_engine_add_timed_source(engine, fire_zero_progress, NULL,
                                      &rate_1_hz, 0U, &id) == BM_STATUS_OK);
    assert(bm_engine_add_timed_source(engine, fire_zero_progress, NULL,
                                      &rate_1_hz, 1U, NULL) ==
           BM_STATUS_CAPACITY_EXCEEDED);
    assert(bm_engine_arm_timed_source(engine, id, 1U) == BM_STATUS_OK);
    assert(bm_engine_run_for(engine, UINT64_C(1000000000)) ==
           BM_STATUS_DEVICE_ERROR);
    bm_engine_destroy(engine);

    engine = make_clocked_engine(1U);
    assert(bm_engine_add_timed_source(engine, fire_invalid_idle, NULL,
                                      &rate_1_hz, 1U, NULL) == BM_STATUS_OK);
    assert(bm_engine_run_for(engine, UINT64_C(1000000000)) ==
           BM_STATUS_DEVICE_ERROR);
    bm_engine_destroy(engine);
}

static void
test_partial_creation_releases_timed_source_storage(void)
{
    bm_engine_config_t config = { 1U, 1U, 1U };
    size_t failure;

    for (failure = 0U; failure < 4U; ++failure) {
        failure_injection_host_t tracker;
        bm_host_services_t host;
        bm_engine_t *engine = NULL;

        failure_injection_host_initialize(&tracker);
        failure_injection_host_fail_on(&tracker, failure);
        host = failure_injection_host_services(&tracker);
        assert(bm_engine_create_clocked(&host, &config, &engine) ==
               BM_STATUS_OUT_OF_MEMORY);
        assert(engine == NULL);
        assert(tracker.outstanding_allocations == 0U);
    }
}

int
main(void)
{
    test_exact_fractional_periods_and_reset();
    test_initially_disarmed_source_can_be_armed_exactly();
    test_reprogram_disarm_and_reset_restore_registration();
    test_same_boundary_event_can_cancel_source();
    test_same_time_order_is_stable();
    test_fractional_source_wakes_idle_cpu_without_time_travel();
    test_cpu_control_uses_exact_instruction_start_boundary();
    test_source_cycle_cursor_tracks_domain_while_disarmed();
    test_control_validation_and_overflow_are_atomic();
    test_validation_and_invalid_progress();
    test_partial_creation_releases_timed_source_storage();
    return 0;
}
