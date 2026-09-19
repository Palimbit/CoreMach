/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/platforms/null_host.h>
#include <blumach/runtime/runtime.h>

#include "failure_injection_host.h"

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
    unsigned int keyboard_led_calls;
    unsigned int geometry_calls;
    unsigned int render_calls;
    unsigned int storage_status_calls;
    unsigned int storage_media_calls;
    int install_timed_source;
    unsigned int timed_source_fire_calls;
    bm_time_point_t timed_source_when;
    bm_storage_media_change_t last_media_change;
    bm_tick_t last_render_time;
    bm_input_event_t last_input;
    bm_keyboard_led_state_t keyboard_leds;
} test_machine_t;

static bm_status_t
test_timed_source_fire(bm_engine_t *engine, void *context,
                       const bm_time_point_t *when,
                       uint64_t *cycles_until_next)
{
    test_machine_t *machine = context;

    assert(engine != NULL);
    assert(machine != NULL);
    assert(when != NULL);
    assert(cycles_until_next != NULL);
    ++machine->timed_source_fire_calls;
    machine->timed_source_when = *when;
    *cycles_until_next = 0U;
    return BM_STATUS_IDLE;
}

static size_t
test_storage_count(const void *context)
{
    return context != NULL ? 1U : 0U;
}

static bm_status_t
test_storage_status(const void *context, size_t index,
                    bm_storage_device_status_t *status)
{
    test_machine_t *machine = (test_machine_t *) context;
    if ((machine == NULL) || (status == NULL) || (index != 0U))
        return BM_STATUS_INVALID_ARGUMENT;
    ++machine->storage_status_calls;
    *status = (bm_storage_device_status_t) {
        BM_STORAGE_DEVICE_FLOPPY, 0U, 1, 1, 1, 1, 7U, 0U
    };
    return BM_STATUS_OK;
}

static bm_status_t
test_storage_media(void *context, bm_storage_device_kind_t kind, uint32_t unit,
                   const bm_storage_media_change_t *change)
{
    test_machine_t *machine = context;
    if ((machine == NULL) || (kind != BM_STORAGE_DEVICE_FLOPPY) ||
        (unit != 0U) || (change == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    ++machine->storage_media_calls;
    machine->last_media_change = *change;
    return BM_STATUS_OK;
}

static bm_status_t
test_keyboard_leds(const void *context, bm_keyboard_led_state_t *state)
{
    test_machine_t *machine = (test_machine_t *) context;
    if ((machine == NULL) || (state == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    ++machine->keyboard_led_calls;
    *state = machine->keyboard_leds;
    return BM_STATUS_OK;
}

static bm_status_t
test_persistent_state_size(const void *context, const char *name, size_t *size)
{
    if ((context == NULL) || (name == NULL) || (size == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    if (strcmp(name, "nv") != 0)
        return BM_STATUS_UNSUPPORTED;
    *size = 4U;
    return BM_STATUS_OK;
}

static bm_status_t
test_save_persistent_state(const void *context, const char *name,
                           uint8_t *data, size_t size)
{
    static const uint8_t expected[] = { 1U, 2U, 3U, 4U };
    if ((context == NULL) || (name == NULL) || (data == NULL) ||
        (strcmp(name, "nv") != 0) || (size != sizeof(expected)))
        return BM_STATUS_INVALID_ARGUMENT;
    memcpy(data, expected, sizeof(expected));
    return BM_STATUS_OK;
}

static bm_status_t
test_media_read(void *context, uint64_t first_block, uint32_t block_count,
                uint8_t *destination)
{
    (void) context;
    (void) first_block;
    memset(destination, 0, block_count);
    return BM_STATUS_OK;
}

static void
initialize_machine(test_machine_t *machine)
{
    memset(machine, 0, sizeof(*machine));
    machine->return_machine = 1;
}

static bm_status_t
test_validate(const bm_configuration_view_t *configuration)
{
    test_machine_t *machine;

    if ((configuration == NULL) || (configuration->type == NULL) ||
        (strcmp(configuration->type, "test.runtime-session.config") != 0) ||
        (configuration->version != 1U) ||
        (configuration->size != sizeof(test_machine_t)) ||
        (configuration->data == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    machine = (test_machine_t *) configuration->data;
    ++machine->validate_calls;
    return machine->validate_status;
}

static bm_status_t
test_create(bm_engine_t *engine,
            const bm_host_services_t *host,
            const bm_configuration_view_t *configuration,
            void **out_machine)
{
    static const bm_clock_rate_t one_ghz = {
        UINT64_C(1000000000), 1U
    };
    test_machine_t *machine;
    bm_status_t status;

    assert(engine != NULL);
    assert(host != NULL);
    assert(configuration != NULL);
    assert(configuration->data != NULL);
    assert(out_machine != NULL);
    machine = (test_machine_t *) configuration->data;
    ++machine->create_calls;
    if (machine->install_timed_source) {
        status = bm_engine_add_timed_source(
            engine, test_timed_source_fire, machine, &one_ghz, 2U, NULL);
        if (status != BM_STATUS_OK)
            return status;
    }
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
    geometry->refresh_numerator = 60U;
    geometry->refresh_denominator = 1U;
    return BM_STATUS_OK;
}

static bm_status_t
test_video_render(const void *context, bm_tick_t emulated_time,
                  bm_video_framebuffer_t *framebuffer)
{
    test_machine_t *machine = (test_machine_t *) context;

    assert(machine != NULL);
    ++machine->render_calls;
    machine->last_render_time = emulated_time;
    if (machine->video_status != BM_STATUS_OK)
        return machine->video_status;
    framebuffer->geometry.width = 320U;
    framebuffer->geometry.height = 200U;
    framebuffer->geometry.format = BM_PIXEL_XRGB8888;
    framebuffer->geometry.refresh_numerator = 60U;
    framebuffer->geometry.refresh_denominator = 1U;
    return BM_STATUS_OK;
}

static bm_machine_config_t
make_configuration(test_machine_t *machine, int optional_operations)
{
    static const bm_machine_definition_t full_definition = {
        .id = "test.runtime-session",
        .scheduler_ticks_per_second = 1000000U,
        .configuration = { "test.runtime-session.config", 1U,
                           sizeof(test_machine_t) },
        .ops = {
            test_validate,
            test_create,
            test_destroy,
            test_video_geometry,
            test_video_render,
            test_reset,
            test_inspect,
            test_input,
            test_storage_count,
            test_storage_status,
            test_storage_media,
            test_keyboard_leds,
            test_persistent_state_size,
            test_save_persistent_state
        },
        .engine = { 1U, 2U, 0U }
    };
    static const bm_machine_definition_t required_definition = {
        .id = "test.runtime-session.required-only",
        .scheduler_ticks_per_second = 1000000U,
        .configuration = { "test.runtime-session.config", 1U,
                           sizeof(test_machine_t) },
        .ops = { test_validate, test_create, test_destroy,
                 NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                 NULL },
        .engine = { 1U, 2U, 0U }
    };
    bm_machine_config_t configuration = {
        .definition = optional_operations ? &full_definition : &required_definition,
        .configuration = { "test.runtime-session.config", 1U,
                           sizeof(*machine), machine }
    };
    return configuration;
}

static void
test_session_allocation_failure(void)
{
    failure_injection_host_t tracker;
    bm_host_services_t host;
    bm_session_t *session = NULL;

    failure_injection_host_initialize(&tracker);
    failure_injection_host_fail_on(&tracker, 0U);
    host = failure_injection_host_services(&tracker);
    assert(bm_session_create(&host, &session) == BM_STATUS_OUT_OF_MEMORY);
    assert(session == NULL);
    assert(tracker.outstanding_allocations == 0U);
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
    bm_machine_definition_t invalid_definition;
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

    invalid_definition = *valid.definition;
    invalid = valid;
    invalid.definition = &invalid_definition;
    invalid_definition.id = NULL;
    assert(bm_session_configure(session, &invalid) == BM_STATUS_INVALID_ARGUMENT);
    invalid_definition = *valid.definition;
    invalid_definition.configuration.type = NULL;
    assert(bm_session_configure(session, &invalid) == BM_STATUS_INVALID_ARGUMENT);
    invalid_definition = *valid.definition;
    invalid_definition.configuration.version = 0U;
    assert(bm_session_configure(session, &invalid) == BM_STATUS_INVALID_ARGUMENT);
    invalid_definition = *valid.definition;
    invalid_definition.configuration.size = 0U;
    assert(bm_session_configure(session, &invalid) == BM_STATUS_INVALID_ARGUMENT);
    invalid_definition = *valid.definition;
    invalid_definition.ops.create = NULL;
    assert(bm_session_configure(session, &invalid) == BM_STATUS_INVALID_ARGUMENT);
    invalid_definition = *valid.definition;
    invalid_definition.ops.destroy = NULL;
    assert(bm_session_configure(session, &invalid) == BM_STATUS_INVALID_ARGUMENT);
    invalid_definition = *valid.definition;
    invalid_definition.engine.max_events = 0U;
    assert(bm_session_configure(session, &invalid) == BM_STATUS_INVALID_ARGUMENT);

    invalid = valid;
    invalid.configuration.type = "test.wrong-config";
    assert(bm_session_configure(session, &invalid) == BM_STATUS_INVALID_ARGUMENT);
    invalid = valid;
    invalid.configuration.version = 2U;
    assert(bm_session_configure(session, &invalid) == BM_STATUS_INVALID_ARGUMENT);
    invalid = valid;
    invalid.configuration.size -= 1U;
    assert(bm_session_configure(session, &invalid) == BM_STATUS_INVALID_ARGUMENT);
    invalid = valid;
    invalid.configuration.data = NULL;
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
    bm_input_event_t input = {
        .kind = BM_INPUT_KEY, .key = BM_KEY_A, .pressed = 1
    };
    bm_video_geometry_t geometry = { 0U, 0U, BM_PIXEL_XRGB8888, 0U, 0U };
    bm_video_framebuffer_t framebuffer = { NULL, 0U, 0U,
                                           { 0U, 0U, BM_PIXEL_XRGB8888,
                                             0U, 0U } };
    bm_storage_device_status_t storage;
    bm_storage_media_change_t media_change = {
        1, 1, { NULL, 1U, 1U, 1, test_media_read, NULL }
    };
    bm_keyboard_led_state_t keyboard_leds = { 0U };
    size_t storage_count = 0U;
    size_t persistent_state_size = 0U;
    uint8_t persistent_state[4] = { 0U };
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
    assert(bm_session_keyboard_leds(session, &keyboard_leds) ==
           BM_STATUS_INVALID_STATE);
    assert(bm_session_storage_device_count(session, &storage_count) ==
           BM_STATUS_INVALID_STATE);
    assert(bm_session_storage_device_status(session, 0U, &storage) ==
           BM_STATUS_INVALID_STATE);
    assert(bm_session_replace_storage_media(
               session, BM_STORAGE_DEVICE_FLOPPY, 0U, &media_change) ==
           BM_STATUS_INVALID_STATE);
    assert(bm_session_persistent_state_size(
               session, "nv", &persistent_state_size) ==
           BM_STATUS_INVALID_STATE);

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
    assert(geometry.refresh_numerator == 60U &&
           geometry.refresh_denominator == 1U);
    assert(bm_session_render_video(session, &framebuffer) == BM_STATUS_OK);
    assert(framebuffer.geometry.width == 320U);
    assert(machine.last_render_time == 5U);
    assert(bm_session_inspect_machine(session, "answer", &value) == BM_STATUS_OK);
    assert(value == 42U);
    assert(bm_session_send_input(session, &input) == BM_STATUS_OK);
    assert(machine.last_input.key == BM_KEY_A);
    machine.keyboard_leds.indicators = BM_KEYBOARD_LED_CAPS_LOCK |
                                       BM_KEYBOARD_LED_NUM_LOCK;
    assert(bm_session_keyboard_leds(session, &keyboard_leds) == BM_STATUS_OK);
    assert(keyboard_leds.indicators == (BM_KEYBOARD_LED_CAPS_LOCK |
                                        BM_KEYBOARD_LED_NUM_LOCK));
    assert(machine.keyboard_led_calls == 1U);
    assert(bm_session_storage_device_count(session, &storage_count) ==
           BM_STATUS_OK && storage_count == 1U);
    assert(bm_session_storage_device_status(session, 0U, &storage) ==
           BM_STATUS_OK);
    assert(storage.kind == BM_STORAGE_DEVICE_FLOPPY && storage.unit == 0U);
    assert(storage.media_present && storage.write_protected &&
           storage.motor_active && storage.read_operations == 7U);
    assert(machine.storage_status_calls == 1U);
    assert(bm_session_replace_storage_media(
               session, BM_STORAGE_DEVICE_FLOPPY, 0U, &media_change) ==
           BM_STATUS_OK);
    assert(machine.storage_media_calls == 1U);
    assert(machine.last_media_change.media_present);
    assert(bm_session_storage_device_status(session, 1U, &storage) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_session_persistent_state_size(
               session, "nv", &persistent_state_size) == BM_STATUS_OK);
    assert(persistent_state_size == sizeof(persistent_state));
    assert(bm_session_persistent_state_size(
               session, "unknown", &persistent_state_size) ==
           BM_STATUS_UNSUPPORTED);
    assert(bm_session_save_persistent_state(
               session, "nv", persistent_state,
               sizeof(persistent_state) - 1U) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_session_save_persistent_state(
               session, "nv", persistent_state,
               sizeof(persistent_state)) == BM_STATUS_OK);
    assert(persistent_state[0] == 1U && persistent_state[3] == 4U);

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
    bm_input_event_t input = {
        .kind = BM_INPUT_KEY, .key = BM_KEY_A, .pressed = 1
    };
    bm_video_geometry_t geometry = { 0U, 0U, BM_PIXEL_XRGB8888, 0U, 0U };
    bm_video_framebuffer_t framebuffer = { NULL, 0U, 0U,
                                           { 0U, 0U, BM_PIXEL_XRGB8888,
                                             0U, 0U } };
    bm_storage_device_status_t storage;
    bm_storage_media_change_t media_change = { 0 };
    bm_keyboard_led_state_t keyboard_leds = { 0U };
    size_t storage_count = 0U;
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
    assert(bm_session_keyboard_leds(session, &keyboard_leds) ==
           BM_STATUS_UNSUPPORTED);
    assert(bm_session_storage_device_count(session, &storage_count) ==
           BM_STATUS_UNSUPPORTED);
    assert(bm_session_storage_device_status(session, 0U, &storage) ==
           BM_STATUS_UNSUPPORTED);
    assert(bm_session_replace_storage_media(
               session, BM_STORAGE_DEVICE_FLOPPY, 0U, &media_change) ==
           BM_STATUS_UNSUPPORTED);
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
    assert(bm_session_keyboard_leds(NULL, &keyboard_leds) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_session_keyboard_leds(session, NULL) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_session_storage_device_count(NULL, &storage_count) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_session_storage_device_count(session, NULL) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_session_storage_device_status(NULL, 0U, &storage) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_session_storage_device_status(session, 0U, NULL) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_session_replace_storage_media(
               NULL, BM_STORAGE_DEVICE_FLOPPY, 0U, &media_change) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_session_replace_storage_media(
               session, BM_STORAGE_DEVICE_FLOPPY, 0U, NULL) ==
           BM_STATUS_INVALID_ARGUMENT);

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

static void
test_machine_engine_mode_selection(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_session_t *session = NULL;
    test_machine_t machine;
    bm_machine_config_t configuration;
    bm_machine_definition_t clocked_definition;

    initialize_machine(&machine);
    machine.install_timed_source = 1;
    configuration = make_configuration(&machine, 1);

    /* The zero/default mode deliberately remains the instruction-tick engine,
     * which rejects a clock-domain source. */
    assert(bm_session_create(&host, &session) == BM_STATUS_OK);
    assert(bm_session_configure(session, &configuration) == BM_STATUS_OK);
    assert(bm_session_start(session) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_session_state(session) == BM_SESSION_CONFIGURED);
    assert(machine.create_calls == 1U);
    assert(machine.engine == NULL);
    bm_session_destroy(session);

    initialize_machine(&machine);
    machine.install_timed_source = 1;
    configuration = make_configuration(&machine, 1);
    clocked_definition = *configuration.definition;
    clocked_definition.engine_mode = BM_MACHINE_ENGINE_CLOCKED;
    clocked_definition.scheduler_ticks_per_second =
        BM_MACHINE_CLOCKED_TICKS_PER_SECOND;
    clocked_definition.engine.max_timed_sources = 1U;
    configuration.definition = &clocked_definition;

    session = NULL;
    assert(bm_session_create(&host, &session) == BM_STATUS_OK);
    assert(bm_session_configure(session, &configuration) == BM_STATUS_OK);
    assert(bm_session_start(session) == BM_STATUS_OK);
    assert(machine.engine != NULL);
    assert(bm_session_run_for(session, 1U) == BM_STATUS_OK);
    assert(machine.timed_source_fire_calls == 0U);
    assert(bm_session_run_for(session, 1U) == BM_STATUS_OK);
    assert(machine.timed_source_fire_calls == 1U);
    assert(machine.timed_source_when.nanoseconds == 2U);
    assert(machine.timed_source_when.subnanosecond_numerator == 0U);
    assert(machine.timed_source_when.subnanosecond_denominator == 1U);
    assert(bm_session_stop(session) == BM_STATUS_OK);
    bm_session_destroy(session);
}

int
main(void)
{
    test_session_allocation_failure();
    test_null_and_configuration_contracts();
    test_session_state_machine();
    test_optional_operations();
    test_start_failure_cleanup_and_retry();
    test_machine_engine_mode_selection();
    return 0;
}
