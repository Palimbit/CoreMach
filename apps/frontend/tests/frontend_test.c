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

int
main(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_machine_registry_t *registry = NULL;
    const bm_frontend_adapter_t *adapter;
    const bm_frontend_asset_requirement_t *assets;
    const bm_machine_definition_t *definition;
    bm_frontend_machine_t *machine = NULL;
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
    bm_frontend_diagnostics_t diagnostics;
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
    assert(strcmp(assets[3].role, "hard-disk-0") == 0);
    assert(!assets[3].required);
    assert(assets[3].accepted_size_count == 1U);
    assert(assets[3].accepted_sizes[0] == 21411840U);

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
    bm_frontend_machine_close(machine);
    return 0;
}
