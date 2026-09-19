/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "machine_runner.h"
#include "scenario_file.h"

#include <blumach/engine/version.h>
#include <blumach/frontend/frontend.h>
#include <blumach/platforms/null_host.h>
#include <blumach/runtime/runtime.h>

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
print_usage(const char *program)
{
    fprintf(stderr,
            "usage:\n"
            "  %s --version\n"
            "  %s --list-machines\n"
            "  %s --describe <machine-id>\n"
            "  %s --machine <machine-id> --firmware-even <path>"
            " --firmware-odd <path> [--floppy <path>]"
            " [--swap-floppy-at <tick> --swap-floppy <path>]"
            " [--hard-disk <path> | --working-hard-disk <path>]"
            " [--persistent-state <role=path> | --depleted-state <role>]"
            " [--ticks <count>]"
            " [--frame <path>] [--type-at <tick> --type-text <text>]..."
            " [--key-ticks <count>] [--trace-tail <1..4096>]"
            " [--trace-memory <all|hex[-hex]>]"
            " [--trace-memory-image <path>]"
            " [--trace-only memory|writes|io]"
            " [--freeze-trace-at <cs>:<ip>]"
            " [--freeze-trace-physical <hex-address>]"
            " [--freeze-trace-after <event-sequence>]"
            " [--expect-frame-crc32 <hex>]\n"
            "  %s --machine <machine-id> --firmware-even <path>"
            " --firmware-odd <path> [--floppy <path>]"
            " [--swap-floppy-at <tick> --swap-floppy <path>]"
            " [--hard-disk <path> | --working-hard-disk <path>]"
    " [--persistent-state <role=path> | --depleted-state <role>]"
            " --scenario <path> [--frame <path>] [--trace-tail <1..4096>]\n"
            "     scenario v2 adds key=<tick>:<name>:down|up and"
            " tap=<tick>:<name>\n"
            "     text accepts \\n, \\r, \\t, \\b and \\\\ escapes\n",
            program, program, program, program, program);
}

static int
parse_persistent_state(const char *value, char *role, size_t role_size,
                       const char **path)
{
    const char *separator = value != NULL ? strchr(value, '=') : NULL;
    size_t length;
    if ((separator == NULL) || (separator == value) ||
        (separator[1] == '\0'))
        return 0;
    length = (size_t) (separator - value);
    if (length >= role_size)
        return 0;
    memcpy(role, value, length);
    role[length] = '\0';
    *path = separator + 1;
    return 1;
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
parse_memory_range(const char *value, uint32_t *first, uint32_t *last)
{
    const char *separator;
    char left[9];
    char right[9];
    size_t left_length;
    size_t right_length;
    if ((value == NULL) || (first == NULL) || (last == NULL))
        return 0;
    if (strcmp(value, "all") == 0) {
        *first = 0U;
        *last = UINT32_MAX;
        return 1;
    }
    separator = strchr(value, '-');
    if (separator == NULL) {
        if (!parse_crc32(value, first))
            return 0;
        *last = *first;
        return 1;
    }
    left_length = (size_t) (separator - value);
    right_length = strlen(separator + 1);
    if ((left_length == 0U) || (left_length >= sizeof(left)) ||
        (right_length == 0U) || (right_length >= sizeof(right)))
        return 0;
    memcpy(left, value, left_length);
    left[left_length] = '\0';
    memcpy(right, separator + 1, right_length + 1U);
    return parse_crc32(left, first) && parse_crc32(right, last) &&
           (*first <= *last);
}

static int
parse_cs_ip(const char *value, uint16_t *cs, uint16_t *ip)
{
    const char *separator;
    char left[5];
    char right[5];
    uint32_t parsed_cs;
    uint32_t parsed_ip;
    size_t left_length;
    size_t right_length;
    if ((value == NULL) || (cs == NULL) || (ip == NULL) ||
        ((separator = strchr(value, ':')) == NULL))
        return 0;
    left_length = (size_t) (separator - value);
    right_length = strlen(separator + 1);
    if ((left_length == 0U) || (left_length >= sizeof(left)) ||
        (right_length == 0U) || (right_length >= sizeof(right)))
        return 0;
    memcpy(left, value, left_length);
    left[left_length] = '\0';
    memcpy(right, separator + 1, right_length + 1U);
    if (!parse_crc32(left, &parsed_cs) || !parse_crc32(right, &parsed_ip) ||
        (parsed_cs > UINT16_MAX) || (parsed_ip > UINT16_MAX))
        return 0;
    *cs = (uint16_t) parsed_cs;
    *ip = (uint16_t) parsed_ip;
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
    int scenario_was_set = 0;
    int persistent_state_was_set = 0;
    int depleted_state_was_set = 0;
    int trace_tail_was_set = 0;
    int trace_memory_was_set = 0;
    int trace_memory_image_was_set = 0;
    int freeze_trace_was_set = 0;
    int trace_only_was_set = 0;
    int swap_floppy_at_was_set = 0;
    int swap_floppy_was_set = 0;

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
        } else if (strcmp(argument, "--swap-floppy") == 0) {
            if (!assign_once(&options->swap_floppy_path, value))
                return 0;
            swap_floppy_was_set = 1;
        } else if (strcmp(argument, "--swap-floppy-at") == 0) {
            if (swap_floppy_at_was_set ||
                !parse_ticks(value, &options->swap_floppy_at))
                return 0;
            swap_floppy_at_was_set = 1;
        } else if (strcmp(argument, "--hard-disk") == 0) {
            if (!assign_once(&options->hard_disk_path, value))
                return 0;
        } else if (strcmp(argument, "--working-hard-disk") == 0) {
            if (!assign_once(&options->hard_disk_path, value))
                return 0;
            options->hard_disk_writable = 1;
        } else if (strcmp(argument, "--frame") == 0) {
            if (!assign_once(&options->frame_path, value))
                return 0;
        } else if (strcmp(argument, "--scenario") == 0) {
            if (!assign_once(&options->scenario_path, value))
                return 0;
            scenario_was_set = 1;
        } else if (strcmp(argument, "--persistent-state") == 0) {
            if (persistent_state_was_set || depleted_state_was_set ||
                !parse_persistent_state(
                    value, options->persistent_state_role,
                    sizeof(options->persistent_state_role),
                    &options->persistent_state_path))
                return 0;
            persistent_state_was_set = 1;
        } else if (strcmp(argument, "--depleted-state") == 0) {
            if (persistent_state_was_set || depleted_state_was_set ||
                (value[0] == '\0') ||
                (strlen(value) >= sizeof(options->depleted_state_role)))
                return 0;
            memcpy(options->depleted_state_role, value, strlen(value) + 1U);
            depleted_state_was_set = 1;
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
        } else if (strcmp(argument, "--trace-tail") == 0) {
            uint64_t count;
            if (trace_tail_was_set || !parse_ticks(value, &count) ||
                (count > 4096U))
                return 0;
            options->trace_tail = (size_t) count;
            trace_tail_was_set = 1;
        } else if (strcmp(argument, "--trace-memory") == 0) {
            if (trace_memory_was_set ||
                !parse_memory_range(value, &options->trace_memory_first,
                                    &options->trace_memory_last))
                return 0;
            options->trace_memory = 1;
            trace_memory_was_set = 1;
        } else if (strcmp(argument, "--trace-memory-image") == 0) {
            if (trace_memory_image_was_set ||
                !assign_once(&options->trace_memory_image_path, value))
                return 0;
            trace_memory_image_was_set = 1;
        } else if (strcmp(argument, "--freeze-trace-at") == 0) {
            if (freeze_trace_was_set ||
                !parse_cs_ip(value, &options->freeze_trace_cs,
                             &options->freeze_trace_ip))
                return 0;
            options->freeze_trace_at = 1;
            freeze_trace_was_set = 1;
        } else if (strcmp(argument, "--freeze-trace-physical") == 0) {
            if (freeze_trace_was_set ||
                !parse_crc32(value, &options->freeze_trace_physical))
                return 0;
            options->freeze_trace_at_physical = 1;
            freeze_trace_was_set = 1;
        } else if (strcmp(argument, "--freeze-trace-after") == 0) {
            if (freeze_trace_was_set ||
                !parse_ticks(value, &options->freeze_trace_sequence))
                return 0;
            options->freeze_trace_after_sequence = 1;
            freeze_trace_was_set = 1;
        } else if (strcmp(argument, "--trace-only") == 0) {
            if (trace_only_was_set ||
                ((strcmp(value, "memory") != 0) &&
                 (strcmp(value, "writes") != 0) &&
                 (strcmp(value, "io") != 0)))
                return 0;
            if (strcmp(value, "memory") == 0)
                options->trace_only_memory = 1;
            else if (strcmp(value, "writes") == 0)
                options->trace_only_memory_writes = 1;
            else
                options->trace_only_io = 1;
            trace_only_was_set = 1;
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
    if (pending_type_at ||
        (swap_floppy_at_was_set != swap_floppy_was_set) ||
        ((trace_memory_was_set || freeze_trace_was_set) &&
         !trace_tail_was_set) ||
        ((options->trace_only_memory || options->trace_only_memory_writes) &&
         !trace_memory_was_set) ||
        (trace_memory_image_was_set &&
         (!trace_memory_was_set ||
          ((uint64_t) options->trace_memory_last -
           options->trace_memory_first + 1U > UINT64_C(1048576)))) ||
        (scenario_was_set &&
         (ticks_was_set || key_ticks_was_set || frame_crc_was_set ||
          (options->text_action_count != 0U))) ||
        (!scenario_was_set && key_ticks_was_set &&
         (options->text_action_count == 0U)))
        return 0;
    if (scenario_was_set &&
        !headless_scenario_load(options->scenario_path, options))
        return 0;
    if (swap_floppy_at_was_set &&
        (options->swap_floppy_at >= options->ticks))
        return 0;
    return
           (options->machine_id != NULL) &&
           (options->firmware_even_path != NULL) &&
           (options->firmware_odd_path != NULL);
}

static int
create_registry(const bm_host_services_t *host, bm_machine_registry_t **registry)
{
    bm_status_t status = bm_machine_registry_create(
        host, bm_frontend_adapter_count(), registry);
    if (status == BM_STATUS_OK)
        status = bm_frontend_register_machines(*registry);
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
    const bm_frontend_adapter_t *adapter;
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
            printf("id=%s scheduler_hz=%" PRIu64
                   " config_type=%s config_version=%" PRIu32
                   " config_size=%zu max_cpus=%zu max_events=%zu"
                   " max_timed_sources=%zu\n",
                   definition->id, definition->scheduler_ticks_per_second,
                   definition->configuration.type,
                   definition->configuration.version,
                   definition->configuration.size, definition->engine.max_cpus,
                   definition->engine.max_events,
                   definition->engine.max_timed_sources);
        }
    } else if (parse_run_options(argc, argv, &options)) {
        if (bm_machine_registry_find(registry, options.machine_id,
                                     &definition) != BM_STATUS_OK) {
            fprintf(stderr, "unknown machine: %s\n", options.machine_id);
            result = 3;
        } else if ((adapter = bm_frontend_adapter_find(definition->id)) ==
                   NULL) {
            fprintf(stderr, "no headless adapter for machine: %s\n",
                    definition->id);
            result = 3;
        } else {
            result = headless_run_machine(adapter, &options);
        }
    } else {
        print_usage(argv[0]);
        result = 2;
    }
    bm_machine_registry_destroy(registry);
    return result;
}
