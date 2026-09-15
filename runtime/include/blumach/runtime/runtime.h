/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_RUNTIME_RUNTIME_H
#define BLUMACH_RUNTIME_RUNTIME_H

#include <blumach/engine/engine.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bm_session bm_session_t;

typedef enum bm_session_state {
    BM_SESSION_NEW = 0,
    BM_SESSION_CONFIGURED,
    BM_SESSION_RUNNING,
    BM_SESSION_PAUSED,
    BM_SESSION_STOPPED
} bm_session_state_t;

typedef bm_status_t (*bm_machine_validate_fn)(const void *configuration);
typedef bm_status_t (*bm_machine_create_fn)(bm_engine_t *engine,
                                            const bm_host_services_t *host,
                                            const void *configuration,
                                            void **out_machine);
typedef void (*bm_machine_destroy_fn)(void *machine);

typedef struct bm_machine_ops {
    bm_machine_validate_fn validate;
    bm_machine_create_fn create;
    bm_machine_destroy_fn destroy;
} bm_machine_ops_t;

typedef struct bm_machine_config {
    const char *definition;
    const void *configuration;
    bm_machine_ops_t ops;
    bm_engine_config_t engine;
} bm_machine_config_t;

bm_status_t bm_session_create(const bm_host_services_t *host, bm_session_t **out_session);
bm_status_t bm_session_configure(bm_session_t *session, const bm_machine_config_t *configuration);
bm_status_t bm_session_start(bm_session_t *session);
bm_status_t bm_session_run_for(bm_session_t *session, bm_tick_t duration);
bm_status_t bm_session_pause(bm_session_t *session);
bm_status_t bm_session_resume(bm_session_t *session);
bm_status_t bm_session_reset(bm_session_t *session);
bm_status_t bm_session_stop(bm_session_t *session);
void bm_session_destroy(bm_session_t *session);
bm_session_state_t bm_session_state(const bm_session_t *session);
bm_tick_t bm_session_time(const bm_session_t *session);
bm_status_t bm_session_inspect_cpu(const bm_session_t *session,
                                   bm_cpu_id_t id,
                                   const char *name,
                                   uint64_t *value);

#ifdef __cplusplus
}
#endif

#endif
