/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/components/pit8253_clock.h>

#include "pit8253_private.h"

#include <limits.h>

typedef struct bm_pit8253_clock_binding {
    bm_engine_t *engine;
    bm_pit8253_t *pit;
    bm_timed_source_id_t source_id;
    uint64_t synchronized_cycles;
    bm_status_t failure;
    int firing;
} bm_pit8253_clock_binding_t;

static bm_status_t
advance_to_cursor(bm_pit8253_clock_binding_t *binding)
{
    uint64_t cursor;
    uint64_t remaining;
    bm_status_t status;

    if (binding->failure != BM_STATUS_OK)
        return binding->failure;
    status = bm_engine_timed_source_cycle_count(binding->engine,
                                                 binding->source_id, &cursor);
    if (status != BM_STATUS_OK) {
        binding->failure = status;
        return status;
    }
    /* Engine reset starts a new virtual-time epoch. Its registered first
     * delay wakes this source once, while an explicit PIT reset or the next
     * access can also establish the new zero before that wake-up. */
    if (cursor < binding->synchronized_cycles)
        binding->synchronized_cycles = 0U;

    remaining = cursor - binding->synchronized_cycles;
    binding->synchronized_cycles = cursor;
    while (remaining != 0U) {
        uint32_t chunk = remaining > UINT32_MAX ? UINT32_MAX :
                                                   (uint32_t) remaining;

        status = bm_pit8253_advance(binding->pit, chunk);
        if (status != BM_STATUS_OK) {
            binding->failure = status;
            return status;
        }
        remaining -= chunk;
    }
    return BM_STATUS_OK;
}

static bm_status_t
schedule_next_transition(bm_pit8253_clock_binding_t *binding)
{
    uint32_t cycles = bm_pit_exact_cycles_until_output_change(
        &binding->pit->exact);
    bm_status_t status;

    if (binding->firing)
        return BM_STATUS_OK;
    if (cycles == 0U)
        status = bm_engine_disarm_timed_source(binding->engine,
                                                binding->source_id);
    else
        status = bm_engine_arm_timed_source(binding->engine,
                                             binding->source_id, cycles);
    if (status != BM_STATUS_OK)
        binding->failure = status;
    return status;
}

static bm_status_t
pit_clock_synchronize(void *context)
{
    return advance_to_cursor(context);
}

static bm_status_t
pit_clock_changed(void *context)
{
    bm_pit8253_clock_binding_t *binding = context;
    bm_status_t status = advance_to_cursor(binding);

    if (status != BM_STATUS_OK)
        return status;
    return schedule_next_transition(binding);
}

static bm_status_t
pit_clock_edge(bm_engine_t *engine, void *context,
               const bm_time_point_t *when, uint64_t *cycles_until_next)
{
    bm_pit8253_clock_binding_t *binding = context;
    uint32_t cycles;
    bm_status_t status;

    (void) engine;
    (void) when;
    binding->firing = 1;
    status = advance_to_cursor(binding);
    binding->firing = 0;
    if (status != BM_STATUS_OK) {
        *cycles_until_next = 0U;
        return status;
    }
    cycles = bm_pit_exact_cycles_until_output_change(&binding->pit->exact);
    if (cycles == 0U) {
        *cycles_until_next = 0U;
        return BM_STATUS_IDLE;
    }
    *cycles_until_next = cycles;
    return BM_STATUS_OK;
}

bm_status_t
bm_pit8253_attach_clock(bm_engine_t *engine, bm_pit8253_t *pit,
                        const bm_clock_rate_t *rate,
                        bm_timed_source_id_t *out_source_id)
{
    bm_pit8253_clock_binding_t *binding;
    bm_status_t status;

    if ((engine == NULL) || (pit == NULL) || (rate == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    if (pit->clock_binding != NULL)
        return BM_STATUS_INVALID_STATE;
    binding = pit->host.allocate(pit->host.context, sizeof(*binding));
    if (binding == NULL)
        return BM_STATUS_OUT_OF_MEMORY;
    binding->engine = engine;
    binding->pit = pit;
    binding->source_id = 0U;
    binding->synchronized_cycles = 0U;
    binding->failure = BM_STATUS_OK;
    binding->firing = 0;

    status = bm_engine_add_timed_source(engine, pit_clock_edge, binding, rate,
                                         1U, &binding->source_id);
    if (status != BM_STATUS_OK) {
        pit->host.release(pit->host.context, binding);
        return status;
    }
    pit->clock_sync = pit_clock_synchronize;
    pit->clock_changed = pit_clock_changed;
    pit->clock_context = binding;
    pit->clock_binding = binding;
    if (out_source_id != NULL)
        *out_source_id = binding->source_id;

    return schedule_next_transition(binding);
}
