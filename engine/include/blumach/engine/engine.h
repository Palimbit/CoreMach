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
typedef void (*bm_engine_event_fn)(bm_engine_t *engine, void *context);
/* One completed architectural boundary, reporting cycles in this CPU's own
 * clock domain. start_ns is its integer virtual boundary-start time, not the
 * host clock or a timestamp for individual bus phases. The CPU includes
 * applicable bus wait states. */
typedef bm_status_t (*bm_clocked_cpu_step_fn)(void *context, bm_tick_t start_ns,
                                              uint64_t *cycles);

typedef struct bm_engine_config {
    size_t max_cpus;
    size_t max_events;
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
                                      bm_clocked_cpu_step_fn step, uint64_t frequency_hz,
                                      bm_cpu_id_t *out_id);
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

#ifdef __cplusplus
}
#endif

#endif
