/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2017-2020 Fred N. van Kempen
 * Copyright 2016-2020 Miran Grca
 * Copyright 2016-2020 Mahod
 * Copyright 2026 BluMach contributors
 *
 * Derived from src/nvr_at.c (format, alarm, rate and IRQ register paths) and
 * src/nvr.c (calendar carry). Replaced global host calendar/timers with instance
 * state. Motorola MC146818A ADI1026R3 governs corrections; no vendor BIOS hacks.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version. This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details. You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
/*
 * VARCem   Virtual ARchaeological Computer EMulator.
 *          An emulator of (mostly) x86-based PC systems and devices,
 *          using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *          spanning the era between 1981 and 1995.
 *
 *          Implement a generic NVRAM/CMOS/RTC device.
 *
 * Authors: Fred N. van Kempen, <decwiz@yahoo.com>,
 *          David Hrdlička, <hrdlickadavid@outlook.com>
 *
 *          Copyright 2017-2019 Fred N. van Kempen.
 *          Copyright 2018-2019 David Hrdlička.
 *
 *          Redistribution and  use  in source  and binary forms, with
 *          or  without modification, are permitted  provided that the
 *          following conditions are met:
 *
 *          1. Redistributions of  source  code must retain the entire
 *             above notice, this list of conditions and the following
 *             disclaimer.
 *
 *          2. Redistributions in binary form must reproduce the above
 *             copyright  notice,  this list  of  conditions  and  the
 *             following disclaimer in  the documentation and/or other
 *             materials provided with the distribution.
 *
 *          3. Neither the  name of the copyright holder nor the names
 *             of  its  contributors may be used to endorse or promote
 *             products  derived from  this  software without specific
 *             prior written permission.
 *
 * THIS SOFTWARE  IS  PROVIDED BY THE  COPYRIGHT  HOLDERS AND CONTRIBUTORS
 * "AS IS" AND  ANY EXPRESS  OR  IMPLIED  WARRANTIES,  INCLUDING, BUT  NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE  ARE  DISCLAIMED. IN  NO  EVENT  SHALL THE COPYRIGHT
 * HOLDER OR  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL,  EXEMPLARY,  OR  CONSEQUENTIAL  DAMAGES  (INCLUDING,  BUT  NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE  GOODS OR SERVICES;  LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED  AND ON  ANY
 * THEORY OF  LIABILITY, WHETHER IN  CONTRACT, STRICT  LIABILITY, OR  TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING  IN ANY  WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "rtc_at_private.h"
#include <string.h>

enum { SEC = 0, MIN = 2, HOUR = 4, DOW = 6, DAY = 7, MONTH = 8, YEAR = 9,
       REGA = 10, REGB = 11, REGC = 12, REGD = 13 };
enum { SET = 0x80, DM = 4, H24 = 2, UIP = 0x80,
       IRQF = 0x80, PF = 0x40, AF = 0x20, UF = 0x10 };
enum { SECOND = 32768, UIP_EDGE = 16376, UPDATE_START = 16384, UPDATE_END = 16449 };
typedef struct calendar { unsigned sec, min, hour, dow, day, month, year; } calendar_t;

static unsigned decode(uint8_t value, int binary)
{
    if (binary) return value;
    if ((value & 15U) > 9U || (value >> 4U) > 9U) return 256U;
    return (value >> 4U) * 10U + (value & 15U);
}
static uint8_t encode(unsigned value, int binary)
{
    return (uint8_t)(binary ? value : (value / 10U) * 16U + value % 10U);
}
static unsigned month_days(unsigned month, unsigned year)
{
    static const uint8_t days[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    return days[month - 1U] + (month == 2U && year % 4U == 0U ? 1U : 0U);
}
static int get_calendar(const bm_at_rtc_t *rtc, calendar_t *c)
{
    const uint8_t *r = rtc->regs;
    int binary = (r[REGB] & DM) != 0, h24 = (r[REGB] & H24) != 0;
    c->sec = decode(r[SEC], binary); c->min = decode(r[MIN], binary);
    /* PM must be removed BEFORE decoding packed BCD (classic did the reverse). */
    c->hour = decode((uint8_t)(r[HOUR] & (h24 ? 0xffU : 0x7fU)), binary);
    if (!h24) {
        if (c->hour < 1U || c->hour > 12U) return 0;
        c->hour = c->hour % 12U + ((r[HOUR] & 0x80U) ? 12U : 0U);
    }
    c->dow = decode(r[DOW], binary); c->day = decode(r[DAY], binary);
    c->month = decode(r[MONTH], binary); c->year = decode(r[YEAR], binary);
    return c->sec < 60U && c->min < 60U && c->hour < 24U &&
        c->dow >= 1U && c->dow <= 7U && c->month >= 1U && c->month <= 12U &&
        c->year <= 99U && c->day >= 1U && c->day <= month_days(c->month, c->year);
}
static void set_calendar(bm_at_rtc_t *rtc, const calendar_t *c)
{
    uint8_t *r = rtc->regs;
    int binary = (r[REGB] & DM) != 0;
    r[SEC] = encode(c->sec, binary); r[MIN] = encode(c->min, binary);
    r[DOW] = encode(c->dow, binary); r[DAY] = encode(c->day, binary);
    r[MONTH] = encode(c->month, binary); r[YEAR] = encode(c->year, binary);
    if (r[REGB] & H24) r[HOUR] = encode(c->hour, binary);
    else r[HOUR] = (uint8_t)(encode(c->hour % 12U ? c->hour % 12U : 12U, binary) |
                            (c->hour >= 12U ? 0x80U : 0U));
}
static bm_status_t update_calendar(bm_at_rtc_t *rtc)
{
    calendar_t c;
    unsigned i;
    if (!get_calendar(rtc, &c)) return BM_STATUS_UNSUPPORTED;
    if (++c.sec == 60U) {
        c.sec = 0;
        if (++c.min == 60U) {
            c.min = 0;
            if (++c.hour == 24U) {
                c.hour = 0; c.dow = c.dow % 7U + 1U;
                if (++c.day > month_days(c.month, c.year)) {
                    c.day = 1;
                    if (++c.month == 13U) { c.month = 1; c.year = (c.year + 1U) % 100U; }
                }
            }
        }
    }
    set_calendar(rtc, &c); /* Century/config/checksum bytes are ordinary RAM. */
    rtc->regs[REGC] |= UF;
    for (i = SEC; i <= HOUR; i += 2U)
        if (rtc->regs[i] != rtc->regs[i + 1U] && (rtc->regs[i + 1U] & 0xc0U) != 0xc0U)
            return BM_STATUS_OK;
    rtc->regs[REGC] |= AF;
    return BM_STATUS_OK;
}
static int divider_running(const bm_at_rtc_t *rtc) { return (rtc->regs[REGA] & 0x70U) == 0x20U; }
static int supported_a(uint8_t value, bm_at_rtc_divider_policy_t policy)
{
    unsigned dv = value & 0x70U;
    return policy == BM_AT_RTC_DIVIDER_CLASSIC_STOP || dv == 0x20U || dv == 0x60U || dv == 0x70U;
}
static unsigned period(const bm_at_rtc_t *rtc)
{
    unsigned rs = rtc->regs[REGA] & 15U;
    return rs == 0U ? 0U : 1U << (rs < 3U ? rs + 6U : rs - 1U);
}
static bm_status_t publish(bm_at_rtc_t *rtc, bm_at_rtc_line_fn fn, int level)
{
    bm_status_t result = fn(rtc->config.output_context, level);
    if (result == BM_STATUS_IDLE) result = BM_STATUS_INVALID_STATE;
    if (result != BM_STATUS_OK) rtc->state.failure = result;
    return result;
}
static bm_status_t irq_update(bm_at_rtc_t *rtc, int force)
{
    int level = (rtc->regs[REGB] & rtc->regs[REGC] & 0x70U) != 0;
    rtc->regs[REGC] = (uint8_t)((rtc->regs[REGC] & 0x70U) | (level ? IRQF : 0U));
    if (!force && level == rtc->state.irq) return BM_STATUS_OK;
    rtc->state.irq = level;
    return publish(rtc, rtc->config.irq, level);
}
static uint8_t reg_read(const bm_at_rtc_t *rtc, unsigned index)
{
    return index == REGA ? (uint8_t)(rtc->regs[REGA] | (rtc->state.uip ? UIP : 0U)) : rtc->regs[index];
}
bm_status_t bm_at_rtc_create(const bm_host_services_t *host,
                             const bm_at_rtc_config_t *config, bm_at_rtc_t **out_rtc)
{
    bm_at_rtc_t *rtc;
    if (out_rtc) *out_rtc = NULL;
    if (bm_host_services_validate(host) != BM_STATUS_OK || !config || !out_rtc ||
        config->io_base == UINT16_MAX || !config->irq || !config->nmi_mask ||
        (config->cmos_size != 64U && config->cmos_size != 128U) ||
        (config->battery_valid != 0 && config->battery_valid != 1) ||
        (config->divider_policy != BM_AT_RTC_DIVIDER_QUALIFIED &&
         config->divider_policy != BM_AT_RTC_DIVIDER_CLASSIC_STOP) ||
        (config->initial_cmos ? config->initial_cmos_size != config->cmos_size :
                               config->initial_cmos_size != 0U || config->battery_valid))
        return BM_STATUS_INVALID_ARGUMENT;
    if (config->initial_cmos && (!supported_a(config->initial_cmos[REGA], config->divider_policy) ||
                                (config->initial_cmos[REGB] & 1U))) return BM_STATUS_UNSUPPORTED;
    rtc = host->allocate(host->context, sizeof(*rtc));
    if (!rtc) return BM_STATUS_OUT_OF_MEMORY;
    memset(rtc, 0, sizeof(*rtc)); rtc->host = *host; rtc->config = *config;
    rtc->config.initial_cmos = NULL; /* Borrowed only for this call. */
    if (config->initial_cmos) memcpy(rtc->regs, config->initial_cmos, config->cmos_size);
    else { rtc->regs[REGA] = 0x60U; rtc->regs[REGB] = SET; }
    rtc->regs[SEC] &= 0x7fU; rtc->regs[REGA] &= 0x7fU;
    rtc->regs[REGB] &= 0x87U; rtc->regs[REGC] = 0;
    rtc->regs[REGD] = config->battery_valid ? 0x80U : 0;
    rtc->state.nmi_mask = 1; *out_rtc = rtc;
    return BM_STATUS_OK;
}
void bm_at_rtc_destroy(bm_at_rtc_t *rtc)
{
    if (rtc && !rtc->busy) rtc->host.release(rtc->host.context, rtc);
}
bm_status_t bm_at_rtc_reset(bm_at_rtc_t *rtc)
{
    bm_status_t result;
    if (!rtc) return BM_STATUS_INVALID_ARGUMENT;
    if (rtc->busy) return BM_STATUS_INVALID_STATE;
    rtc->busy = 1; rtc->state.failure = BM_STATUS_OK;
    rtc->regs[REGB] &= 0x87U; rtc->regs[REGC] = 0;
    rtc->state.index = 0; rtc->state.nmi_mask = 1;
    /* Accepted internal reset is complete; stop at the first failed endpoint. */
    rtc->state.irq = 0;
    result = publish(rtc, rtc->config.nmi_mask, 1);
    if (result == BM_STATUS_OK) result = irq_update(rtc, 1);
    rtc->busy = 0; return result;
}
bm_status_t bm_at_rtc_io(void *context, bm_bus_transaction_t *t)
{
    bm_at_rtc_t *rtc = context;
    bm_status_t result = BM_STATUS_OK;
    uint8_t value, index;
    unsigned port;
    int debug;
    if (!rtc || !t) return BM_STATUS_INVALID_ARGUMENT;
    if (t->space != BM_ADDRESS_IO) return BM_STATUS_UNMAPPED;
    if (t->operation < BM_BUS_READ || t->operation > BM_BUS_FETCH || t->address > UINT16_MAX ||
        t->alignment > 1U || (t->attributes & ~(uint32_t)(BM_BUS_TRANSACTION_DEBUG | BM_BUS_TRANSACTION_LOCKED)) ||
        (t->endianness != BM_ENDIAN_LITTLE && t->endianness != BM_ENDIAN_BIG)) return BM_STATUS_INVALID_ARGUMENT;
    if (t->size != 1U || t->operation == BM_BUS_FETCH) return BM_STATUS_UNSUPPORTED;
    if (t->address < rtc->config.io_base || t->address > (unsigned)rtc->config.io_base + 1U)
        return BM_STATUS_UNMAPPED;
    port = (unsigned)t->address - rtc->config.io_base; index = rtc->state.index;
    debug = (t->attributes & BM_BUS_TRANSACTION_DEBUG) != 0;
    if (debug) {
        if (t->operation != BM_BUS_READ) return BM_STATUS_UNSUPPORTED;
        t->value = port ? reg_read(rtc, index) : (uint8_t)(index | (rtc->state.nmi_mask ? 0x80U : 0U));
        return BM_STATUS_OK;
    }
    if (rtc->busy) return BM_STATUS_INVALID_STATE;
    if (rtc->state.failure != BM_STATUS_OK) return rtc->state.failure;
    if (!port && t->operation == BM_BUS_READ) return BM_STATUS_UNMAPPED;
    if (port && index < REGA && rtc->state.updating) return BM_STATUS_UNSUPPORTED;
    value = (uint8_t)t->value;
    if (port && t->operation == BM_BUS_WRITE &&
        ((index == REGA && !supported_a(value, rtc->config.divider_policy)) || (index == REGB && (value & 1U))))
        return BM_STATUS_UNSUPPORTED;
    rtc->busy = 1;
    if (!port) {
        int mask = (value & 0x80U) != 0;
        rtc->state.index = (uint8_t)(value & (rtc->config.cmos_size - 1U));
        if (mask != rtc->state.nmi_mask) {
            rtc->state.nmi_mask = mask; result = publish(rtc, rtc->config.nmi_mask, mask);
        }
    } else if (t->operation == BM_BUS_READ) {
        value = reg_read(rtc, index);
        if (index == REGC) { rtc->regs[REGC] = 0; result = irq_update(rtc, 0); }
        else if (index == REGD) rtc->regs[REGD] = 0x80U; /* PS high, acknowledge prior invalidity. */
        if (result == BM_STATUS_OK) t->value = value;
    } else if (index == REGA) {
        rtc->regs[REGA] = value & 0x7fU;
        if (!divider_running(rtc)) {
            rtc->state.divider_phase = 0; rtc->state.uip = 0; rtc->state.updating = 0;
        }
    } else if (index == REGB) {
        rtc->regs[REGB] = value;
        if (value & SET) {
            rtc->regs[REGB] &= 0xefU;
            rtc->state.uip = 0; rtc->state.updating = 0;
        }
        result = irq_update(rtc, 0);
    } else if (index != REGC && index != REGD) rtc->regs[index] = index == SEC ? value & 0x7fU : value;
    rtc->busy = 0; return result;
}
static uint64_t deadline(const bm_at_rtc_t *rtc)
{
    uint64_t next = 0;
    unsigned p = period(rtc), phase = rtc->state.divider_phase;
    if (!divider_running(rtc)) return 0;
    if (!(rtc->regs[REGB] & SET)) {
        unsigned target = rtc->state.updating ? UPDATE_END : rtc->state.uip ? UPDATE_START : UIP_EDGE;
        next = target > phase ? target - phase : SECOND - phase + target;
    }
    if (p && !(rtc->regs[REGC] & PF)) {
        unsigned delta = p - phase % p;
        if (!next || delta < next) next = delta;
    }
    return next;
}
bm_status_t bm_at_rtc_advance(bm_at_rtc_t *rtc, uint64_t cycles)
{
    bm_status_t result = BM_STATUS_OK;
    if (!rtc) return BM_STATUS_INVALID_ARGUMENT;
    if (rtc->busy) return BM_STATUS_INVALID_STATE;
    if (rtc->state.failure != BM_STATUS_OK) return rtc->state.failure;
    if (cycles > UINT64_MAX - rtc->state.cycles) return BM_STATUS_CAPACITY_EXCEEDED;
    rtc->busy = 1;
    while (cycles) {
        uint64_t next = deadline(rtc), step = next && next < cycles ? next : cycles;
        unsigned p = period(rtc);
        rtc->state.cycles += step; cycles -= step;
        if (divider_running(rtc))
            rtc->state.divider_phase = (rtc->state.divider_phase + (uint32_t)(step % SECOND)) % SECOND;
        if (!next || step != next) continue;
        if (p && rtc->state.divider_phase % p == 0U) rtc->regs[REGC] |= PF;
        if (!(rtc->regs[REGB] & SET)) {
            if (rtc->state.divider_phase == UIP_EDGE) rtc->state.uip = 1;
            else if (rtc->state.divider_phase == UPDATE_START && rtc->state.uip) rtc->state.updating = 1;
            else if (rtc->state.divider_phase == UPDATE_END && rtc->state.updating) {
                rtc->state.uip = 0; rtc->state.updating = 0; result = update_calendar(rtc);
            }
        }
        if (result == BM_STATUS_OK) result = irq_update(rtc, 0);
        if (result != BM_STATUS_OK) break;
    }
    rtc->busy = 0; return result;
}
bm_status_t bm_at_rtc_next_deadline(const bm_at_rtc_t *rtc, uint64_t *cycles)
{
    if (!rtc || !cycles) return BM_STATUS_INVALID_ARGUMENT;
    if (rtc->state.failure != BM_STATUS_OK) return rtc->state.failure;
    *cycles = deadline(rtc); return *cycles ? BM_STATUS_OK : BM_STATUS_IDLE;
}
bm_status_t bm_at_rtc_state(const bm_at_rtc_t *rtc, bm_at_rtc_state_t *state)
{
    if (!rtc || !state) return BM_STATUS_INVALID_ARGUMENT;
    *state = rtc->state; return BM_STATUS_OK;
}
bm_status_t bm_at_rtc_export_cmos(const bm_at_rtc_t *rtc, uint8_t *bytes, size_t size)
{
    if (!rtc || !bytes || size != rtc->config.cmos_size) return BM_STATUS_INVALID_ARGUMENT;
    memcpy(bytes, rtc->regs, size); bytes[REGA] = reg_read(rtc, REGA); return BM_STATUS_OK;
}
