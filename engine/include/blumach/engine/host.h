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
    /* bm_host_serial_services_t: characters a machine transmits on its
     * serial ports. */
    BM_HOST_CAPABILITY_SERIAL,
    /* bm_host_cassette_services_t: a machine's cassette recorder motor and
     * recording signal. */
    BM_HOST_CAPABILITY_CASSETTE,
    /* bm_host_parallel_services_t: a printer on a machine's parallel port. */
    BM_HOST_CAPABILITY_PARALLEL,
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

/* Serial output. A machine calls transmit when a character has left its
 * serial port, at the emulated time the last stop bit ended. data holds the
 * character bits only. Input goes the other way, through the runtime's
 * bm_session_serial_receive(). */
#define BM_HOST_SERIAL_VERSION 1U

typedef void (*bm_host_serial_transmit_fn)(void *context, uint32_t port,
                                           uint8_t data, bm_tick_t time);

typedef struct bm_host_serial_services {
    bm_host_serial_transmit_fn transmit;
    void *context;
} bm_host_serial_services_t;

/* Audio output (BM_HOST_CAPABILITY_AUDIO_OUTPUT). A one-bit sound source,
 * such as a buzzer, reports each change of what it plays at the emulated
 * time it happens: either a steady drive level (tone 0/0) or a square wave
 * of tone_numerator / tone_denominator hertz. Hosts synthesise the sound
 * from these changes. */
#define BM_HOST_AUDIO_VERSION 1U

typedef struct bm_audio_change {
    uint32_t size;
    uint32_t channel;
    bm_tick_t time;
    int level;                 /* 0 or 1 when there is no tone */
    uint64_t tone_numerator;   /* 0 when there is no tone */
    uint64_t tone_denominator;
} bm_audio_change_t;

typedef void (*bm_host_audio_change_fn)(void *context,
                                        const bm_audio_change_t *change);

typedef struct bm_host_audio_services {
    bm_host_audio_change_fn change;
    void *context;
} bm_host_audio_services_t;

/* Cassette recorder. motor reports the remote relay turning the recorder's
 * motor on or off; record reports each change of the one-bit signal sent to
 * the recorder's MIC input, whether or not the motor runs. Both carry the
 * emulated time of the change. Playback goes the other way, through the
 * runtime's bm_session_cassette_load(). */
#define BM_HOST_CASSETTE_VERSION 1U

typedef void (*bm_host_cassette_motor_fn)(void *context, uint32_t port,
                                          int on, bm_tick_t time);
typedef void (*bm_host_cassette_record_fn)(void *context, uint32_t port,
                                           int level, bm_tick_t time);

typedef struct bm_host_cassette_services {
    bm_host_cassette_motor_fn motor;
    bm_host_cassette_record_fn record;
    void *context;
} bm_host_cassette_services_t;

/* Parallel (Centronics) printer. write receives each byte the machine
 * strobes, at the emulated time of the strobe. status, when not NULL,
 * returns the printer's BM_PARALLEL_STATUS_* lines; without it the printer
 * is never busy and always on line. */
#define BM_HOST_PARALLEL_VERSION 1U
#define BM_PARALLEL_STATUS_BUSY 0x1U
#define BM_PARALLEL_STATUS_SELECTED 0x2U

typedef void (*bm_host_parallel_write_fn)(void *context, uint32_t port,
                                          uint8_t data, bm_tick_t time);
typedef uint32_t (*bm_host_parallel_status_fn)(void *context, uint32_t port);

typedef struct bm_host_parallel_services {
    bm_host_parallel_write_fn write;
    bm_host_parallel_status_fn status;
    void *context;
} bm_host_parallel_services_t;

bm_status_t bm_host_services_validate(const bm_host_services_t *services);
bm_status_t bm_host_capability_lookup(const bm_host_services_t *services,
                                      bm_host_capability_id_t id,
                                      uint32_t minimum_version,
                                      size_t minimum_services_size,
                                      const bm_host_capability_t **out_capability);
/* The serial, audio, cassette or parallel services of a host, or NULL without them. */
const bm_host_serial_services_t *bm_host_serial(
    const bm_host_services_t *services);
const bm_host_audio_services_t *bm_host_audio(
    const bm_host_services_t *services);
const bm_host_cassette_services_t *bm_host_cassette(
    const bm_host_services_t *services);
const bm_host_parallel_services_t *bm_host_parallel(
    const bm_host_services_t *services);
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
