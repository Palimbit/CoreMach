/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_TESTS_FAILURE_INJECTION_HOST_H
#define BLUMACH_TESTS_FAILURE_INJECTION_HOST_H

#include <blumach/engine/host.h>

#include <stddef.h>

typedef struct failure_injection_host {
    size_t allocation_calls;
    size_t fail_on_call;
    size_t outstanding_allocations;
} failure_injection_host_t;

void failure_injection_host_initialize(failure_injection_host_t *host);
bm_host_services_t failure_injection_host_services(failure_injection_host_t *host);
void failure_injection_host_fail_on(failure_injection_host_t *host,
                                    size_t absolute_call);
void failure_injection_host_fail_after(failure_injection_host_t *host,
                                       size_t additional_calls);

#endif
