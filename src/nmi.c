/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <86box/io.h>
#include <86box/nmi.h>
#include <86box/plat_unused.h>

int nmi_mask;

static nmi_mask_callback_t nmi_mask_callback;
static void               *nmi_mask_callback_priv;

void
nmi_set_mask_callback(nmi_mask_callback_t callback, void *priv)
{
    nmi_mask_callback      = callback;
    nmi_mask_callback_priv = priv;
}

void
nmi_write(UNUSED(uint16_t port), uint8_t val, UNUSED(void *priv))
{
    const int old_mask = nmi_mask;

    nmi_mask = val & 0x80;
    if ((nmi_mask != old_mask) && nmi_mask_callback)
        nmi_mask_callback(!!nmi_mask, nmi_mask_callback_priv);
}

void
nmi_init(void)
{
    nmi_set_mask_callback(NULL, NULL);
    io_sethandler(0x00a0, 0x000f, NULL, NULL, NULL, nmi_write, NULL, NULL, NULL);
    nmi_mask = 0;
}
