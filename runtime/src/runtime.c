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
bm_machine_definition_validate(const bm_machine_definition_t *definition)
{
    if ((definition == NULL) || (definition->id == NULL) ||
        (definition->id[0] == '\0') ||
        (definition->scheduler_ticks_per_second == 0U) ||
        (definition->configuration.type == NULL) ||
        (definition->configuration.type[0] == '\0') ||
        (definition->configuration.version == 0U) ||
        (definition->configuration.size == 0U) ||
        (definition->ops.create == NULL) ||
        (definition->ops.destroy == NULL) ||
        (definition->engine.max_cpus == 0U) ||
        (definition->engine.max_events == 0U))
        return BM_STATUS_INVALID_ARGUMENT;
    return BM_STATUS_OK;
}

bm_status_t
bm_machine_config_validate(const bm_machine_config_t *configuration)
{
    const bm_machine_definition_t *definition;

    if ((configuration == NULL) ||
        (bm_machine_definition_validate(configuration->definition) !=
         BM_STATUS_OK))
        return BM_STATUS_INVALID_ARGUMENT;
    definition = configuration->definition;
    if ((configuration->configuration.type == NULL) ||
        (configuration->configuration.data == NULL) ||
        (strcmp(configuration->configuration.type,
                definition->configuration.type) != 0) ||
        (configuration->configuration.version !=
         definition->configuration.version) ||
        (configuration->configuration.size != definition->configuration.size))
        return BM_STATUS_INVALID_ARGUMENT;
    return BM_STATUS_OK;
}

bm_status_t
bm_session_configure(bm_session_t *session, const bm_machine_config_t *configuration)
{
    const bm_machine_definition_t *definition;
    bm_status_t status;

    if ((session == NULL) ||
        (bm_machine_config_validate(configuration) != BM_STATUS_OK))
        return BM_STATUS_INVALID_ARGUMENT;
    definition = configuration->definition;
    if ((session->state != BM_SESSION_NEW) && (session->state != BM_SESSION_STOPPED) &&
        (session->state != BM_SESSION_CONFIGURED))
        return BM_STATUS_INVALID_STATE;
    if (definition->ops.validate != NULL) {
        status = definition->ops.validate(&configuration->configuration);
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
    status = bm_engine_create(&session->host,
                              &session->configuration.definition->engine,
                              &session->engine);
    if (status != BM_STATUS_OK)
        return status;
    status = session->configuration.definition->ops.create(
        session->engine, &session->host, &session->configuration.configuration,
        &session->machine);
    if ((status == BM_STATUS_OK) && (session->machine == NULL))
        status = BM_STATUS_DEVICE_ERROR;
    if (status == BM_STATUS_OK)
        status = bm_engine_reset(session->engine);
    if ((status == BM_STATUS_OK) &&
        (session->configuration.definition->ops.reset != NULL))
        status = session->configuration.definition->ops.reset(session->machine);
    if (status != BM_STATUS_OK) {
        bm_engine_destroy(session->engine);
        session->engine = NULL;
        if (session->machine != NULL)
            session->configuration.definition->ops.destroy(session->machine);
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
    bm_status_t status;

    if (session == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    if ((session->state != BM_SESSION_RUNNING) && (session->state != BM_SESSION_PAUSED))
        return BM_STATUS_INVALID_STATE;
    status = bm_engine_reset(session->engine);
    if ((status == BM_STATUS_OK) &&
        (session->configuration.definition->ops.reset != NULL))
        status = session->configuration.definition->ops.reset(session->machine);
    return status;
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
    session->configuration.definition->ops.destroy(session->machine);
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
    if ((session == NULL) || (name == NULL) || (value == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    if (session->engine == NULL)
        return BM_STATUS_INVALID_STATE;
    return bm_engine_inspect_cpu(session->engine, id, name, value);
}

bm_status_t
bm_session_video_geometry(const bm_session_t *session, bm_video_geometry_t *geometry)
{
    if ((session == NULL) || (geometry == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    if ((session->state != BM_SESSION_RUNNING) && (session->state != BM_SESSION_PAUSED))
        return BM_STATUS_INVALID_STATE;
    if ((session->machine == NULL) ||
        (session->configuration.definition->ops.video_geometry == NULL))
        return BM_STATUS_UNSUPPORTED;
    return session->configuration.definition->ops.video_geometry(session->machine,
                                                                  geometry);
}

bm_status_t
bm_session_render_video(const bm_session_t *session, bm_video_framebuffer_t *framebuffer)
{
    if ((session == NULL) || (framebuffer == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    if ((session->state != BM_SESSION_RUNNING) && (session->state != BM_SESSION_PAUSED))
        return BM_STATUS_INVALID_STATE;
    if ((session->machine == NULL) ||
        (session->configuration.definition->ops.video_render == NULL))
        return BM_STATUS_UNSUPPORTED;
    return session->configuration.definition->ops.video_render(session->machine,
                                                                bm_engine_now(session->engine),
                                                                framebuffer);
}

bm_status_t
bm_session_inspect_machine(const bm_session_t *session,
                           const char *name,
                           uint64_t *value)
{
    if ((session == NULL) || (name == NULL) || (value == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    if ((session->state != BM_SESSION_RUNNING) && (session->state != BM_SESSION_PAUSED))
        return BM_STATUS_INVALID_STATE;
    if ((session->machine == NULL) ||
        (session->configuration.definition->ops.inspect == NULL))
        return BM_STATUS_UNSUPPORTED;
    return session->configuration.definition->ops.inspect(session->machine,
                                                           name, value);
}

bm_status_t
bm_session_send_input(bm_session_t *session, const bm_input_event_t *event)
{
    if ((session == NULL) || (event == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    if ((session->state != BM_SESSION_RUNNING) &&
        (session->state != BM_SESSION_PAUSED))
        return BM_STATUS_INVALID_STATE;
    if ((session->machine == NULL) ||
        (session->configuration.definition->ops.input == NULL))
        return BM_STATUS_UNSUPPORTED;
    return session->configuration.definition->ops.input(session->machine, event);
}

bm_status_t
bm_session_keyboard_leds(const bm_session_t *session,
                         bm_keyboard_led_state_t *state)
{
    if ((session == NULL) || (state == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    if ((session->state != BM_SESSION_RUNNING) &&
        (session->state != BM_SESSION_PAUSED))
        return BM_STATUS_INVALID_STATE;
    if ((session->machine == NULL) ||
        (session->configuration.definition->ops.keyboard_leds == NULL))
        return BM_STATUS_UNSUPPORTED;
    return session->configuration.definition->ops.keyboard_leds(
        session->machine, state);
}

bm_status_t
bm_session_storage_device_count(const bm_session_t *session, size_t *count)
{
    if ((session == NULL) || (count == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    if ((session->state != BM_SESSION_RUNNING) &&
        (session->state != BM_SESSION_PAUSED))
        return BM_STATUS_INVALID_STATE;
    if ((session->machine == NULL) ||
        (session->configuration.definition->ops.storage_count == NULL))
        return BM_STATUS_UNSUPPORTED;
    *count = session->configuration.definition->ops.storage_count(
        session->machine);
    return BM_STATUS_OK;
}

bm_status_t
bm_session_storage_device_status(const bm_session_t *session, size_t index,
                                 bm_storage_device_status_t *status)
{
    size_t count;
    bm_status_t result;

    if ((session == NULL) || (status == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    result = bm_session_storage_device_count(session, &count);
    if (result != BM_STATUS_OK)
        return result;
    if (index >= count)
        return BM_STATUS_INVALID_ARGUMENT;
    if (session->configuration.definition->ops.storage_status == NULL)
        return BM_STATUS_UNSUPPORTED;
    return session->configuration.definition->ops.storage_status(
        session->machine, index, status);
}

bm_status_t
bm_session_replace_storage_media(bm_session_t *session,
                                 bm_storage_device_kind_t kind,
                                 uint32_t unit,
                                 const bm_storage_media_change_t *change)
{
    if ((session == NULL) || (change == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    if ((session->state != BM_SESSION_RUNNING) &&
        (session->state != BM_SESSION_PAUSED))
        return BM_STATUS_INVALID_STATE;
    if ((session->machine == NULL) ||
        (session->configuration.definition->ops.storage_media == NULL))
        return BM_STATUS_UNSUPPORTED;
    if (change->media_present &&
        (bm_block_media_validate(&change->media) != BM_STATUS_OK))
        return BM_STATUS_INVALID_ARGUMENT;
    return session->configuration.definition->ops.storage_media(
        session->machine, kind, unit, change);
}
