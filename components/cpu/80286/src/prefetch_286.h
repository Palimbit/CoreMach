/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors */
#ifndef BLUMACH_PREFETCH_286_H
#define BLUMACH_PREFETCH_286_H
#include <blumach/engine/types.h>
#include <stddef.h>

/* Experimental private FIFO, NOT connected to the interpreter.
 * Intel 210760-002 p3-24: six bytes, refill when two slots free, initial
 * odd physical control-transfer target fetched as byte, then words.
 * Caller supplies an already validated contiguous physical window and bus
 * grants. No segment wrap, faults, arbitration, IU queue or timing is modeled.
 * Zero-initialize once before redirect. Never reinitialize during a request.
 * Single owner, no callbacks. Errors leave output/state unchanged.
 */
typedef struct bm_286_prefetch_request {
    uint64_t ticket;
    uint32_t address;
    uint8_t size;
} bm_286_prefetch_request_t;

typedef struct bm_286_prefetch {
    uint64_t serial;
    uint32_t next, last;
    uint8_t bytes[6], head, count, active, stopped, pending, protected_tail;
    bm_286_prefetch_request_t request;
} bm_286_prefetch_t;

bm_status_t bm_286_prefetch_redirect(bm_286_prefetch_t *q, uint32_t first, uint32_t last);
/* Intel p3-25: protected segment ending at even physical address still fetches
 * a word; its extra byte is discarded. Contiguous non-wrapping window only.
 * Caller must still fault an attempted execution beyond the segment limit. */
bm_status_t bm_286_prefetch_redirect_protected(bm_286_prefetch_t *q, uint32_t first, uint32_t last);
/* Stops issuing new requests; an already granted fetch can still complete.
 * Redirect invalidates it. This is queue policy, not HLT decode timing. */
bm_status_t bm_286_prefetch_stop(bm_286_prefetch_t *q);
bm_status_t bm_286_prefetch_begin(bm_286_prefetch_t *q, bm_286_prefetch_request_t *out);
/* Supply exactly the requested bytes after successful bus completion.
 * Ticket rejects stale completion after a redirect, even to the same address.
 * On bus failure caller must redirect/discard; no fabricated zero-fill. */
bm_status_t bm_286_prefetch_complete(bm_286_prefetch_t *q, uint64_t ticket,
                                    const uint8_t *bytes, size_t length);
bm_status_t bm_286_prefetch_take(bm_286_prefetch_t *q, uint8_t *out);
#endif
