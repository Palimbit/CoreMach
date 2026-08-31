/*
 * Headland HT101SX package identity and board-level coordination.
 *
 * No public HT101SX data sheet is currently known.  Consequently this
 * device intentionally exposes no guessed registers.  The firmware-visible
 * memory interface is implemented by the linked HT113 device and can be
 * reassigned later if exact documentation proves a different ownership.
 */
#include <stdint.h>
#include <stdlib.h>

#include <86box/device.h>
#include <86box/chipset.h>
#include <86box/plat_unused.h>

#include "headland_pcs386sx.h"

typedef struct headland_ht101sx_t {
    headland_pcs386sx_t *board;
} headland_ht101sx_t;

static void *
headland_ht101sx_init(UNUSED(const device_t *info))
{
    headland_pcs386sx_t *board = (headland_pcs386sx_t *) device_get_common_priv();
    headland_ht101sx_t  *dev;

    if (board == NULL)
        return NULL;

    dev = (headland_ht101sx_t *) calloc(1, sizeof(headland_ht101sx_t));
    if (dev == NULL)
        return NULL;

    dev->board      = board;
    board->ht101sx  = dev;
    headland_pcs386sx_retain(board);

    return dev;
}

static void
headland_ht101sx_close(void *priv)
{
    headland_ht101sx_t *dev = (headland_ht101sx_t *) priv;

    if ((dev != NULL) && (dev->board != NULL)) {
        dev->board->ht101sx = NULL;
        headland_pcs386sx_release(dev->board);
    }
    free(dev);
}

const device_t headland_ht101sx_device = {
    .name          = "Headland HT101SX System Controller",
    .internal_name = "headland_ht101sx",
    .flags         = 0,
    .local         = 0,
    .init          = headland_ht101sx_init,
    .close         = headland_ht101sx_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
