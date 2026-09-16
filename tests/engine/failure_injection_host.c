/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "failure_injection_host.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

static void *
failure_allocate(void *context, size_t size)
{
    failure_injection_host_t *host = context;
    void *allocation;

    if (host->allocation_calls++ == host->fail_on_call)
        return NULL;
    allocation = malloc(size);
    if (allocation != NULL)
        ++host->outstanding_allocations;
    return allocation;
}

static void
failure_release(void *context, void *allocation)
{
    failure_injection_host_t *host = context;

    if (allocation != NULL) {
        assert(host->outstanding_allocations > 0U);
        --host->outstanding_allocations;
    }
    free(allocation);
}

static bm_tick_t
failure_monotonic_time(void *context)
{
    (void) context;
    return 0U;
}

static void
failure_log(void *context, bm_log_level_t level, const char *message)
{
    (void) context;
    (void) level;
    (void) message;
}

void
failure_injection_host_initialize(failure_injection_host_t *host)
{
    assert(host != NULL);
    host->allocation_calls = 0U;
    host->fail_on_call = SIZE_MAX;
    host->outstanding_allocations = 0U;
}

bm_host_services_t
failure_injection_host_services(failure_injection_host_t *host)
{
    bm_host_services_t services = {
        host,
        failure_allocate,
        failure_release,
        failure_monotonic_time,
        failure_log
    };

    assert(host != NULL);
    return services;
}

void
failure_injection_host_fail_on(failure_injection_host_t *host,
                               size_t absolute_call)
{
    assert(host != NULL);
    host->fail_on_call = absolute_call;
}

void
failure_injection_host_fail_after(failure_injection_host_t *host,
                                  size_t additional_calls)
{
    assert(host != NULL);
    assert(additional_calls <= SIZE_MAX - host->allocation_calls);
    host->fail_on_call = host->allocation_calls + additional_calls;
}
