/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 */
#include "page_spare_latches.h"
#include <assert.h>
#include <string.h>

static bm_bus_transaction_t access(uint16_t port, bm_bus_operation_t operation)
{
    bm_bus_transaction_t transaction={0};
    transaction.space=BM_ADDRESS_IO;
    transaction.address=port;
    transaction.operation=operation;
    transaction.endianness=BM_ENDIAN_LITTLE;
    transaction.size=1U;
    return transaction;
}

static void unchanged(bm_pcs286_page_spare_latches_t *latches,
                      bm_bus_transaction_t transaction, bm_status_t expected)
{
    bm_bus_transaction_t before=transaction;
    bm_pcs286_page_spare_latches_t saved;
    if (latches) saved=*latches;
    assert(bm_pcs286_page_spare_latches_io(latches,&transaction)==expected);
    assert(!memcmp(&transaction,&before,sizeof(transaction)));
    if (latches) assert(!memcmp(latches,&saved,sizeof(saved)));
}

int main(void)
{
    static const uint16_t spare[]={0x80,0x84,0x85,0x86,0x88,0x8c,0x8d,0x8e,0x8f};
    static const uint16_t channel[]={0x81,0x82,0x83,0x87,0x89,0x8a,0x8b};
    bm_pcs286_page_spare_latches_t a, b;
    bm_bus_transaction_t transaction;
    unsigned int i, pattern;
    bm_pcs286_page_spare_latches_initialize(&a);
    bm_pcs286_page_spare_latches_initialize(&b);

    for (pattern=1U;pattern<0x100U;pattern<<=1U) {
        for (i=0;i<sizeof(spare)/sizeof(spare[0]);++i) {
            transaction=access(spare[i],BM_BUS_WRITE);
            transaction.value=(pattern<<(spare[i]&7U))&0xffU;
            assert(bm_pcs286_page_spare_latches_io(&a,&transaction)==BM_STATUS_OK);
        }
        for (i=0;i<sizeof(spare)/sizeof(spare[0]);++i) {
            transaction=access(spare[i],BM_BUS_READ); transaction.value=0x99U;
            assert(bm_pcs286_page_spare_latches_io(&a,&transaction)==BM_STATUS_OK);
            assert(transaction.value==((pattern<<(spare[i]&7U))&0xffU));
        }
    }
    /* CPU-only reset does not initialize this board state. */
    transaction=access(0x80U,BM_BUS_READ);
    assert(bm_pcs286_page_spare_latches_io(&a,&transaction)==BM_STATUS_OK);
    assert(transaction.value==0x80U);
    transaction=access(0x80U,BM_BUS_READ);
    assert(bm_pcs286_page_spare_latches_io(&b,&transaction)==BM_STATUS_OK);
    assert(transaction.value==0U);

    transaction=access(0x80U,BM_BUS_READ);
    transaction.attributes=BM_BUS_TRANSACTION_DEBUG;
    assert(bm_pcs286_page_spare_latches_io(&a,&transaction)==BM_STATUS_OK);
    assert(transaction.value==0x80U && a.value[0]==0x80U);
    transaction=access(0x80U,BM_BUS_WRITE);
    transaction.attributes=BM_BUS_TRANSACTION_DEBUG;
    unchanged(&a,transaction,BM_STATUS_UNSUPPORTED);
    for (i=0;i<sizeof(channel)/sizeof(channel[0]);++i) {
        transaction=access(channel[i],BM_BUS_READ);
        unchanged(&a,transaction,BM_STATUS_INVALID_ARGUMENT);
    }
    transaction=access(0x90U,BM_BUS_READ);
    unchanged(&a,transaction,BM_STATUS_INVALID_ARGUMENT);
    transaction=access(0x80U,BM_BUS_READ); transaction.size=2U;
    unchanged(&a,transaction,BM_STATUS_INVALID_ARGUMENT);
    transaction=access(0x80U,BM_BUS_READ); transaction.space=BM_ADDRESS_MEMORY;
    unchanged(&a,transaction,BM_STATUS_INVALID_ARGUMENT);
    transaction=access(0x80U,BM_BUS_FETCH);
    unchanged(&a,transaction,BM_STATUS_INVALID_ARGUMENT);
    unchanged(NULL,transaction,BM_STATUS_INVALID_ARGUMENT);
    assert(bm_pcs286_page_spare_latches_io(&a,NULL)==BM_STATUS_INVALID_ARGUMENT);
    bm_pcs286_page_spare_latches_initialize(NULL);
    return 0;
}
