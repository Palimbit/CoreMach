/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/platforms/null_host.h>

#include <stdlib.h>

static void *
null_allocate(void *context, size_t size)
{
    (void) context;
    return malloc(size);
}

static void
null_release(void *context, void *allocation)
{
    (void) context;
    free(allocation);
}

static bm_tick_t
null_time(void *context)
{
    (void) context;
    return 0;
}

static void
null_log(void *context, bm_log_level_t level, const char *message)
{
    (void) context;
    (void) level;
    (void) message;
}

bm_host_services_t
bm_null_host_services(void)
{
    bm_host_services_t services = {
        .context = NULL,
        .allocate = null_allocate,
        .release = null_release,
        .monotonic_time = null_time,
        .log = null_log,
        .capabilities = NULL,
        .capability_count = 0U
    };
    return services;
}
