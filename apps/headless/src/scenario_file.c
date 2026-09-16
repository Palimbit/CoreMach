/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "scenario_file.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define SCENARIO_HEADER "blumach-headless-scenario-v1"
#define SCENARIO_LINE_CAPACITY 512U

static int
parse_unsigned(const char *text, int base, uint64_t maximum, int nonzero,
               uint64_t *value)
{
    char *end = NULL;
    const char *cursor;
    unsigned long long parsed;
    if ((text == NULL) || (text[0] == '\0'))
        return 0;
    for (cursor = text; *cursor != '\0'; ++cursor) {
        const int digit = ((*cursor >= '0') && (*cursor <= '9')) ||
                          ((base == 16) && (*cursor >= 'a') && (*cursor <= 'f')) ||
                          ((base == 16) && (*cursor >= 'A') && (*cursor <= 'F'));
        if (!digit)
            return 0;
    }
    errno = 0;
    parsed = strtoull(text, &end, base);
    if ((errno == ERANGE) || (end == NULL) || (*end != '\0') ||
        (nonzero && (parsed == 0U)) || ((uint64_t) parsed > maximum))
        return 0;
    *value = (uint64_t) parsed;
    return 1;
}

static int
store_action(headless_run_options_t *options, char *value)
{
    char *separator = strchr(value, ':');
    uint64_t at;
    size_t length;
    size_t index = options->text_action_count;
    if ((separator == NULL) || (separator == value) || (separator[1] == '\0') ||
        (index >= HEADLESS_MAX_TEXT_ACTIONS))
        return 0;
    *separator = '\0';
    if (!parse_unsigned(value, 10, UINT64_MAX, 1, &at))
        return 0;
    length = strlen(separator + 1);
    if (length >= HEADLESS_MAX_ACTION_TEXT)
        return 0;
    memcpy(options->scenario_text[index], separator + 1, length + 1U);
    options->text_actions[index] = (headless_text_action_t) {
        options->scenario_text[index], at
    };
    ++options->text_action_count;
    return 1;
}

int
headless_scenario_parse(FILE *file, headless_run_options_t *options)
{
    char line[SCENARIO_LINE_CAPACITY];
    int header_seen = 0;
    int ticks_seen = 0;
    int key_ticks_seen = 0;
    int crc_seen = 0;
    if ((file == NULL) || (options == NULL))
        return 0;
    options->text_action_count = 0U;
    options->expect_frame_crc32 = 0;
    while (fgets(line, sizeof(line), file) != NULL) {
        size_t length = strlen(line);
        uint64_t parsed;
        if ((length != 0U) && (line[length - 1U] == '\n'))
            line[--length] = '\0';
        else if (!feof(file))
            return 0;
        if ((length != 0U) && (line[length - 1U] == '\r'))
            line[--length] = '\0';
        if ((length == 0U) || (line[0] == '#'))
            continue;
        if (!header_seen) {
            if (strcmp(line, SCENARIO_HEADER) != 0)
                return 0;
            header_seen = 1;
        } else if (strncmp(line, "ticks=", 6U) == 0) {
            if (ticks_seen ||
                !parse_unsigned(line + 6, 10, UINT64_MAX, 1,
                                &options->ticks))
                return 0;
            ticks_seen = 1;
        } else if (strncmp(line, "key_ticks=", 10U) == 0) {
            if (key_ticks_seen ||
                !parse_unsigned(line + 10, 10, UINT64_MAX, 1,
                                &options->key_ticks))
                return 0;
            key_ticks_seen = 1;
        } else if (strncmp(line, "type=", 5U) == 0) {
            if (!store_action(options, line + 5))
                return 0;
        } else if (strncmp(line, "expect_frame_crc32=", 19U) == 0) {
            if (crc_seen ||
                !parse_unsigned(line + 19, 16, UINT32_MAX, 0, &parsed))
                return 0;
            options->expected_frame_crc32 = (uint32_t) parsed;
            options->expect_frame_crc32 = 1;
            crc_seen = 1;
        } else {
            return 0;
        }
    }
    return !ferror(file) && header_seen && ticks_seen &&
           (headless_text_schedule_validate(
                options->text_actions, options->text_action_count,
                options->key_ticks, options->ticks) == BM_STATUS_OK);
}

int
headless_scenario_load(const char *path, headless_run_options_t *options)
{
    FILE *file = NULL;
    int result;
    if ((path == NULL) || (options == NULL))
        return 0;
#ifdef _MSC_VER
    if (fopen_s(&file, path, "rb") != 0)
        file = NULL;
#else
    file = fopen(path, "rb");
#endif
    if (file == NULL)
        return 0;
    result = headless_scenario_parse(file, options);
    if (fclose(file) != 0)
        result = 0;
    return result;
}
