/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors */
#include "frontend_internal.h"
#include <blumach/systems/olivetti_pcs286.h>
#include <stdlib.h>
#include <string.h>

typedef struct prepared { bm_frontend_machine_t base; bm_pcs286_config_t config; } prepared_t;
static const uint64_t bios_sizes[]={131072}, floppy_sizes[]={1474560};
static const bm_frontend_asset_requirement_t assets[]={
 {.role="firmware",.label="PCS286 experimental BIOS (128 KiB combined)",.kind=BM_FRONTEND_ASSET_BLOB,.required=1,.accepted_sizes=bios_sizes,.accepted_size_count=1},
 {.role="floppy-0",.label="Drive A 1.44 MiB (read-only; boot not validated)",.kind=BM_FRONTEND_ASSET_READ_ONLY_MEDIA,.accepted_sizes=floppy_sizes,.accepted_size_count=1,.block_size=512,.storage_kind=BM_STORAGE_DEVICE_FLOPPY}
};
/* Calendar only: not a manufacturer default. Firmware must run Setup. */
static const uint8_t calendar[128]={[4]=1,[6]=3,[7]=1,[8]=1,[9]=0x80,[10]=0x60,[11]=0x82};
static const bm_frontend_persistent_state_requirement_t states[]={
 {"rtc","Experimental PCS286 CMOS",128,calendar,1}
};
static void close_machine(bm_frontend_machine_t *m) { free(m); }
static bm_status_t open_machine(const bm_frontend_asset_binding_t *bindings,size_t count,
 const bm_frontend_persistent_state_binding_t *state_bindings,size_t state_count,
 const bm_frontend_machine_option_t *options,size_t option_count,bm_frontend_machine_t **out)
{
 const bm_frontend_asset_binding_t *fw=bm_frontend_binding_find(bindings,count,"firmware");
 const bm_frontend_asset_binding_t *fd=bm_frontend_binding_find(bindings,count,"floppy-0");
 const bm_frontend_persistent_state_binding_t *rtc=bm_frontend_persistent_state_binding_find(state_bindings,state_count,"rtc");
 prepared_t *m;
 if (!fw || fw->kind!=BM_FRONTEND_ASSET_BLOB || !fw->value.blob.data || fw->value.blob.size!=131072)
     return BM_STATUS_INVALID_ARGUMENT;
 if (state_count>1 || (state_count && !rtc) || (rtc && (!rtc->data || rtc->size!=128))) return BM_STATUS_INVALID_ARGUMENT;
 for (size_t i=0;i<option_count;++i)
     if (strcmp(options[i].name,"ram_kib") || options[i].value!=1024) return BM_STATUS_UNSUPPORTED;
 if (fd && (fd->kind!=BM_FRONTEND_ASSET_READ_ONLY_MEDIA || !fd->value.media.read ||
     fd->value.media.write || !fd->value.media.read_only || fd->value.media.block_size!=512 ||
     fd->value.media.block_count!=2880)) return BM_STATUS_INVALID_ARGUMENT;
 m=calloc(1,sizeof(*m)); if (!m) return BM_STATUS_OUT_OF_MEMORY;
 m->base.destroy=close_machine; m->config.size=sizeof(m->config); m->config.version=BM_PCS286_CONFIG_VERSION;
 m->config.ram_kib=1024; m->config.firmware.image[0]=fw->value.blob;
 m->config.initial_cmos=rtc ? rtc->data : calendar; m->config.initial_cmos_size=128; m->config.battery_valid=1;
 m->config.floppy[0].installed=1; m->config.floppy[0].geometry=(bm_floppy_geometry_t){80,2,18,512};
 if (fd) { m->config.floppy[0].media_present=1; m->config.floppy[0].write_protected=1;
     m->config.floppy[0].media=fd->value.media; m->base.diagnostics.read_only_media_bytes=1474560; }
 m->base.configuration=bm_pcs286_machine_config(&m->config); *out=&m->base; return BM_STATUS_OK;
}
const bm_frontend_adapter_t bm_frontend_pcs286_adapter={bm_pcs286_machine_definition,assets,2,states,1,open_machine};
