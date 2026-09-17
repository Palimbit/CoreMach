/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_RUNTIME_RUNTIME_H
#define BLUMACH_RUNTIME_RUNTIME_H

#include <blumach/engine/engine.h>
#include <blumach/engine/input.h>
#include <blumach/engine/storage.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bm_session bm_session_t;
typedef struct bm_machine_registry bm_machine_registry_t;

typedef enum bm_session_state {
    BM_SESSION_NEW = 0,
    BM_SESSION_CONFIGURED,
    BM_SESSION_RUNNING,
    BM_SESSION_PAUSED,
    BM_SESSION_STOPPED
} bm_session_state_t;

typedef struct bm_configuration_view {
    const char *type;
    uint32_t version;
    size_t size;
    const void *data;
} bm_configuration_view_t;

typedef struct bm_configuration_contract {
    const char *type;
    uint32_t version;
    size_t size;
} bm_configuration_contract_t;

typedef bm_status_t (*bm_machine_validate_fn)(const bm_configuration_view_t *configuration);
typedef bm_status_t (*bm_machine_create_fn)(bm_engine_t *engine,
                                            const bm_host_services_t *host,
                                            const bm_configuration_view_t *configuration,
                                            void **out_machine);
typedef void (*bm_machine_destroy_fn)(void *machine);
typedef bm_status_t (*bm_machine_reset_fn)(void *machine);
typedef bm_status_t (*bm_machine_inspect_fn)(const void *machine,
                                             const char *name,
                                             uint64_t *value);
typedef bm_status_t (*bm_machine_input_fn)(void *machine,
                                          const bm_input_event_t *event);
typedef bm_status_t (*bm_machine_keyboard_leds_fn)(
    const void *machine, bm_keyboard_led_state_t *state);
typedef bm_status_t (*bm_machine_video_geometry_fn)(const void *machine,
                                                     bm_video_geometry_t *geometry);
/* Rendering observes session time but cannot advance it. Frontends that render
 * the same machine state at the same emulated_time receive the same frame. */
typedef bm_status_t (*bm_machine_video_render_fn)(const void *machine,
                                                   bm_tick_t emulated_time,
                                                   bm_video_framebuffer_t *framebuffer);
typedef size_t (*bm_machine_storage_count_fn)(const void *machine);
typedef bm_status_t (*bm_machine_storage_status_fn)(
    const void *machine, size_t index, bm_storage_device_status_t *status);
typedef bm_status_t (*bm_machine_storage_media_fn)(
    void *machine, bm_storage_device_kind_t kind, uint32_t unit,
    const bm_storage_media_change_t *change);

typedef struct bm_machine_ops {
    bm_machine_validate_fn validate;
    bm_machine_create_fn create;
    bm_machine_destroy_fn destroy;
    bm_machine_video_geometry_fn video_geometry;
    bm_machine_video_render_fn video_render;
    bm_machine_reset_fn reset;
    bm_machine_inspect_fn inspect;
    bm_machine_input_fn input;
    bm_machine_storage_count_fn storage_count;
    bm_machine_storage_status_fn storage_status;
    bm_machine_storage_media_fn storage_media;
    bm_machine_keyboard_leds_fn keyboard_leds;
} bm_machine_ops_t;

typedef struct bm_machine_definition {
    const char *id;
    /* Rate of the machine's current scheduler tick domain. Frontends use this
     * for wall-clock pacing; it is distinct from a CPU crystal frequency and
     * may change when a machine adopts cycle-accounted scheduling. */
    uint64_t scheduler_ticks_per_second;
    bm_configuration_contract_t configuration;
    bm_machine_ops_t ops;
    bm_engine_config_t engine;
} bm_machine_definition_t;

typedef struct bm_machine_config {
    /* The definition, configuration metadata and data remain caller-owned and
     * valid until the session is reconfigured or destroyed. */
    const bm_machine_definition_t *definition;
    bm_configuration_view_t configuration;
} bm_machine_config_t;

bm_status_t bm_machine_definition_validate(const bm_machine_definition_t *definition);
bm_status_t bm_machine_config_validate(const bm_machine_config_t *configuration);
/* Registered definitions remain caller-owned and valid until the registry is
 * destroyed. Registration order is stable for deterministic enumeration. */
bm_status_t bm_machine_registry_create(const bm_host_services_t *host,
                                       size_t capacity,
                                       bm_machine_registry_t **out_registry);
void bm_machine_registry_destroy(bm_machine_registry_t *registry);
bm_status_t bm_machine_registry_register(bm_machine_registry_t *registry,
                                         const bm_machine_definition_t *definition);
bm_status_t bm_machine_registry_find(const bm_machine_registry_t *registry,
                                     const char *id,
                                     const bm_machine_definition_t **out_definition);
bm_status_t bm_machine_registry_at(const bm_machine_registry_t *registry,
                                   size_t index,
                                   const bm_machine_definition_t **out_definition);
size_t bm_machine_registry_count(const bm_machine_registry_t *registry);

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
bm_status_t bm_session_video_geometry(const bm_session_t *session,
                                      bm_video_geometry_t *geometry);
bm_status_t bm_session_render_video(const bm_session_t *session,
                                    bm_video_framebuffer_t *framebuffer);
bm_status_t bm_session_inspect_machine(const bm_session_t *session,
                                       const char *name,
                                       uint64_t *value);
bm_status_t bm_session_send_input(bm_session_t *session,
                                  const bm_input_event_t *event);
bm_status_t bm_session_keyboard_leds(const bm_session_t *session,
                                     bm_keyboard_led_state_t *state);
bm_status_t bm_session_storage_device_count(const bm_session_t *session,
                                            size_t *count);
bm_status_t bm_session_storage_device_status(
    const bm_session_t *session, size_t index,
    bm_storage_device_status_t *status);
bm_status_t bm_session_replace_storage_media(
    bm_session_t *session, bm_storage_device_kind_t kind, uint32_t unit,
    const bm_storage_media_change_t *change);

#ifdef __cplusplus
}
#endif

#endif
