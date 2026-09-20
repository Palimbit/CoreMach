/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/components/xta_clock.h>

#include "xta_private.h"

typedef struct bm_xta_clock_binding {
    bm_engine_t *engine;
    bm_xta_t *xta;
    bm_timed_source_id_t source_id;
    uint64_t service_interval_cycles;
    bm_status_t failure;
    int firing;
} bm_xta_clock_binding_t;

static bm_status_t
schedule_service(void *context)
{
    bm_xta_clock_binding_t *binding = context;
    bm_status_t status;

    if (binding->failure != BM_STATUS_OK)
        return binding->failure;
    if (binding->firing)
        return BM_STATUS_OK;
    if (bm_xta_service_pending(binding->xta))
        status = bm_engine_arm_timed_source(binding->engine,
                                             binding->source_id,
                                             binding->service_interval_cycles);
    else
        status = bm_engine_disarm_timed_source(binding->engine,
                                                binding->source_id);
    if (status != BM_STATUS_OK)
        binding->failure = status;
    return status;
}

static bm_status_t
service_at_deadline(bm_engine_t *engine, void *context,
                    const bm_time_point_t *when,
                    uint64_t *cycles_until_next)
{
    bm_xta_clock_binding_t *binding = context;

    (void) engine;
    (void) when;
    binding->firing = 1;
    bm_xta_service(binding->xta);
    binding->firing = 0;
    if (!bm_xta_service_pending(binding->xta)) {
        *cycles_until_next = 0U;
        return BM_STATUS_IDLE;
    }
    *cycles_until_next = binding->service_interval_cycles;
    return BM_STATUS_OK;
}

bm_status_t
bm_xta_attach_service_clock(bm_engine_t *engine, bm_xta_t *xta,
                            const bm_clock_rate_t *rate,
                            uint64_t service_interval_cycles,
                            bm_timed_source_id_t *out_source_id)
{
    bm_xta_clock_binding_t *binding;
    bm_status_t status;

    if ((engine == NULL) || (xta == NULL) || (rate == NULL) ||
        (service_interval_cycles == 0U))
        return BM_STATUS_INVALID_ARGUMENT;
    if (xta->service_binding != NULL)
        return BM_STATUS_INVALID_STATE;
    binding = xta->host.allocate(xta->host.context, sizeof(*binding));
    if (binding == NULL)
        return BM_STATUS_OUT_OF_MEMORY;
    binding->engine = engine;
    binding->xta = xta;
    binding->source_id = 0U;
    binding->service_interval_cycles = service_interval_cycles;
    binding->failure = BM_STATUS_OK;
    binding->firing = 0;

    status = bm_engine_add_timed_source(engine, service_at_deadline, binding,
                                         rate, 0U, &binding->source_id);
    if (status != BM_STATUS_OK) {
        xta->host.release(xta->host.context, binding);
        return status;
    }
    xta->service_changed = schedule_service;
    xta->service_context = binding;
    xta->service_binding = binding;
    if (out_source_id != NULL)
        *out_source_id = binding->source_id;
    return BM_STATUS_OK;
}
