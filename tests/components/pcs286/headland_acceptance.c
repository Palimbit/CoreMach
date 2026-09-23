/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Lifecycle/query gate only. Register/map truth tables still require evidence.
 */
#include "failure_injection_host.h"
#include <blumach/components/headland_gc10x.h>
#include <assert.h>

int main(void)
{
    bm_gc10x_config_t c = {0};
    size_t fail;
    int reached_success = 0;
    c.size = sizeof(c);
    c.version = BM_GC10X_CONTRACT_VERSION;
    c.ram_bytes = 1024U * 1024U;
    c.profile = BM_GC10X_PROFILE_PCS286;
    for (fail = 0U; fail < 64U; ++fail) {
        failure_injection_host_t tracker;
        bm_host_services_t host;
        bm_gc10x_t *chipset = NULL;
        bm_status_t status;
        failure_injection_host_initialize(&tracker);
        failure_injection_host_fail_on(&tracker, fail);
        host = failure_injection_host_services(&tracker);
        status = bm_gc10x_create(&host, &c, &chipset);
        if (status == BM_STATUS_OK) {
            bm_gc10x_route_t a = {0}, b = {0};
            bm_gc10x_reset(chipset);
            assert(bm_gc10x_resolve(chipset, BM_GC10X_CPU, 1, 0x1000U,
                                    BM_BUS_READ, &a) == BM_STATUS_OK);
            assert(bm_gc10x_resolve(chipset, BM_GC10X_CPU, 1, 0x1000U,
                                    BM_BUS_READ, &b) == BM_STATUS_OK);
            assert(a.target == b.target && a.offset == b.offset);
            assert(a.contiguous_bytes > 0U && a.contiguous_bytes == b.contiguous_bytes);
            assert(a.extra_memory_clocks == b.extra_memory_clocks && a.writable == b.writable);
            assert(a.wait_quality == b.wait_quality);
            assert(bm_gc10x_resolve(chipset, BM_GC10X_CPU, 1, 0x1000000U,
                                    BM_BUS_READ, &b) == BM_STATUS_INVALID_ARGUMENT);
            bm_gc10x_destroy(chipset);
            reached_success = 1;
        } else {
            assert(status == BM_STATUS_OUT_OF_MEMORY && chipset == NULL);
        }
        assert(tracker.outstanding_allocations == 0U);
        if (reached_success) break;
    }
    assert(reached_success);
    bm_gc10x_destroy(NULL);
    return 0;
}
