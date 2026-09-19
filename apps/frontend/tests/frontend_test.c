/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/frontend/frontend.h>
#include <blumach/platforms/null_host.h>

#include <assert.h>
#include <stdint.h>
#include <string.h>

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

    assert(bm_frontend_adapter_count() == 1U);
    assert(bm_frontend_adapter_at(1U) == NULL);
    adapter = bm_frontend_adapter_find("olivetti-pcs86");
    assert(adapter != NULL);
    assert(bm_frontend_adapter_find("not-a-machine") == NULL);
    definition = bm_frontend_adapter_definition(adapter);
    assert(definition != NULL);
    assert(strcmp(definition->id, "olivetti-pcs86") == 0);
    assert(definition->scheduler_ticks_per_second == UINT64_C(2000000));
    assets = bm_frontend_adapter_assets(adapter, &asset_count);
    assert(assets != NULL);
    assert(asset_count == 4U);
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
    assert(strcmp(assets[3].role, "hard-disk-0") == 0);
    assert(!assets[3].required);
    assert(assets[3].kind == BM_FRONTEND_ASSET_BLOCK_MEDIA);
    assert(assets[3].accepted_size_count == 1U);
    assert(assets[3].accepted_sizes[0] == 21411840U);
    assert(!assets[3].replaceable);

    assert(bm_machine_registry_create(&host, bm_frontend_adapter_count(),
                                      &registry) == BM_STATUS_OK);
    assert(bm_frontend_register_machines(registry) == BM_STATUS_OK);
    assert(bm_machine_registry_count(registry) == 1U);
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
    return 0;
}
