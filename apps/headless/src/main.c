/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "pcs86_frontend.h"

#include <blumach/engine/version.h>
#include <blumach/platforms/null_host.h>
#include <blumach/runtime/runtime.h>
#include <blumach/systems/olivetti_pcs86.h>

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int (*headless_run_fn)(const headless_run_options_t *options);

typedef struct headless_machine_adapter {
    const bm_machine_definition_t *(*definition)(void);
    headless_run_fn run;
} headless_machine_adapter_t;

static const headless_machine_adapter_t machine_adapters[] = {
    { bm_pcs86_machine_definition, headless_run_pcs86 }
};

static void
print_usage(const char *program)
{
    fprintf(stderr,
            "usage:\n"
            "  %s --version\n"
            "  %s --list-machines\n"
            "  %s --describe <machine-id>\n"
            "  %s --machine <machine-id> --firmware-even <path>"
            " --firmware-odd <path> [--floppy <path>] [--ticks <count>]"
            " [--frame <path>] [--type-at <tick> --type-text <text>]..."
            " [--key-ticks <count>] [--expect-frame-crc32 <hex>]\n"
            "     text accepts \\n, \\r, \\t, \\b and \\\\ escapes\n",
            program, program, program, program);
}

static int
parse_crc32(const char *value, uint32_t *crc)
{
    char *end = NULL;
    const char *cursor;
    unsigned long parsed;
    size_t length;
    if ((value == NULL) || (crc == NULL))
        return 0;
    length = strlen(value);
    if ((length == 0U) || (length > 8U))
        return 0;
    for (cursor = value; *cursor != '\0'; ++cursor) {
        if (!(((*cursor >= '0') && (*cursor <= '9')) ||
              ((*cursor >= 'a') && (*cursor <= 'f')) ||
              ((*cursor >= 'A') && (*cursor <= 'F'))))
            return 0;
    }
    errno = 0;
    parsed = strtoul(value, &end, 16);
    if ((errno == ERANGE) || (end == NULL) || (*end != '\0') ||
        (parsed > UINT32_MAX))
        return 0;
    *crc = (uint32_t) parsed;
    return 1;
}

static int
assign_once(const char **target, const char *value)
{
    if ((*target != NULL) || (value == NULL) || (value[0] == '\0'))
        return 0;
    *target = value;
    return 1;
}

static int
parse_ticks(const char *value, uint64_t *ticks)
{
    char *end = NULL;
    const char *cursor;
    unsigned long long parsed;

    if ((value == NULL) || (value[0] == '\0'))
        return 0;
    for (cursor = value; *cursor != '\0'; ++cursor) {
        if ((*cursor < '0') || (*cursor > '9'))
            return 0;
    }
    errno = 0;
    parsed = strtoull(value, &end, 10);
    if ((errno == ERANGE) || (end == NULL) || (*end != '\0') ||
        (parsed == 0U))
        return 0;
    *ticks = (uint64_t) parsed;
    return 1;
}

static int
parse_run_options(int argc, char **argv, headless_run_options_t *options)
{
    int index;
    int ticks_was_set = 0;
    int pending_type_at = 0;
    int key_ticks_was_set = 0;
    int frame_crc_was_set = 0;

    *options = (headless_run_options_t) { 0 };
    options->ticks = UINT64_C(10000000);
    options->key_ticks = UINT64_C(2000);
    for (index = 1; index < argc; ++index) {
        const char *argument = argv[index];
        const char *value;
        if (++index >= argc)
            return 0;
        value = argv[index];
        if (strcmp(argument, "--machine") == 0) {
            if (!assign_once(&options->machine_id, value))
                return 0;
        } else if (strcmp(argument, "--firmware-even") == 0) {
            if (!assign_once(&options->firmware_even_path, value))
                return 0;
        } else if (strcmp(argument, "--firmware-odd") == 0) {
            if (!assign_once(&options->firmware_odd_path, value))
                return 0;
        } else if (strcmp(argument, "--floppy") == 0) {
            if (!assign_once(&options->floppy_path, value))
                return 0;
        } else if (strcmp(argument, "--frame") == 0) {
            if (!assign_once(&options->frame_path, value))
                return 0;
        } else if (strcmp(argument, "--type-text") == 0) {
            if (!pending_type_at ||
                !assign_once(&options->text_actions[
                    options->text_action_count].text, value))
                return 0;
            ++options->text_action_count;
            pending_type_at = 0;
        } else if (strcmp(argument, "--ticks") == 0) {
            if (ticks_was_set || !parse_ticks(value, &options->ticks))
                return 0;
            ticks_was_set = 1;
        } else if (strcmp(argument, "--type-at") == 0) {
            if (pending_type_at ||
                (options->text_action_count >= HEADLESS_MAX_TEXT_ACTIONS) ||
                !parse_ticks(value, &options->text_actions[
                    options->text_action_count].at))
                return 0;
            pending_type_at = 1;
        } else if (strcmp(argument, "--key-ticks") == 0) {
            if (key_ticks_was_set || !parse_ticks(value, &options->key_ticks))
                return 0;
            key_ticks_was_set = 1;
        } else if (strcmp(argument, "--expect-frame-crc32") == 0) {
            if (frame_crc_was_set ||
                !parse_crc32(value, &options->expected_frame_crc32))
                return 0;
            frame_crc_was_set = 1;
            options->expect_frame_crc32 = 1;
        } else {
            return 0;
        }
    }
    return !pending_type_at &&
           (!key_ticks_was_set || (options->text_action_count != 0U)) &&
           (options->machine_id != NULL) &&
           (options->firmware_even_path != NULL) &&
           (options->firmware_odd_path != NULL);
}

static const headless_machine_adapter_t *
find_adapter(const bm_machine_definition_t *definition)
{
    size_t index;
    for (index = 0U;
         index < sizeof(machine_adapters) / sizeof(machine_adapters[0]);
         ++index) {
        if (machine_adapters[index].definition() == definition)
            return &machine_adapters[index];
    }
    return NULL;
}

static int
create_registry(const bm_host_services_t *host, bm_machine_registry_t **registry)
{
    size_t index;
    bm_status_t status = bm_machine_registry_create(
        host, sizeof(machine_adapters) / sizeof(machine_adapters[0]), registry);
    for (index = 0U;
         (status == BM_STATUS_OK) &&
         (index < sizeof(machine_adapters) / sizeof(machine_adapters[0]));
         ++index)
        status = bm_machine_registry_register(
            *registry, machine_adapters[index].definition());
    if (status != BM_STATUS_OK) {
        bm_machine_registry_destroy(*registry);
        *registry = NULL;
        return 0;
    }
    return 1;
}

int
main(int argc, char **argv)
{
    bm_host_services_t host = bm_null_host_services();
    bm_machine_registry_t *registry = NULL;
    const bm_machine_definition_t *definition = NULL;
    const headless_machine_adapter_t *adapter;
    headless_run_options_t options;
    int result = 0;

    if ((argc == 2) && (strcmp(argv[1], "--version") == 0)) {
        puts("BluMach portable engine " BM_ENGINE_VERSION);
        return 0;
    }
    if (!create_registry(&host, &registry)) {
        fputs("could not create the machine registry\n", stderr);
        return 1;
    }
    if ((argc == 2) && (strcmp(argv[1], "--list-machines") == 0)) {
        size_t index;
        for (index = 0U; index < bm_machine_registry_count(registry); ++index) {
            if (bm_machine_registry_at(registry, index, &definition) ==
                BM_STATUS_OK)
                puts(definition->id);
        }
    } else if ((argc == 3) && (strcmp(argv[1], "--describe") == 0)) {
        if (bm_machine_registry_find(registry, argv[2], &definition) !=
            BM_STATUS_OK) {
            fprintf(stderr, "unknown machine: %s\n", argv[2]);
            result = 3;
        } else {
            printf("id=%s config_type=%s config_version=%" PRIu32
                   " config_size=%zu max_cpus=%zu max_events=%zu\n",
                   definition->id, definition->configuration.type,
                   definition->configuration.version,
                   definition->configuration.size, definition->engine.max_cpus,
                   definition->engine.max_events);
        }
    } else if (parse_run_options(argc, argv, &options)) {
        if (bm_machine_registry_find(registry, options.machine_id,
                                     &definition) != BM_STATUS_OK) {
            fprintf(stderr, "unknown machine: %s\n", options.machine_id);
            result = 3;
        } else if ((adapter = find_adapter(definition)) == NULL) {
            fprintf(stderr, "no headless adapter for machine: %s\n",
                    definition->id);
            result = 3;
        } else {
            result = adapter->run(&options);
        }
    } else {
        print_usage(argv[0]);
        result = 2;
    }
    bm_machine_registry_destroy(registry);
    return result;
}
