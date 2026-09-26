/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2021 Miran Grca
 * Copyright 2026 rtzor, Project BluMach
 * Copyright 2026 BluMach contributors
 * Register transport derived from src/port_6x.c, corrected against IBM6280070
 * pp1-23,1-92,1-96. REF DET and errors are board inputs, not PIT1 substitutions.
 * Explicit clock/ownership replaces globals; analog audio is separate. */
#include "port61.h"
#include <string.h>

static void speaker_update(bm_pcs286_port61_t *port)
{
    int level = !!(port->state.latch & 2U) && port->state.out2;
    if (level == port->state.speaker_level) return;
    port->state.speaker_level = level;
    if (port->config.speaker) {
        port->notifying = 1;
        port->config.speaker(port->config.speaker_context, level);
        port->notifying = 0;
    }
}

bm_status_t bm_pcs286_port61_initialize(bm_pcs286_port61_t *port,
                                       const bm_pcs286_port61_config_t *config)
{
    bm_pcs286_port61_t candidate;
    if (!port || !config || !config->pit || !config->clock || !config->status || !config->checks ||
        config->profile != BM_PCS286_PORT61_AT_SIGNALS)
        return BM_STATUS_INVALID_ARGUMENT;
    memset(&candidate, 0, sizeof(candidate));
    candidate.config = *config;
    candidate.state.failure = BM_STATUS_OK;
    /* Constructor publishes nothing; owner supplies a freshly reset PIT. */
    if (bm_pit8254_output(config->pit, 2, &candidate.state.out2) != BM_STATUS_OK)
        return BM_STATUS_INVALID_STATE;
    *port = candidate;
    return BM_STATUS_OK;
}

bm_status_t bm_pcs286_port61_reset(bm_pcs286_port61_t *port)
{
    int out2;
    bm_status_t status;
    if (!port) return BM_STATUS_INVALID_ARGUMENT;
    if (port->busy || port->notifying || port->sampling) return BM_STATUS_INVALID_STATE;
    /* Owner has reset engine/link first. A retained link error must not be
     * hidden by resetting only this latch. */
    port->busy = 1;
    status = bm_at_clock_link_sync(port->config.clock);
    if (status == BM_STATUS_OK) status = bm_pit8254_output(port->config.pit, 2, &out2);
    if (status == BM_STATUS_OK) {
        port->state.latch = 0;
        port->state.out2 = out2;
        speaker_update(port);
        port->notifying = 1;
        status = port->config.checks(port->config.board_context, 1, 1);
        port->notifying = 0;
    }
    port->state.failure = status;
    port->busy = 0;
    return status;
}

bm_status_t bm_pcs286_port61_pit_input(bm_pcs286_port61_t *port,
                                      unsigned channel, int level)
{
    if (!port || channel > 2U || (level != 0 && level != 1))
        return BM_STATUS_INVALID_ARGUMENT;
    if (channel != 2U) return BM_STATUS_UNSUPPORTED;
    if (port->notifying || port->sampling) return BM_STATUS_INVALID_STATE;
    /* Pin changes do not retry a failed access or clear its failure. */
    port->state.out2 = level;
    speaker_update(port);
    return BM_STATUS_OK;
}

static bm_status_t readback(bm_pcs286_port61_t *port, uint8_t *value)
{
    uint8_t bits = 0;
    bm_status_t status;
    if (port->sampling) return BM_STATUS_INVALID_STATE;
    port->sampling = 1;
    status = port->config.status(port->config.board_context, &bits);
    port->sampling = 0;
    if (status != BM_STATUS_OK) return status;
    if (bits & ~0xd0U) return BM_STATUS_DEVICE_ERROR;
    *value = port->state.latch | bits | (port->state.out2 ? 0x20U : 0U);
    return BM_STATUS_OK;
}

bm_status_t bm_pcs286_port61_io(void *context, bm_bus_transaction_t *t)
{
    bm_pcs286_port61_t *port = context;
    bm_status_t status;
    uint8_t value = 0;
    if (!port || !t) return BM_STATUS_INVALID_ARGUMENT;
    if (t->space != BM_ADDRESS_IO) return BM_STATUS_UNMAPPED;
    if (t->operation < BM_BUS_READ || t->operation > BM_BUS_FETCH ||
        t->address > UINT16_MAX || t->alignment > 1U ||
        (t->attributes & ~(uint32_t)(BM_BUS_TRANSACTION_DEBUG | BM_BUS_TRANSACTION_LOCKED)) ||
        (t->endianness != BM_ENDIAN_LITTLE && t->endianness != BM_ENDIAN_BIG))
        return BM_STATUS_INVALID_ARGUMENT;
    if (t->size != 1U || t->operation == BM_BUS_FETCH) return BM_STATUS_UNSUPPORTED;
    if (t->address != 0x61U) return BM_STATUS_UNMAPPED;
    if (t->attributes & BM_BUS_TRANSACTION_DEBUG) {
        if (t->operation == BM_BUS_WRITE) return BM_STATUS_UNSUPPORTED;
        status = readback(port, &value);
        if (status == BM_STATUS_OK) t->value = value;
        return status;
    }
    if (port->busy || port->notifying || port->sampling) return BM_STATUS_INVALID_STATE;
    if (port->state.failure != BM_STATUS_OK) return port->state.failure;
    port->busy = 1;
    status = bm_at_clock_link_sync(port->config.clock);
    if (status == BM_STATUS_OK) {
        if (t->operation == BM_BUS_READ) status = readback(port, &value);
        else {
            /* Boundary-level simultaneous latch update: callbacks caused by
             * the new gate see the new enable, not a transient old bit1. */
            port->state.latch = (uint8_t)t->value & 0x0fU;
            status = bm_pit8254_set_gate(port->config.pit, 2, port->state.latch & 1U);
            if (status == BM_STATUS_OK) {
                bm_status_t schedule_status;
                speaker_update(port);
                port->notifying = 1;
                status = port->config.checks(port->config.board_context,
                    !(port->state.latch & 4U), !(port->state.latch & 8U));
                port->notifying = 0;
                /* Rearm accepted GATE effects even if the check endpoint failed.
                 * Preserve the first host failure; owner must stop execution. */
                schedule_status = bm_at_clock_link_changed(port->config.clock);
                if (status == BM_STATUS_OK) status = schedule_status;
            }
        }
    }
    if (status != BM_STATUS_OK) port->state.failure = status;
    else if (t->operation == BM_BUS_READ) t->value = value;
    port->busy = 0;
    return status;
}

bm_status_t bm_pcs286_port61_state(const bm_pcs286_port61_t *port,
                                  bm_pcs286_port61_state_t *state)
{
    if (!port || !state) return BM_STATUS_INVALID_ARGUMENT;
    *state = port->state;
    return BM_STATUS_OK;
}
