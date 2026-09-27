/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors */
#ifndef BLUMACH_WRITE_BUFFER_286_H
#define BLUMACH_WRITE_BUFFER_286_H
#include <blumach/components/bus.h>
#include <stdbool.h>

/* Single pending-write slot, Intel210760-002 p3-25. A snapshot, not a
 * deferred pointer into CPU registers. Active bus-transfer storage belongs
 * to the caller; issue moves the slot into that storage after a bus grant.
 * No electrical register lifetime, EU overlap latency or ISA wait inferred.
 * Zero-initialize. CPU/board already split odd words/8-bit target accesses.
 */
typedef struct bm_286_write_buffer {
    bool pending;
    bm_bus_transaction_t request;
} bm_286_write_buffer_t;
bm_status_t bm_286_write_buffer_put(bm_286_write_buffer_t *b, const bm_bus_transaction_t *t);
/* granted means eligible AND physically granted, not merely bus idle.
 * IDLE leaves buffer/output untouched. Reset/discard policy owned by CPU. */
bm_status_t bm_286_write_buffer_issue(bm_286_write_buffer_t *b, bool granted, bm_bus_transaction_t *out);
#endif
