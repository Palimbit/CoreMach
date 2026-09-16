/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_HEADLESS_MACHINE_RUNNER_H
#define BLUMACH_HEADLESS_MACHINE_RUNNER_H

#include "run_options.h"

#include <blumach/frontend/frontend.h>

int headless_run_machine(const bm_frontend_adapter_t *adapter,
                         const headless_run_options_t *options);

#endif
