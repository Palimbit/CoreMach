/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/platforms/null_host.h>
#include <blumach/runtime/runtime.h>

#include "failure_injection_host.h"

#include <assert.h>
#include <string.h>

static bm_status_t
test_create(bm_engine_t *engine,
            const bm_host_services_t *host,
            const bm_configuration_view_t *configuration,
            void **out_machine)
{
    (void) engine;
    (void) host;
    (void) configuration;
    *out_machine = out_machine;
    return BM_STATUS_OK;
}

static void
test_destroy(void *machine)
{
    (void) machine;
}

static bm_machine_definition_t
make_definition(const char *id)
{
    bm_machine_definition_t definition = {
        .id = id,
        .scheduler_ticks_per_second = 1000000U,
        .configuration = { "test.registry.config", 1U, sizeof(int) },
        .ops = { NULL, test_create, test_destroy,
                 NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                 NULL },
        .engine = { 1U, 1U, 0U }
    };
    return definition;
}

static void
test_definition_and_configuration_validation(void)
{
    int value = 7;
    bm_machine_definition_t definition = make_definition("test.first");
    bm_machine_definition_t invalid;
    bm_machine_config_t configuration = {
        .definition = &definition,
        .configuration = { "test.registry.config", 1U, sizeof(value), &value }
    };
    bm_machine_config_t invalid_configuration;

    assert(bm_machine_definition_validate(&definition) == BM_STATUS_OK);
    assert(bm_machine_config_validate(&configuration) == BM_STATUS_OK);
    assert(bm_machine_definition_validate(NULL) == BM_STATUS_INVALID_ARGUMENT);

    invalid = definition;
    invalid.id = "";
    assert(bm_machine_definition_validate(&invalid) == BM_STATUS_INVALID_ARGUMENT);
    invalid = definition;
    invalid.scheduler_ticks_per_second = 0U;
    assert(bm_machine_definition_validate(&invalid) == BM_STATUS_INVALID_ARGUMENT);
    invalid = definition;
    invalid.configuration.type = "";
    assert(bm_machine_definition_validate(&invalid) == BM_STATUS_INVALID_ARGUMENT);
    invalid = definition;
    invalid.configuration.version = 0U;
    assert(bm_machine_definition_validate(&invalid) == BM_STATUS_INVALID_ARGUMENT);
    invalid = definition;
    invalid.configuration.size = 0U;
    assert(bm_machine_definition_validate(&invalid) == BM_STATUS_INVALID_ARGUMENT);
    invalid = definition;
    invalid.ops.create = NULL;
    assert(bm_machine_definition_validate(&invalid) == BM_STATUS_INVALID_ARGUMENT);
    invalid = definition;
    invalid.ops.destroy = NULL;
    assert(bm_machine_definition_validate(&invalid) == BM_STATUS_INVALID_ARGUMENT);
    invalid = definition;
    invalid.engine.max_cpus = 0U;
    assert(bm_machine_definition_validate(&invalid) == BM_STATUS_INVALID_ARGUMENT);

    assert(bm_machine_config_validate(NULL) == BM_STATUS_INVALID_ARGUMENT);
    invalid_configuration = configuration;
    invalid_configuration.configuration.type = "test.other.config";
    assert(bm_machine_config_validate(&invalid_configuration) ==
           BM_STATUS_INVALID_ARGUMENT);
    invalid_configuration = configuration;
    invalid_configuration.configuration.version = 2U;
    assert(bm_machine_config_validate(&invalid_configuration) ==
           BM_STATUS_INVALID_ARGUMENT);
    invalid_configuration = configuration;
    invalid_configuration.configuration.size += 1U;
    assert(bm_machine_config_validate(&invalid_configuration) ==
           BM_STATUS_INVALID_ARGUMENT);
    invalid_configuration = configuration;
    invalid_configuration.configuration.data = NULL;
    assert(bm_machine_config_validate(&invalid_configuration) ==
           BM_STATUS_INVALID_ARGUMENT);
}

static void
test_registry_order_lookup_and_capacity(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_machine_registry_t *registry = NULL;
    bm_machine_definition_t first = make_definition("test.first");
    bm_machine_definition_t second = make_definition("test.second");
    bm_machine_definition_t duplicate = make_definition("test.first");
    bm_machine_definition_t third = make_definition("test.third");
    const bm_machine_definition_t *result = NULL;

    assert(bm_machine_registry_create(&host, 2U, &registry) == BM_STATUS_OK);
    assert(registry != NULL);
    assert(bm_machine_registry_count(registry) == 0U);
    assert(bm_machine_registry_register(registry, &first) == BM_STATUS_OK);
    assert(bm_machine_registry_register(registry, &duplicate) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_machine_registry_register(registry, &second) == BM_STATUS_OK);
    assert(bm_machine_registry_register(registry, &third) ==
           BM_STATUS_CAPACITY_EXCEEDED);
    assert(bm_machine_registry_count(registry) == 2U);

    assert(bm_machine_registry_at(registry, 0U, &result) == BM_STATUS_OK);
    assert(result == &first);
    assert(bm_machine_registry_at(registry, 1U, &result) == BM_STATUS_OK);
    assert(result == &second);
    assert(bm_machine_registry_find(registry, "test.second", &result) ==
           BM_STATUS_OK);
    assert(result == &second);
    result = &first;
    assert(bm_machine_registry_find(registry, "test.missing", &result) ==
           BM_STATUS_UNSUPPORTED);
    assert(result == NULL);

    assert(bm_machine_registry_register(NULL, &first) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_machine_registry_register(registry, NULL) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_machine_registry_find(NULL, "test.first", &result) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_machine_registry_find(registry, NULL, &result) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_machine_registry_find(registry, "", &result) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_machine_registry_find(registry, "test.first", NULL) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_machine_registry_at(registry, 2U, &result) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_machine_registry_at(registry, 0U, NULL) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_machine_registry_count(NULL) == 0U);
    bm_machine_registry_destroy(registry);
    bm_machine_registry_destroy(NULL);
}

static void
test_registry_creation_failures(void)
{
    failure_injection_host_t tracker;
    bm_host_services_t host;
    bm_machine_registry_t *registry = NULL;

    assert(bm_machine_registry_create(NULL, 1U, &registry) ==
           BM_STATUS_INVALID_ARGUMENT);
    host = bm_null_host_services();
    assert(bm_machine_registry_create(&host, 0U, &registry) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_machine_registry_create(&host, 1U, NULL) ==
           BM_STATUS_INVALID_ARGUMENT);

    failure_injection_host_initialize(&tracker);
    failure_injection_host_fail_on(&tracker, 0U);
    host = failure_injection_host_services(&tracker);
    assert(bm_machine_registry_create(&host, 1U, &registry) ==
           BM_STATUS_OUT_OF_MEMORY);
    assert(registry == NULL);
    assert(tracker.outstanding_allocations == 0U);

    failure_injection_host_initialize(&tracker);
    failure_injection_host_fail_on(&tracker, 1U);
    host = failure_injection_host_services(&tracker);
    assert(bm_machine_registry_create(&host, 1U, &registry) ==
           BM_STATUS_OUT_OF_MEMORY);
    assert(registry == NULL);
    assert(tracker.outstanding_allocations == 0U);
}

int
main(void)
{
    test_definition_and_configuration_validation();
    test_registry_order_lookup_and_capacity();
    test_registry_creation_failures();
    return 0;
}
