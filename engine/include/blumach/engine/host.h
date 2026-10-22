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
    /* bm_host_tolerance_services_t: the host accepts documented fallbacks
     * instead of failures and wants each one reported. */
    BM_HOST_CAPABILITY_TOLERANCE,
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

/* Tolerance. Without this capability every component is strict: what it
 * does not model or does not know fails with a status. With it, a component
 * may apply its documented fallback instead (an unmodelled read returns the
 * open-bus value, an unknown device command is ignored, a known but
 * undocumented instruction executes) and reports the case through report.
 * A fallback never hides an internal error such as exhausted memory or
 * inconsistent state. Components report a given case at most once per
 * instance; hosts deduplicate further as they see fit. */
#define BM_HOST_TOLERANCE_VERSION 1U

typedef enum bm_tolerance_event_kind {
    BM_TOLERANCE_UNMODELLED_IO_READ = 1,
    BM_TOLERANCE_UNMODELLED_IO_WRITE = 2,
    BM_TOLERANCE_UNMODELLED_MEMORY_READ = 3,
    BM_TOLERANCE_UNMODELLED_MEMORY_WRITE = 4,
    BM_TOLERANCE_UNDOCUMENTED_INSTRUCTION = 5,
    BM_TOLERANCE_UNKNOWN_DEVICE_COMMAND = 6
} bm_tolerance_event_kind_t;

/* The behaviour applied is assumed from another part, such as Intel NMOS
 * 8085 semantics on a CMOS second source, without evidence for this one. */
#define BM_TOLERANCE_FLAG_ASSUMED_BEHAVIOUR 0x1U
/* pc and opcode are valid. */
#define BM_TOLERANCE_FLAG_HAS_PC 0x2U

typedef struct bm_tolerance_event {
    uint32_t size;
    bm_tolerance_event_kind_t kind;
    uint32_t flags;
    /* Static strings: the reporting device, such as "i8085" or "olivetti-m10
     * I/O", and a short English note on the fallback applied. */
    const char *device;
    const char *note;
    uint64_t pc;
    uint32_t opcode;
    uint64_t address;
    uint64_t value;
} bm_tolerance_event_t;

typedef void (*bm_tolerance_report_fn)(void *context,
                                       const bm_tolerance_event_t *event);

typedef struct bm_host_tolerance_services {
    bm_tolerance_report_fn report;
    void *context;
} bm_host_tolerance_services_t;

bm_status_t bm_host_services_validate(const bm_host_services_t *services);
bm_status_t bm_host_capability_lookup(const bm_host_services_t *services,
                                      bm_host_capability_id_t id,
                                      uint32_t minimum_version,
                                      size_t minimum_services_size,
                                      const bm_host_capability_t **out_capability);
/* The tolerance services of a host, or NULL when the host is strict. */
const bm_host_tolerance_services_t *bm_host_tolerance(
    const bm_host_services_t *services);
/* Reports an event to a tolerant host; does nothing for a strict one. */
void bm_tolerance_report(const bm_host_tolerance_services_t *tolerance,
                         const bm_tolerance_event_t *event);

#ifdef __cplusplus
}
#endif

#endif
