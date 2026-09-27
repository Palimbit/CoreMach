/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2022-2026 Daniel Balsom
 * Copyright 2026 Clara
 * Copyright 2026 BluMach contributors
 *
 * Selective port of BluMach's Intel 8253/8254 edge-state core. Portions of
 * that state model were derived from MartyPC's MIT-licensed PIT model. The
 * portable component keeps the original attribution while exposing no legacy
 * globals, timers, callbacks or platform APIs.
 */
#include "pit8253_exact.h"

#include <assert.h>
#include <string.h>

static uint8_t
canonical_mode(uint8_t raw)
{
    raw &= 7U;
    return raw > 5U ? (uint8_t) (raw & 3U) : raw;
}

static uint32_t
reload_value(const bm_pit_exact_channel_t *channel)
{
    return channel->count_register ? channel->count_register : 0x10000U;
}

static uint16_t
visible_count(uint32_t value)
{
    return value == 0x10000U ? 0U : (uint16_t) value;
}

static void
update_live_latch(bm_pit_exact_channel_t *channel)
{
    if (!channel->count_latched)
        channel->output_latch = visible_count(channel->counting_element);
}

static uint32_t
bcd_decrement(uint32_t value)
{
    if ((value == 0U) || (value == 0x10000U))
        return 0x9999U;
    if ((value & 0x000fU) != 0U)
        return (value - 1U) & 0xffffU;
    if ((value & 0x00f0U) != 0U)
        return (value - 0x0007U) & 0xffffU;
    if ((value & 0x0f00U) != 0U)
        return (value - 0x0067U) & 0xffffU;
    return (value - 0x0667U) & 0xffffU;
}

static void
decrement(bm_pit_exact_channel_t *channel)
{
    if (channel->bcd)
        channel->counting_element = bcd_decrement(channel->counting_element);
    else if ((channel->counting_element == 0U) ||
             (channel->counting_element == 0x10000U))
        channel->counting_element = 0xffffU;
    else
        channel->counting_element = (channel->counting_element - 1U) & 0xffffU;
    update_live_latch(channel);
}

static void
decrement_n(bm_pit_exact_channel_t *channel, unsigned int count)
{
    while (count-- != 0U)
        decrement(channel);
}

static void
load_counting_element(bm_pit_exact_channel_t *channel)
{
    channel->counting_element = reload_value(channel);
    if (channel->is_8254) {
        channel->active_count = channel->count_register;
        if (channel->mode == 3U && (channel->active_count & 1U))
            channel->counting_element &= 0xfffeU;
    }
    channel->null_count = false;
    channel->ce_undefined = false;
    if (!channel->is_8254 && (channel->rw_mode == 3U) && (channel->write_phase == 1U))
        channel->incomplete_reload = true;
    update_live_latch(channel);

    switch (channel->mode) {
        case 0:
        case 1:
            channel->output = false;
            channel->state = BM_PIT_COUNTING;
            break;
        case 2:
            channel->output = true;
            channel->state = channel->gate ? BM_PIT_COUNTING : BM_PIT_WAIT_GATE;
            break;
        case 3:
            if (channel->toggle_on_reload) {
                channel->output = !channel->output;
                channel->toggle_on_reload = false;
            } else if (channel->state != BM_PIT_RELOAD_NEXT) {
                channel->output = true;
            }
            channel->state = channel->gate ? BM_PIT_COUNTING : BM_PIT_WAIT_GATE;
            break;
        case 4:
        case 5:
            channel->output = true;
            channel->state = BM_PIT_COUNTING;
            break;
        default:
            assert(0);
    }
}

static void
reset_channel(bm_pit_exact_channel_t *channel)
{
    memset(channel, 0, sizeof(*channel));
    channel->rw_mode = 1U;
    channel->initial_load = true;
    channel->null_count = true;
    channel->state = BM_PIT_WAIT_COUNT;
}

void
bm_pit_exact_reset(bm_pit_exact_device_t *pit)
{
    unsigned int index;
    memset(pit, 0, sizeof(*pit));
    for (index = 0; index < 3U; ++index)
        reset_channel(&pit->channel[index]);
}

void bm_pit_exact_reset_8254(bm_pit_exact_device_t *pit)
{
    unsigned int i;
    bm_pit_exact_reset(pit);
    for (i = 0; i < 3U; ++i) pit->channel[i].is_8254 = true;
}

static void
latch_count(bm_pit_exact_channel_t *channel)
{
    if (channel->count_latched)
        return;
    channel->output_latch = visible_count(channel->counting_element);
    channel->count_latched = true;
    channel->read_phase = 0U;
}

static void
program_mode(bm_pit_exact_channel_t *channel, uint8_t control)
{
    channel->control = control;
    channel->mode_raw = (uint8_t) ((control >> 1U) & 7U);
    channel->mode = canonical_mode(channel->mode_raw);
    channel->rw_mode = (uint8_t) ((control >> 4U) & 3U);
    channel->bcd = (control & 1U) != 0U;
    channel->write_phase = 0U;
    channel->read_phase = 0U;
    channel->count_latched = false;
    channel->status_latched = false;
    channel->null_count = true;
    channel->armed = false;
    channel->initial_load = true;
    channel->incomplete_reload = false;
    channel->ce_undefined = true;
    channel->toggle_on_reload = false;
    channel->state = BM_PIT_WAIT_COUNT;
    channel->output = channel->mode != 0U;
}

void
bm_pit_exact_control_write(bm_pit_exact_device_t *pit, uint8_t value)
{
    unsigned int selected = value >> 6U;
    bm_pit_exact_channel_t *channel;
    pit->last_control = value;
    if (selected >= 3U) {
        /* Selective port of classic pitx read-back; Intel 231164-005 figs10-13.
         * Public 8254 wrapper rejects reserved D0 before reaching the core. */
        unsigned int i;
        for (i = 0; i < 3U; ++i) {
            bm_pit_exact_channel_t *c = &pit->channel[i];
            if (!c->is_8254 || !(value & (2U << i))) continue;
            if (!(value & 0x20U)) latch_count(c);
            if (!(value & 0x10U) && !c->status_latched) {
                c->status_latch = (uint8_t)((c->output ? 0x80U : 0U) |
                    (c->null_count ? 0x40U : 0U) | (c->control & 0x3fU));
                c->status_latched = true;
            }
        }
        return;
    }
    channel = &pit->channel[selected];
    if ((value & 0x30U) == 0U) {
        latch_count(channel);
        return;
    }
    program_mode(channel, value);
}

static void
finalize_write(bm_pit_exact_channel_t *channel)
{
    if (!channel->is_8254 && channel->count_register == 1U) {
        if (channel->mode == 2U)
            channel->count_register = 2U;
        else if (channel->mode == 3U)
            channel->count_register = 0U;
    }
    channel->null_count = true;
    /* Hardware-triggered 8254 modes become active only on GATE, not a CR
     * rewrite after the previous one-shot has completed. */
    if (!channel->is_8254 || (channel->mode != 1U && channel->mode != 5U))
        channel->armed = true;
    if (channel->initial_load) {
        channel->initial_load = false;
        channel->state = ((channel->mode == 1U) || (channel->mode == 5U)) ?
            BM_PIT_WAIT_TRIGGER : BM_PIT_LOAD_NEXT;
        return;
    }
    if (channel->incomplete_reload || (channel->mode == 0U) ||
        (channel->mode == 4U))
        channel->state = BM_PIT_LOAD_NEXT;
    channel->incomplete_reload = false;
}

void
bm_pit_exact_data_write(bm_pit_exact_device_t *pit,
                        unsigned int selected,
                        uint8_t value)
{
    bm_pit_exact_channel_t *channel;
    if (selected >= 3U)
        return;
    channel = &pit->channel[selected];
    switch (channel->rw_mode) {
        case 1:
            channel->count_register = value;
            finalize_write(channel);
            break;
        case 2:
            channel->count_register = (uint16_t) value << 8U;
            finalize_write(channel);
            break;
        case 3:
            if (channel->write_phase == 0U) {
                if (channel->is_8254) channel->pending_lsb = value;
                else channel->count_register = (uint16_t)
                         ((channel->count_register & 0xff00U) | value);
                channel->write_phase = 1U;
                if (channel->mode == 0U) {
                    channel->state = BM_PIT_WAIT_COUNT;
                    channel->output = false;
                }
            } else {
                channel->count_register = (uint16_t)
                    ((channel->is_8254 ? channel->pending_lsb : (channel->count_register & 0x00ffU)) |
                     ((uint16_t) value << 8U));
                channel->write_phase = 0U;
                finalize_write(channel);
            }
            break;
        default:
            break;
    }
}

uint8_t
bm_pit_exact_data_read(bm_pit_exact_device_t *pit, unsigned int selected)
{
    bm_pit_exact_channel_t *channel;
    uint16_t value;
    if (selected >= 3U)
        return 0xffU;
    channel = &pit->channel[selected];
    if (channel->is_8254 && channel->status_latched) {
        channel->status_latched = false;
        return channel->status_latch;
    }
    /* A completed count write is transferred on the next input clock. The
     * portable scheduler may execute several adjacent I/O instructions before
     * its next clock event, although a physical bus transaction spans enough
     * CPU cycles for that edge to occur. Reading while the transfer is still
     * pending is hardware-undefined; choose the newly completed count rather
     * than leaking the previous counting element. This changes no active
     * counter and remains independent of scheduler/host granularity. */
    if (!channel->is_8254 && channel->state == BM_PIT_LOAD_NEXT)
        load_counting_element(channel);
    value = channel->count_latched ? channel->output_latch :
        visible_count(channel->counting_element);
    if (!channel->is_8254 && !channel->count_latched && (channel->rw_mode == 3U) &&
        (channel->write_phase == 1U))
        return (uint8_t) ~(channel->count_register & 0xffU);
    switch (channel->rw_mode) {
        case 1:
            channel->count_latched = false;
            return (uint8_t) value;
        case 2:
            channel->count_latched = false;
            return (uint8_t) (value >> 8U);
        case 3:
            if (channel->read_phase == 0U) {
                channel->read_phase = 1U;
                return (uint8_t) value;
            }
            channel->read_phase = 0U;
            channel->count_latched = false;
            return (uint8_t) (value >> 8U);
        default:
            return 0U;
    }
}

void
bm_pit_exact_set_gate(bm_pit_exact_device_t *pit,
                      unsigned int selected,
                      bool gate)
{
    bm_pit_exact_channel_t *channel;
    bool old;
    if (selected >= 3U)
        return;
    channel = &pit->channel[selected];
    old = channel->gate;
    channel->gate = gate;
    if (!old && gate) {
        switch (channel->mode) {
            case 1:
            case 5:
                if (channel->state != BM_PIT_WAIT_COUNT) {
                    channel->armed = true;
                    channel->state = BM_PIT_LOAD_NEXT;
                }
                break;
            case 2:
            case 3:
                if (channel->state != BM_PIT_WAIT_COUNT)
                    channel->state = BM_PIT_LOAD_NEXT;
                break;
            default:
                break;
        }
    } else if (old && !gate) {
        if ((channel->mode == 2U) || (channel->mode == 3U)) {
            channel->output = true;
            if (channel->state != BM_PIT_WAIT_COUNT)
                channel->state = BM_PIT_WAIT_GATE;
        }
    }
}

static void
count_mode3(bm_pit_exact_channel_t *channel)
{
    if (channel->is_8254) {
        /* Classic 8254 even-CE policy, with the ACTIVE divisor's parity until
         * reload. New/partial CR writes cannot change the current half-cycle.
         * Intel 231164-005 p13: odd high half defers its reload by one pulse. */
        decrement_n(channel, 2U);
        if (visible_count(channel->counting_element) == 0U) {
            if ((channel->active_count & 1U) && channel->output) {
                channel->toggle_on_reload = true;
                channel->state = BM_PIT_RELOAD_NEXT;
            } else {
                channel->output = !channel->output;
                channel->active_count = channel->count_register;
                channel->counting_element = reload_value(channel);
                if (channel->active_count & 1U) channel->counting_element &= 0xfffeU;
                channel->null_count = false;
                update_live_latch(channel);
            }
        }
        return;
    }
    bool odd = (channel->count_register & 1U) != 0U;
    if (odd && ((channel->counting_element & 1U) != 0U))
        decrement_n(channel, channel->output ? 1U : 3U);
    else
        decrement_n(channel, 2U);
    if (visible_count(channel->counting_element) == 0U) {
        channel->output = !channel->output;
        channel->counting_element = reload_value(channel);
        update_live_latch(channel);
    }
}

static void
tick_channel(bm_pit_exact_device_t *pit, unsigned int selected)
{
    bm_pit_exact_channel_t *channel = &pit->channel[selected];
    ++channel->clocks;
    switch (channel->state) {
        case BM_PIT_LOAD_NEXT:
        case BM_PIT_RELOAD_NEXT:
            load_counting_element(channel);
            return;
        case BM_PIT_STROBE_RECOVER:
            if (channel->is_8254 && (channel->mode == 5U || channel->gate))
                decrement(channel); /* CE keeps counting while OUT recovers. */
            channel->output = true;
            channel->state = BM_PIT_COUNTING;
            return;
        case BM_PIT_WAIT_TRIGGER:
            if (!channel->is_8254 && channel->ce_undefined && channel->armed) {
                channel->counting_element = 3U;
                update_live_latch(channel);
            }
            return;
        case BM_PIT_WAIT_COUNT:
        case BM_PIT_WAIT_GATE:
        case BM_PIT_DONE:
            return;
        case BM_PIT_COUNTING:
            break;
    }
    switch (channel->mode) {
        case 0:
            if (channel->gate) {
                decrement(channel);
                if (visible_count(channel->counting_element) == 0U)
                    channel->output = true;
            }
            break;
        case 1:
            decrement(channel);
            if (visible_count(channel->counting_element) == 0U) {
                if (channel->armed)
                    channel->output = true;
                channel->armed = false;
            }
            break;
        case 2:
            if (channel->gate) {
                decrement(channel);
                if (visible_count(channel->counting_element) == 1U) {
                    channel->output = false;
                    channel->state = BM_PIT_RELOAD_NEXT;
                }
            }
            break;
        case 3:
            if (channel->gate)
                count_mode3(channel);
            break;
        case 4:
            if (channel->gate) {
                decrement(channel);
                if (visible_count(channel->counting_element) == 0U &&
                    (!channel->is_8254 || channel->armed)) {
                    channel->output = false;
                    if (channel->is_8254) channel->armed = false;
                    channel->state = BM_PIT_STROBE_RECOVER;
                }
            }
            break;
        case 5:
            decrement(channel);
            if (visible_count(channel->counting_element) == 0U &&
                (!channel->is_8254 || channel->armed)) {
                channel->output = false;
                channel->armed = false;
                channel->state = BM_PIT_STROBE_RECOVER;
            }
            break;
        default:
            break;
    }
}

static uint32_t
binary_countdown(uint32_t value)
{
    value &= 0xffffU;
    return value == 0U ? 0x10000U : value;
}

static bool
valid_bcd(uint32_t value)
{
    return (value == 0x10000U) ||
        (((value & 0x000fU) <= 9U) &&
         (((value >> 4U) & 0x000fU) <= 9U) &&
         (((value >> 8U) & 0x000fU) <= 9U) &&
         (((value >> 12U) & 0x000fU) <= 9U));
}

static uint32_t
bcd_to_decimal(uint32_t value)
{
    if ((value == 0U) || (value == 0x10000U))
        return 0U;
    return (value & 0x000fU) +
        (((value >> 4U) & 0x000fU) * 10U) +
        (((value >> 8U) & 0x000fU) * 100U) +
        (((value >> 12U) & 0x000fU) * 1000U);
}

static uint32_t
decimal_to_bcd(uint32_t value)
{
    value %= 10000U;
    return (value % 10U) |
        (((value / 10U) % 10U) << 4U) |
        (((value / 100U) % 10U) << 8U) |
        (((value / 1000U) % 10U) << 12U);
}

static void
decrement_many(bm_pit_exact_channel_t *channel, uint64_t count)
{
    if (channel->bcd && valid_bcd(channel->counting_element)) {
        uint32_t decimal = bcd_to_decimal(channel->counting_element);
        uint32_t amount = (uint32_t) (count % 10000U);

        decimal = (decimal + 10000U - amount) % 10000U;
        channel->counting_element = decimal_to_bcd(decimal);
    } else if (!channel->bcd) {
        uint32_t value = channel->counting_element & 0xffffU;

        value = (value - (uint32_t) (count & 0xffffU)) & 0xffffU;
        channel->counting_element = value;
    } else {
        while (count-- != 0U)
            decrement(channel);
        return;
    }
    update_live_latch(channel);
}

static uint32_t
binary_counting_cycles_until_change(const bm_pit_exact_channel_t *channel)
{
    uint32_t count = binary_countdown(channel->counting_element);

    switch (channel->mode) {
        case 0:
            return (channel->gate && !channel->output) ? count : 0U;
        case 1:
            return (channel->armed && !channel->output) ? count : 0U;
        case 2:
            if (!channel->gate || !channel->output)
                return 0U;
            if (count == 1U)
                return 0x10000U;
            return count - 1U;
        case 3:
            if (!channel->gate)
                return 0U;
            if (channel->is_8254)
                return count / 2U + ((channel->active_count & 1U) && channel->output ? 1U : 0U);
            if ((count & 1U) == 0U)
                return count / 2U;
            if (channel->output)
                return (count + 1U) / 2U;
            return count == 1U ? 0x8000U : (count - 1U) / 2U;
        case 4:
            return (channel->gate && channel->output && (!channel->is_8254 || channel->armed)) ? count : 0U;
        case 5:
            return (channel->output && (!channel->is_8254 || channel->armed)) ? count : 0U;
        default:
            return 0U;
    }
}

static bool
counting_can_change_output(const bm_pit_exact_channel_t *channel)
{
    switch (channel->mode) {
        case 0:
            return channel->gate && !channel->output;
        case 1:
            return channel->armed && !channel->output;
        case 2:
        case 3:
            return channel->gate;
        case 4:
            return channel->gate && channel->output && (!channel->is_8254 || channel->armed);
        case 5:
            return channel->output && (!channel->is_8254 || channel->armed);
        default:
            return false;
    }
}

static uint32_t
channel_cycles_until_output_change(const bm_pit_exact_channel_t *source)
{
    bm_pit_exact_device_t simulated;
    bm_pit_exact_channel_t *channel;
    uint32_t offset = 0U;

    memset(&simulated, 0, sizeof(simulated));
    simulated.channel[0] = *source;
    channel = &simulated.channel[0];

    while ((channel->state == BM_PIT_LOAD_NEXT) ||
           (channel->state == BM_PIT_RELOAD_NEXT) ||
           (channel->state == BM_PIT_STROBE_RECOVER)) {
        bool previous = channel->output;

        tick_channel(&simulated, 0U);
        ++offset;
        if (channel->output != previous)
            return offset;
    }
    if (channel->state != BM_PIT_COUNTING)
        return 0U;
    if (!counting_can_change_output(channel))
        return 0U;
    if (!channel->bcd) {
        uint32_t result = binary_counting_cycles_until_change(channel);

        return result == 0U ? 0U : offset + result;
    }

    /* BCD and deliberately invalid BCD reloads are uncommon. Retain exact
     * edge semantics with a bounded reference walk while binary counters use
     * the constant-time path above. */
    {
        uint32_t index;

        for (index = 1U; index <= 0x10001U; ++index) {
            bool previous = channel->output;

            tick_channel(&simulated, 0U);
            if (channel->output != previous)
                return offset + index;
            if ((channel->state != BM_PIT_COUNTING) &&
                (channel->state != BM_PIT_RELOAD_NEXT) &&
                (channel->state != BM_PIT_STROBE_RECOVER))
                return 0U;
        }
    }
    return 0U;
}

uint32_t
bm_pit_exact_cycles_until_output_change(const bm_pit_exact_device_t *pit)
{
    uint32_t earliest = 0U;
    unsigned int channel;

    if (pit == NULL)
        return 0U;
    for (channel = 0U; channel < 3U; ++channel) {
        uint32_t candidate =
            channel_cycles_until_output_change(&pit->channel[channel]);

        if ((candidate != 0U) &&
            ((earliest == 0U) || (candidate < earliest)))
            earliest = candidate;
    }
    return earliest;
}

static void
skip_channel_without_output_change(bm_pit_exact_device_t *pit,
                                   unsigned int selected,
                                   uint64_t ticks)
{
    bm_pit_exact_channel_t *channel = &pit->channel[selected];

    while ((ticks != 0U) &&
           ((channel->state == BM_PIT_LOAD_NEXT) ||
            (channel->state == BM_PIT_RELOAD_NEXT) ||
            (channel->state == BM_PIT_STROBE_RECOVER))) {
        tick_channel(pit, selected);
        --ticks;
    }
    if (ticks == 0U)
        return;
    if (channel->state != BM_PIT_COUNTING) {
        channel->clocks += ticks;
        return;
    }

    channel->clocks += ticks;
    switch (channel->mode) {
        case 0:
        case 4:
            if (channel->gate)
                decrement_many(channel, ticks);
            break;
        case 1:
        case 5:
            decrement_many(channel, ticks);
            break;
        case 2:
            if (channel->gate)
                decrement_many(channel, ticks);
            break;
        case 3:
            if (channel->gate) {
                /* With gate high a valid mode3 counter has a near output
                 * deadline, so ticks is bounded before this doubling. */
                uint64_t decrements = ticks * 2U;

                if (!channel->is_8254 && ((channel->count_register & 1U) != 0U) &&
                    ((channel->counting_element & 1U) != 0U)) {
                    decrements -= 2U;
                    decrements += channel->output ? 1U : 3U;
                }
                decrement_many(channel, decrements);
            }
            break;
        default:
            break;
    }
}

uint64_t
bm_pit_exact_advance_until_output_change64(bm_pit_exact_device_t *pit,
                                           uint64_t maximum_ticks)
{
    uint32_t transition;
    uint64_t consumed;
    unsigned int channel;

    if ((pit == NULL) || (maximum_ticks == 0U))
        return 0U;
    transition = bm_pit_exact_cycles_until_output_change(pit);
    /* 8254 odd mode3 has an internal boundary at CE=0, one pulse BEFORE
     * the high->low OUT edge. Batch stepping must execute that state change
     * even though it has no observable output transition yet. */
    for (channel = 0; channel < 3U; ++channel) {
        const bm_pit_exact_channel_t *c = &pit->channel[channel];
        if (c->is_8254 && c->mode == 3U &&
            (c->state == BM_PIT_LOAD_NEXT || c->state == BM_PIT_RELOAD_NEXT)) {
            transition = 1U;
        }
        if (c->is_8254 && c->mode == 3U && c->state == BM_PIT_COUNTING &&
            c->gate && c->output && (c->active_count & 1U)) {
            uint32_t boundary = c->bcd ? 1U : binary_countdown(c->counting_element) / 2U;
            if (boundary && (!transition || boundary < transition)) transition = boundary;
        }
    }
    consumed = ((transition != 0U) && (transition <= maximum_ticks)) ?
        transition : maximum_ticks;

    if ((transition != 0U) && (transition <= maximum_ticks)) {
        if (consumed > 1U) {
            for (channel = 0U; channel < 3U; ++channel)
                skip_channel_without_output_change(pit, channel,
                                                   consumed - 1U);
        }
        bm_pit_exact_tick(pit);
    } else {
        for (channel = 0U; channel < 3U; ++channel)
            skip_channel_without_output_change(pit, channel, consumed);
    }
    return consumed;
}

uint32_t bm_pit_exact_advance_until_output_change(bm_pit_exact_device_t *pit,
                                                  uint32_t maximum_ticks)
{
    return (uint32_t)bm_pit_exact_advance_until_output_change64(pit, maximum_ticks);
}

void
bm_pit_exact_tick(bm_pit_exact_device_t *pit)
{
    unsigned int index;
    for (index = 0; index < 3U; ++index)
        tick_channel(pit, index);
}

uint16_t
bm_pit_exact_get_count(const bm_pit_exact_device_t *pit, unsigned int channel)
{
    return channel < 3U ? visible_count(pit->channel[channel].counting_element) : 0U;
}

bool
bm_pit_exact_get_output(const bm_pit_exact_device_t *pit, unsigned int channel)
{
    return (channel < 3U) && pit->channel[channel].output;
}
