/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_ENGINE_HOST_H
#define BLUMACH_ENGINE_HOST_H

#include <stddef.h>
#include <blumach/engine/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum bm_log_level {
    BM_LOG_DEBUG = 0,
    BM_LOG_INFO,
    BM_LOG_WARNING,
    BM_LOG_ERROR
} bm_log_level_t;

typedef void *(*bm_host_allocate_fn)(void *context, size_t size);
typedef void (*bm_host_release_fn)(void *context, void *allocation);
typedef bm_tick_t (*bm_host_monotonic_time_fn)(void *context);
typedef void (*bm_host_log_fn)(void *context, bm_log_level_t level, const char *message);

typedef struct bm_host_services {
    void *context;
    bm_host_allocate_fn allocate;
    bm_host_release_fn release;
    bm_host_monotonic_time_fn monotonic_time;
    bm_host_log_fn log;
} bm_host_services_t;

bm_status_t bm_host_services_validate(const bm_host_services_t *services);

#ifdef __cplusplus
}
#endif

#endif
