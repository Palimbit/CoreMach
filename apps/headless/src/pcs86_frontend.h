/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_HEADLESS_PCS86_FRONTEND_H
#define BLUMACH_HEADLESS_PCS86_FRONTEND_H

#include "text_input.h"

#include <stddef.h>
#include <stdint.h>

#define HEADLESS_MAX_TEXT_ACTIONS 16U

typedef struct headless_run_options {
    const char *machine_id;
    const char *firmware_even_path;
    const char *firmware_odd_path;
    const char *floppy_path;
    const char *frame_path;
    uint64_t ticks;
    uint64_t key_ticks;
    headless_text_action_t text_actions[HEADLESS_MAX_TEXT_ACTIONS];
    size_t text_action_count;
    uint32_t expected_frame_crc32;
    int expect_frame_crc32;
} headless_run_options_t;

int headless_run_pcs86(const headless_run_options_t *options);

#endif
