/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Draft RAMDAC contract; separate palette logic from PVGA1A scanout.
 */
#ifndef BLUMACH_COMPONENTS_IMS_G171_H
#define BLUMACH_COMPONENTS_IMS_G171_H
#include <blumach/components/bus.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct bm_ims_g171 bm_ims_g171_t;
bm_status_t bm_ims_g171_create(const bm_host_services_t *host,
                               bm_ims_g171_t **out_dac);
void bm_ims_g171_destroy(bm_ims_g171_t *dac);
void bm_ims_g171_reset(bm_ims_g171_t *dac);
/* Standard palette access is expected at 3C6h-3C9h; precise mask/index,
 * component-width and read/write auto-increment behaviour needs evidence.
 * Integration must give these ports to ONE owner: an external DAC or the
 * existing PVGA1A internal palette. Never map both or duplicate palette state. */
bm_status_t bm_ims_g171_io(void *context, bm_bus_transaction_t *transaction);
/* Pure output query, returns 00RRGGBB, independent of host endianness/window. */
bm_status_t bm_ims_g171_colour(const bm_ims_g171_t *dac, uint8_t pixel,
                               uint32_t *xrgb8888);
#ifdef __cplusplus
}
#endif
#endif
