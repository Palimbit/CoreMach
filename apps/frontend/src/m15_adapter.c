/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "frontend_internal.h"

#include <blumach/systems/olivetti_m15.h>

#include <stdlib.h>

typedef struct m15_frontend_machine {
    bm_frontend_machine_t base;
    bm_m15_config_t config;
} m15_frontend_machine_t;

static const uint64_t firmware_sizes[] = { BM_M15_FIRMWARE_SIZE };
static const uint64_t floppy_sizes[] = { 737280U };

static const bm_frontend_asset_requirement_t assets[] = {
    { .role = "firmware", .label = "M15 system BIOS (local 64 KiB image)",
      .kind = BM_FRONTEND_ASSET_BLOB, .required = 1,
      .accepted_sizes = firmware_sizes, .accepted_size_count = 1U },
    { .role = "floppy-0", .label = "Drive A 720 KiB floppy image",
      .kind = BM_FRONTEND_ASSET_READ_ONLY_MEDIA,
      .accepted_sizes = floppy_sizes, .accepted_size_count = 1U,
      .block_size = 512U, .replaceable = 1,
      .storage_kind = BM_STORAGE_DEVICE_FLOPPY, .storage_unit = 0U }
};

static void
destroy_machine(bm_frontend_machine_t *base)
{
    free(base);
}

static bm_status_t
open_machine(const bm_frontend_asset_binding_t *bindings, size_t binding_count,
             const bm_frontend_persistent_state_binding_t *state_bindings,
             size_t state_binding_count,
             bm_frontend_machine_t **out_machine)
{
    const bm_frontend_asset_binding_t *firmware = bm_frontend_binding_find(
        bindings, binding_count, "firmware");
    const bm_frontend_asset_binding_t *floppy = bm_frontend_binding_find(
        bindings, binding_count, "floppy-0");
    m15_frontend_machine_t *machine;
    size_t index;

    (void) state_bindings;
    if ((state_binding_count != 0U) || (firmware == NULL) ||
        (firmware->kind != BM_FRONTEND_ASSET_BLOB) ||
        (firmware->value.blob.data == NULL) ||
        (firmware->value.blob.size != BM_M15_FIRMWARE_SIZE))
        return BM_STATUS_INVALID_ARGUMENT;
    if ((floppy != NULL) &&
        ((floppy->kind != BM_FRONTEND_ASSET_READ_ONLY_MEDIA) ||
         (floppy->value.media.read == NULL) ||
         (floppy->value.media.write != NULL) ||
         !floppy->value.media.read_only ||
         (floppy->value.media.block_size != 512U) ||
         (floppy->value.media.block_count != 1440U)))
        return BM_STATUS_INVALID_ARGUMENT;

    machine = calloc(1U, sizeof(*machine));
    if (machine == NULL)
        return BM_STATUS_OUT_OF_MEMORY;
    machine->base.destroy = destroy_machine;
    machine->config.firmware = firmware->value.blob;
    machine->config.ram_kib = 512U;
    machine->config.startup_display_switches = 0x20U;
    for (index = 0U; index < 2U; ++index) {
        machine->config.floppy[index] = (bm_floppy_drive_config_t) {
            .installed = 1, .geometry = { 80U, 2U, 9U, 512U }
        };
    }
    if (floppy != NULL) {
        machine->base.diagnostics.read_only_media_bytes = 737280U;
        machine->config.floppy[0].media_present = 1;
        machine->config.floppy[0].write_protected = 1;
        machine->config.floppy[0].media = floppy->value.media;
    }
    machine->base.configuration = bm_m15_machine_config(&machine->config);
    *out_machine = &machine->base;
    return BM_STATUS_OK;
}

const bm_frontend_adapter_t bm_frontend_m15_adapter = {
    bm_m15_machine_definition,
    assets,
    sizeof(assets) / sizeof(assets[0]),
    NULL,
    0U,
    open_machine
};
