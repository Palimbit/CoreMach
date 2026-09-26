/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * AT architectural-boundary arbitration policy; no physical cycle model.
 */
#include <blumach/components/at_bus.h>

#include <string.h>

struct bm_at_bus {
    bm_host_services_t host;
    bm_at_bus_config_t config;
    bm_at_master_t requester;
    int requested;
    int locked;
    int hold;
    int hlda;
};

static int
valid_clock(bm_clock_rate_t clock)
{
    return clock.cycles_per_second_numerator != 0U &&
           clock.cycles_per_second_denominator != 0U;
}

static int
valid_master(bm_at_master_t master)
{
    return master >= BM_AT_MASTER_CPU && master <= BM_AT_MASTER_REFRESH;
}

static int
valid_transfer(const bm_at_transfer_t *transfer)
{
    const bm_bus_transaction_t *t;
    if (transfer == NULL || !valid_master(transfer->master) ||
        transfer->master == BM_AT_MASTER_REFRESH ||
        !valid_clock(transfer->requester_clock))
        return 0;
    t = &transfer->bus;
    return t->space >= BM_ADDRESS_MEMORY && t->space <= BM_ADDRESS_DATA &&
           t->operation >= BM_BUS_READ && t->operation <= BM_BUS_FETCH &&
           (t->endianness == BM_ENDIAN_LITTLE || t->endianness == BM_ENDIAN_BIG) &&
           (t->attributes & ~(BM_BUS_TRANSACTION_DEBUG |
                              BM_BUS_TRANSACTION_LOCKED)) == 0U &&
           t->size >= 1U && t->size <= 8U &&
           t->address <= UINT64_MAX - (uint64_t) t->size + 1U &&
           t->wait_states == 0U;
}

static void
set_hold(bm_at_bus_t *bus, int asserted)
{
    if (bus->hold == asserted)
        return;
    bus->hold = asserted;
    if (bus->config.hold != NULL)
        bus->config.hold(bus->config.hold_context, asserted);
}

bm_status_t
bm_at_bus_create(const bm_host_services_t *host,
                 const bm_at_bus_config_t *config,
                 bm_at_bus_t **out_bus)
{
    bm_at_bus_t *bus;

    if (out_bus == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    *out_bus = NULL;
    if (bm_host_services_validate(host) != BM_STATUS_OK || config == NULL ||
        !valid_clock(config->cpu_clock) || !valid_clock(config->isa_clock) ||
        config->memory == NULL || config->io == NULL)
        return BM_STATUS_INVALID_ARGUMENT;

    bus = host->allocate(host->context, sizeof(*bus));
    if (bus == NULL)
        return BM_STATUS_OUT_OF_MEMORY;
    memset(bus, 0, sizeof(*bus));
    bus->host = *host;
    bus->config = *config;
    *out_bus = bus;
    return BM_STATUS_OK;
}

void
bm_at_bus_destroy(bm_at_bus_t *bus)
{
    if (bus == NULL)
        return;
    bm_at_bus_reset(bus);
    bus->host.release(bus->host.context, bus);
}

void
bm_at_bus_reset(bm_at_bus_t *bus)
{
    if (bus == NULL)
        return;
    bus->requested = 0;
    bus->locked = 0;
    bus->hlda = 0;
    set_hold(bus, 0);
}

bm_status_t
bm_at_bus_access(bm_at_bus_t *bus, bm_at_transfer_t *transfer)
{
    bm_at_transfer_t decoded;
    bm_at_access_fn access;
    bm_status_t status;
    int debug;

    if (bus == NULL || !valid_transfer(transfer))
        return BM_STATUS_INVALID_ARGUMENT;
    debug = (transfer->bus.attributes & BM_BUS_TRANSACTION_DEBUG) != 0U;
    if (debug && transfer->bus.operation == BM_BUS_WRITE)
        return BM_STATUS_UNSUPPORTED;
    if (!debug && ((transfer->master == BM_AT_MASTER_CPU && bus->hlda) ||
        (transfer->master != BM_AT_MASTER_CPU &&
         (!bus->hlda || !bus->hold || !bus->requested ||
          bus->requester != transfer->master))))
        return BM_STATUS_IDLE;

    decoded = *transfer;
    access = decoded.bus.space == BM_ADDRESS_IO ? bus->config.io :
                                                    bus->config.memory;
    status = access(bus->config.decode_context, &decoded);
    if (status == BM_STATUS_OK) {
        /* Decode has already converted the duration to requester clocks.
         * Inspection consumes no simulated time, even if a faulty endpoint
         * reports a wait count. */
        if (debug)
            decoded.bus.wait_states = 0U;
        transfer->bus = decoded.bus;
    }
    return status;
}

bm_status_t
bm_at_bus_cpu_access(void *context, bm_bus_transaction_t *transaction)
{
    bm_at_bus_t *bus = context;
    bm_at_transfer_t transfer;
    bm_status_t status;

    if (bus == NULL || transaction == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    transfer.master = BM_AT_MASTER_CPU;
    transfer.bus = *transaction;
    transfer.requester_clock = bus->config.cpu_clock;
    status = bm_at_bus_access(bus, &transfer);
    if (status == BM_STATUS_OK)
        *transaction = transfer.bus;
    return status;
}

bm_status_t
bm_at_bus_set_lock(bm_at_bus_t *bus, int asserted)
{
    if (bus == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    asserted = asserted != 0;
    if (bus->locked == asserted)
        return BM_STATUS_OK;
    if (asserted && bus->hlda)
        return BM_STATUS_INVALID_STATE;
    bus->locked = asserted;
    set_hold(bus, bus->requested && !bus->locked);
    return BM_STATUS_OK;
}

bm_status_t
bm_at_bus_request(bm_at_bus_t *bus, bm_at_master_t master, int asserted)
{
    if (bus == NULL || !valid_master(master) || master == BM_AT_MASTER_CPU)
        return BM_STATUS_INVALID_ARGUMENT;
    asserted = asserted != 0;
    if (!asserted) {
        if (bus->requested && bus->requester == master) {
            bus->requested = 0;
            set_hold(bus, 0);
        }
        return BM_STATUS_OK;
    }
    if (bus->requested)
        return bus->requester == master ? BM_STATUS_OK : BM_STATUS_UNSUPPORTED;
    /* HLDA may remain high after cancellation. That stale level cannot
     * acknowledge a newly asserted requester; require it to fall first. */
    if (bus->hlda)
        return BM_STATUS_INVALID_STATE;
    bus->requester = master;
    bus->requested = 1;
    if (!bus->locked)
        set_hold(bus, 1);
    return BM_STATUS_OK;
}

bm_status_t
bm_at_bus_hold_ack(bm_at_bus_t *bus, int asserted)
{
    if (bus == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    asserted = asserted != 0;
    if (bus->hlda == asserted)
        return BM_STATUS_OK;
    if (asserted && (!bus->hold || !bus->requested || bus->locked))
        return BM_STATUS_INVALID_STATE;
    bus->hlda = asserted;
    return BM_STATUS_OK;
}

bm_status_t
bm_at_bus_arbitration(const bm_at_bus_t *bus, bm_at_bus_arbitration_t *state)
{
    if (bus == NULL || state == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    state->requester = bus->requested ? bus->requester : BM_AT_MASTER_CPU;
    state->requested = bus->requested;
    state->locked = bus->locked;
    state->hold = bus->hold;
    state->hlda = bus->hlda;
    return BM_STATUS_OK;
}
