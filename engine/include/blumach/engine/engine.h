/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_ENGINE_ENGINE_H
#define BLUMACH_ENGINE_ENGINE_H

#include <stddef.h>
#include <stdint.h>
#include <blumach/engine/cpu.h>
#include <blumach/engine/host.h>
#include <blumach/engine/video.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bm_engine bm_engine_t;
typedef uint32_t bm_cpu_id_t;
typedef uint32_t bm_timed_source_id_t;
/* Exact native clock rate in cycles per second. Keeping the divider explicit
 * avoids rounding clocks derived from a shared crystal to an integer hertz. */
typedef struct bm_clock_rate {
    uint64_t cycles_per_second_numerator;
    uint64_t cycles_per_second_denominator;
} bm_clock_rate_t;
/* Exact virtual time. nanoseconds is the whole part; the remaining fields are
 * a normalized proper fraction of one nanosecond. */
typedef struct bm_time_point {
    uint64_t nanoseconds;
    uint64_t subnanosecond_numerator;
    uint64_t subnanosecond_denominator;
} bm_time_point_t;
typedef void (*bm_engine_event_fn)(bm_engine_t *engine, void *context);
/* One completed architectural boundary, reporting cycles in this CPU's own
 * clock domain. start_ns is its integer virtual boundary-start time, not the
 * host clock or a timestamp for individual bus phases. The CPU includes
 * applicable bus wait states. */
typedef bm_status_t (*bm_clocked_cpu_step_fn)(void *context, bm_tick_t start_ns,
                                              uint64_t *cycles);
/* A host-independent source of device deadlines. BM_STATUS_OK rearms the
 * source by cycles_until_next native cycles from this exact firing time.
 * BM_STATUS_IDLE with zero cycles disarms it until explicitly armed or reset. */
typedef bm_status_t (*bm_timed_source_fire_fn)(bm_engine_t *engine,
                                               void *context,
                                               const bm_time_point_t *when,
                                               uint64_t *cycles_until_next);

typedef struct bm_engine_config {
    size_t max_cpus;
    size_t max_events;
    size_t max_timed_sources;
} bm_engine_config_t;

bm_status_t bm_engine_create(const bm_host_services_t *host,
                             const bm_engine_config_t *config,
                             bm_engine_t **out_engine);
/* A separate mode for cycle-reported CPUs. The existing create/run path keeps
 * its one-instruction tick semantics. In this mode, run_for, schedule_at and
 * now use integer virtual nanoseconds, never host elapsed time. */
bm_status_t bm_engine_create_clocked(const bm_host_services_t *host,
                                     const bm_engine_config_t *config,
                                     bm_engine_t **out_engine);
void bm_engine_destroy(bm_engine_t *engine);
bm_status_t bm_engine_add_cpu(bm_engine_t *engine, const bm_cpu_t *cpu, bm_cpu_id_t *out_id);
bm_status_t bm_engine_add_clocked_cpu(bm_engine_t *engine, const bm_cpu_t *cpu,
                                      bm_clocked_cpu_step_fn step,
                                      const bm_clock_rate_t *rate,
                                      bm_cpu_id_t *out_id);
/* A zero first delay registers an initially disarmed source; reset restores
 * that registered state. */
bm_status_t bm_engine_add_timed_source(bm_engine_t *engine,
                                       bm_timed_source_fire_fn fire,
                                       void *context,
                                       const bm_clock_rate_t *rate,
                                       uint64_t first_delay_cycles,
                                       bm_timed_source_id_t *out_id);
/* Arm or replace a timed-source deadline. delay_cycles counts source-clock
 * edges strictly after the effective scheduling boundary. A call made while a
 * clocked CPU step is executing uses that CPU's exact instruction-start
 * boundary; other calls use the engine's current exact time. This does not
 * imply bus-phase timing within an instruction. */
bm_status_t bm_engine_arm_timed_source(bm_engine_t *engine,
                                       bm_timed_source_id_t id,
                                       uint64_t delay_cycles);
bm_status_t bm_engine_disarm_timed_source(bm_engine_t *engine,
                                          bm_timed_source_id_t id);
bm_status_t bm_engine_reset(bm_engine_t *engine);
bm_status_t bm_engine_run_for(bm_engine_t *engine, bm_tick_t duration);
bm_status_t bm_engine_schedule_at(bm_engine_t *engine,
                                  bm_tick_t when,
                                  bm_engine_event_fn callback,
                                  void *context);
bm_status_t bm_engine_signal_cpu(bm_engine_t *engine, bm_cpu_id_t id, uint32_t line, int asserted);
bm_status_t bm_engine_inspect_cpu(const bm_engine_t *engine,
                                  bm_cpu_id_t id,
                                  const char *name,
                                  uint64_t *value);
bm_tick_t bm_engine_now(const bm_engine_t *engine);
bm_status_t bm_engine_now_exact(const bm_engine_t *engine,
                                bm_time_point_t *out_time);

#ifdef __cplusplus
}
#endif

#endif
