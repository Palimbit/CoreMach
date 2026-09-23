/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/frontend/frontend.h>
#include <blumach/platforms/null_host.h>
#include <blumach/systems/olivetti_pcs86.h>

#include <assert.h>
#include <stdint.h>
#include <string.h>

typedef struct debug_observation {
    uint64_t calls;
    uint64_t instructions;
    uint64_t io;
    uint64_t interrupts;
    uint64_t memory;
} debug_observation_t;

static void
observe_debug(void *context, const bm_frontend_debug_event_t *event)
{
    debug_observation_t *observation = context;
    assert(event->sequence == observation->calls);
    ++observation->calls;
    if (event->kind == BM_FRONTEND_DEBUG_INSTRUCTION)
        ++observation->instructions;
    else if (event->kind == BM_FRONTEND_DEBUG_IO)
        ++observation->io;
    else if (event->kind == BM_FRONTEND_DEBUG_INTERRUPT)
        ++observation->interrupts;
    else if (event->kind == BM_FRONTEND_DEBUG_MEMORY)
        ++observation->memory;
    else
        assert(0);
}

static bm_status_t
read_zero_blocks(void *context, uint64_t first_block, uint32_t block_count,
                 uint8_t *destination)
{
    (void) context;
    (void) first_block;
    memset(destination, 0, (size_t) block_count * 512U);
    return BM_STATUS_OK;
}

static bm_status_t
write_zero_blocks(void *context, uint64_t first_block, uint32_t block_count,
                  const uint8_t *source)
{
    (void) context;
    (void) first_block;
    (void) block_count;
    (void) source;
    return BM_STATUS_OK;
}

int
main(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_machine_registry_t *registry = NULL;
    const bm_frontend_adapter_t *adapter;
    const bm_frontend_asset_requirement_t *assets;
    const bm_frontend_persistent_state_requirement_t *persistent_states;
    const bm_machine_definition_t *definition;
    bm_frontend_machine_t *machine = NULL;
    bm_session_t *session = NULL;
    static uint8_t even_bytes[32768];
    static uint8_t odd_bytes[32768];
    const bm_frontend_asset_binding_t bindings[] = {
        { "firmware-even", BM_FRONTEND_ASSET_BLOB,
          { .blob = { "test-even", even_bytes, sizeof(even_bytes), NULL } } },
        { "firmware-odd", BM_FRONTEND_ASSET_BLOB,
          { .blob = { "test-odd", odd_bytes, sizeof(odd_bytes), NULL } } },
        { "floppy-0", BM_FRONTEND_ASSET_READ_ONLY_MEDIA,
          { .media = { NULL, 1440U, 512U, 1, read_zero_blocks, NULL } } },
        { "hard-disk-0", BM_FRONTEND_ASSET_READ_ONLY_MEDIA,
          { .media = { NULL, 41820U, 512U, 1, read_zero_blocks, NULL } } }
    };
    const bm_frontend_asset_binding_t writable_bindings[] = {
        { "firmware-even", BM_FRONTEND_ASSET_BLOB,
          { .blob = { "test-even", even_bytes, sizeof(even_bytes), NULL } } },
        { "firmware-odd", BM_FRONTEND_ASSET_BLOB,
          { .blob = { "test-odd", odd_bytes, sizeof(odd_bytes), NULL } } },
        { "hard-disk-0", BM_FRONTEND_ASSET_BLOCK_MEDIA,
          { .media = { NULL, 41820U, 512U, 0, read_zero_blocks,
                       write_zero_blocks } } }
    };
    bm_frontend_diagnostics_t diagnostics;
    bm_storage_device_status_t storage_status;
    size_t storage_count = 0U;
    bm_storage_media_change_t media_change = { 0 };
    const bm_frontend_asset_binding_t unknown_binding = {
        "unknown", BM_FRONTEND_ASSET_BLOB,
        { .blob = { "unknown", even_bytes, sizeof(even_bytes), NULL } }
    };
    size_t asset_count = 0U;
    size_t persistent_state_count = 0U;
    debug_observation_t observation = { 0 };

    assert(bm_frontend_adapter_count() == 2U);
    assert(bm_frontend_adapter_at(2U) == NULL);
    adapter = bm_frontend_adapter_find("olivetti-pcs86");
    assert(adapter != NULL);
    assert(bm_frontend_adapter_find("not-a-machine") == NULL);
    definition = bm_frontend_adapter_definition(adapter);
    assert(definition != NULL);
    assert(strcmp(definition->id, "olivetti-pcs86") == 0);
    assert(definition->scheduler_ticks_per_second ==
           BM_MACHINE_CLOCKED_TICKS_PER_SECOND);
    assert(definition->engine_mode == BM_MACHINE_ENGINE_CLOCKED);
    assets = bm_frontend_adapter_assets(adapter, &asset_count);
    assert(assets != NULL);
    assert(asset_count == 5U);
    assert(strcmp(assets[0].role, "firmware-even") == 0);
    assert(assets[0].required);
    assert(assets[0].accepted_size_count == 1U);
    assert(assets[0].accepted_sizes[0] == 32768U);
    assert(strcmp(assets[2].role, "floppy-0") == 0);
    assert(!assets[2].required);
    assert(assets[2].kind == BM_FRONTEND_ASSET_READ_ONLY_MEDIA);
    assert(assets[2].accepted_size_count == 2U);
    assert(assets[2].block_size == 512U);
    assert(assets[2].replaceable);
    assert(assets[2].storage_kind == BM_STORAGE_DEVICE_FLOPPY);
    assert(assets[2].storage_unit == 0U);
    assert(strcmp(assets[3].role, "floppy-1") == 0);
    assert(assets[3].replaceable && assets[3].storage_unit == 1U);
    assert(strcmp(assets[4].role, "hard-disk-0") == 0);
    assert(!assets[4].required);
    assert(assets[4].kind == BM_FRONTEND_ASSET_BLOCK_MEDIA);
    assert(assets[4].accepted_size_count == 1U);
    assert(assets[4].accepted_sizes[0] == 21411840U);
    assert(!assets[4].replaceable);
    persistent_states = bm_frontend_adapter_persistent_states(
        adapter, &persistent_state_count);
    assert(persistent_states != NULL);
    assert(persistent_state_count == 1U);
    assert(strcmp(persistent_states[0].role, "rtc") == 0);
    assert(persistent_states[0].size == 32U);
    assert(persistent_states[0].default_data != NULL);
    assert(persistent_states[0].battery_backed);

    assert(bm_machine_registry_create(&host, bm_frontend_adapter_count(),
                                      &registry) == BM_STATUS_OK);
    assert(bm_frontend_register_machines(registry) == BM_STATUS_OK);
    assert(bm_machine_registry_count(registry) == 2U);
    bm_machine_registry_destroy(registry);

    assert(bm_frontend_machine_open(adapter, NULL, 0U, &machine) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(machine == NULL);
    assert(bm_frontend_machine_open(adapter, &unknown_binding, 1U, &machine) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_frontend_machine_open(
               adapter, bindings, sizeof(bindings) / sizeof(bindings[0]),
               &machine) == BM_STATUS_OK);
    assert(machine != NULL);
    assert(bm_frontend_machine_set_debug_observer(NULL, observe_debug,
                                                  &observation) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_frontend_machine_set_debug_observer(machine, observe_debug,
                                                  &observation) ==
           BM_STATUS_OK);
    assert(bm_frontend_machine_config(machine) != NULL);
    assert(bm_frontend_machine_config(machine)->definition == definition);
    assert(bm_frontend_machine_diagnostics(machine, &diagnostics) ==
           BM_STATUS_OK);
    assert(diagnostics.read_only_media_bytes == 22149120U);
    assert(diagnostics.instructions == 0U);
    assert(bm_session_create(&host, &session) == BM_STATUS_OK);
    assert(bm_session_configure(session, bm_frontend_machine_config(machine)) ==
           BM_STATUS_OK);
    assert(bm_session_start(session) == BM_STATUS_OK);
    assert(bm_session_run_for(session, 1000U) == BM_STATUS_OK);
    assert(observation.calls != 0U);
    assert(observation.instructions != 0U);
    assert(observation.memory != 0U);
    {
        const uint64_t calls = observation.calls;
        assert(bm_frontend_machine_set_debug_observer(machine, NULL,
                                                      &observation) ==
               BM_STATUS_OK);
        assert(bm_session_run_for(session, 1000U) == BM_STATUS_OK);
        assert(observation.calls == calls);
    }
    {
        uint64_t value = UINT64_MAX;
        assert(bm_session_inspect_machine(session, "rtc_hours", &value) ==
               BM_STATUS_OK);
        assert(value == 0x22U);
        assert(bm_session_inspect_machine(session, "rtc_day_of_month", &value) ==
               BM_STATUS_OK);
        assert(value == 0x18U);
        assert(bm_session_inspect_machine(session, "rtc_month", &value) ==
               BM_STATUS_OK);
        assert(value == 0x09U);
        assert(bm_session_inspect_machine(
                   session, "rtc_alarm_milliseconds", &value) == BM_STATUS_OK);
        assert(value == 0xe0U);
        assert(bm_session_inspect_machine(
                   session, "rtc_alarm_day_of_week", &value) == BM_STATUS_OK);
        assert(value == 0xcdU);
        assert(bm_session_inspect_machine(
                   session, "rtc_alarm_day_of_month", &value) == BM_STATUS_OK);
        assert(value == 0xfcU);
        assert(bm_session_inspect_machine(
                   session, "rtc_alarm_month", &value) == BM_STATUS_OK);
        assert(value == 0xceU);
    }
    assert(bm_session_storage_device_count(session, &storage_count) ==
           BM_STATUS_OK);
    assert(storage_count == 3U);
    assert(bm_session_storage_device_status(session, 2U, &storage_status) ==
           BM_STATUS_OK);
    assert(storage_status.kind == BM_STORAGE_DEVICE_HARD_DISK);
    assert(storage_status.installed && storage_status.media_present);
    assert(storage_status.write_protected);
    assert(bm_session_replace_storage_media(
               session, BM_STORAGE_DEVICE_FLOPPY, 0U, &media_change) ==
           BM_STATUS_OK);
    assert(bm_session_storage_device_status(session, 0U, &storage_status) ==
           BM_STATUS_OK);
    assert(storage_status.installed && !storage_status.media_present);
    media_change.media_present = 1;
    media_change.write_protected = 1;
    media_change.media = bindings[2].value.media;
    assert(bm_session_replace_storage_media(
               session, BM_STORAGE_DEVICE_FLOPPY, 0U, &media_change) ==
           BM_STATUS_OK);
    assert(bm_session_storage_device_status(session, 0U, &storage_status) ==
           BM_STATUS_OK);
    assert(storage_status.media_present && storage_status.write_protected);
    assert(bm_session_replace_storage_media(
               session, BM_STORAGE_DEVICE_FLOPPY, 1U, &media_change) ==
           BM_STATUS_UNSUPPORTED);
    bm_session_destroy(session);
    bm_frontend_machine_close(machine);

    {
        const bm_frontend_machine_option_t two_drives[] = {
            { "commercial_profile", 0U },
            { "floppy_a_type", 1U }, { "floppy_b_type", 1U },
            { "jumper_bank", 256U }
        };
        const bm_frontend_machine_option_t commercial_two[] = {
            { "commercial_profile", 2U },
            { "floppy_a_type", 1U }, { "floppy_b_type", 1U },
            { "jumper_bank", 0U }
        };
        const bm_frontend_machine_option_t absent_a = {
            "floppy_a_type", 0U
        };
        const bm_frontend_machine_option_t hd_a = {
            "floppy_a_type", 2U
        };
        const bm_frontend_machine_option_t no_xta = {
            "commercial_profile", 3U
        };
        const bm_frontend_asset_binding_t b_media = {
            "floppy-1", BM_FRONTEND_ASSET_READ_ONLY_MEDIA,
            { .media = { NULL, 1440U, 512U, 1, read_zero_blocks, NULL } }
        };
        const bm_pcs86_config_t *configuration;
        assert(bm_frontend_machine_open_configured(
                   adapter, bindings, 3U, NULL, 0U, &absent_a, 1U,
                   &machine) == BM_STATUS_INVALID_ARGUMENT);
        assert(bm_frontend_machine_open_configured(
                   adapter, bindings, 2U, NULL, 0U, &no_xta, 1U,
                   &machine) == BM_STATUS_INVALID_ARGUMENT);
        assert(bm_frontend_machine_open_configured(
                   adapter, bindings, 2U, NULL, 0U, two_drives, 4U,
                   &machine) == BM_STATUS_OK);
        configuration = bm_frontend_machine_config(machine)->configuration.data;
        assert(configuration->floppy[0].installed &&
               !configuration->floppy[0].media_present);
        assert(configuration->floppy[1].installed &&
               !configuration->floppy[1].media_present);
        assert(configuration->floppy_drive_type[0] == 1U &&
               configuration->floppy_drive_type[1] == 1U);
        assert(configuration->jumpers_manual &&
               configuration->jumpers_value == 0xffU);
        assert(bm_session_create(&host, &session) == BM_STATUS_OK);
        assert(bm_session_configure(session, bm_frontend_machine_config(machine)) ==
               BM_STATUS_OK);
        assert(bm_session_start(session) == BM_STATUS_OK);
        assert(bm_session_storage_device_status(session, 1U, &storage_status) ==
               BM_STATUS_OK);
        assert(storage_status.installed && !storage_status.media_present);
        media_change.media_present = 1;
        media_change.write_protected = 1;
        media_change.media = (bm_block_media_t) {
            NULL, 2880U, 512U, 1, read_zero_blocks, NULL
        };
        assert(bm_session_replace_storage_media(
                   session, BM_STORAGE_DEVICE_FLOPPY, 1U, &media_change) ==
               BM_STATUS_INVALID_ARGUMENT);
        media_change.media = b_media.value.media;
        assert(bm_session_replace_storage_media(
                   session, BM_STORAGE_DEVICE_FLOPPY, 1U, &media_change) ==
               BM_STATUS_OK);
        bm_session_destroy(session);
        bm_frontend_machine_close(machine);
        assert(bm_frontend_machine_open_configured(
                   adapter, bindings, 2U, NULL, 0U, commercial_two, 4U,
                   &machine) == BM_STATUS_OK);
        bm_frontend_machine_close(machine);
        assert(bm_frontend_machine_open_configured(
                   adapter, bindings, 3U, NULL, 0U, &hd_a, 1U,
                   &machine) == BM_STATUS_OK);
        configuration = bm_frontend_machine_config(machine)->configuration.data;
        assert(configuration->floppy_drive_type[0] == 2U &&
               configuration->floppy[0].geometry.sectors_per_track == 9U);
        bm_frontend_machine_close(machine);
    }

    {
        const bm_frontend_machine_option_t no_ems = { "ems_kib", 0U };
        const bm_frontend_machine_option_t ems_384 = { "ems_kib", 384U };
        const bm_frontend_machine_option_t invalid = { "ems_kib", 123U };
        const bm_frontend_machine_option_t unknown = { "ram_kib", 256U };
        const bm_frontend_machine_option_t duplicate[] = {
            { "ems_kib", 0U }, { "ems_kib", 384U }
        };
        uint64_t ems = UINT64_MAX;
        assert(bm_frontend_machine_open_configured(
                   adapter, bindings, 2U, NULL, 0U, &invalid, 1U, &machine) ==
               BM_STATUS_INVALID_ARGUMENT);
        assert(bm_frontend_machine_open_configured(
                   adapter, bindings, 2U, NULL, 0U, &unknown, 1U, &machine) ==
               BM_STATUS_INVALID_ARGUMENT);
        assert(bm_frontend_machine_open_configured(
                   adapter, bindings, 2U, NULL, 0U, duplicate, 2U, &machine) ==
               BM_STATUS_INVALID_ARGUMENT);
        assert(bm_frontend_machine_open_configured(
                   adapter, bindings, 2U, NULL, 0U, &no_ems, 1U, &machine) ==
               BM_STATUS_OK);
        assert(bm_session_create(&host, &session) == BM_STATUS_OK);
        assert(bm_session_configure(session, bm_frontend_machine_config(machine)) ==
               BM_STATUS_OK);
        assert(bm_session_start(session) == BM_STATUS_OK);
        assert(bm_session_inspect_machine(session, "ems_kib", &ems) ==
               BM_STATUS_OK && ems == 0U);
        bm_session_destroy(session);
        bm_frontend_machine_close(machine);
        assert(bm_frontend_machine_open_configured(
                   adapter, bindings, 2U, NULL, 0U, &ems_384, 1U, &machine) ==
               BM_STATUS_OK);
        assert(bm_session_create(&host, &session) == BM_STATUS_OK);
        assert(bm_session_configure(session, bm_frontend_machine_config(machine)) ==
               BM_STATUS_OK);
        assert(bm_session_start(session) == BM_STATUS_OK);
        assert(bm_session_inspect_machine(session, "ems_kib", &ems) ==
               BM_STATUS_OK && ems == 384U);
        bm_session_destroy(session);
        bm_frontend_machine_close(machine);
    }

    {
        static const uint8_t restored_rtc[32] = {
            0x00U, 0x00U, 0x45U, 0x34U, 0x11U, 0x03U, 0x19U, 0x09U
        };
        bm_frontend_persistent_state_binding_t state_binding = {
            "rtc", restored_rtc, sizeof(restored_rtc)
        };
        uint64_t value = UINT64_MAX;
        machine = NULL;
        state_binding.size = sizeof(restored_rtc) - 1U;
        assert(bm_frontend_machine_open_with_persistent_state(
                   adapter, bindings, 2U, &state_binding, 1U, &machine) ==
               BM_STATUS_INVALID_ARGUMENT);
        assert(machine == NULL);
        state_binding.size = sizeof(restored_rtc);
        assert(bm_frontend_machine_open_with_persistent_state(
                   adapter, bindings, 2U, &state_binding, 1U, &machine) ==
               BM_STATUS_OK);
        assert(bm_session_create(&host, &session) == BM_STATUS_OK);
        assert(bm_session_configure(
                   session, bm_frontend_machine_config(machine)) == BM_STATUS_OK);
        assert(bm_session_start(session) == BM_STATUS_OK);
        assert(bm_session_inspect_machine(session, "rtc_hours", &value) ==
               BM_STATUS_OK);
        assert(value == 0x11U);
        bm_session_destroy(session);
        bm_frontend_machine_close(machine);
    }

    machine = NULL;
    assert(bm_frontend_machine_open(
               adapter, writable_bindings,
               sizeof(writable_bindings) / sizeof(writable_bindings[0]),
               &machine) == BM_STATUS_OK);
    assert(machine != NULL);
    assert(bm_frontend_machine_diagnostics(machine, &diagnostics) ==
           BM_STATUS_OK);
    assert(diagnostics.read_only_media_bytes == 0U);
    bm_frontend_machine_close(machine);

    {
        static uint8_t synthetic_m15_firmware[65536];
        const bm_frontend_adapter_t *m15 =
            bm_frontend_adapter_find("olivetti-m15");
        const bm_frontend_asset_binding_t m15_bindings[] = {
            { "firmware", BM_FRONTEND_ASSET_BLOB,
              { .blob = { "synthetic-test", synthetic_m15_firmware,
                          sizeof(synthetic_m15_firmware), NULL } } },
            { "floppy-0", BM_FRONTEND_ASSET_READ_ONLY_MEDIA,
              { .media = { NULL, 1440U, 512U, 1,
                           read_zero_blocks, NULL } } }
        };
        size_t m15_asset_count = 0U;
        const bm_frontend_asset_requirement_t *m15_assets;
        size_t device_count = 0U;
        bm_storage_device_status_t device;
        const bm_storage_media_change_t eject = { 0 };

        assert(m15 != NULL);
        assert(strcmp(bm_frontend_adapter_definition(m15)->id,
                      "olivetti-m15") == 0);
        m15_assets = bm_frontend_adapter_assets(m15, &m15_asset_count);
        assert(m15_asset_count == 2U);
        assert(m15_assets[1].replaceable);
        assert(bm_frontend_machine_open(m15, m15_bindings, 2U,
                                        &machine) == BM_STATUS_OK);
        assert(bm_session_create(&host, &session) == BM_STATUS_OK);
        assert(bm_session_configure(
                   session, bm_frontend_machine_config(machine)) == BM_STATUS_OK);
        assert(bm_session_start(session) == BM_STATUS_OK);
        assert(bm_session_storage_device_count(session, &device_count) ==
               BM_STATUS_OK);
        assert(device_count == 2U);
        assert(bm_session_storage_device_status(session, 0U, &device) ==
               BM_STATUS_OK);
        assert(device.media_present && device.write_protected);
        assert(bm_session_replace_storage_media(
                   session, BM_STORAGE_DEVICE_FLOPPY, 0U,
                   &eject) == BM_STATUS_OK);
        assert(bm_session_storage_device_status(session, 0U, &device) ==
               BM_STATUS_OK);
        assert(!device.media_present);
        assert(bm_session_reset(session) == BM_STATUS_OK);
        bm_session_destroy(session);
        bm_frontend_machine_close(machine);
        {
            const bm_frontend_machine_option_t ram_256 = { "ram_kib", 256U };
            const bm_frontend_machine_option_t invalid_ram = { "ram_kib", 384U };
            uint64_t ram = UINT64_MAX;
            assert(bm_frontend_machine_open_configured(
                       m15, m15_bindings, 2U, NULL, 0U,
                       &invalid_ram, 1U, &machine) == BM_STATUS_INVALID_ARGUMENT);
            assert(bm_frontend_machine_open_configured(
                       m15, m15_bindings, 2U, NULL, 0U,
                       &ram_256, 1U, &machine) == BM_STATUS_OK);
            assert(bm_session_create(&host, &session) == BM_STATUS_OK);
            assert(bm_session_configure(session, bm_frontend_machine_config(machine)) ==
                   BM_STATUS_OK);
            assert(bm_session_start(session) == BM_STATUS_OK);
            assert(bm_session_inspect_machine(session, "ram_kib", &ram) ==
                   BM_STATUS_OK && ram == 256U);
            bm_session_destroy(session);
            bm_frontend_machine_close(machine);
        }
    }
    return 0;
}
