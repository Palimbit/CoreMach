/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/runtime/runtime.h>

#include <string.h>

struct bm_session {
    bm_host_services_t host;
    bm_machine_config_t configuration;
    bm_engine_t *engine;
    void *machine;
    bm_session_state_t state;
};

bm_status_t
bm_session_create(const bm_host_services_t *host, bm_session_t **out_session)
{
    bm_session_t *session;

    if ((bm_host_services_validate(host) != BM_STATUS_OK) || (out_session == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    *out_session = NULL;
    session = host->allocate(host->context, sizeof(*session));
    if (session == NULL)
        return BM_STATUS_OUT_OF_MEMORY;
    memset(session, 0, sizeof(*session));
    session->host = *host;
    session->state = BM_SESSION_NEW;
    *out_session = session;
    return BM_STATUS_OK;
}

bm_status_t
bm_session_configure(bm_session_t *session, const bm_machine_config_t *configuration)
{
    bm_status_t status;

    if ((session == NULL) || (configuration == NULL) || (configuration->definition == NULL) ||
        (configuration->ops.create == NULL) || (configuration->ops.destroy == NULL) ||
        (configuration->engine.max_cpus == 0) || (configuration->engine.max_events == 0))
        return BM_STATUS_INVALID_ARGUMENT;
    if ((session->state != BM_SESSION_NEW) && (session->state != BM_SESSION_STOPPED) &&
        (session->state != BM_SESSION_CONFIGURED))
        return BM_STATUS_INVALID_STATE;
    if (configuration->ops.validate != NULL) {
        status = configuration->ops.validate(configuration->configuration);
        if (status != BM_STATUS_OK)
            return status;
    }
    session->configuration = *configuration;
    session->state = BM_SESSION_CONFIGURED;
    return BM_STATUS_OK;
}

bm_status_t
bm_session_start(bm_session_t *session)
{
    bm_status_t status;

    if (session == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    if (session->state != BM_SESSION_CONFIGURED)
        return BM_STATUS_INVALID_STATE;
    status = bm_engine_create(&session->host, &session->configuration.engine, &session->engine);
    if (status != BM_STATUS_OK)
        return status;
    status = session->configuration.ops.create(session->engine,
                                               &session->host,
                                               session->configuration.configuration,
                                               &session->machine);
    if ((status == BM_STATUS_OK) && (session->machine == NULL))
        status = BM_STATUS_DEVICE_ERROR;
    if (status == BM_STATUS_OK)
        status = bm_engine_reset(session->engine);
    if (status != BM_STATUS_OK) {
        bm_engine_destroy(session->engine);
        session->engine = NULL;
        if (session->machine != NULL)
            session->configuration.ops.destroy(session->machine);
        session->machine = NULL;
        return status;
    }
    session->state = BM_SESSION_RUNNING;
    return BM_STATUS_OK;
}

bm_status_t
bm_session_run_for(bm_session_t *session, bm_tick_t duration)
{
    if (session == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    if (session->state != BM_SESSION_RUNNING)
        return BM_STATUS_INVALID_STATE;
    return bm_engine_run_for(session->engine, duration);
}

bm_status_t
bm_session_pause(bm_session_t *session)
{
    if (session == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    if (session->state != BM_SESSION_RUNNING)
        return BM_STATUS_INVALID_STATE;
    session->state = BM_SESSION_PAUSED;
    return BM_STATUS_OK;
}

bm_status_t
bm_session_resume(bm_session_t *session)
{
    if (session == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    if (session->state != BM_SESSION_PAUSED)
        return BM_STATUS_INVALID_STATE;
    session->state = BM_SESSION_RUNNING;
    return BM_STATUS_OK;
}

bm_status_t
bm_session_reset(bm_session_t *session)
{
    if (session == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    if ((session->state != BM_SESSION_RUNNING) && (session->state != BM_SESSION_PAUSED))
        return BM_STATUS_INVALID_STATE;
    return bm_engine_reset(session->engine);
}

bm_status_t
bm_session_stop(bm_session_t *session)
{
    if (session == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    if ((session->state != BM_SESSION_RUNNING) && (session->state != BM_SESSION_PAUSED))
        return BM_STATUS_INVALID_STATE;
    bm_engine_destroy(session->engine);
    session->engine = NULL;
    session->configuration.ops.destroy(session->machine);
    session->machine = NULL;
    session->state = BM_SESSION_STOPPED;
    return BM_STATUS_OK;
}

void
bm_session_destroy(bm_session_t *session)
{
    if (session == NULL)
        return;
    if ((session->state == BM_SESSION_RUNNING) || (session->state == BM_SESSION_PAUSED))
        (void) bm_session_stop(session);
    session->host.release(session->host.context, session);
}

bm_session_state_t
bm_session_state(const bm_session_t *session)
{
    return (session == NULL) ? BM_SESSION_STOPPED : session->state;
}

bm_tick_t
bm_session_time(const bm_session_t *session)
{
    return ((session == NULL) || (session->engine == NULL)) ? 0 : bm_engine_now(session->engine);
}

bm_status_t
bm_session_inspect_cpu(const bm_session_t *session, bm_cpu_id_t id, const char *name, uint64_t *value)
{
    if ((session == NULL) || (session->engine == NULL))
        return BM_STATUS_INVALID_STATE;
    return bm_engine_inspect_cpu(session->engine, id, name, value);
}
