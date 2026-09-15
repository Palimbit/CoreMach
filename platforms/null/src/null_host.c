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
        NULL,
        null_allocate,
        null_release,
        null_time,
        null_log
    };
    return services;
}
