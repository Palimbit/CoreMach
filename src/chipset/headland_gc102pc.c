/*
 * Headland GC102-PC data-buffer package used by the PCS/Dario 386SX.
 *
 * The exact PC-suffix data sheet and the parity/NMI wiring have not been
 * preserved.  v0.1 therefore models a transparent 16-bit path and retains
 * explicit parity state without asserting an undocumented NMI.
 */
#include <stdint.h>
#include <stdlib.h>

#include <86box/device.h>
#include <86box/chipset.h>
#include <86box/plat_unused.h>

#include "headland_pcs386sx.h"

typedef struct headland_gc102pc_t {
    headland_pcs386sx_t *board;
    uint8_t              parity_error;
} headland_gc102pc_t;

static void *
headland_gc102pc_init(UNUSED(const device_t *info))
{
    headland_pcs386sx_t *board = (headland_pcs386sx_t *) device_get_common_priv();
    headland_gc102pc_t  *dev;

    if (board == NULL)
        return NULL;

    dev = (headland_gc102pc_t *) calloc(1, sizeof(headland_gc102pc_t));
    if (dev == NULL)
        return NULL;

    dev->board                 = board;
    dev->parity_error          = 0;
    board->gc102pc             = dev;
    board->parity_error        = 0;
    board->parity_nmi_connected = 0;
    headland_pcs386sx_retain(board);

    return dev;
}

static void
headland_gc102pc_close(void *priv)
{
    headland_gc102pc_t *dev = (headland_gc102pc_t *) priv;

    if ((dev != NULL) && (dev->board != NULL)) {
        dev->board->gc102pc = NULL;
        headland_pcs386sx_release(dev->board);
    }
    free(dev);
}

const device_t headland_gc102pc_device = {
    .name          = "Headland GC102-PC Data Buffer",
    .internal_name = "headland_gc102pc",
    .flags         = 0,
    .local         = 0,
    .init          = headland_gc102pc_init,
    .close         = headland_gc102pc_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
