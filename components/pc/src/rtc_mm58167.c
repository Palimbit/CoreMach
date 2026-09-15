/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2018 Fred N. van Kempen
 * Copyright 2026 BluMach contributors
 *
 * Derived rewrite of the inherited MM58167 interrupt-register front end.
 * The original source was distributed under the following BSD terms, which
 * are retained in full:
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
#include <blumach/components/rtc_mm58167.h>

#include <string.h>

struct bm_mm58167 {
    bm_host_services_t host;
    uint16_t interrupt_io_base;
    uint8_t interrupt_status;
    uint8_t interrupt_control;
};

static bm_status_t
interrupt_access(void *context, bm_bus_transaction_t *transaction)
{
    bm_mm58167_t *rtc = context;
    unsigned int offset;

    if ((transaction->size != 1) || (transaction->operation == BM_BUS_FETCH))
        return BM_STATUS_UNSUPPORTED;
    offset = (unsigned int) (transaction->address - rtc->interrupt_io_base);
    if (transaction->operation == BM_BUS_READ) {
        if (offset == 0U) {
            transaction->value = rtc->interrupt_status;
            rtc->interrupt_status = 0;
        } else {
            transaction->value = rtc->interrupt_control;
        }
        return BM_STATUS_OK;
    }

    if (offset == 1U) {
        rtc->interrupt_status = 0;
        rtc->interrupt_control = (uint8_t) transaction->value;
    }
    return BM_STATUS_OK;
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
        (config->interrupt_io_base == UINT16_MAX))
        return BM_STATUS_INVALID_ARGUMENT;
    *out_rtc = NULL;
    rtc = host->allocate(host->context, sizeof(*rtc));
    if (rtc == NULL)
        return BM_STATUS_OUT_OF_MEMORY;
    memset(rtc, 0, sizeof(*rtc));
    rtc->host = *host;
    rtc->interrupt_io_base = config->interrupt_io_base;
    status = bm_bus_map(bus, BM_ADDRESS_IO, config->interrupt_io_base,
                        (uint32_t) config->interrupt_io_base + 1U,
                        interrupt_access, rtc);
    if (status != BM_STATUS_OK) {
        host->release(host->context, rtc);
        return status;
    }
    *out_rtc = rtc;
    return BM_STATUS_OK;
}

void
bm_mm58167_destroy(bm_mm58167_t *rtc)
{
    if (rtc != NULL)
        rtc->host.release(rtc->host.context, rtc);
}

void
bm_mm58167_reset(bm_mm58167_t *rtc)
{
    if (rtc == NULL)
        return;
    rtc->interrupt_status = 0;
    rtc->interrupt_control = 0;
}

bm_status_t
bm_mm58167_set_interrupt_status(bm_mm58167_t *rtc, uint8_t status)
{
    if (rtc == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    rtc->interrupt_status = status;
    return BM_STATUS_OK;
}

uint8_t
bm_mm58167_interrupt_control(const bm_mm58167_t *rtc)
{
    return rtc != NULL ? rtc->interrupt_control : 0;
}
