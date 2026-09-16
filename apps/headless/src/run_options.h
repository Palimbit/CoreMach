/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_HEADLESS_RUN_OPTIONS_H
#define BLUMACH_HEADLESS_RUN_OPTIONS_H

#include "text_input.h"

#include <stddef.h>
#include <stdint.h>

#define HEADLESS_MAX_TEXT_ACTIONS 16U
#define HEADLESS_MAX_ACTION_TEXT 256U

typedef struct headless_run_options {
    const char *machine_id;
    const char *firmware_even_path;
    const char *firmware_odd_path;
    const char *floppy_path;
    const char *frame_path;
    const char *scenario_path;
    uint64_t ticks;
    uint64_t key_ticks;
    headless_text_action_t text_actions[HEADLESS_MAX_TEXT_ACTIONS];
    char scenario_text[HEADLESS_MAX_TEXT_ACTIONS][HEADLESS_MAX_ACTION_TEXT];
    size_t text_action_count;
    uint32_t expected_frame_crc32;
    int expect_frame_crc32;
} headless_run_options_t;

#endif
