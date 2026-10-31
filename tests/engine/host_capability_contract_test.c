/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/engine/host.h>
#include <blumach/platforms/null_host.h>

#include <assert.h>
#include <stdint.h>

typedef struct test_firmware_services {
    uint32_t marker;
} test_firmware_services_t;

typedef struct test_block_services {
    uint32_t first;
    uint32_t second;
} test_block_services_t;

static void
test_empty_host_reports_unsupported(void)
{
    bm_host_services_t host = bm_null_host_services();
    const bm_host_capability_t *result = (const bm_host_capability_t *) &host;

    assert(bm_host_services_validate(&host) == BM_STATUS_OK);
    assert(bm_host_capability_lookup(&host, BM_HOST_CAPABILITY_FIRMWARE,
                                     1U, 1U, &result) == BM_STATUS_UNSUPPORTED);
    assert(result == NULL);
}

static void
test_capability_lookup_and_requirements(void)
{
    static const test_firmware_services_t firmware = { 0x4649524dU };
    static const test_block_services_t blocks = { 1U, 2U };
    static const bm_host_capability_t capabilities[] = {
        { BM_HOST_CAPABILITY_FIRMWARE, 2U, sizeof(firmware), &firmware },
        { BM_HOST_CAPABILITY_BLOCK_MEDIA, 1U, sizeof(blocks), &blocks }
    };
    bm_host_services_t host = bm_null_host_services();
    const bm_host_capability_t *result = NULL;

    host.capabilities = capabilities;
    host.capability_count = sizeof(capabilities) / sizeof(capabilities[0]);
    assert(bm_host_services_validate(&host) == BM_STATUS_OK);

    assert(bm_host_capability_lookup(&host, BM_HOST_CAPABILITY_FIRMWARE,
                                     1U, sizeof(firmware), &result) == BM_STATUS_OK);
    assert(result == &capabilities[0]);
    assert(result->services == &firmware);
    assert(((const test_firmware_services_t *) result->services)->marker ==
           0x4649524dU);

    result = NULL;
    assert(bm_host_capability_lookup(&host, BM_HOST_CAPABILITY_BLOCK_MEDIA,
                                     1U, sizeof(blocks), &result) == BM_STATUS_OK);
    assert(result == &capabilities[1]);
    assert(((const test_block_services_t *) result->services)->second == 2U);

    result = &capabilities[0];
    assert(bm_host_capability_lookup(&host, BM_HOST_CAPABILITY_FIRMWARE,
                                     3U, sizeof(firmware), &result) ==
           BM_STATUS_UNSUPPORTED);
    assert(result == NULL);
    result = &capabilities[0];
    assert(bm_host_capability_lookup(&host, BM_HOST_CAPABILITY_FIRMWARE,
                                     1U, sizeof(firmware) + 1U, &result) ==
           BM_STATUS_UNSUPPORTED);
    assert(result == NULL);
    result = &capabilities[0];
    assert(bm_host_capability_lookup(&host, BM_HOST_CAPABILITY_AUDIO_OUTPUT,
                                     1U, 1U, &result) == BM_STATUS_UNSUPPORTED);
    assert(result == NULL);
}

static void
test_lookup_argument_validation(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_host_services_t invalid_host = host;
    const bm_host_capability_t *result = (const bm_host_capability_t *) &host;

    invalid_host.log = NULL;
    assert(bm_host_capability_lookup(NULL, BM_HOST_CAPABILITY_FIRMWARE,
                                     1U, 1U, &result) == BM_STATUS_INVALID_ARGUMENT);
    assert(result == NULL);
    result = (const bm_host_capability_t *) &host;
    assert(bm_host_capability_lookup(&invalid_host, BM_HOST_CAPABILITY_FIRMWARE,
                                     1U, 1U, &result) == BM_STATUS_INVALID_ARGUMENT);
    assert(result == NULL);
    assert(bm_host_capability_lookup(&host, (bm_host_capability_id_t) -1,
                                     1U, 1U, &result) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_host_capability_lookup(&host, BM_HOST_CAPABILITY_COUNT,
                                     1U, 1U, &result) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_host_capability_lookup(&host, BM_HOST_CAPABILITY_FIRMWARE,
                                     0U, 1U, &result) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_host_capability_lookup(&host, BM_HOST_CAPABILITY_FIRMWARE,
                                     1U, 0U, &result) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_host_capability_lookup(&host, BM_HOST_CAPABILITY_FIRMWARE,
                                     1U, 1U, NULL) == BM_STATUS_INVALID_ARGUMENT);
}

static void
test_registry_validation(void)
{
    static const uint32_t service = 1U;
    bm_host_capability_t entries[2] = {
        { BM_HOST_CAPABILITY_FIRMWARE, 1U, sizeof(service), &service },
        { BM_HOST_CAPABILITY_BLOCK_MEDIA, 1U, sizeof(service), &service }
    };
    bm_host_services_t host = bm_null_host_services();

    host.capability_count = 1U;
    assert(bm_host_services_validate(&host) == BM_STATUS_INVALID_ARGUMENT);
    host.capabilities = entries;
    host.capability_count = 0U;
    assert(bm_host_services_validate(&host) == BM_STATUS_INVALID_ARGUMENT);
    host.capability_count = (size_t) BM_HOST_CAPABILITY_COUNT + 1U;
    assert(bm_host_services_validate(&host) == BM_STATUS_INVALID_ARGUMENT);

    host.capability_count = 2U;
    entries[1].id = BM_HOST_CAPABILITY_FIRMWARE;
    assert(bm_host_services_validate(&host) == BM_STATUS_INVALID_ARGUMENT);
    entries[1].id = (bm_host_capability_id_t) -1;
    assert(bm_host_services_validate(&host) == BM_STATUS_INVALID_ARGUMENT);
    entries[1].id = BM_HOST_CAPABILITY_COUNT;
    assert(bm_host_services_validate(&host) == BM_STATUS_INVALID_ARGUMENT);
    entries[1].id = BM_HOST_CAPABILITY_BLOCK_MEDIA;
    entries[1].version = 0U;
    assert(bm_host_services_validate(&host) == BM_STATUS_INVALID_ARGUMENT);
    entries[1].version = 1U;
    entries[1].services_size = 0U;
    assert(bm_host_services_validate(&host) == BM_STATUS_INVALID_ARGUMENT);
    entries[1].services_size = sizeof(service);
    entries[1].services = NULL;
    assert(bm_host_services_validate(&host) == BM_STATUS_INVALID_ARGUMENT);
}

static unsigned int tolerance_reports;
static bm_tolerance_event_kind_t tolerance_last_kind;

static void
count_tolerance_report(void *context, const bm_tolerance_event_t *event)
{
    assert(context == &tolerance_reports);
    ++tolerance_reports;
    tolerance_last_kind = event->kind;
}

static void
test_tolerance_is_opt_in(void)
{
    const bm_host_tolerance_services_t tolerance = {
        count_tolerance_report, &tolerance_reports
    };
    const bm_host_tolerance_services_t silent = { NULL, NULL };
    bm_host_capability_t entry = {
        BM_HOST_CAPABILITY_TOLERANCE, BM_HOST_TOLERANCE_VERSION,
        sizeof(tolerance), &tolerance
    };
    bm_host_services_t host = bm_null_host_services();
    const bm_tolerance_event_t event = {
        sizeof(bm_tolerance_event_t), BM_TOLERANCE_UNKNOWN_DEVICE_COMMAND,
        0U, "test", "ignored", 0U, 0U, 0U, 0U
    };

    /* A host without the capability is strict, and reports go nowhere. */
    assert(bm_host_tolerance(&host) == NULL);
    assert(bm_host_tolerance(NULL) == NULL);
    bm_tolerance_report(NULL, &event);

    host.capabilities = &entry;
    host.capability_count = 1U;
    assert(bm_host_tolerance(&host) == &tolerance);
    bm_tolerance_report(bm_host_tolerance(&host), &event);
    bm_tolerance_report(bm_host_tolerance(&host), NULL);
    assert(tolerance_reports == 1U);
    assert(tolerance_last_kind == BM_TOLERANCE_UNKNOWN_DEVICE_COMMAND);

    /* A table without a report function, or one too small, stays strict. */
    entry.services = &silent;
    assert(bm_host_tolerance(&host) == NULL);
    entry.services = &tolerance;
    entry.services_size = sizeof(tolerance) - 1U;
    assert(bm_host_tolerance(&host) == NULL);
}

int
main(void)
{
    test_empty_host_reports_unsupported();
    test_tolerance_is_opt_in();
    test_capability_lookup_and_requirements();
    test_lookup_argument_validation();
    test_registry_validation();
    return 0;
}
