/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2018 Fred N. van Kempen
 * Copyright 2026 BluMach contributors
 *
 * Selective port of the inherited MM58167 implementation. The original source
 * was distributed under the following BSD terms, retained in full:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include "rtc_mm58167_private.h"

#include <limits.h>
#include <string.h>

enum {
    MM67_MSEC = 0,
    MM67_HUNTEN = 1,
    MM67_SEC = 2,
    MM67_MIN = 3,
    MM67_HOUR = 4,
    MM67_DOW = 5,
    MM67_DOM = 6,
    MM67_MON = 7,
    MM67_AL_MSEC = 8,
    MM67_AL_HUNTEN = 9,
    MM67_AL_SEC = 10,
    MM67_AL_MIN = 11,
    MM67_AL_HOUR = 12,
    MM67_AL_DOW = 13,
    MM67_AL_DOM = 14,
    MM67_AL_MON = 15,
    MM67_ISTAT = 16,
    MM67_ICTRL = 17,
    MM67_RSTCTR = 18,
    MM67_RSTRAM = 19,
    MM67_STATUS = 20,
    MM67_GOCMD = 21,
    MM67_STBYIRQ = 22,
    MM67_TEST = 31
};

enum {
    MM67_INT_COMPARE = 0x01,
    MM67_INT_TENTH = 0x02,
    MM67_INT_SEC = 0x04,
    MM67_INT_MIN = 0x08,
    MM67_INT_HOUR = 0x10,
    MM67_INT_DAY = 0x20,
    MM67_INT_WEEK = 0x40,
    MM67_INT_MON = 0x80
};

static bm_status_t
synchronize_clock(bm_mm58167_t *rtc)
{
    if (rtc->clock_sync == NULL)
        return BM_STATUS_OK;
    return rtc->clock_sync(rtc->clock_context);
}

static bm_status_t
clock_state_changed(bm_mm58167_t *rtc)
{
    if (rtc->clock_changed == NULL)
        return BM_STATUS_OK;
    return rtc->clock_changed(rtc->clock_context);
}

static uint8_t
to_bcd(unsigned int value)
{
    return (uint8_t) (((value / 10U) << 4U) | (value % 10U));
}

static unsigned int
from_bcd(uint8_t value)
{
    return ((value >> 4U) * 10U) + (value & 0x0fU);
}

static uint8_t
bcd_increment(uint8_t value)
{
    return to_bcd(from_bcd(value) + 1U);
}

static unsigned int
days_in_month(unsigned int month)
{
    static const uint8_t days[] = { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    return ((month >= 1U) && (month <= 12U)) ? days[month - 1U] : 31U;
}

static void
set_irq(bm_mm58167_t *rtc, int asserted)
{
    uint8_t value = (uint8_t) !!asserted;
    if (rtc->irq_asserted == value)
        return;
    rtc->irq_asserted = value;
    if (rtc->interrupt != NULL)
        rtc->interrupt(rtc->interrupt_context, value);
}

static void
set_interrupt_status(bm_mm58167_t *rtc, uint8_t status)
{
    rtc->registers[MM67_ISTAT] = status;
    set_irq(rtc, status != 0U);
}

static int
alarm_field_matches(const bm_mm58167_t *rtc, unsigned int alarm_register)
{
    uint8_t alarm = rtc->registers[alarm_register];
    uint8_t current = rtc->registers[alarm_register & 7U];
    int match = 1;

    if ((alarm_register != MM67_AL_MSEC) && ((alarm & 0x0cU) != 0x0cU))
        match = ((current ^ alarm) & 0x0fU) == 0U;
    if (alarm_register != MM67_AL_DOW) {
        if ((alarm & 0xc0U) != 0xc0U)
            match = match && (((current ^ alarm) & 0xf0U) == 0U);
    }
    return match;
}

static int
alarm_matches(const bm_mm58167_t *rtc)
{
    unsigned int reg;
    for (reg = MM67_AL_MSEC; reg <= MM67_AL_MON; ++reg) {
        if (!alarm_field_matches(rtc, reg))
            return 0;
    }
    return 1;
}

static void
tick_calendar(bm_mm58167_t *rtc, int forced_minute, uint8_t initial_status)
{
    uint8_t *regs = rtc->registers;
    uint8_t status = initial_status;

    if (!forced_minute) {
        regs[MM67_SEC] = bcd_increment(regs[MM67_SEC]);
        if ((regs[MM67_ICTRL] & MM67_INT_SEC) != 0U)
            status = MM67_INT_SEC;
    }
    if (forced_minute || (from_bcd(regs[MM67_SEC]) >= 60U)) {
        unsigned int month;
        regs[MM67_SEC] = 0U;
        regs[MM67_MIN] = bcd_increment(regs[MM67_MIN]);
        if (!forced_minute && ((regs[MM67_ICTRL] & MM67_INT_MIN) != 0U))
            status = MM67_INT_MIN;
        if (from_bcd(regs[MM67_MIN]) >= 60U) {
            regs[MM67_MIN] = 0U;
            regs[MM67_HOUR] = bcd_increment(regs[MM67_HOUR]);
            if (!forced_minute && ((regs[MM67_ICTRL] & MM67_INT_HOUR) != 0U))
                status = MM67_INT_HOUR;
            if (from_bcd(regs[MM67_HOUR]) >= 24U) {
                regs[MM67_HOUR] = 0U;
                regs[MM67_DOW] = bcd_increment(regs[MM67_DOW]);
                if (!forced_minute && ((regs[MM67_ICTRL] & MM67_INT_DAY) != 0U))
                    status = MM67_INT_DAY;
                if (from_bcd(regs[MM67_DOW]) > 7U) {
                    regs[MM67_DOW] = 1U;
                    if (!forced_minute && ((regs[MM67_ICTRL] & MM67_INT_WEEK) != 0U))
                        status = MM67_INT_WEEK;
                }
                regs[MM67_DOM] = bcd_increment(regs[MM67_DOM]);
                month = from_bcd(regs[MM67_MON]);
                if (from_bcd(regs[MM67_DOM]) > days_in_month(month)) {
                    regs[MM67_DOM] = 1U;
                    regs[MM67_MON] = bcd_increment(regs[MM67_MON]);
                    if (!forced_minute && ((regs[MM67_ICTRL] & MM67_INT_MON) != 0U))
                        status = MM67_INT_MON;
                    if (from_bcd(regs[MM67_MON]) > 12U)
                        regs[MM67_MON] = 1U;
                }
            }
        }
    }

    if (!forced_minute) {
        if ((regs[MM67_ICTRL] & MM67_INT_COMPARE) != 0U)
            status = alarm_matches(rtc) ? MM67_INT_COMPARE : 0U;
        if (status != 0U)
            set_interrupt_status(rtc, status);
    }
}

static void
tick_millisecond(bm_mm58167_t *rtc)
{
    uint8_t status = 0U;
    rtc->millisecond_count = (uint16_t) ((rtc->millisecond_count + 1U) % 1000U);
    rtc->registers[MM67_MSEC] = (uint8_t) ((rtc->millisecond_count % 10U) << 4U);
    rtc->registers[MM67_HUNTEN] = to_bcd(rtc->millisecond_count / 10U);

    if (((rtc->millisecond_count % 100U) == 0U) &&
        ((rtc->registers[MM67_ICTRL] & MM67_INT_TENTH) != 0U))
        status = MM67_INT_TENTH;
    if (rtc->millisecond_count == 0U)
        tick_calendar(rtc, 0, status);
    else if (((rtc->registers[MM67_ICTRL] & MM67_INT_COMPARE) != 0U) &&
             alarm_matches(rtc))
        set_interrupt_status(rtc, MM67_INT_COMPARE);
    else if (status != 0U)
        set_interrupt_status(rtc, status);

    rtc->registers[MM67_STATUS] = 1U;
    rtc->rollover_microseconds = 150U;
}

static uint8_t
pcs86_checksum(const uint8_t *regs)
{
    unsigned int reg;
    uint8_t sum = regs[MM67_AL_MSEC] >> 4U;
    for (reg = MM67_AL_HUNTEN; reg <= MM67_AL_HOUR; ++reg)
        sum = (uint8_t) (sum + (regs[reg] >> 4U) + (regs[reg] & 0x0fU));
    sum = (uint8_t) (sum + (regs[MM67_AL_DOW] & 0x0fU) +
                     (regs[MM67_AL_DOM] & 0x0fU));
    return (uint8_t) ((sum & 0x3fU) ^ 0x15U);
}

static void
repair_pcs86_checksum(bm_mm58167_t *rtc)
{
    uint8_t checksum = pcs86_checksum(rtc->registers);
    rtc->registers[MM67_AL_DOM] = (uint8_t)
        ((rtc->registers[MM67_AL_DOM] & 0x0fU) | 0xc0U | (checksum & 0x30U));
    rtc->registers[MM67_AL_MON] = (uint8_t)
        (0xccU | ((checksum & 0x0cU) << 2U) | (checksum & 0x03U));
}

static void
reset_counters(bm_mm58167_t *rtc)
{
    unsigned int reg;
    for (reg = MM67_MSEC; reg <= MM67_MON; ++reg)
        rtc->registers[reg] = 0U;
    rtc->registers[MM67_DOW] = 1U;
    rtc->registers[MM67_DOM] = 1U;
    rtc->registers[MM67_MON] = 1U;
    rtc->registers[MM67_STATUS] = 0U;
    rtc->millisecond_count = 0U;
    rtc->microsecond_remainder = 0U;
    rtc->rollover_microseconds = 0U;
}

static unsigned int
register_for_port(const bm_mm58167_t *rtc, uint64_t address)
{
    if ((address >= rtc->counter_io_base) &&
        (address <= (uint32_t) rtc->counter_io_base + 15U))
        return (unsigned int) (address - rtc->counter_io_base);
    if ((address >= rtc->interrupt_io_base) &&
        (address <= (uint32_t) rtc->interrupt_io_base + 6U))
        return MM67_ISTAT + (unsigned int) (address - rtc->interrupt_io_base);
    return MM67_TEST; /* PCS 86 B7h aliases the test register. */
}

static uint8_t
read_register(bm_mm58167_t *rtc, unsigned int reg)
{
    uint8_t value;
    switch (reg) {
        case MM67_STATUS:
            value = rtc->registers[reg] & 1U;
            rtc->registers[reg] = 0U;
            break;
        case MM67_ISTAT:
            value = rtc->registers[reg];
            set_interrupt_status(rtc, 0U);
            break;
        case MM67_AL_MSEC:
        case MM67_MSEC:
            value = rtc->registers[reg] & 0xf0U;
            break;
        case MM67_AL_DOW:
            value = rtc->registers[reg] & 0x0fU;
            break;
        case MM67_DOW:
            value = rtc->registers[reg] & 0x07U;
            break;
        default:
            value = rtc->registers[reg];
            break;
    }
    if ((reg <= MM67_MON) && (rtc->rollover_microseconds != 0U))
        rtc->registers[MM67_STATUS] = 1U;
    return value;
}

static void
write_register(bm_mm58167_t *rtc, unsigned int reg, uint8_t value)
{
    static const uint8_t masks[8] = { 0xf0, 0xff, 0x7f, 0x7f, 0x3f, 0x07, 0x3f, 0x1f };
    unsigned int index;
    switch (reg) {
        case MM67_ISTAT:
        case MM67_STATUS:
            break;
        case MM67_ICTRL:
            rtc->registers[reg] = value;
            set_interrupt_status(rtc, 0U);
            break;
        case MM67_RSTCTR:
            if (value == 0xffU)
                reset_counters(rtc);
            break;
        case MM67_RSTRAM:
            if (value == 0xffU) {
                for (index = MM67_AL_MSEC; index <= MM67_AL_MON; ++index)
                    rtc->registers[index] = 0U;
            }
            break;
        case MM67_GOCMD:
            if (from_bcd(rtc->registers[MM67_SEC]) > 39U)
                tick_calendar(rtc, 1, 0U);
            rtc->registers[MM67_SEC] = 0U;
            rtc->registers[MM67_HUNTEN] = 0U;
            rtc->registers[MM67_MSEC] = 0U;
            rtc->millisecond_count = 0U;
            rtc->microsecond_remainder = 0U;
            break;
        case MM67_STBYIRQ:
        case MM67_TEST:
            rtc->registers[reg] = value;
            break;
        case MM67_AL_MSEC:
            rtc->registers[reg] = value & 0xf0U;
            break;
        case MM67_AL_DOW:
            rtc->registers[reg] = value & 0x0fU;
            break;
        case MM67_MSEC:
        case MM67_HUNTEN:
            rtc->registers[reg] = value & masks[reg];
            rtc->millisecond_count = (uint16_t)
                ((rtc->registers[MM67_MSEC] >> 4U) +
                 (from_bcd(rtc->registers[MM67_HUNTEN]) * 10U));
            break;
        case MM67_SEC:
        case MM67_MIN:
        case MM67_HOUR:
        case MM67_DOW:
        case MM67_DOM:
            rtc->registers[reg] = value & masks[reg];
            break;
        default:
            rtc->registers[reg] = value;
            break;
    }
}

static bm_status_t
rtc_access(void *context, bm_bus_transaction_t *transaction)
{
    bm_mm58167_t *rtc = context;
    unsigned int reg;
    bm_status_t status;

    if ((transaction->size != 1U) || (transaction->operation == BM_BUS_FETCH))
        return BM_STATUS_UNSUPPORTED;
    status = synchronize_clock(rtc);
    if (status != BM_STATUS_OK)
        return status;
    reg = register_for_port(rtc, transaction->address);
    if (transaction->operation == BM_BUS_READ)
        transaction->value = read_register(rtc, reg);
    else {
        write_register(rtc, reg, (uint8_t) transaction->value);
        status = clock_state_changed(rtc);
    }
    return status;
}

bm_status_t
bm_mm58167_create(const bm_host_services_t *host,
                  bm_bus_t *bus,
                  const bm_mm58167_config_t *config,
                  bm_mm58167_t **out_rtc)
{
    bm_mm58167_t *rtc;
    bm_status_t status;
    if ((bm_host_services_validate(host) != BM_STATUS_OK) || (bus == NULL) ||
        (config == NULL) || (out_rtc == NULL) ||
        (config->interrupt_io_base > UINT16_MAX - 7U) ||
        (config->counter_io_base > UINT16_MAX - 15U) ||
        ((config->initial_state != NULL) &&
         (config->initial_state_size != BM_MM58167_STATE_SIZE)) ||
        ((config->initial_state == NULL) && (config->initial_state_size != 0U)))
        return BM_STATUS_INVALID_ARGUMENT;
    *out_rtc = NULL;
    rtc = host->allocate(host->context, sizeof(*rtc));
    if (rtc == NULL)
        return BM_STATUS_OUT_OF_MEMORY;
    memset(rtc, 0, sizeof(*rtc));
    rtc->host = *host;
    rtc->interrupt_io_base = config->interrupt_io_base;
    rtc->counter_io_base = config->counter_io_base;
    rtc->interrupt = config->interrupt;
    rtc->interrupt_context = config->interrupt_context;
    if (config->initial_state != NULL)
        memcpy(rtc->registers, config->initial_state, BM_MM58167_STATE_SIZE);
    else
        bm_mm58167_cold_reset(rtc);
    rtc->millisecond_count = (uint16_t)
        ((rtc->registers[MM67_MSEC] >> 4U) +
         (from_bcd(rtc->registers[MM67_HUNTEN]) * 10U));
    status = bm_bus_map(bus, BM_ADDRESS_IO, config->counter_io_base,
                        (uint32_t) config->counter_io_base + 15U, rtc_access, rtc);
    if (status == BM_STATUS_OK)
        status = bm_bus_map(bus, BM_ADDRESS_IO, config->interrupt_io_base,
                            (uint32_t) config->interrupt_io_base + 7U,
                            rtc_access, rtc);
    if (status != BM_STATUS_OK) {
        host->release(host->context, rtc);
        return status;
    }
    set_irq(rtc, rtc->registers[MM67_ISTAT] != 0U);
    *out_rtc = rtc;
    return BM_STATUS_OK;
}

void
bm_mm58167_destroy(bm_mm58167_t *rtc)
{
    if (rtc != NULL) {
        set_irq(rtc, 0);
        if (rtc->clock_binding != NULL)
            rtc->host.release(rtc->host.context, rtc->clock_binding);
        rtc->host.release(rtc->host.context, rtc);
    }
}

void
bm_mm58167_reset(bm_mm58167_t *rtc)
{
    uint8_t checksum;
    uint8_t stored;
    if (rtc == NULL)
        return;
    (void) synchronize_clock(rtc);
    checksum = pcs86_checksum(rtc->registers);
    stored = (uint8_t) ((rtc->registers[MM67_AL_DOM] & 0x30U) |
                        ((rtc->registers[MM67_AL_MON] & 0x30U) >> 2U) |
                        (rtc->registers[MM67_AL_MON] & 0x03U));
    if (stored != checksum)
        repair_pcs86_checksum(rtc);
    set_interrupt_status(rtc, 0U);
    (void) clock_state_changed(rtc);
}

void
bm_mm58167_cold_reset(bm_mm58167_t *rtc)
{
    if (rtc == NULL)
        return;
    (void) synchronize_clock(rtc);
    memset(rtc->registers, 0, sizeof(rtc->registers));
    reset_counters(rtc);
    repair_pcs86_checksum(rtc);
    set_interrupt_status(rtc, 0U);
    (void) clock_state_changed(rtc);
}

bm_status_t
bm_mm58167_advance_microseconds(bm_mm58167_t *rtc, uint64_t microseconds)
{
    if (rtc == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    while (microseconds != 0U) {
        uint64_t until_tick = 1000U - rtc->microsecond_remainder;
        uint64_t step = microseconds < until_tick ? microseconds : until_tick;
        if (rtc->rollover_microseconds != 0U) {
            uint64_t busy_step = step < rtc->rollover_microseconds ?
                step : rtc->rollover_microseconds;
            rtc->rollover_microseconds = (uint16_t)
                (rtc->rollover_microseconds - busy_step);
        }
        rtc->microsecond_remainder = (uint16_t)
            (rtc->microsecond_remainder + step);
        microseconds -= step;
        if (rtc->microsecond_remainder == 1000U) {
            rtc->microsecond_remainder = 0U;
            tick_millisecond(rtc);
        }
    }
    return BM_STATUS_OK;
}

bm_status_t
bm_mm58167_save_state(bm_mm58167_t *rtc,
                      uint8_t *state,
                      size_t state_size)
{
    bm_status_t status;

    if ((rtc == NULL) || (state == NULL) || (state_size != BM_MM58167_STATE_SIZE))
        return BM_STATUS_INVALID_ARGUMENT;
    status = synchronize_clock(rtc);
    if (status != BM_STATUS_OK)
        return status;
    memcpy(state, rtc->registers, BM_MM58167_STATE_SIZE);
    return BM_STATUS_OK;
}

bm_status_t
bm_mm58167_load_state(bm_mm58167_t *rtc,
                      const uint8_t *state,
                      size_t state_size)
{
    bm_status_t status;

    if ((rtc == NULL) || (state == NULL) || (state_size != BM_MM58167_STATE_SIZE))
        return BM_STATUS_INVALID_ARGUMENT;
    status = synchronize_clock(rtc);
    if (status != BM_STATUS_OK)
        return status;
    memcpy(rtc->registers, state, BM_MM58167_STATE_SIZE);
    rtc->millisecond_count = (uint16_t)
        ((rtc->registers[MM67_MSEC] >> 4U) +
         (from_bcd(rtc->registers[MM67_HUNTEN]) * 10U));
    rtc->microsecond_remainder = 0U;
    rtc->rollover_microseconds = 0U;
    set_irq(rtc, rtc->registers[MM67_ISTAT] != 0U);
    return clock_state_changed(rtc);
}

bm_status_t
bm_mm58167_set_interrupt_status(bm_mm58167_t *rtc,
                                uint8_t interrupt_status)
{
    bm_status_t sync_status;

    if (rtc == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    sync_status = synchronize_clock(rtc);
    if (sync_status != BM_STATUS_OK)
        return sync_status;
    set_interrupt_status(rtc, interrupt_status);
    return BM_STATUS_OK;
}

uint8_t
bm_mm58167_interrupt_control(const bm_mm58167_t *rtc)
{
    return rtc != NULL ? rtc->registers[MM67_ICTRL] : 0U;
}
