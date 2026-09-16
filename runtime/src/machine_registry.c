/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/runtime/runtime.h>

#include <stdint.h>
#include <string.h>

struct bm_machine_registry {
    bm_host_services_t host;
    const bm_machine_definition_t **definitions;
    size_t count;
    size_t capacity;
};

bm_status_t
bm_machine_registry_create(const bm_host_services_t *host,
                           size_t capacity,
                           bm_machine_registry_t **out_registry)
{
    bm_machine_registry_t *registry;

    if ((bm_host_services_validate(host) != BM_STATUS_OK) ||
        (capacity == 0U) ||
        (capacity > SIZE_MAX / sizeof(*registry->definitions)) ||
        (out_registry == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    *out_registry = NULL;
    registry = host->allocate(host->context, sizeof(*registry));
    if (registry == NULL)
        return BM_STATUS_OUT_OF_MEMORY;
    memset(registry, 0, sizeof(*registry));
    registry->host = *host;
    registry->definitions = host->allocate(
        host->context, capacity * sizeof(*registry->definitions));
    if (registry->definitions == NULL) {
        host->release(host->context, registry);
        return BM_STATUS_OUT_OF_MEMORY;
    }
    memset(registry->definitions, 0,
           capacity * sizeof(*registry->definitions));
    registry->capacity = capacity;
    *out_registry = registry;
    return BM_STATUS_OK;
}

void
bm_machine_registry_destroy(bm_machine_registry_t *registry)
{
    if (registry == NULL)
        return;
    registry->host.release(registry->host.context, registry->definitions);
    registry->host.release(registry->host.context, registry);
}

bm_status_t
bm_machine_registry_register(bm_machine_registry_t *registry,
                             const bm_machine_definition_t *definition)
{
    size_t index;

    if ((registry == NULL) ||
        (bm_machine_definition_validate(definition) != BM_STATUS_OK))
        return BM_STATUS_INVALID_ARGUMENT;
    for (index = 0U; index < registry->count; ++index) {
        if (strcmp(registry->definitions[index]->id, definition->id) == 0)
            return BM_STATUS_INVALID_ARGUMENT;
    }
    if (registry->count >= registry->capacity)
        return BM_STATUS_CAPACITY_EXCEEDED;
    registry->definitions[registry->count++] = definition;
    return BM_STATUS_OK;
}

bm_status_t
bm_machine_registry_find(const bm_machine_registry_t *registry,
                         const char *id,
                         const bm_machine_definition_t **out_definition)
{
    size_t index;

    if (out_definition == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    *out_definition = NULL;
    if ((registry == NULL) || (id == NULL) || (id[0] == '\0'))
        return BM_STATUS_INVALID_ARGUMENT;
    for (index = 0U; index < registry->count; ++index) {
        if (strcmp(registry->definitions[index]->id, id) == 0) {
            *out_definition = registry->definitions[index];
            return BM_STATUS_OK;
        }
    }
    return BM_STATUS_UNSUPPORTED;
}

bm_status_t
bm_machine_registry_at(const bm_machine_registry_t *registry,
                       size_t index,
                       const bm_machine_definition_t **out_definition)
{
    if (out_definition == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    *out_definition = NULL;
    if ((registry == NULL) || (index >= registry->count))
        return BM_STATUS_INVALID_ARGUMENT;
    *out_definition = registry->definitions[index];
    return BM_STATUS_OK;
}

size_t
bm_machine_registry_count(const bm_machine_registry_t *registry)
{
    return (registry == NULL) ? 0U : registry->count;
}
