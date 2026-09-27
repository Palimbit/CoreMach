/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2021 Miran Grca
 * Copyright 2026 rtzor, Project BluMach
 * Copyright 2026 BluMach contributors
 * Private AT port61 signal adapter, NOT the complete public PCS286 board. */
#ifndef BM_PCS286_PORT61_H
#define BM_PCS286_PORT61_H
#include <blumach/components/at_clock.h>

typedef enum bm_pcs286_port61_profile {
    BM_PCS286_PORT61_AT_SIGNALS = 1
} bm_pcs286_port61_profile_t;
/* Required board endpoints. Status is PURE (also used by DEBUG), supplies only
 * bits4/6/7 = REF DET, IO CH CK, PCK. Checks accepts semantic enables, the inverse
 * of written bits2/3. No missing endpoint may fabricate successful status.
 * Board owns refresh completion, error capture/clearing and NMI delivery. */
typedef bm_status_t (*bm_pcs286_port61_status_fn)(void *context, uint8_t *bits);
typedef bm_status_t (*bm_pcs286_port61_checks_fn)(void *context,
                                               int ram_enabled, int io_enabled);
typedef struct bm_pcs286_port61_config {
    bm_pcs286_port61_profile_t profile;
    bm_pit8254_t *pit;
    bm_at_clock_link_t *clock; /* Must belong to this PIT. */
    bm_pcs286_port61_status_fn status;
    bm_pcs286_port61_checks_fn checks;
    void *board_context;
    bm_at_line_fn speaker; /* Optional digital AND observer, not analog audio. */
    void *speaker_context;
} bm_pcs286_port61_config_t;
typedef struct bm_pcs286_port61_state {
    uint8_t latch;
    int out2, speaker_level;
    bm_status_t failure;
} bm_pcs286_port61_state_t;
typedef struct bm_pcs286_port61 {
    bm_pcs286_port61_config_t config;
    bm_pcs286_port61_state_t state;
    int busy, notifying, sampling;
} bm_pcs286_port61_t;
/* Caller-owned UNUSED storage, copied config, borrowed reset PIT/clock/context.
 * No allocations/maps/callbacks. Owner initializes board check circuitry with
 * both enables asserted (zero port latch) before publication and forwards every
 * PIT2 output to pit_input. PIT0/PIT1 belong to PIC/refresh circuitry elsewhere.
 * Requires freshly reset PIT with GATE2 low; initialize before programming/run.
 * AT signal contract from IBM6280070, not proof of exact PCS286 board wiring. */
bm_status_t bm_pcs286_port61_initialize(bm_pcs286_port61_t *port,
                                       const bm_pcs286_port61_config_t *config);
/* Full reset only: engine, link and board status circuits reset first, then this
 * reset before resuming. Clears latch/failure, samples PIT2, publishes enables
 * and changed speaker level. Does not reset children. CPU reset preserves it. */
bm_status_t bm_pcs286_port61_reset(bm_pcs286_port61_t *port);
/* Exact byte61; no62/63/92 mirrors/A20. Normal I/O syncs PIT first. Writes retain
 * bits0..3: bit0 GATE2, bit1 speaker data, bits2/3 disable RAM/IO check. Reads
 * use external bits4/6/7 and RAW OUT2 bit5, independent of speaker enable.
 * DEBUG reads pure/stale; DEBUG writes unsupported. Wait count preserved.
 * Invalid transport fails before sync. Host failure latches; caller unchanged,
 * completed elapsed-time/pin/register/endpoint effects retained, no retry or
 * guestfault. Mutator reentry rejects; state/DEBUG inspection allowed, except
 * recursive status sampling. Output callbacks caused by sync/GATE are accepted
 * during busy I/O, but not while an external observer/check callback executes.
 * Changes are functional boundary ordering, not electrical propagation timing. */
bm_status_t bm_pcs286_port61_io(void *context, bm_bus_transaction_t *transaction);
bm_status_t bm_pcs286_port61_pit_input(bm_pcs286_port61_t *port,
                                      unsigned channel, int level);
bm_status_t bm_pcs286_port61_state(const bm_pcs286_port61_t *port,
                                  bm_pcs286_port61_state_t *state);
#endif
