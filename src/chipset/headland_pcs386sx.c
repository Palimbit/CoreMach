/*
 * BluMach, a preservation-focused fork of 86Box.
 *
 * Board wiring for the discrete Headland HT101SX + HT113 + GC102-PC set.
 * The aggregate owns shared signals only; it is not a fourth physical chip.
 *
 * Author: rtzor
 *
 * Copyright 2026 rtzor.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdint.h>
#include <stdlib.h>

#include <86box/device.h>
#include <86box/chipset.h>
#include <86box/plat_unused.h>

#include "headland_pcs386sx.h"

void
headland_pcs386sx_retain(headland_pcs386sx_t *board)
{
    if (board != NULL)
        board->references++;
}

void
headland_pcs386sx_release(headland_pcs386sx_t *board)
{
    if ((board != NULL) && (--board->references == 0))
        free(board);
}

static void *
headland_pcs386sx_chipset_init(UNUSED(const device_t *info))
{
    headland_pcs386sx_t *board;

    board = (headland_pcs386sx_t *) calloc(1, sizeof(headland_pcs386sx_t));
    if (board == NULL)
        return NULL;

    board->cold_start = 1;
    board->references = 1;

    if (device_add_linked(&headland_ht101sx_device, board) == NULL) {
        headland_pcs386sx_release(board);
        return NULL;
    }
    if (device_add_linked(&headland_ht113_device, board) == NULL) {
        device_close(&headland_ht101sx_device);
        headland_pcs386sx_release(board);
        return NULL;
    }
    if (device_add_linked(&headland_gc102pc_device, board) == NULL) {
        device_close(&headland_ht113_device);
        device_close(&headland_ht101sx_device);
        headland_pcs386sx_release(board);
        return NULL;
    }

    board->cold_start = 0;
    return board;
}

static void
headland_pcs386sx_chipset_close(void *priv)
{
    headland_pcs386sx_release((headland_pcs386sx_t *) priv);
}

const device_t headland_pcs386sx_chipset_device = {
    .name          = "Headland HT101SX + HT113 + GC102-PC",
    .internal_name = "headland_pcs386sx_chipset",
    .flags         = 0,
    .local         = 0,
    .init          = headland_pcs386sx_chipset_init,
    .close         = headland_pcs386sx_chipset_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

/* Manufacturer-neutral identity for unrelated boards using the same three
   physical Headland packages. Keep the older PCS-named descriptor for source
   compatibility with the Olivetti driver only. */
const device_t headland_ht101sx_chipset_device = {
    .name          = "Headland HT101SX + HT113 + GC102-PC",
    .internal_name = "headland_ht101sx_chipset",
    .flags         = 0,
    .local         = 0,
    .init          = headland_pcs386sx_chipset_init,
    .close         = headland_pcs386sx_chipset_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
