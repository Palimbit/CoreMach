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
    uint64_t ticks;
} headless_run_options_t;

int headless_run_pcs86(const headless_run_options_t *options);

#endif
