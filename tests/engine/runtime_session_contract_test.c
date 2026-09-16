/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/platforms/null_host.h>
#include <blumach/runtime/runtime.h>

#include <assert.h>
#include <stdint.h>
#include <string.h>

typedef struct test_machine {
    bm_engine_t *engine;
    bm_status_t validate_status;
    bm_status_t create_status;
    bm_status_t reset_status;
    bm_status_t inspect_status;
    bm_status_t input_status;
    bm_status_t video_status;
    int return_machine;
    unsigned int validate_calls;
    unsigned int create_calls;
    unsigned int destroy_calls;
    unsigned int reset_calls;
    unsigned int inspect_calls;
    unsigned int input_calls;
    unsigned int geometry_calls;
    unsigned int render_calls;
    bm_input_event_t last_input;
} test_machine_t;

static void
initialize_machine(test_machine_t *machine)
{
    memset(machine, 0, sizeof(*machine));
    machine->return_machine = 1;
}

static bm_status_t
test_validate(const void *configuration)
{
    test_machine_t *machine = (test_machine_t *) configuration;

    if (machine == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    ++machine->validate_calls;
    return machine->validate_status;
}

static bm_status_t
test_create(bm_engine_t *engine,
            const bm_host_services_t *host,
            const void *configuration,
            void **out_machine)
{
    test_machine_t *machine = (test_machine_t *) configuration;

    assert(engine != NULL);
    assert(host != NULL);
    assert(machine != NULL);
    assert(out_machine != NULL);
    ++machine->create_calls;
    machine->engine = engine;
    *out_machine = machine->return_machine ? machine : NULL;
    return machine->create_status;
}

static void
test_destroy(void *context)
{
    test_machine_t *machine = context;

    assert(machine != NULL);
    ++machine->destroy_calls;
    machine->engine = NULL;
}

static bm_status_t
test_reset(void *context)
{
    test_machine_t *machine = context;

    assert(machine != NULL);
    ++machine->reset_calls;
    return machine->reset_status;
}

static bm_status_t
test_inspect(const void *context, const char *name, uint64_t *value)
{
    test_machine_t *machine = (test_machine_t *) context;

    assert(machine != NULL);
    ++machine->inspect_calls;
    if (machine->inspect_status != BM_STATUS_OK)
        return machine->inspect_status;
    if (strcmp(name, "answer") != 0)
        return BM_STATUS_INVALID_ARGUMENT;
    *value = 42U;
    return BM_STATUS_OK;
}

static bm_status_t
test_input(void *context, const bm_input_event_t *event)
{
    test_machine_t *machine = context;

    assert(machine != NULL);
    ++machine->input_calls;
    machine->last_input = *event;
    return machine->input_status;
}

static bm_status_t
test_video_geometry(const void *context, bm_video_geometry_t *geometry)
{
    test_machine_t *machine = (test_machine_t *) context;

    assert(machine != NULL);
    ++machine->geometry_calls;
    if (machine->video_status != BM_STATUS_OK)
        return machine->video_status;
    geometry->width = 320U;
    geometry->height = 200U;
    geometry->format = BM_PIXEL_XRGB8888;
    return BM_STATUS_OK;
}

static bm_status_t
test_video_render(const void *context, bm_video_framebuffer_t *framebuffer)
{
    test_machine_t *machine = (test_machine_t *) context;

    assert(machine != NULL);
    ++machine->render_calls;
    if (machine->video_status != BM_STATUS_OK)
        return machine->video_status;
    framebuffer->geometry.width = 320U;
    framebuffer->geometry.height = 200U;
    framebuffer->geometry.format = BM_PIXEL_XRGB8888;
    return BM_STATUS_OK;
}

static bm_machine_config_t
make_configuration(test_machine_t *machine, int optional_operations)
{
    bm_machine_config_t configuration = {
        "test.runtime-session",
        machine,
        {
            test_validate,
            test_create,
            test_destroy,
            optional_operations ? test_video_geometry : NULL,
            optional_operations ? test_video_render : NULL,
            optional_operations ? test_reset : NULL,
            optional_operations ? test_inspect : NULL,
            optional_operations ? test_input : NULL
        },
        { 1U, 2U }
    };
    return configuration;
}

static void
test_null_and_configuration_contracts(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_host_services_t invalid_host = host;
    bm_session_t *session = NULL;
    test_machine_t first_machine;
    test_machine_t rejected_machine;
    bm_machine_config_t valid;
    bm_machine_config_t invalid;
    bm_machine_config_t rejected;
    uint64_t value = 0U;

    initialize_machine(&first_machine);
    initialize_machine(&rejected_machine);
    valid = make_configuration(&first_machine, 1);

    assert(bm_session_create(NULL, &session) == BM_STATUS_INVALID_ARGUMENT);
    invalid_host.allocate = NULL;
    assert(bm_session_create(&invalid_host, &session) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_session_create(&host, NULL) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_session_create(&host, &session) == BM_STATUS_OK);
    assert(session != NULL);
    assert(bm_session_state(session) == BM_SESSION_NEW);
    assert(bm_session_time(session) == 0U);

    assert(bm_session_configure(NULL, &valid) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_session_configure(session, NULL) == BM_STATUS_INVALID_ARGUMENT);
    invalid = valid;
    invalid.definition = NULL;
    assert(bm_session_configure(session, &invalid) == BM_STATUS_INVALID_ARGUMENT);
    invalid = valid;
    invalid.ops.create = NULL;
    assert(bm_session_configure(session, &invalid) == BM_STATUS_INVALID_ARGUMENT);
    invalid = valid;
    invalid.ops.destroy = NULL;
    assert(bm_session_configure(session, &invalid) == BM_STATUS_INVALID_ARGUMENT);
    invalid = valid;
    invalid.engine.max_events = 0U;
    assert(bm_session_configure(session, &invalid) == BM_STATUS_INVALID_ARGUMENT);

    assert(bm_session_configure(session, &valid) == BM_STATUS_OK);
    assert(bm_session_state(session) == BM_SESSION_CONFIGURED);
    assert(first_machine.validate_calls == 1U);

    rejected = make_configuration(&rejected_machine, 1);
    rejected_machine.validate_status = BM_STATUS_UNSUPPORTED;
    assert(bm_session_configure(session, &rejected) == BM_STATUS_UNSUPPORTED);
    assert(bm_session_state(session) == BM_SESSION_CONFIGURED);
    assert(bm_session_start(session) == BM_STATUS_OK);
    assert(first_machine.create_calls == 1U);
    assert(rejected_machine.create_calls == 0U);

    assert(bm_session_inspect_cpu(NULL, 0U, "x", &value) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_session_inspect_cpu(session, 0U, NULL, &value) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_session_inspect_cpu(session, 0U, "x", NULL) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_session_inspect_cpu(session, 0U, "x", &value) ==
           BM_STATUS_INVALID_ARGUMENT);

    assert(bm_session_stop(session) == BM_STATUS_OK);
    bm_session_destroy(session);
    assert(first_machine.destroy_calls == 1U);

    assert(bm_session_state(NULL) == BM_SESSION_STOPPED);
    assert(bm_session_time(NULL) == 0U);
    assert(bm_session_start(NULL) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_session_run_for(NULL, 1U) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_session_pause(NULL) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_session_resume(NULL) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_session_reset(NULL) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_session_stop(NULL) == BM_STATUS_INVALID_ARGUMENT);
    bm_session_destroy(NULL);
}

static void
test_session_state_machine(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_session_t *session = NULL;
    test_machine_t machine;
    bm_machine_config_t configuration;
    bm_input_event_t input = { BM_INPUT_KEY, BM_KEY_A, 1, 0 };
    bm_video_geometry_t geometry = { 0U, 0U, BM_PIXEL_XRGB8888 };
    bm_video_framebuffer_t framebuffer = { NULL, 0U, 0U,
                                           { 0U, 0U, BM_PIXEL_XRGB8888 } };
    uint64_t value = 0U;

    initialize_machine(&machine);
    configuration = make_configuration(&machine, 1);
    assert(bm_session_create(&host, &session) == BM_STATUS_OK);

    assert(bm_session_start(session) == BM_STATUS_INVALID_STATE);
    assert(bm_session_run_for(session, 1U) == BM_STATUS_INVALID_STATE);
    assert(bm_session_pause(session) == BM_STATUS_INVALID_STATE);
    assert(bm_session_resume(session) == BM_STATUS_INVALID_STATE);
    assert(bm_session_reset(session) == BM_STATUS_INVALID_STATE);
    assert(bm_session_stop(session) == BM_STATUS_INVALID_STATE);
    assert(bm_session_inspect_cpu(session, 0U, "x", &value) ==
           BM_STATUS_INVALID_STATE);
    assert(bm_session_video_geometry(session, &geometry) ==
           BM_STATUS_INVALID_STATE);
    assert(bm_session_render_video(session, &framebuffer) ==
           BM_STATUS_INVALID_STATE);
    assert(bm_session_inspect_machine(session, "answer", &value) ==
           BM_STATUS_INVALID_STATE);
    assert(bm_session_send_input(session, &input) == BM_STATUS_INVALID_STATE);

    assert(bm_session_configure(session, &configuration) == BM_STATUS_OK);
    assert(bm_session_state(session) == BM_SESSION_CONFIGURED);
    assert(bm_session_start(session) == BM_STATUS_OK);
    assert(bm_session_state(session) == BM_SESSION_RUNNING);
    assert(machine.create_calls == 1U);
    assert(machine.reset_calls == 1U);
    assert(bm_session_start(session) == BM_STATUS_INVALID_STATE);
    assert(bm_session_configure(session, &configuration) ==
           BM_STATUS_INVALID_STATE);

    assert(bm_session_run_for(session, 5U) == BM_STATUS_OK);
    assert(bm_session_time(session) == 5U);
    assert(bm_session_video_geometry(session, &geometry) == BM_STATUS_OK);
    assert(geometry.width == 320U && geometry.height == 200U);
    assert(bm_session_render_video(session, &framebuffer) == BM_STATUS_OK);
    assert(framebuffer.geometry.width == 320U);
    assert(bm_session_inspect_machine(session, "answer", &value) == BM_STATUS_OK);
    assert(value == 42U);
    assert(bm_session_send_input(session, &input) == BM_STATUS_OK);
    assert(machine.last_input.key == BM_KEY_A);

    assert(bm_session_pause(session) == BM_STATUS_OK);
    assert(bm_session_state(session) == BM_SESSION_PAUSED);
    assert(bm_session_pause(session) == BM_STATUS_INVALID_STATE);
    assert(bm_session_run_for(session, 1U) == BM_STATUS_INVALID_STATE);
    assert(bm_session_send_input(session, &input) == BM_STATUS_OK);
    assert(bm_session_inspect_machine(session, "answer", &value) == BM_STATUS_OK);
    assert(bm_session_reset(session) == BM_STATUS_OK);
    assert(bm_session_state(session) == BM_SESSION_PAUSED);
    assert(bm_session_time(session) == 0U);
    assert(machine.reset_calls == 2U);
    assert(bm_session_resume(session) == BM_STATUS_OK);
    assert(bm_session_state(session) == BM_SESSION_RUNNING);
    assert(bm_session_resume(session) == BM_STATUS_INVALID_STATE);

    assert(bm_session_stop(session) == BM_STATUS_OK);
    assert(bm_session_state(session) == BM_SESSION_STOPPED);
    assert(bm_session_time(session) == 0U);
    assert(machine.destroy_calls == 1U);
    assert(bm_session_stop(session) == BM_STATUS_INVALID_STATE);
    assert(bm_session_start(session) == BM_STATUS_INVALID_STATE);

    assert(bm_session_configure(session, &configuration) == BM_STATUS_OK);
    assert(bm_session_start(session) == BM_STATUS_OK);
    bm_session_destroy(session);
    assert(machine.create_calls == 2U);
    assert(machine.destroy_calls == 2U);
}

static void
test_optional_operations(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_session_t *session = NULL;
    test_machine_t machine;
    bm_machine_config_t configuration;
    bm_input_event_t input = { BM_INPUT_KEY, BM_KEY_A, 1, 0 };
    bm_video_geometry_t geometry = { 0U, 0U, BM_PIXEL_XRGB8888 };
    bm_video_framebuffer_t framebuffer = { NULL, 0U, 0U,
                                           { 0U, 0U, BM_PIXEL_XRGB8888 } };
    uint64_t value = 0U;

    initialize_machine(&machine);
    configuration = make_configuration(&machine, 0);
    assert(bm_session_create(&host, &session) == BM_STATUS_OK);
    assert(bm_session_configure(session, &configuration) == BM_STATUS_OK);
    assert(bm_session_start(session) == BM_STATUS_OK);
    assert(bm_session_video_geometry(session, &geometry) == BM_STATUS_UNSUPPORTED);
    assert(bm_session_render_video(session, &framebuffer) == BM_STATUS_UNSUPPORTED);
    assert(bm_session_inspect_machine(session, "answer", &value) ==
           BM_STATUS_UNSUPPORTED);
    assert(bm_session_send_input(session, &input) == BM_STATUS_UNSUPPORTED);
    assert(bm_session_reset(session) == BM_STATUS_OK);

    assert(bm_session_video_geometry(NULL, &geometry) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_session_video_geometry(session, NULL) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_session_render_video(NULL, &framebuffer) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_session_render_video(session, NULL) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_session_inspect_machine(NULL, "x", &value) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_session_inspect_machine(session, NULL, &value) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_session_inspect_machine(session, "x", NULL) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_session_send_input(NULL, &input) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_session_send_input(session, NULL) == BM_STATUS_INVALID_ARGUMENT);

    assert(bm_session_stop(session) == BM_STATUS_OK);
    bm_session_destroy(session);
}

static void
test_start_failure_cleanup_and_retry(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_session_t *session = NULL;
    test_machine_t machine;
    bm_machine_config_t configuration;

    initialize_machine(&machine);
    configuration = make_configuration(&machine, 1);
    assert(bm_session_create(&host, &session) == BM_STATUS_OK);
    assert(bm_session_configure(session, &configuration) == BM_STATUS_OK);

    machine.create_status = BM_STATUS_DEVICE_ERROR;
    assert(bm_session_start(session) == BM_STATUS_DEVICE_ERROR);
    assert(bm_session_state(session) == BM_SESSION_CONFIGURED);
    assert(machine.create_calls == 1U);
    assert(machine.destroy_calls == 1U);
    assert(machine.engine == NULL);

    machine.create_status = BM_STATUS_OK;
    machine.return_machine = 0;
    assert(bm_session_start(session) == BM_STATUS_DEVICE_ERROR);
    assert(bm_session_state(session) == BM_SESSION_CONFIGURED);
    assert(machine.create_calls == 2U);
    assert(machine.destroy_calls == 1U);

    machine.return_machine = 1;
    machine.reset_status = BM_STATUS_UNSUPPORTED;
    assert(bm_session_start(session) == BM_STATUS_UNSUPPORTED);
    assert(bm_session_state(session) == BM_SESSION_CONFIGURED);
    assert(machine.create_calls == 3U);
    assert(machine.destroy_calls == 2U);
    assert(machine.engine == NULL);

    machine.reset_status = BM_STATUS_OK;
    assert(bm_session_start(session) == BM_STATUS_OK);
    assert(bm_session_state(session) == BM_SESSION_RUNNING);
    assert(machine.create_calls == 4U);
    assert(bm_session_stop(session) == BM_STATUS_OK);
    assert(machine.destroy_calls == 3U);
    bm_session_destroy(session);
}

int
main(void)
{
    test_null_and_configuration_contracts();
    test_session_state_machine();
    test_optional_operations();
    test_start_failure_cleanup_and_retry();
    return 0;
}
