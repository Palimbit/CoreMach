/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "frontend_internal.h"

#include <blumach/systems/olivetti_pcs86.h>

#include <stdlib.h>
#include <string.h>

typedef struct pcs86_frontend_machine {
    bm_frontend_machine_t base;
    bm_pcs86_config_t pcs86;
} pcs86_frontend_machine_t;

static const uint64_t firmware_sizes[] = { BM_PCS86_FIRMWARE_HALF_SIZE };
static const uint64_t floppy_sizes[] = { 737280U, 1474560U };

static const bm_frontend_asset_requirement_t assets[] = {
    { "firmware-even", "Even firmware EPROM", BM_FRONTEND_ASSET_BLOB, 1,
      firmware_sizes, sizeof(firmware_sizes) / sizeof(firmware_sizes[0]), 0U },
    { "firmware-odd", "Odd firmware EPROM", BM_FRONTEND_ASSET_BLOB, 1,
      firmware_sizes, sizeof(firmware_sizes) / sizeof(firmware_sizes[0]), 0U },
    { "floppy-0", "Drive A floppy image", BM_FRONTEND_ASSET_READ_ONLY_MEDIA,
      0, floppy_sizes, sizeof(floppy_sizes) / sizeof(floppy_sizes[0]), 512U }
};

static const bm_pcs86_firmware_identity_t *
find_firmware_identity(const bm_pcs86_firmware_identity_t *identities,
                       size_t count, const char *role)
{
    size_t index;
    for (index = 0U; index < count; ++index) {
        if ((identities[index].role != NULL) &&
            (strcmp(identities[index].role, role) == 0))
            return &identities[index];
    }
    return NULL;
}

static void
capture_instruction(void *context, const bm_808x_trace_t *trace)
{
    pcs86_frontend_machine_t *machine = context;
    bm_frontend_diagnostics_t *diagnostics = &machine->base.diagnostics;
    ++diagnostics->instructions;
    diagnostics->last_cs = trace->cs;
    diagnostics->last_ip = trace->ip;
    diagnostics->last_physical_address = trace->physical_address;
    diagnostics->last_opcode = trace->opcode;
    diagnostics->last_effective_opcode = trace->effective_opcode;
    diagnostics->last_prefix_count = trace->prefix_count;
    diagnostics->has_last_instruction = 1;
}

static void
capture_io(void *context, const bm_pcs86_io_trace_t *trace)
{
    pcs86_frontend_machine_t *machine = context;
    (void) trace;
    ++machine->base.diagnostics.io_operations;
}

static void
destroy_machine(bm_frontend_machine_t *base)
{
    free(base);
}

static bm_status_t
open_machine(const bm_frontend_asset_binding_t *bindings, size_t binding_count,
             bm_frontend_machine_t **out_machine)
{
    const bm_frontend_asset_binding_t *even = bm_frontend_binding_find(
        bindings, binding_count, "firmware-even");
    const bm_frontend_asset_binding_t *odd = bm_frontend_binding_find(
        bindings, binding_count, "firmware-odd");
    const bm_frontend_asset_binding_t *floppy = bm_frontend_binding_find(
        bindings, binding_count, "floppy-0");
    const bm_pcs86_firmware_identity_t *identities;
    const bm_pcs86_firmware_identity_t *even_identity;
    const bm_pcs86_firmware_identity_t *odd_identity;
    pcs86_frontend_machine_t *machine;
    size_t identity_count = 0U;

    if ((even == NULL) || (odd == NULL) ||
        (even->kind != BM_FRONTEND_ASSET_BLOB) ||
        (odd->kind != BM_FRONTEND_ASSET_BLOB) ||
        (even->value.blob.data == NULL) ||
        (odd->value.blob.data == NULL) ||
        (even->value.blob.size != BM_PCS86_FIRMWARE_HALF_SIZE) ||
        (odd->value.blob.size != BM_PCS86_FIRMWARE_HALF_SIZE))
        return BM_STATUS_INVALID_ARGUMENT;
    if ((floppy != NULL) &&
        ((floppy->kind != BM_FRONTEND_ASSET_READ_ONLY_MEDIA) ||
         (floppy->value.media.read == NULL) ||
         (floppy->value.media.write != NULL) ||
         !floppy->value.media.read_only ||
         (floppy->value.media.block_size != 512U) ||
         ((floppy->value.media.block_count != 1440U) &&
          (floppy->value.media.block_count != 2880U))))
        return BM_STATUS_INVALID_ARGUMENT;
    machine = calloc(1U, sizeof(*machine));
    if (machine == NULL)
        return BM_STATUS_OUT_OF_MEMORY;
    machine->base.destroy = destroy_machine;
    identities = bm_pcs86_expected_firmware(&identity_count);
    even_identity = find_firmware_identity(identities, identity_count, "even");
    odd_identity = find_firmware_identity(identities, identity_count, "odd");
    if ((even_identity == NULL) || (odd_identity == NULL)) {
        destroy_machine(&machine->base);
        return BM_STATUS_INVALID_STATE;
    }
    machine->pcs86.firmware_even = (bm_blob_view_t) {
        even_identity->asset_id, even->value.blob.data, even->value.blob.size,
        even->value.blob.sha256
    };
    machine->pcs86.firmware_odd = (bm_blob_view_t) {
        odd_identity->asset_id, odd->value.blob.data, odd->value.blob.size,
        odd->value.blob.sha256
    };
    machine->pcs86.trace = capture_instruction;
    machine->pcs86.trace_context = machine;
    machine->pcs86.io_trace = capture_io;
    machine->pcs86.io_trace_context = machine;
    machine->pcs86.ems_kib = BM_PCS86_EMS_1920_KIB;
    if (floppy != NULL) {
        machine->base.diagnostics.read_only_media_bytes =
            floppy->value.media.block_count * floppy->value.media.block_size;
        machine->pcs86.floppy[0] = (bm_floppy_drive_config_t) {
            1, 1, 1,
            { 80U, 2U,
              (uint8_t) (floppy->value.media.block_count == 1440U ? 9U : 18U),
              512U },
            floppy->value.media
        };
    }
    machine->base.configuration = bm_pcs86_machine_config(&machine->pcs86);
    *out_machine = &machine->base;
    return BM_STATUS_OK;
}

const bm_frontend_adapter_t bm_frontend_pcs86_adapter = {
    bm_pcs86_machine_definition,
    assets,
    sizeof(assets) / sizeof(assets[0]),
    open_machine
};
