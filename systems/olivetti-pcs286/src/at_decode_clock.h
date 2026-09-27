/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors */
#ifndef BM_PCS286_AT_DECODE_CLOCK_H
#define BM_PCS286_AT_DECODE_CLOCK_H
#include <blumach/components/at_bus.h>
/* Extra service clocks -> requester clocks, ceil once per logical access.
 * Uses the engine's bounded exact representation; no rounding carry. */
bm_status_t bm_pcs286_at_convert_waits(bm_clock_rate_t service,
    bm_clock_rate_t requester, uint64_t clocks, uint32_t *out);
#endif
