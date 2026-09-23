/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_TEST_BUS_FIXTURE_H
#define BLUMACH_TEST_BUS_FIXTURE_H
#include <blumach/components/bus.h>
#include <stddef.h>

/* Authored synthetic endpoint, not a model of any historical chip. */
typedef struct fixture_access {
    bm_bus_transaction_t request;
    bm_status_t result;
    uint64_t response;
} fixture_access_t;
typedef struct bus_fixture {
    uint8_t bytes[256];
    uint64_t base;
    uint32_t waits;
    bm_status_t result;
    size_t reads, writes, fetches;
    fixture_access_t trace[64];
    size_t trace_count;
} bus_fixture_t;
void fixture_init(bus_fixture_t *fixture, uint64_t base);
bm_status_t fixture_access(void *context, bm_bus_transaction_t *transaction);
#endif
