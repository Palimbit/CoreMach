/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/engine/engine.h>
#include "clock_math.h"

#include <limits.h>
#include <string.h>

typedef struct bm_cpu_slot {
    bm_cpu_t cpu;
    bm_clocked_cpu_step_fn step_cycles;
    bm_tick_t local_time;
    bm_clock_position_t clock_position;
    int suspended;
} bm_cpu_slot_t;

typedef struct bm_event_slot {
    bm_tick_t when;
    uint64_t sequence;
    bm_engine_event_fn callback;
    void *context;
    int active;
} bm_event_slot_t;

struct bm_engine {
    bm_host_services_t host;
    bm_cpu_slot_t *cpus;
    bm_event_slot_t *events;
    size_t cpu_count;
    size_t max_cpus;
    size_t max_events;
    bm_tick_t now;
    uint64_t next_sequence;
    int clocked;
};

bm_status_t
bm_host_services_validate(const bm_host_services_t *services)
{
    size_t index;

    if ((services == NULL) || (services->allocate == NULL) || (services->release == NULL) ||
        (services->monotonic_time == NULL) || (services->log == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    if (((services->capabilities == NULL) != (services->capability_count == 0U)) ||
        (services->capability_count > (size_t) BM_HOST_CAPABILITY_COUNT))
        return BM_STATUS_INVALID_ARGUMENT;
    for (index = 0U; index < services->capability_count; ++index) {
        const bm_host_capability_t *capability = &services->capabilities[index];
        size_t previous;

        if (((int) capability->id < 0) ||
            (capability->id >= BM_HOST_CAPABILITY_COUNT) ||
            (capability->version == 0U) || (capability->services_size == 0U) ||
            (capability->services == NULL))
            return BM_STATUS_INVALID_ARGUMENT;
        for (previous = 0U; previous < index; ++previous) {
            if (services->capabilities[previous].id == capability->id)
                return BM_STATUS_INVALID_ARGUMENT;
        }
    }
    return BM_STATUS_OK;
}

bm_status_t
bm_host_capability_lookup(const bm_host_services_t *services,
                          bm_host_capability_id_t id,
                          uint32_t minimum_version,
                          size_t minimum_services_size,
                          const bm_host_capability_t **out_capability)
{
    size_t index;

    if (out_capability == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    *out_capability = NULL;
    if ((bm_host_services_validate(services) != BM_STATUS_OK) ||
        ((int) id < 0) || (id >= BM_HOST_CAPABILITY_COUNT) ||
        (minimum_version == 0U) || (minimum_services_size == 0U))
        return BM_STATUS_INVALID_ARGUMENT;

    for (index = 0U; index < services->capability_count; ++index) {
        const bm_host_capability_t *capability = &services->capabilities[index];

        if (capability->id != id)
            continue;
        if ((capability->version < minimum_version) ||
            (capability->services_size < minimum_services_size))
            return BM_STATUS_UNSUPPORTED;
        *out_capability = capability;
        return BM_STATUS_OK;
    }
    return BM_STATUS_UNSUPPORTED;
}

static void *
engine_allocate(const bm_host_services_t *host, size_t size)
{
    void *allocation = host->allocate(host->context, size);
    if (allocation != NULL)
        memset(allocation, 0, size);
    return allocation;
}

bm_status_t
bm_engine_create(const bm_host_services_t *host,
                 const bm_engine_config_t *config,
                 bm_engine_t **out_engine)
{
    bm_engine_t *engine;

    if ((bm_host_services_validate(host) != BM_STATUS_OK) || (config == NULL) ||
        (out_engine == NULL) || (config->max_cpus == 0) || (config->max_events == 0))
        return BM_STATUS_INVALID_ARGUMENT;
    if ((config->max_cpus > UINT32_MAX) ||
        (config->max_cpus > (SIZE_MAX / sizeof(bm_cpu_slot_t))) ||
        (config->max_events > (SIZE_MAX / sizeof(bm_event_slot_t))))
        return BM_STATUS_INVALID_ARGUMENT;

    *out_engine = NULL;
    engine = engine_allocate(host, sizeof(*engine));
    if (engine == NULL)
        return BM_STATUS_OUT_OF_MEMORY;
    engine->host = *host;
    engine->max_cpus = config->max_cpus;
    engine->max_events = config->max_events;
    engine->cpus = engine_allocate(host, config->max_cpus * sizeof(*engine->cpus));
    engine->events = engine_allocate(host, config->max_events * sizeof(*engine->events));
    if ((engine->cpus == NULL) || (engine->events == NULL)) {
        bm_engine_destroy(engine);
        return BM_STATUS_OUT_OF_MEMORY;
    }

    *out_engine = engine;
    return BM_STATUS_OK;
}

bm_status_t
bm_engine_create_clocked(const bm_host_services_t *host,
                         const bm_engine_config_t *config,
                         bm_engine_t **out_engine)
{
    bm_status_t status = bm_engine_create(host, config, out_engine);

    if (status == BM_STATUS_OK)
        (*out_engine)->clocked = 1;
    return status;
}

void
bm_engine_destroy(bm_engine_t *engine)
{
    size_t index;

    if (engine == NULL)
        return;
    if (engine->cpus != NULL) {
        for (index = 0; index < engine->cpu_count; ++index) {
            if (engine->cpus[index].cpu.ops.destroy != NULL)
                engine->cpus[index].cpu.ops.destroy(engine->cpus[index].cpu.context);
        }
        engine->host.release(engine->host.context, engine->cpus);
    }
    if (engine->events != NULL)
        engine->host.release(engine->host.context, engine->events);
    engine->host.release(engine->host.context, engine);
}

bm_status_t
bm_engine_add_cpu(bm_engine_t *engine, const bm_cpu_t *cpu, bm_cpu_id_t *out_id)
{
    size_t index;

    if ((engine == NULL) || engine->clocked || (cpu == NULL) || (cpu->ops.reset == NULL) ||
        (cpu->ops.run == NULL) || (cpu->ops.signal == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    if (engine->cpu_count >= engine->max_cpus)
        return BM_STATUS_CAPACITY_EXCEEDED;

    index = engine->cpu_count++;
    engine->cpus[index].cpu = *cpu;
    engine->cpus[index].local_time = engine->now;
    if (out_id != NULL)
        *out_id = (bm_cpu_id_t) index;
    return BM_STATUS_OK;
}

bm_status_t
bm_engine_add_clocked_cpu(bm_engine_t *engine, const bm_cpu_t *cpu,
                          bm_clocked_cpu_step_fn step, uint64_t frequency_hz,
                          bm_cpu_id_t *out_id)
{
    size_t index;
    bm_cpu_slot_t *slot;

    if ((engine == NULL) || !engine->clocked || (cpu == NULL) ||
        (cpu->ops.reset == NULL) || (step == NULL) ||
        (cpu->ops.signal == NULL) || (frequency_hz == 0U))
        return BM_STATUS_INVALID_ARGUMENT;
    if (engine->cpu_count >= engine->max_cpus)
        return BM_STATUS_CAPACITY_EXCEEDED;
    index = engine->cpu_count;
    slot = &engine->cpus[index];
    if (bm_clock_position_init(&slot->clock_position, frequency_hz) != BM_STATUS_OK)
        return BM_STATUS_INVALID_ARGUMENT;
    slot->clock_position.nanoseconds = engine->now;
    slot->cpu = *cpu;
    slot->step_cycles = step;
    ++engine->cpu_count;
    if (out_id != NULL)
        *out_id = (bm_cpu_id_t) index;
    return BM_STATUS_OK;
}

bm_status_t
bm_engine_reset(bm_engine_t *engine)
{
    size_t index;
    bm_status_t status;

    if (engine == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    engine->now = 0;
    engine->next_sequence = 0;
    for (index = 0; index < engine->max_events; ++index)
        engine->events[index].active = 0;
    for (index = 0; index < engine->cpu_count; ++index) {
        status = engine->cpus[index].cpu.ops.reset(engine->cpus[index].cpu.context);
        if (status != BM_STATUS_OK)
            return status;
        engine->cpus[index].local_time = 0;
        if (engine->clocked) {
            engine->cpus[index].clock_position.nanoseconds = 0U;
            engine->cpus[index].clock_position.phase = 0U;
            engine->cpus[index].suspended = 0;
        }
    }
    return BM_STATUS_OK;
}

bm_status_t
bm_engine_schedule_at(bm_engine_t *engine,
                      bm_tick_t when,
                      bm_engine_event_fn callback,
                      void *context)
{
    size_t index;

    if ((engine == NULL) || (callback == NULL) || (when < engine->now))
        return BM_STATUS_INVALID_ARGUMENT;
    if (engine->next_sequence == UINT64_MAX)
        return BM_STATUS_CAPACITY_EXCEEDED;
    for (index = 0; index < engine->max_events; ++index) {
        if (!engine->events[index].active) {
            engine->events[index].when = when;
            engine->events[index].sequence = engine->next_sequence++;
            engine->events[index].callback = callback;
            engine->events[index].context = context;
            engine->events[index].active = 1;
            return BM_STATUS_OK;
        }
    }
    return BM_STATUS_CAPACITY_EXCEEDED;
}

static bm_tick_t
next_event_time(const bm_engine_t *engine, bm_tick_t limit)
{
    size_t index;
    bm_tick_t next = limit;

    for (index = 0; index < engine->max_events; ++index) {
        /* The requested boundary is inclusive for scheduled events. */
        if (engine->events[index].active && (engine->events[index].when <= next))
            next = engine->events[index].when;
    }
    return next;
}

static bm_status_t
run_cpus_to(bm_engine_t *engine, bm_tick_t limit)
{
    for (;;) {
        size_t index;
        size_t selected = SIZE_MAX;
        bm_tick_t earliest = limit;
        bm_tick_t consumed = 0;
        bm_status_t status;
        bm_cpu_slot_t *slot;

        for (index = 0; index < engine->cpu_count; ++index) {
            if (engine->cpus[index].local_time < earliest) {
                selected = index;
                earliest = engine->cpus[index].local_time;
            }
        }
        if (selected == SIZE_MAX)
            return BM_STATUS_OK;
        slot = &engine->cpus[selected];
        status = slot->cpu.ops.run(slot->cpu.context, 1, &consumed);

        if (consumed > 1)
            return BM_STATUS_DEVICE_ERROR;
        slot->local_time += consumed;
        if (status == BM_STATUS_IDLE) {
            slot->local_time = limit;
            continue;
        }
        if (status != BM_STATUS_OK)
            return status;
        if (consumed == 0)
            return BM_STATUS_DEVICE_ERROR;
    }
}

static bm_status_t
run_clocked_cpus_to(bm_engine_t *engine, bm_tick_t *limit)
{
    for (;;) {
        size_t index;
        size_t selected = SIZE_MAX;
        bm_clock_position_t deadline = { *limit, 0U, 1U };
        bm_cpu_slot_t *slot;
        uint64_t cycles = 0U;
        bm_status_t status;

        for (index = 0U; index < engine->cpu_count; ++index) {
            const bm_cpu_slot_t *candidate = &engine->cpus[index];

            if (candidate->suspended ||
                (bm_clock_position_compare(&candidate->clock_position, &deadline) >= 0))
                continue;
            if ((selected == SIZE_MAX) ||
                (bm_clock_position_compare(&candidate->clock_position,
                                           &engine->cpus[selected].clock_position) < 0))
                selected = index;
        }
        if (selected == SIZE_MAX)
            return BM_STATUS_OK;
        slot = &engine->cpus[selected];
        status = slot->step_cycles(slot->cpu.context,
                                   slot->clock_position.nanoseconds, &cycles);
        if (status == BM_STATUS_IDLE) {
            if (cycles != 0U)
                return BM_STATUS_DEVICE_ERROR;
            slot->suspended = 1;
            *limit = next_event_time(engine, *limit);
            continue;
        }
        if (status != BM_STATUS_OK)
            return status;
        if (cycles == 0U)
            return BM_STATUS_DEVICE_ERROR;
        status = bm_clock_position_advance(&slot->clock_position, cycles);
        if (status != BM_STATUS_OK)
            return status;
        /* A CPU/device may schedule an earlier event during this boundary.
         * Recompute the slice before any other CPU executes past it. */
        *limit = next_event_time(engine, *limit);
    }
}

static int
take_next_event(bm_engine_t *engine, bm_tick_t when, bm_engine_event_fn *callback, void **context)
{
    size_t index;
    size_t selected = SIZE_MAX;
    uint64_t sequence = UINT64_MAX;

    for (index = 0; index < engine->max_events; ++index) {
        if (engine->events[index].active && (engine->events[index].when == when) &&
            (engine->events[index].sequence < sequence)) {
            selected = index;
            sequence = engine->events[index].sequence;
        }
    }
    if (selected == SIZE_MAX)
        return 0;
    *callback = engine->events[selected].callback;
    *context = engine->events[selected].context;
    engine->events[selected].active = 0;
    return 1;
}

bm_status_t
bm_engine_run_for(bm_engine_t *engine, bm_tick_t duration)
{
    bm_tick_t finish;

    if (engine == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    if (duration > (UINT64_MAX - engine->now))
        return BM_STATUS_INVALID_ARGUMENT;
    finish = engine->now + duration;

    while (engine->now < finish) {
        bm_tick_t slice_end = next_event_time(engine, finish);
        bm_engine_event_fn callback;
        void *context;

        bm_status_t status = engine->clocked ? run_clocked_cpus_to(engine, &slice_end) :
                                                run_cpus_to(engine, slice_end);
        if (status != BM_STATUS_OK)
            return status;
        engine->now = slice_end;
        while (take_next_event(engine, engine->now, &callback, &context))
            callback(engine, context);
    }
    return BM_STATUS_OK;
}

bm_status_t
bm_engine_signal_cpu(bm_engine_t *engine, bm_cpu_id_t id, uint32_t line, int asserted)
{
    bm_status_t status;
    bm_cpu_slot_t *slot;

    if ((engine == NULL) || ((size_t) id >= engine->cpu_count))
        return BM_STATUS_INVALID_ARGUMENT;
    slot = &engine->cpus[id];
    status = slot->cpu.ops.signal(slot->cpu.context, line, asserted);
    if ((status == BM_STATUS_OK) && engine->clocked && asserted && slot->suspended) {
        /* A whole instruction may have crossed the current event deadline.
         * Do not move its local clock back when that event wakes it. */
        if (slot->clock_position.nanoseconds < engine->now) {
            slot->clock_position.nanoseconds = engine->now;
            slot->clock_position.phase = 0U;
        }
        slot->suspended = 0;
    }
    return status;
}

bm_status_t
bm_engine_inspect_cpu(const bm_engine_t *engine, bm_cpu_id_t id, const char *name, uint64_t *value)
{
    const bm_cpu_t *cpu;

    if ((engine == NULL) || ((size_t) id >= engine->cpu_count) || (name == NULL) || (value == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    cpu = &engine->cpus[id].cpu;
    if (cpu->ops.inspect == NULL)
        return BM_STATUS_INVALID_STATE;
    return cpu->ops.inspect(cpu->context, name, value);
}

bm_tick_t
bm_engine_now(const bm_engine_t *engine)
{
    return (engine == NULL) ? 0 : engine->now;
}
