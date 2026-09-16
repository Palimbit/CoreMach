/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_HEADLESS_SCENARIO_FILE_H
#define BLUMACH_HEADLESS_SCENARIO_FILE_H

#include "run_options.h"

#include <stdio.h>

int headless_scenario_parse(FILE *file, headless_run_options_t *options);
int headless_scenario_load(const char *path, headless_run_options_t *options);

#endif
