/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Functional board latches observed in the PCS286 BIOS 1.42 DMA-page test.
 */
#include "page_spare_latches.h"
#include <string.h>

static int spare_port(uint64_t port)
{
    return port==0x80U || (port>=0x84U && port<=0x86U) || port==0x88U ||
           (port>=0x8cU && port<=0x8fU);
}

void bm_pcs286_page_spare_latches_initialize(
    bm_pcs286_page_spare_latches_t *latches)
{
    if (latches) memset(latches,0,sizeof(*latches));
}

bm_status_t bm_pcs286_page_spare_latches_io(
    void *context, bm_bus_transaction_t *transaction)
{
    bm_pcs286_page_spare_latches_t *latches=context;
    unsigned int index;
    if (!latches || !transaction) return BM_STATUS_INVALID_ARGUMENT;
    if (transaction->space!=BM_ADDRESS_IO || !spare_port(transaction->address) ||
        transaction->size!=1U || transaction->endianness!=BM_ENDIAN_LITTLE ||
        (transaction->attributes&~BM_BUS_TRANSACTION_DEBUG)!=0U)
        return BM_STATUS_INVALID_ARGUMENT;
    index=(unsigned int)transaction->address&15U;
    if (transaction->operation==BM_BUS_READ) {
        transaction->value=latches->value[index];
        return BM_STATUS_OK;
    }
    if (transaction->operation==BM_BUS_WRITE) {
        if (transaction->attributes&BM_BUS_TRANSACTION_DEBUG)
            return BM_STATUS_UNSUPPORTED;
        latches->value[index]=(uint8_t)transaction->value;
        return BM_STATUS_OK;
    }
    return BM_STATUS_INVALID_ARGUMENT;
}
