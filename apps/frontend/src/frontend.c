/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "frontend_internal.h"

#include <string.h>

static const bm_frontend_adapter_t *const adapters[] = {
    &bm_frontend_pcs86_adapter,
    &bm_frontend_m15_adapter
};

size_t
bm_frontend_adapter_count(void)
{
    return sizeof(adapters) / sizeof(adapters[0]);
}

const bm_frontend_adapter_t *
bm_frontend_adapter_at(size_t index)
{
    return index < bm_frontend_adapter_count() ? adapters[index] : NULL;
}

const bm_machine_definition_t *
bm_frontend_adapter_definition(const bm_frontend_adapter_t *adapter)
{
    return adapter != NULL ? adapter->definition() : NULL;
}

const bm_frontend_adapter_t *
bm_frontend_adapter_find(const char *machine_id)
{
    size_t index;
    if (machine_id == NULL)
        return NULL;
    for (index = 0U; index < bm_frontend_adapter_count(); ++index) {
        const bm_machine_definition_t *definition = adapters[index]->definition();
        if ((definition != NULL) && (strcmp(definition->id, machine_id) == 0))
            return adapters[index];
    }
    return NULL;
}

const bm_frontend_asset_requirement_t *
bm_frontend_adapter_assets(const bm_frontend_adapter_t *adapter, size_t *count)
{
    if (count != NULL)
        *count = adapter != NULL ? adapter->asset_count : 0U;
    return adapter != NULL ? adapter->assets : NULL;
}

const bm_frontend_persistent_state_requirement_t *
bm_frontend_adapter_persistent_states(const bm_frontend_adapter_t *adapter,
                                      size_t *count)
{
    if (count != NULL)
        *count = adapter != NULL ? adapter->persistent_state_count : 0U;
    return adapter != NULL ? adapter->persistent_states : NULL;
}

bm_status_t
bm_frontend_register_machines(bm_machine_registry_t *registry)
{
    size_t index;
    bm_status_t status = registry != NULL ? BM_STATUS_OK :
                                            BM_STATUS_INVALID_ARGUMENT;
    for (index = 0U;
         (status == BM_STATUS_OK) && (index < bm_frontend_adapter_count());
         ++index)
        status = bm_machine_registry_register(registry,
                                               adapters[index]->definition());
    return status;
}

bm_status_t
bm_frontend_machine_open(const bm_frontend_adapter_t *adapter,
                         const bm_frontend_asset_binding_t *bindings,
                         size_t binding_count,
                         bm_frontend_machine_t **out_machine)
{
    return bm_frontend_machine_open_with_persistent_state(
        adapter, bindings, binding_count, NULL, 0U, out_machine);
}

bm_status_t
bm_frontend_machine_open_with_persistent_state(
    const bm_frontend_adapter_t *adapter,
    const bm_frontend_asset_binding_t *bindings, size_t binding_count,
    const bm_frontend_persistent_state_binding_t *state_bindings,
    size_t state_binding_count,
    bm_frontend_machine_t **out_machine)
{
    size_t binding_index;
    size_t requirement_index;
    if ((adapter == NULL) || (out_machine == NULL) ||
        ((bindings == NULL) && (binding_count != 0U)) ||
        ((state_bindings == NULL) && (state_binding_count != 0U)))
        return BM_STATUS_INVALID_ARGUMENT;
    *out_machine = NULL;
    for (binding_index = 0U; binding_index < binding_count; ++binding_index) {
        const bm_frontend_asset_requirement_t *matched = NULL;
        size_t matches = 0U;
        for (requirement_index = 0U;
             requirement_index < adapter->asset_count; ++requirement_index) {
            if ((bindings[binding_index].role != NULL) &&
                (strcmp(bindings[binding_index].role,
                        adapter->assets[requirement_index].role) == 0) &&
                ((bindings[binding_index].kind ==
                  adapter->assets[requirement_index].kind) ||
                 ((adapter->assets[requirement_index].kind == BM_FRONTEND_ASSET_BLOCK_MEDIA) &&
                  (bindings[binding_index].kind == BM_FRONTEND_ASSET_READ_ONLY_MEDIA))))
                {
                    matched = &adapter->assets[requirement_index];
                    ++matches;
                }
        }
        if (matches != 1U)
            return BM_STATUS_INVALID_ARGUMENT;
        if (matched->accepted_size_count != 0U) {
            uint64_t byte_size;
            size_t size_index;
            int accepted = 0;
            if (bindings[binding_index].kind == BM_FRONTEND_ASSET_BLOB) {
                byte_size = bindings[binding_index].value.blob.size;
            } else {
                const bm_block_media_t *media =
                    &bindings[binding_index].value.media;
                if ((matched->block_size == 0U) ||
                    (media->block_size != matched->block_size) ||
                    (media->block_count > UINT64_MAX / media->block_size))
                    return BM_STATUS_INVALID_ARGUMENT;
                byte_size = media->block_count * media->block_size;
            }
            for (size_index = 0U;
                 size_index < matched->accepted_size_count; ++size_index) {
                if (byte_size == matched->accepted_sizes[size_index])
                    accepted = 1;
            }
            if (!accepted)
                return BM_STATUS_INVALID_ARGUMENT;
        }
        for (requirement_index = binding_index + 1U;
             requirement_index < binding_count; ++requirement_index) {
            if ((bindings[requirement_index].role != NULL) &&
                (strcmp(bindings[binding_index].role,
                        bindings[requirement_index].role) == 0))
                return BM_STATUS_INVALID_ARGUMENT;
        }
    }
    for (requirement_index = 0U;
         requirement_index < adapter->asset_count; ++requirement_index) {
        if (adapter->assets[requirement_index].required &&
            (bm_frontend_binding_find(
                 bindings, binding_count,
                 adapter->assets[requirement_index].role) == NULL))
            return BM_STATUS_INVALID_ARGUMENT;
    }
    for (binding_index = 0U; binding_index < state_binding_count;
         ++binding_index) {
        const bm_frontend_persistent_state_requirement_t *matched = NULL;
        size_t matches = 0U;
        if ((state_bindings[binding_index].role == NULL) ||
            (state_bindings[binding_index].data == NULL))
            return BM_STATUS_INVALID_ARGUMENT;
        for (requirement_index = 0U;
             requirement_index < adapter->persistent_state_count;
             ++requirement_index) {
            if (strcmp(state_bindings[binding_index].role,
                       adapter->persistent_states[requirement_index].role) == 0) {
                matched = &adapter->persistent_states[requirement_index];
                ++matches;
            }
        }
        if ((matches != 1U) ||
            (state_bindings[binding_index].size != matched->size))
            return BM_STATUS_INVALID_ARGUMENT;
        for (requirement_index = binding_index + 1U;
             requirement_index < state_binding_count; ++requirement_index) {
            if (strcmp(state_bindings[binding_index].role,
                       state_bindings[requirement_index].role) == 0)
                return BM_STATUS_INVALID_ARGUMENT;
        }
    }
    return adapter->open(bindings, binding_count, state_bindings,
                         state_binding_count, out_machine);
}

const bm_machine_config_t *
bm_frontend_machine_config(const bm_frontend_machine_t *machine)
{
    return machine != NULL ? &machine->configuration : NULL;
}

bm_status_t
bm_frontend_machine_diagnostics(
    const bm_frontend_machine_t *machine,
    bm_frontend_diagnostics_t *out_diagnostics)
{
    if ((machine == NULL) || (out_diagnostics == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    *out_diagnostics = machine->diagnostics;
    return BM_STATUS_OK;
}

bm_status_t
bm_frontend_machine_set_debug_observer(
    bm_frontend_machine_t *machine, bm_frontend_debug_observer_fn observer,
    void *context)
{
    if (machine == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    machine->debug_observer = observer;
    machine->debug_context = observer != NULL ? context : NULL;
    return BM_STATUS_OK;
}

void
bm_frontend_machine_close(bm_frontend_machine_t *machine)
{
    if (machine != NULL)
        machine->destroy(machine);
}

const bm_frontend_asset_binding_t *
bm_frontend_binding_find(const bm_frontend_asset_binding_t *bindings,
                         size_t binding_count, const char *role)
{
    size_t index;
    const bm_frontend_asset_binding_t *result = NULL;
    if ((bindings == NULL) || (role == NULL))
        return NULL;
    for (index = 0U; index < binding_count; ++index) {
        if ((bindings[index].role == NULL) ||
            (strcmp(bindings[index].role, role) != 0))
            continue;
        if (result != NULL)
            return NULL;
        result = &bindings[index];
    }
    return result;
}

const bm_frontend_persistent_state_binding_t *
bm_frontend_persistent_state_binding_find(
    const bm_frontend_persistent_state_binding_t *bindings,
    size_t binding_count, const char *role)
{
    size_t index;
    const bm_frontend_persistent_state_binding_t *result = NULL;
    if ((bindings == NULL) || (role == NULL))
        return NULL;
    for (index = 0U; index < binding_count; ++index) {
        if ((bindings[index].role == NULL) ||
            (strcmp(bindings[index].role, role) != 0))
            continue;
        if (result != NULL)
            return NULL;
        result = &bindings[index];
    }
    return result;
}
