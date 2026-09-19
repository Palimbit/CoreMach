/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2019-2020 Miran Grca
 * Copyright 2022-2026 Daniel Balsom
 * Copyright 2026 Clara
 * Copyright 2026 BluMach contributors
 */
#include <blumach/components/pit8253.h>

#include "pit8253_private.h"

#include <stdbool.h>
#include <string.h>

static bm_status_t
synchronize_clock(bm_pit8253_t *pit)
{
    if (pit->clock_sync == NULL)
        return BM_STATUS_OK;
    return pit->clock_sync(pit->clock_context);
}

static bm_status_t
clock_state_changed(bm_pit8253_t *pit)
{
    if (pit->clock_changed == NULL)
        return BM_STATUS_OK;
    return pit->clock_changed(pit->clock_context);
}

static void
notify_changed_outputs(bm_pit8253_t *pit, const bool previous[3])
{
    unsigned int channel;
    if (pit->output == NULL)
        return;
    for (channel = 0; channel < 3U; ++channel) {
        bool current = bm_pit_exact_get_output(&pit->exact, channel);
        if (current != previous[channel])
            pit->output(pit->output_context, channel, current ? 1 : 0);
    }
}

static void
remember_outputs(const bm_pit8253_t *pit, bool output[3])
{
    unsigned int channel;
    for (channel = 0; channel < 3U; ++channel)
        output[channel] = bm_pit_exact_get_output(&pit->exact, channel);
}

static bm_status_t
pit_access(void *context, bm_bus_transaction_t *transaction)
{
    bm_pit8253_t *pit = context;
    unsigned int port = (unsigned int) (transaction->address - pit->io_base);
    bool previous[3];

    if ((transaction->size != 1U) || (transaction->operation == BM_BUS_FETCH))
        return BM_STATUS_UNSUPPORTED;
    {
        bm_status_t status = synchronize_clock(pit);

        if (status != BM_STATUS_OK)
            return status;
    }
    if (transaction->operation == BM_BUS_READ) {
        if (port >= 3U)
            return BM_STATUS_UNMAPPED;
        transaction->value = bm_pit_exact_data_read(&pit->exact, port);
        return BM_STATUS_OK;
    }

    remember_outputs(pit, previous);
    if (port < 3U)
        bm_pit_exact_data_write(&pit->exact, port, (uint8_t) transaction->value);
    else
        bm_pit_exact_control_write(&pit->exact, (uint8_t) transaction->value);
    notify_changed_outputs(pit, previous);
    return clock_state_changed(pit);
}

bm_status_t
bm_pit8253_create(const bm_host_services_t *host,
                  bm_bus_t *bus,
                  const bm_pit8253_config_t *config,
                  bm_pit8253_t **out_pit)
{
    bm_pit8253_t *pit;
    bm_status_t status;
    if ((bm_host_services_validate(host) != BM_STATUS_OK) || (bus == NULL) ||
        (config == NULL) || (out_pit == NULL) || (config->io_base > UINT16_MAX - 3U))
        return BM_STATUS_INVALID_ARGUMENT;
    *out_pit = NULL;
    pit = host->allocate(host->context, sizeof(*pit));
    if (pit == NULL)
        return BM_STATUS_OUT_OF_MEMORY;
    memset(pit, 0, sizeof(*pit));
    pit->host = *host;
    pit->io_base = config->io_base;
    pit->output = config->output;
    pit->output_context = config->output_context;
    bm_pit8253_reset(pit);
    status = bm_bus_map(bus, BM_ADDRESS_IO, config->io_base,
                        (uint32_t) config->io_base + 3U, pit_access, pit);
    if (status != BM_STATUS_OK) {
        host->release(host->context, pit);
        return status;
    }
    *out_pit = pit;
    return BM_STATUS_OK;
}

void
bm_pit8253_destroy(bm_pit8253_t *pit)
{
    if (pit != NULL) {
        if (pit->clock_binding != NULL)
            pit->host.release(pit->host.context, pit->clock_binding);
        pit->host.release(pit->host.context, pit);
    }
}

void
bm_pit8253_reset(bm_pit8253_t *pit)
{
    bool previous[3];
    if (pit == NULL)
        return;
    (void) synchronize_clock(pit);
    remember_outputs(pit, previous);
    bm_pit_exact_reset(&pit->exact);
    bm_pit_exact_set_gate(&pit->exact, 0U, true);
    bm_pit_exact_set_gate(&pit->exact, 1U, true);
    bm_pit_exact_set_gate(&pit->exact, 2U, false);
    notify_changed_outputs(pit, previous);
    (void) clock_state_changed(pit);
}

bm_status_t
bm_pit8253_set_gate(bm_pit8253_t *pit, unsigned int channel, int asserted)
{
    bool previous[3];
    bm_status_t status;
    if ((pit == NULL) || (channel >= 3U))
        return BM_STATUS_INVALID_ARGUMENT;
    status = synchronize_clock(pit);
    if (status != BM_STATUS_OK)
        return status;
    remember_outputs(pit, previous);
    bm_pit_exact_set_gate(&pit->exact, channel, asserted != 0);
    notify_changed_outputs(pit, previous);
    return clock_state_changed(pit);
}

bm_status_t
bm_pit8253_advance(bm_pit8253_t *pit, uint32_t input_ticks)
{
    if (pit == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    while (input_ticks != 0U) {
        bool previous[3];
        uint32_t consumed;

        remember_outputs(pit, previous);
        consumed = bm_pit_exact_advance_until_output_change(&pit->exact,
                                                             input_ticks);
        if ((consumed == 0U) || (consumed > input_ticks))
            return BM_STATUS_DEVICE_ERROR;
        notify_changed_outputs(pit, previous);
        input_ticks -= consumed;
    }
    return BM_STATUS_OK;
}

bm_status_t
bm_pit8253_cycles_until_output_change(bm_pit8253_t *pit,
                                      uint32_t *cycles)
{
    uint32_t result;
    bm_status_t status;

    if ((pit == NULL) || (cycles == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    status = synchronize_clock(pit);
    if (status != BM_STATUS_OK)
        return status;
    result = bm_pit_exact_cycles_until_output_change(&pit->exact);
    *cycles = result;
    return result == 0U ? BM_STATUS_IDLE : BM_STATUS_OK;
}

bm_status_t
bm_pit8253_count(bm_pit8253_t *pit, unsigned int channel, uint16_t *count)
{
    bm_status_t status;

    if ((pit == NULL) || (channel >= 3U) || (count == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    status = synchronize_clock(pit);
    if (status != BM_STATUS_OK)
        return status;
    *count = bm_pit_exact_get_count(&pit->exact, channel);
    return BM_STATUS_OK;
}

bm_status_t
bm_pit8253_output(bm_pit8253_t *pit, unsigned int channel, int *output)
{
    bm_status_t status;

    if ((pit == NULL) || (channel >= 3U) || (output == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    status = synchronize_clock(pit);
    if (status != BM_STATUS_OK)
        return status;
    *output = bm_pit_exact_get_output(&pit->exact, channel) ? 1 : 0;
    return BM_STATUS_OK;
}
