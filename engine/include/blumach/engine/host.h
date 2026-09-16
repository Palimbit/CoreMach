/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_ENGINE_HOST_H
#define BLUMACH_ENGINE_HOST_H

#include <stddef.h>
#include <stdint.h>
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

typedef enum bm_host_capability_id {
    BM_HOST_CAPABILITY_FIRMWARE = 0,
    BM_HOST_CAPABILITY_BLOCK_MEDIA,
    BM_HOST_CAPABILITY_VIDEO_OUTPUT,
    BM_HOST_CAPABILITY_AUDIO_OUTPUT,
    BM_HOST_CAPABILITY_INPUT_SOURCE,
    BM_HOST_CAPABILITY_NETWORK,
    BM_HOST_CAPABILITY_TASKS,
    BM_HOST_CAPABILITY_EXECUTABLE_MEMORY,
    BM_HOST_CAPABILITY_COUNT
} bm_host_capability_id_t;

/* A capability-specific service table owned by the host. Its storage and the
 * storage referenced by services must remain valid for the lifetime of every
 * engine or session using the host. Later versions must preserve the prefix
 * represented by earlier versions. */
typedef struct bm_host_capability {
    bm_host_capability_id_t id;
    uint32_t version;
    size_t services_size;
    const void *services;
} bm_host_capability_t;

/* Immutable bytes supplied by a frontend or platform adapter. The caller owns
 * both the bytes and metadata for the lifetime of the configured session. */
typedef struct bm_blob_view {
    const char *id;
    const uint8_t *data;
    size_t size;
    const char *sha256;
} bm_blob_view_t;

typedef struct bm_host_services {
    void *context;
    bm_host_allocate_fn allocate;
    bm_host_release_fn release;
    bm_host_monotonic_time_fn monotonic_time;
    bm_host_log_fn log;
    const bm_host_capability_t *capabilities;
    size_t capability_count;
} bm_host_services_t;

bm_status_t bm_host_services_validate(const bm_host_services_t *services);
bm_status_t bm_host_capability_lookup(const bm_host_services_t *services,
                                      bm_host_capability_id_t id,
                                      uint32_t minimum_version,
                                      size_t minimum_services_size,
                                      const bm_host_capability_t **out_capability);

#ifdef __cplusplus
}
#endif

#endif
