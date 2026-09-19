/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_HEADLESS_RUN_OPTIONS_H
#define BLUMACH_HEADLESS_RUN_OPTIONS_H

#include "text_input.h"

#include <stddef.h>
#include <stdint.h>

#define HEADLESS_MAX_TEXT_ACTIONS 16U
#define HEADLESS_MAX_KEY_ACTIONS 64U
#define HEADLESS_MAX_ACTION_TEXT 256U
#define HEADLESS_MAX_STATE_ROLE 64U

typedef struct headless_run_options {
    const char *machine_id;
    const char *firmware_even_path;
    const char *firmware_odd_path;
    const char *floppy_path;
    const char *swap_floppy_path;
    uint64_t swap_floppy_at;
    const char *hard_disk_path;
    int hard_disk_writable;
    const char *frame_path;
    const char *scenario_path;
    char persistent_state_role[HEADLESS_MAX_STATE_ROLE];
    const char *persistent_state_path;
    char depleted_state_role[HEADLESS_MAX_STATE_ROLE];
    uint64_t ticks;
    uint64_t key_ticks;
    size_t trace_tail;
    int trace_memory;
    uint32_t trace_memory_first;
    uint32_t trace_memory_last;
    const char *trace_memory_image_path;
    uint16_t freeze_trace_cs;
    uint16_t freeze_trace_ip;
    int freeze_trace_at;
    uint32_t freeze_trace_physical;
    int freeze_trace_at_physical;
    uint64_t freeze_trace_sequence;
    int freeze_trace_after_sequence;
    int trace_only_memory;
    int trace_only_memory_writes;
    int trace_only_io;
    headless_text_action_t text_actions[HEADLESS_MAX_TEXT_ACTIONS];
    char scenario_text[HEADLESS_MAX_TEXT_ACTIONS][HEADLESS_MAX_ACTION_TEXT];
    size_t text_action_count;
    headless_key_action_t key_actions[HEADLESS_MAX_KEY_ACTIONS];
    size_t key_action_count;
    uint32_t expected_frame_crc32;
    int expect_frame_crc32;
} headless_run_options_t;

#endif
