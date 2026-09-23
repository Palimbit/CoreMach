/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 */
#include "bus_fixture.h"
#include <string.h>

void fixture_init(bus_fixture_t *fixture, uint64_t base)
{
    memset(fixture, 0, sizeof(*fixture));
    fixture->base = base;
}

bm_status_t fixture_access(void *context, bm_bus_transaction_t *transaction)
{
    bus_fixture_t *fixture = context;
    fixture_access_t *event;
    uint64_t offset;
    uint32_t i;
    int debug;
    if (!fixture || !transaction || transaction->size == 0U ||
        transaction->size > 8U || transaction->operation < BM_BUS_READ ||
        transaction->operation > BM_BUS_FETCH ||
        (transaction->endianness != BM_ENDIAN_BIG &&
         transaction->endianness != BM_ENDIAN_LITTLE))
        return BM_STATUS_INVALID_ARGUMENT;
    if (transaction->address < fixture->base)
        return BM_STATUS_UNMAPPED;
    offset = transaction->address - fixture->base;
    if (offset > sizeof(fixture->bytes) - transaction->size)
        return BM_STATUS_UNMAPPED;
    if (fixture->trace_count == 64U)
        return BM_STATUS_CAPACITY_EXCEEDED;
    debug = (transaction->attributes & BM_BUS_TRANSACTION_DEBUG) != 0U;
    event = &fixture->trace[fixture->trace_count++];
    event->request = *transaction;
    event->result = fixture->result;
    if (fixture->result != BM_STATUS_OK)
        return fixture->result;
    if (debug && transaction->operation == BM_BUS_WRITE) {
        event->result = BM_STATUS_UNSUPPORTED;
        return event->result;
    }
    if (transaction->operation != BM_BUS_WRITE)
        transaction->value = 0U;
    for (i = 0U; i < transaction->size; ++i) {
        uint32_t shift = 8U * (transaction->endianness == BM_ENDIAN_LITTLE
            ? i : transaction->size - 1U - i);
        if (transaction->operation == BM_BUS_WRITE)
            fixture->bytes[(size_t) offset + i] = (uint8_t) (transaction->value >> shift);
        else
            transaction->value |= (uint64_t) fixture->bytes[(size_t) offset + i] << shift;
    }
    transaction->wait_states = debug ? 0U : fixture->waits;
    if (!debug) {
        if (transaction->operation == BM_BUS_READ) ++fixture->reads;
        else if (transaction->operation == BM_BUS_WRITE) ++fixture->writes;
        else ++fixture->fetches;
    }
    event->response = transaction->value;
    return BM_STATUS_OK;
}
