/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "frontend_internal.h"

#include <blumach/systems/olivetti_pcs86.h>

#include <stdlib.h>
#include <string.h>

typedef struct pcs86_frontend_machine {
    bm_frontend_machine_t base;
    bm_pcs86_config_t pcs86;
    uint8_t rtc_state[BM_PCS86_RTC_STATE_SIZE];
} pcs86_frontend_machine_t;

static const uint64_t firmware_sizes[] = { BM_PCS86_FIRMWARE_HALF_SIZE };
static const uint64_t floppy_sizes[] = { 737280U, 1474560U };
static const uint64_t hard_disk_sizes[] = { 21411840U };

/* The portable engine has no wall-clock dependency. Frontends provide the
 * battery-backed RTC image explicitly; this deterministic fallback uses the
 * calendar fields observed during physical validation. (The MM58167 has no
 * year counter.) A future persistence layer may replace it without changing
 * the machine contract. */
static const uint8_t default_rtc_state[BM_PCS86_RTC_STATE_SIZE] = {
    0x00U, 0x00U, 0x00U, 0x00U, 0x22U, 0x05U, 0x18U, 0x09U,
    /* Alarm RAM holds the BIOS weekday/checksum encoding as well as alarms. */
    0xe0U, 0x00U, 0x00U, 0x00U, 0x00U, 0xcdU, 0xfcU, 0xceU
};

static const bm_frontend_persistent_state_requirement_t persistent_states[] = {
    { "rtc", "RTC and calendar", BM_PCS86_RTC_STATE_SIZE,
      default_rtc_state, 1 }
};

static const bm_frontend_asset_requirement_t assets[] = {
    { .role = "firmware-even", .label = "Even firmware EPROM",
      .kind = BM_FRONTEND_ASSET_BLOB, .required = 1,
      .accepted_sizes = firmware_sizes,
      .accepted_size_count = sizeof(firmware_sizes) / sizeof(firmware_sizes[0]) },
    { .role = "firmware-odd", .label = "Odd firmware EPROM",
      .kind = BM_FRONTEND_ASSET_BLOB, .required = 1,
      .accepted_sizes = firmware_sizes,
      .accepted_size_count = sizeof(firmware_sizes) / sizeof(firmware_sizes[0]) },
    { .role = "floppy-0", .label = "Drive A floppy image",
      .kind = BM_FRONTEND_ASSET_READ_ONLY_MEDIA,
      .accepted_sizes = floppy_sizes,
      .accepted_size_count = sizeof(floppy_sizes) / sizeof(floppy_sizes[0]),
      .block_size = 512U, .replaceable = 1,
      .storage_kind = BM_STORAGE_DEVICE_FLOPPY, .storage_unit = 0U },
    { .role = "floppy-1", .label = "Drive B floppy image",
      .kind = BM_FRONTEND_ASSET_READ_ONLY_MEDIA,
      .accepted_sizes = floppy_sizes,
      .accepted_size_count = sizeof(floppy_sizes) / sizeof(floppy_sizes[0]),
      .block_size = 512U, .replaceable = 1,
      .storage_kind = BM_STORAGE_DEVICE_FLOPPY, .storage_unit = 1U },
    { .role = "hard-disk-0", .label = "Conner CP3026 XTA disk image",
      .kind = BM_FRONTEND_ASSET_BLOCK_MEDIA,
      .accepted_sizes = hard_disk_sizes,
      .accepted_size_count = sizeof(hard_disk_sizes) / sizeof(hard_disk_sizes[0]),
      .block_size = 512U,
      .storage_kind = BM_STORAGE_DEVICE_HARD_DISK, .storage_unit = 0U }
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
    if (machine->base.debug_observer != NULL) {
        const bm_frontend_debug_event_t event = {
            .kind = BM_FRONTEND_DEBUG_INSTRUCTION,
            .sequence = machine->base.debug_sequence++,
            .value.instruction = {
                .cs = trace->cs,
                .ip = trace->ip,
                .ds = trace->ds,
                .es = trace->es,
                .ss = trace->ss,
                .sp = trace->sp,
                .ax = trace->ax,
                .bx = trace->bx,
                .cx = trace->cx,
                .dx = trace->dx,
                .bp = trace->bp,
                .si = trace->si,
                .di = trace->di,
                .flags = trace->flags,
                .physical_address = trace->physical_address,
                .opcode = trace->opcode,
                .effective_opcode = trace->effective_opcode,
                .prefix_count = trace->prefix_count
            }
        };
        machine->base.debug_observer(machine->base.debug_context, &event);
    }
}

static void
capture_io(void *context, const bm_pcs86_io_trace_t *trace)
{
    pcs86_frontend_machine_t *machine = context;
    ++machine->base.diagnostics.io_operations;
    if (machine->base.debug_observer != NULL) {
        const bm_frontend_debug_event_t event = {
            .kind = BM_FRONTEND_DEBUG_IO,
            .sequence = machine->base.debug_sequence++,
            .value.io = {
                trace->port, trace->value, 1U,
                (uint8_t) (trace->operation == BM_BUS_WRITE)
            }
        };
        machine->base.debug_observer(machine->base.debug_context, &event);
    }
}

static void
capture_memory(void *context, const bm_pcs86_memory_trace_t *trace)
{
    pcs86_frontend_machine_t *machine = context;
    if (machine->base.debug_observer != NULL) {
        const bm_frontend_debug_event_t event = {
            .kind = BM_FRONTEND_DEBUG_MEMORY,
            .sequence = machine->base.debug_sequence++,
            .value.memory = {
                trace->address, trace->value, trace->size,
                (uint8_t) (trace->operation == BM_BUS_WRITE)
            }
        };
        machine->base.debug_observer(machine->base.debug_context, &event);
    }
}

static void
capture_interrupt(void *context, uint8_t vector)
{
    pcs86_frontend_machine_t *machine = context;
    if (machine->base.debug_observer != NULL) {
        const bm_frontend_debug_event_t event = {
            .kind = BM_FRONTEND_DEBUG_INTERRUPT,
            .sequence = machine->base.debug_sequence++,
            .value.interrupt = { vector }
        };
        machine->base.debug_observer(machine->base.debug_context, &event);
    }
}

static void
destroy_machine(bm_frontend_machine_t *base)
{
    free(base);
}

static bm_status_t
open_machine(const bm_frontend_asset_binding_t *bindings, size_t binding_count,
             const bm_frontend_persistent_state_binding_t *state_bindings,
             size_t state_binding_count,
             const bm_frontend_machine_option_t *options, size_t option_count,
             bm_frontend_machine_t **out_machine)
{
    const bm_frontend_asset_binding_t *even = bm_frontend_binding_find(
        bindings, binding_count, "firmware-even");
    const bm_frontend_asset_binding_t *odd = bm_frontend_binding_find(
        bindings, binding_count, "firmware-odd");
    const bm_frontend_asset_binding_t *floppy[2] = {
        bm_frontend_binding_find(bindings, binding_count, "floppy-0"),
        bm_frontend_binding_find(bindings, binding_count, "floppy-1")
    };
    const bm_frontend_asset_binding_t *hard_disk = bm_frontend_binding_find(
        bindings, binding_count, "hard-disk-0");
    const bm_frontend_persistent_state_binding_t *rtc_state =
        bm_frontend_persistent_state_binding_find(
            state_bindings, state_binding_count, "rtc");
    const bm_pcs86_firmware_identity_t *identities;
    const bm_pcs86_firmware_identity_t *even_identity;
    const bm_pcs86_firmware_identity_t *odd_identity;
    pcs86_frontend_machine_t *machine;
    size_t identity_count = 0U;
    size_t option_index;
    uint32_t ems_kib = BM_PCS86_EMS_1920_KIB;
    uint32_t drive_type[2] = { 0U, 0U };
    int drive_type_given[2] = { 0, 0 };
    uint32_t jumper_bank = 0U;
    uint32_t commercial_profile = 0U;

    if ((even == NULL) || (odd == NULL) ||
        (even->kind != BM_FRONTEND_ASSET_BLOB) ||
        (odd->kind != BM_FRONTEND_ASSET_BLOB) ||
        (even->value.blob.data == NULL) ||
        (odd->value.blob.data == NULL) ||
        (even->value.blob.size != BM_PCS86_FIRMWARE_HALF_SIZE) ||
        (odd->value.blob.size != BM_PCS86_FIRMWARE_HALF_SIZE))
        return BM_STATUS_INVALID_ARGUMENT;
    for (size_t index = 0U; index < 2U; ++index) {
        if ((floppy[index] != NULL) &&
            ((floppy[index]->kind != BM_FRONTEND_ASSET_READ_ONLY_MEDIA) ||
             (floppy[index]->value.media.read == NULL) ||
             (floppy[index]->value.media.write != NULL) ||
             !floppy[index]->value.media.read_only ||
             (floppy[index]->value.media.block_size != 512U) ||
             ((floppy[index]->value.media.block_count != 1440U) &&
              (floppy[index]->value.media.block_count != 2880U))))
            return BM_STATUS_INVALID_ARGUMENT;
    }
    if ((hard_disk != NULL) &&
        (((hard_disk->kind != BM_FRONTEND_ASSET_READ_ONLY_MEDIA) &&
          (hard_disk->kind != BM_FRONTEND_ASSET_BLOCK_MEDIA)) ||
         (hard_disk->value.media.read == NULL) ||
         (hard_disk->value.media.read_only ?
             (hard_disk->value.media.write != NULL) :
             ((hard_disk->kind != BM_FRONTEND_ASSET_BLOCK_MEDIA) ||
              (hard_disk->value.media.write == NULL))) ||
         (hard_disk->value.media.block_size != 512U) ||
         (hard_disk->value.media.block_count != 41820U)))
        return BM_STATUS_INVALID_ARGUMENT;
    for (option_index = 0U; option_index < option_count; ++option_index) {
        const char *name = options[option_index].name;
        const uint32_t value = options[option_index].value;
        if (strcmp(name, "ems_kib") == 0) {
            if ((value != BM_PCS86_EMS_NONE_KIB) &&
                (value != BM_PCS86_EMS_384_KIB) &&
                (value != BM_PCS86_EMS_1920_KIB))
                return BM_STATUS_INVALID_ARGUMENT;
            ems_kib = value;
        } else if ((strcmp(name, "floppy_a_type") == 0) ||
                   (strcmp(name, "floppy_b_type") == 0)) {
            const size_t index = name[7] == 'a' ? 0U : 1U;
            if (value > 2U)
                return BM_STATUS_INVALID_ARGUMENT;
            drive_type[index] = value;
            drive_type_given[index] = 1;
        } else if (strcmp(name, "jumper_bank") == 0) {
            if (value > 256U)
                return BM_STATUS_INVALID_ARGUMENT;
            jumper_bank = value;
        } else if (strcmp(name, "commercial_profile") == 0) {
            if (value > 3U)
                return BM_STATUS_INVALID_ARGUMENT;
            commercial_profile = value;
        } else {
            return BM_STATUS_INVALID_ARGUMENT;
        }
    }
    for (size_t index = 0U; index < 2U; ++index) {
        if (!drive_type_given[index])
            drive_type[index] = floppy[index] == NULL ? 0U :
                (floppy[index]->value.media.block_count == 1440U ? 1U : 2U);
        if (((drive_type[index] == 0U) && (floppy[index] != NULL)) ||
            ((drive_type[index] == 1U) && (floppy[index] != NULL) &&
             (floppy[index]->value.media.block_count == 2880U)))
            return BM_STATUS_INVALID_ARGUMENT;
    }
    if (((commercial_profile != 0U) && (jumper_bank != 0U)) ||
        ((commercial_profile == 1U) &&
         ((drive_type[0] != 1U) || (drive_type[1] != 0U) ||
          (hard_disk != NULL))) ||
        ((commercial_profile == 2U) &&
         ((drive_type[0] != 1U) || (drive_type[1] != 1U) ||
          (hard_disk != NULL))) ||
        ((commercial_profile == 3U) &&
         ((drive_type[0] != 1U) || (drive_type[1] != 0U) ||
          (hard_disk == NULL))))
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
    machine->pcs86.memory_trace = capture_memory;
    machine->pcs86.memory_trace_context = machine;
    machine->pcs86.interrupt_trace = capture_interrupt;
    machine->pcs86.interrupt_trace_context = machine;
    machine->pcs86.ems_kib = ems_kib;
    memcpy(machine->rtc_state,
           rtc_state != NULL ? rtc_state->data : default_rtc_state,
           sizeof(machine->rtc_state));
    machine->pcs86.rtc_initial_state = machine->rtc_state;
    machine->pcs86.rtc_initial_state_size = sizeof(machine->rtc_state);
    for (size_t index = 0U; index < 2U; ++index) {
        if (drive_type[index] == 0U) {
            machine->pcs86.floppy_drive_type[index] = 3U;
            continue;
        }
        machine->pcs86.floppy_drive_type[index] = (uint8_t) drive_type[index];
        machine->pcs86.floppy[index] = (bm_floppy_drive_config_t) {
            .installed = 1,
            .media_present = floppy[index] != NULL,
            .write_protected = 1,
            .geometry = { 80U, 2U,
                (uint8_t) (floppy[index] != NULL ?
                    (floppy[index]->value.media.block_count == 1440U ? 9U : 18U) :
                    (drive_type[index] == 1U ? 9U : 18U)),
                512U }
        };
        if (floppy[index] != NULL) {
            machine->pcs86.floppy[index].media = floppy[index]->value.media;
            machine->base.diagnostics.read_only_media_bytes +=
                floppy[index]->value.media.block_count *
                floppy[index]->value.media.block_size;
        }
    }
    if (jumper_bank != 0U) {
        machine->pcs86.jumpers_manual = 1;
        machine->pcs86.jumpers_value = (uint8_t) (jumper_bank - 1U);
    }
    if (hard_disk != NULL) {
        if (hard_disk->value.media.read_only)
            machine->base.diagnostics.read_only_media_bytes +=
                hard_disk->value.media.block_count * hard_disk->value.media.block_size;
        machine->pcs86.hard_disk = (bm_pcs86_hard_disk_config_t) {
            1, { 615U, 4U, 17U }, hard_disk->value.media
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
    persistent_states,
    sizeof(persistent_states) / sizeof(persistent_states[0]),
    open_machine
};
