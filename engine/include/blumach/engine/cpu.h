/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_ENGINE_CPU_H
#define BLUMACH_ENGINE_CPU_H

#include <stdint.h>
#include <blumach/engine/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef bm_status_t (*bm_cpu_reset_fn)(void *context);
typedef bm_status_t (*bm_cpu_run_fn)(void *context, bm_tick_t budget, bm_tick_t *consumed);
typedef bm_status_t (*bm_cpu_signal_fn)(void *context, uint32_t line, int asserted);
typedef bm_status_t (*bm_cpu_inspect_fn)(const void *context, const char *name, uint64_t *value);
typedef void (*bm_cpu_destroy_fn)(void *context);

typedef struct bm_cpu_ops {
    bm_cpu_reset_fn reset;
    bm_cpu_run_fn run;
    bm_cpu_signal_fn signal;
    bm_cpu_inspect_fn inspect;
    bm_cpu_destroy_fn destroy;
} bm_cpu_ops_t;

typedef struct bm_cpu {
    const char *name;
    void *context;
    bm_cpu_ops_t ops;
} bm_cpu_t;

#ifdef __cplusplus
}
#endif

#endif
