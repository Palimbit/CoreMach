/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright holders: Sarah Walker
 * Copyright 2026 BluMach contributors
 *
 * Derived from the standard parallel-port register behaviour in BluMach's
 * inherited src/device/lpt.c. Host character devices, EPP/ECP, DMA, timers
 * and global port tables are deliberately outside this SPP component.
 */
#include <blumach/components/lpt_spp.h>

#include <string.h>

struct bm_lpt_spp {
    bm_host_services_t host;
    bm_lpt_spp_config_t configuration;
    uint8_t data;
    uint8_t status;
    uint8_t control;
    int irq_asserted;
};

static void
set_irq(bm_lpt_spp_t *lpt, int asserted)
{
    asserted = !!asserted;
    if (lpt->irq_asserted == asserted)
        return;
    lpt->irq_asserted = asserted;
    if (lpt->configuration.irq != NULL)
        lpt->configuration.irq(lpt->configuration.context, asserted);
}

bm_status_t
bm_lpt_spp_create(const bm_host_services_t *host,
                  const bm_lpt_spp_config_t *configuration,
                  bm_lpt_spp_t **out_lpt)
{
    bm_lpt_spp_t *lpt;

    if ((bm_host_services_validate(host) != BM_STATUS_OK) ||
        (configuration == NULL) || (out_lpt == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    *out_lpt = NULL;
    lpt = host->allocate(host->context, sizeof(*lpt));
    if (lpt == NULL)
        return BM_STATUS_OUT_OF_MEMORY;
    memset(lpt, 0, sizeof(*lpt));
    lpt->host = *host;
    lpt->configuration = *configuration;
    bm_lpt_spp_reset(lpt);
    *out_lpt = lpt;
    return BM_STATUS_OK;
}

void
bm_lpt_spp_destroy(bm_lpt_spp_t *lpt)
{
    if (lpt != NULL)
        lpt->host.release(lpt->host.context, lpt);
}

void
bm_lpt_spp_reset(bm_lpt_spp_t *lpt)
{
    if (lpt == NULL)
        return;
    set_irq(lpt, 0);
    lpt->data = 0;
    /* No attached printer: not busy, selected, no paper, no error. */
    lpt->status = 0xdfU;
    lpt->control = 0;
}

bm_status_t
bm_lpt_spp_read(bm_lpt_spp_t *lpt, unsigned int register_index, uint8_t *value)
{
    if ((lpt == NULL) || (value == NULL) || (register_index > 2U))
        return BM_STATUS_INVALID_ARGUMENT;
    if (register_index == 0U)
        *value = lpt->data;
    else if (register_index == 1U)
        *value = lpt->status;
    else
        *value = (uint8_t) (0xe0U | lpt->control);
    return BM_STATUS_OK;
}

bm_status_t
bm_lpt_spp_write(bm_lpt_spp_t *lpt, unsigned int register_index, uint8_t value)
{
    if ((lpt == NULL) || (register_index > 2U))
        return BM_STATUS_INVALID_ARGUMENT;
    if (register_index == 0U)
        lpt->data = value;
    else if (register_index == 2U) {
        /* The PCS 86 exposes an SPP: direction bit 5 has no effect. */
        lpt->control = value & 0x1fU;
        if ((lpt->control & 0x10U) == 0)
            set_irq(lpt, 0);
    }
    if (lpt->configuration.output != NULL)
        lpt->configuration.output(lpt->configuration.context,
                                  lpt->data, lpt->control);
    return BM_STATUS_OK;
}

bm_status_t
bm_lpt_spp_set_status(bm_lpt_spp_t *lpt, uint8_t status)
{
    int acknowledge_fell;
    int acknowledge_rose;
    if (lpt == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    acknowledge_fell = ((lpt->status & 0x40U) != 0) && ((status & 0x40U) == 0);
    acknowledge_rose = ((lpt->status & 0x40U) == 0) && ((status & 0x40U) != 0);
    lpt->status = status;
    if (acknowledge_fell && ((lpt->control & 0x10U) != 0))
        set_irq(lpt, 1);
    else if (acknowledge_rose)
        set_irq(lpt, 0);
    return BM_STATUS_OK;
}

bm_status_t
bm_lpt_spp_state(const bm_lpt_spp_t *lpt, bm_lpt_spp_state_t *state)
{
    if ((lpt == NULL) || (state == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    *state = (bm_lpt_spp_state_t) { lpt->data, lpt->status, lpt->control };
    return BM_STATUS_OK;
}
