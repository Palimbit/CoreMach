/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors */
#ifndef BM_AT_CLOCK_PRIVATE_H
#define BM_AT_CLOCK_PRIVATE_H
#include <blumach/components/at_clock.h>
/* Internal dispatch only. Callbacks, busy and owner point into the borrowed
 * device's lifetime; ops must have static storage. No public generic plugin ABI. */
typedef struct bm_at_clock_device_ops {
    bm_status_t (*advance)(void *, uint64_t);
    bm_status_t (*next)(const void *, uint64_t *);
    bm_status_t (*io)(void *, bm_bus_transaction_t *);
    bm_status_t (*reset)(void *);
} bm_at_clock_device_ops_t;
bm_status_t bm_at_clock_attach(const bm_host_services_t *host, bm_engine_t *engine,
    void *device, const bm_clock_rate_t *rate, const bm_at_clock_device_ops_t *ops,
    int *busy, void **owner, bm_at_clock_link_t **out_link);
/* Typed external-input dispatch. Identity prevents applying keyboard mutations
 * to a PIT/RTC/KBC link. One guarded sync/mutate/rearm interval, not callbacks
 * between independently attached peers. No new public generic plugin ABI. */
bm_status_t bm_at_clock_apply(bm_at_clock_link_t *link,
    const bm_at_clock_device_ops_t *identity,
    bm_status_t (*change)(void *, const void *), const void *input);
int bm_at_clock_rates_equal(const bm_clock_rate_t *a, const bm_clock_rate_t *b);
#endif
