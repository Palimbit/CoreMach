/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_HEADLESS_PCS86_FRONTEND_H
#define BLUMACH_HEADLESS_PCS86_FRONTEND_H

#include <stdint.h>

typedef struct headless_run_options {
    const char *machine_id;
    const char *firmware_even_path;
    const char *firmware_odd_path;
    const char *floppy_path;
    const char *frame_path;
    const char *type_text;
    uint64_t ticks;
    uint64_t type_at;
    uint64_t key_ticks;
} headless_run_options_t;

int headless_run_pcs86(const headless_run_options_t *options);

#endif
