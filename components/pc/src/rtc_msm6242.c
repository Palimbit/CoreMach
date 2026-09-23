/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2018 Fred N. van Kempen
 * Copyright 2026 BluMach contributors
 *
 * Derived from the inherited MSM6242 implementation in src/device/isartc.c.
 * Its original BSD notice and disclaimer are retained below.
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
#include <blumach/components/rtc_msm6242.h>

#include <string.h>

enum {
    M42_SECOND1 = 0, M42_SECOND10, M42_MINUTE1, M42_MINUTE10,
    M42_HOUR1, M42_HOUR10, M42_DAY1, M42_DAY10,
    M42_MONTH1, M42_MONTH10, M42_YEAR1, M42_YEAR10,
    M42_WEEKDAY, M42_CONTROL_D, M42_CONTROL_E, M42_CONTROL_F
};

enum {
    M42_HOLD = 0x01, M42_ADJUST = 0x08,
    M42_REST = 0x01, M42_STOP = 0x02, M42_24H = 0x04
};

struct bm_msm6242 {
    bm_host_services_t host;
    uint16_t io_base;
    uint8_t registers[BM_MSM6242_STATE_SIZE];
    int hold_pending_second;
};

static unsigned int
decimal_pair(const uint8_t *regs, unsigned int units)
{
    return (unsigned int) (regs[units] & 0x0fU) +
           10U * (unsigned int) (regs[units + 1U] & 0x0fU);
}

static void
set_decimal_pair(uint8_t *regs, unsigned int units, unsigned int value)
{
    regs[units] = (uint8_t) (value % 10U);
    regs[units + 1U] = (uint8_t) ((value / 10U) % 10U);
}

static unsigned int
hour24(const uint8_t *regs)
{
    unsigned int hour = (unsigned int) regs[M42_HOUR1] +
                        10U * (unsigned int) (regs[M42_HOUR10] & 0x03U);

    if ((regs[M42_CONTROL_F] & M42_24H) == 0U)
        hour = (hour % 12U) +
               ((regs[M42_HOUR10] & 0x04U) != 0U ? 12U : 0U);
    return hour % 24U;
}

static void
set_hour(uint8_t *regs, unsigned int hour)
{
    hour %= 24U;
    if ((regs[M42_CONTROL_F] & M42_24H) != 0U)
        set_decimal_pair(regs, M42_HOUR1, hour);
    else {
        unsigned int display = hour % 12U;
        if (display == 0U)
            display = 12U;
        regs[M42_HOUR1] = (uint8_t) (display % 10U);
        regs[M42_HOUR10] = (uint8_t) ((display / 10U) |
            (hour >= 12U ? 0x04U : 0U));
    }
}

static unsigned int
days_in_month(unsigned int month, unsigned int year)
{
    static const uint8_t days[] = {
        31U, 28U, 31U, 30U, 31U, 30U,
        31U, 31U, 30U, 31U, 30U, 31U
    };

    if ((month < 1U) || (month > 12U))
        return 31U;
    if ((month == 2U) && ((year % 4U) == 0U))
        return 29U;
    return days[month - 1U];
}

static int
valid_initial_state(const uint8_t *regs)
{
    unsigned int second = decimal_pair(regs, M42_SECOND1);
    unsigned int minute = decimal_pair(regs, M42_MINUTE1);
    unsigned int hour = decimal_pair(regs, M42_HOUR1);
    unsigned int day = decimal_pair(regs, M42_DAY1);
    unsigned int month = decimal_pair(regs, M42_MONTH1);
    unsigned int year = decimal_pair(regs, M42_YEAR1);
    unsigned int index;

    for (index = 0U; index < BM_MSM6242_STATE_SIZE; ++index) {
        if (regs[index] > 0x0fU)
            return 0;
    }
    if ((regs[M42_SECOND1] > 9U) || (regs[M42_SECOND10] > 5U) ||
        (regs[M42_MINUTE1] > 9U) || (regs[M42_MINUTE10] > 5U) ||
        (regs[M42_HOUR1] > 9U) || (regs[M42_DAY1] > 9U) ||
        (regs[M42_MONTH1] > 9U) || (regs[M42_YEAR1] > 9U) ||
        (regs[M42_YEAR10] > 9U) || (regs[M42_WEEKDAY] > 6U) ||
        (second > 59U) || (minute > 59U) ||
        (month < 1U) || (month > 12U) ||
        (day < 1U) || (day > days_in_month(month, year)))
        return 0;
    if ((regs[M42_CONTROL_F] & M42_24H) != 0U)
        return hour <= 23U;
    hour = (unsigned int) regs[M42_HOUR1] +
           10U * (unsigned int) (regs[M42_HOUR10] & 0x03U);
    return (hour >= 1U) && (hour <= 12U);
}

static void
advance_minute(uint8_t *regs)
{
    unsigned int minute = decimal_pair(regs, M42_MINUTE1);
    unsigned int hour;
    unsigned int day;
    unsigned int month;
    unsigned int year;

    minute = minute >= 59U ? 0U : minute + 1U;
    set_decimal_pair(regs, M42_MINUTE1, minute);
    if (minute != 0U)
        return;
    hour = (hour24(regs) + 1U) % 24U;
    set_hour(regs, hour);
    if (hour != 0U)
        return;
    day = decimal_pair(regs, M42_DAY1);
    month = decimal_pair(regs, M42_MONTH1);
    year = decimal_pair(regs, M42_YEAR1);
    day = (day < 1U) || (day >= days_in_month(month, year)) ? 1U : day + 1U;
    set_decimal_pair(regs, M42_DAY1, day);
    regs[M42_WEEKDAY] = (uint8_t) ((regs[M42_WEEKDAY] + 1U) % 7U);
    if (day != 1U)
        return;
    month = (month < 1U) || (month >= 12U) ? 1U : month + 1U;
    set_decimal_pair(regs, M42_MONTH1, month);
    if (month == 1U)
        set_decimal_pair(regs, M42_YEAR1, (year + 1U) % 100U);
}

static void
count_second(bm_msm6242_t *rtc)
{
    uint8_t *regs = rtc->registers;
    unsigned int second = decimal_pair(regs, M42_SECOND1);

    second = second >= 59U ? 0U : second + 1U;
    set_decimal_pair(regs, M42_SECOND1, second);
    if (second == 0U)
        advance_minute(regs);
}

static bm_status_t
rtc_access(void *context, bm_bus_transaction_t *transaction)
{
    bm_msm6242_t *rtc = context;
    uint8_t reg;
    uint8_t value;

    if ((transaction == NULL) || (transaction->size != 1U) ||
        (transaction->operation == BM_BUS_FETCH))
        return BM_STATUS_UNSUPPORTED;
    reg = (uint8_t) (transaction->address - rtc->io_base);
    if (transaction->operation == BM_BUS_READ) {
        value = rtc->registers[reg];
        if (reg == M42_CONTROL_D)
            value &= 0x0dU; /* Atomic I/O: BUSY not modeled. */
        transaction->value = value & 0x0fU;
        return BM_STATUS_OK;
    }
    if ((transaction->attributes & BM_BUS_TRANSACTION_DEBUG) != 0U)
        return BM_STATUS_OK;
    value = (uint8_t) transaction->value & 0x0fU;
    if (reg == M42_CONTROL_D) {
        int release_hold = ((rtc->registers[reg] & M42_HOLD) != 0U) &&
                           ((value & M42_HOLD) == 0U);

        /* BUSY is read-only. IRQ FLAG is not generated by this subset. */
        rtc->registers[reg] = value & M42_HOLD;
        if ((value & M42_ADJUST) != 0U) {
            unsigned int second = decimal_pair(rtc->registers, M42_SECOND1);
            set_decimal_pair(rtc->registers, M42_SECOND1, 0U);
            if (second >= 30U)
                advance_minute(rtc->registers);
        }
        if (release_hold && rtc->hold_pending_second) {
            count_second(rtc);
            rtc->hold_pending_second = 0;
        }
    } else if (reg == M42_CONTROL_F) {
        /* The 12/24-hour selector is writable while REST is asserted. */
        if (((value | rtc->registers[reg]) & M42_REST) == 0U)
            value = (uint8_t) ((value & (uint8_t) ~M42_24H) |
                               (rtc->registers[reg] & M42_24H));
        rtc->registers[reg] = value;
    } else
        rtc->registers[reg] = value;
    return BM_STATUS_OK;
}

bm_status_t
bm_msm6242_create(const bm_host_services_t *host, bm_bus_t *bus,
                  const bm_msm6242_config_t *config, bm_msm6242_t **out_rtc)
{
    bm_msm6242_t *rtc;
    bm_status_t status;

    if ((bm_host_services_validate(host) != BM_STATUS_OK) || (bus == NULL) ||
        (config == NULL) || (out_rtc == NULL) ||
        (config->io_base > UINT16_MAX - 15U) ||
        ((config->initial_state == NULL) && (config->initial_state_size != 0U)) ||
        ((config->initial_state != NULL) &&
         (config->initial_state_size != BM_MSM6242_STATE_SIZE)))
        return BM_STATUS_INVALID_ARGUMENT;
    if ((config->initial_state != NULL) &&
        !valid_initial_state(config->initial_state))
        return BM_STATUS_INVALID_ARGUMENT;
    *out_rtc = NULL;
    rtc = host->allocate(host->context, sizeof(*rtc));
    if (rtc == NULL)
        return BM_STATUS_OUT_OF_MEMORY;
    memset(rtc, 0, sizeof(*rtc));
    rtc->host = *host;
    rtc->io_base = config->io_base;
    if (config->initial_state != NULL)
        memcpy(rtc->registers, config->initial_state, BM_MSM6242_STATE_SIZE);
    else {
        set_decimal_pair(rtc->registers, M42_DAY1, 1U);
        set_decimal_pair(rtc->registers, M42_MONTH1, 1U);
        set_decimal_pair(rtc->registers, M42_YEAR1, 80U);
        rtc->registers[M42_WEEKDAY] = 2U;
        rtc->registers[M42_CONTROL_F] = M42_24H;
    }
    status = bm_bus_map(bus, BM_ADDRESS_IO, config->io_base,
                        (uint32_t) config->io_base + 15U, rtc_access, rtc);
    if (status != BM_STATUS_OK) {
        host->release(host->context, rtc);
        return status;
    }
    *out_rtc = rtc;
    return BM_STATUS_OK;
}

void
bm_msm6242_destroy(bm_msm6242_t *rtc)
{
    if (rtc != NULL)
        rtc->host.release(rtc->host.context, rtc);
}

bm_status_t
bm_msm6242_advance_second(bm_msm6242_t *rtc)
{
    uint8_t *regs;

    if (rtc == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    regs = rtc->registers;
    if ((regs[M42_CONTROL_F] & (M42_STOP | M42_REST)) != 0U)
        return BM_STATUS_OK;
    if ((regs[M42_CONTROL_D] & M42_HOLD) != 0U)
        rtc->hold_pending_second = 1;
    else
        count_second(rtc);
    return BM_STATUS_OK;
}

bm_status_t
bm_msm6242_save_state(const bm_msm6242_t *rtc, uint8_t *state, size_t size)
{
    if ((rtc == NULL) || (state == NULL) || (size != BM_MSM6242_STATE_SIZE))
        return BM_STATUS_INVALID_ARGUMENT;
    memcpy(state, rtc->registers, BM_MSM6242_STATE_SIZE);
    return BM_STATUS_OK;
}
