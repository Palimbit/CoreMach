/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2019-2020 Miran Grca
 * Copyright 2026 BluMach contributors
 *
 * Derived rewrite of the inherited PIT. This first deterministic contract
 * implements binary modes 0, 2 and 3; BCD and counter latching remain future work.
 */
#include <blumach/components/pit8253.h>

#include <string.h>

typedef struct bm_pit_channel {
    uint32_t reload;
    uint32_t count;
    uint8_t mode;
    uint8_t access;
    uint8_t write_low;
    uint8_t read_low;
    uint8_t pending_low;
    uint16_t latched_count;
    uint8_t latch_pending;
    uint8_t latch_low_next;
    uint8_t gate;
    uint8_t output;
} bm_pit_channel_t;

struct bm_pit8253 {
    bm_host_services_t host;
    uint16_t io_base;
    bm_pit8253_output_fn output;
    void *output_context;
    bm_pit_channel_t channels[3];
};

static void
set_output(bm_pit8253_t *pit, unsigned int channel, int value)
{
    bm_pit_channel_t *counter = &pit->channels[channel];
    value = !!value;
    if (counter->output == (uint8_t) value)
        return;
    counter->output = (uint8_t) value;
    if (pit->output != NULL)
        pit->output(pit->output_context, channel, value);
}

static void
load_count(bm_pit8253_t *pit, unsigned int channel, uint16_t value)
{
    bm_pit_channel_t *counter = &pit->channels[channel];
    counter->reload = value ? value : 65536U;
    counter->count = counter->reload;
    set_output(pit, channel, counter->mode == 0 ? 0 : 1);
}

static bm_status_t
counter_write(bm_pit8253_t *pit, unsigned int channel, uint8_t value)
{
    bm_pit_channel_t *counter = &pit->channels[channel];
    switch (counter->access) {
        case 1:
            load_count(pit, channel, value);
            break;
        case 2:
            load_count(pit, channel, (uint16_t) value << 8U);
            break;
        case 3:
            if (counter->write_low) {
                load_count(pit, channel,
                           (uint16_t) (counter->pending_low | ((uint16_t) value << 8U)));
                counter->write_low = 0;
            } else {
                counter->pending_low = value;
                counter->write_low = 1;
            }
            break;
        default:
            return BM_STATUS_INVALID_STATE;
    }
    return BM_STATUS_OK;
}

static uint8_t
counter_read(bm_pit_channel_t *counter)
{
    uint16_t value;
    if (counter->latch_pending != 0U) {
        value = counter->latched_count;
        --counter->latch_pending;
        if (counter->access == 2U)
            return (uint8_t) (value >> 8U);
        if (counter->access == 3U) {
            uint8_t result = counter->latch_low_next ?
                (uint8_t) value : (uint8_t) (value >> 8U);
            counter->latch_low_next ^= 1U;
            return result;
        }
        return (uint8_t) value;
    }
    value = (uint16_t) counter->count;
    if (counter->access == 2)
        return (uint8_t) (value >> 8U);
    if (counter->access != 3)
        return (uint8_t) value;
    counter->read_low ^= 1U;
    return counter->read_low ? (uint8_t) value : (uint8_t) (value >> 8U);
}

static bm_status_t
pit_access(void *context, bm_bus_transaction_t *transaction)
{
    bm_pit8253_t *pit = context;
    unsigned int port = (unsigned int) (transaction->address - pit->io_base);
    if ((transaction->size != 1) || (transaction->operation == BM_BUS_FETCH))
        return BM_STATUS_UNSUPPORTED;
    if (transaction->operation == BM_BUS_READ) {
        if (port >= 3)
            return BM_STATUS_UNSUPPORTED;
        transaction->value = counter_read(&pit->channels[port]);
        return BM_STATUS_OK;
    }
    if (port < 3)
        return counter_write(pit, port, (uint8_t) transaction->value);
    {
        uint8_t control = (uint8_t) transaction->value;
        unsigned int channel = control >> 6U;
        bm_pit_channel_t *counter;
        if (channel >= 3)
            return BM_STATUS_UNSUPPORTED;
        counter = &pit->channels[channel];
        if (((control >> 4U) & 3U) == 0U) {
            if (counter->latch_pending == 0U) {
                counter->latched_count = (uint16_t) counter->count;
                counter->latch_pending = counter->access == 3U ? 2U : 1U;
                counter->latch_low_next = 1U;
            }
            return BM_STATUS_OK;
        }
        counter->access = (control >> 4U) & 3U;
        counter->mode = (control >> 1U) & 7U;
        if (counter->mode > 5)
            counter->mode &= 3U;
        if (((control & 1U) != 0) || ((counter->mode != 0) &&
            (counter->mode != 2) && (counter->mode != 3)))
            return BM_STATUS_UNSUPPORTED;
        counter->write_low = 0;
        counter->read_low = 0;
        counter->latch_pending = 0;
        return BM_STATUS_OK;
    }
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
    status = bm_bus_map(bus, BM_ADDRESS_IO, config->io_base, config->io_base + 3U,
                        pit_access, pit);
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
    if (pit != NULL)
        pit->host.release(pit->host.context, pit);
}

void
bm_pit8253_reset(bm_pit8253_t *pit)
{
    unsigned int channel;
    if (pit == NULL)
        return;
    for (channel = 0; channel < 3; ++channel) {
        memset(&pit->channels[channel], 0, sizeof(pit->channels[channel]));
        pit->channels[channel].gate = 1;
        pit->channels[channel].output = 1;
    }
}

bm_status_t
bm_pit8253_set_gate(bm_pit8253_t *pit, unsigned int channel, int asserted)
{
    if ((pit == NULL) || (channel >= 3))
        return BM_STATUS_INVALID_ARGUMENT;
    pit->channels[channel].gate = (uint8_t) !!asserted;
    return BM_STATUS_OK;
}

bm_status_t
bm_pit8253_advance(bm_pit8253_t *pit, uint32_t input_ticks)
{
    unsigned int channel;
    if (pit == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    for (channel = 0; channel < 3; ++channel) {
        bm_pit_channel_t *counter = &pit->channels[channel];
        if (!counter->gate || (counter->count == 0))
            continue;
        while (input_ticks >= counter->count) {
            input_ticks -= counter->count;
            if (counter->mode == 0) {
                counter->count = 0;
                set_output(pit, channel, 1);
                break;
            }
            counter->count = counter->reload;
            if (counter->mode == 3)
                set_output(pit, channel, !counter->output);
            else {
                set_output(pit, channel, 0);
                set_output(pit, channel, 1);
            }
        }
        if (counter->count != 0)
            counter->count -= input_ticks;
    }
    return BM_STATUS_OK;
}
