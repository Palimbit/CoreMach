/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_FRONTEND_FRONTEND_H
#define BLUMACH_FRONTEND_FRONTEND_H

#include <blumach/runtime/runtime.h>
#include <blumach/engine/storage.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum bm_frontend_asset_kind {
    BM_FRONTEND_ASSET_BLOB = 0,
    BM_FRONTEND_ASSET_READ_ONLY_MEDIA = 1,
    /* Accepts either explicitly writable media or a protected binding. */
    BM_FRONTEND_ASSET_BLOCK_MEDIA = 2
} bm_frontend_asset_kind_t;

typedef struct bm_frontend_asset_requirement {
    const char *role;
    const char *label;
    bm_frontend_asset_kind_t kind;
    int required;
    const uint64_t *accepted_sizes;
    size_t accepted_size_count;
    uint32_t block_size;
    int replaceable;
    bm_storage_device_kind_t storage_kind;
    uint32_t storage_unit;
} bm_frontend_asset_requirement_t;

typedef struct bm_frontend_asset_binding {
    const char *role;
    bm_frontend_asset_kind_t kind;
    union {
        bm_blob_view_t blob;
        bm_block_media_t media;
    } value;
} bm_frontend_asset_binding_t;

/* Bindings and their backing bytes/callback contexts remain caller-owned and
 * must outlive both the prepared machine and every session configured from it.
 * The adapter contract has no dependency on paths, file APIs, Qt or a host UI. */

typedef struct bm_frontend_diagnostics {
    uint64_t instructions;
    uint64_t io_operations;
    uint64_t read_only_media_bytes;
    uint16_t last_cs;
    uint16_t last_ip;
    uint32_t last_physical_address;
    uint8_t last_opcode;
    uint8_t last_effective_opcode;
    uint8_t last_prefix_count;
    int has_last_instruction;
} bm_frontend_diagnostics_t;

typedef struct bm_frontend_adapter bm_frontend_adapter_t;
typedef struct bm_frontend_machine bm_frontend_machine_t;

size_t bm_frontend_adapter_count(void);
const bm_frontend_adapter_t *bm_frontend_adapter_at(size_t index);
const bm_frontend_adapter_t *bm_frontend_adapter_find(const char *machine_id);
const bm_machine_definition_t *bm_frontend_adapter_definition(
    const bm_frontend_adapter_t *adapter);
const bm_frontend_asset_requirement_t *bm_frontend_adapter_assets(
    const bm_frontend_adapter_t *adapter, size_t *count);
bm_status_t bm_frontend_register_machines(bm_machine_registry_t *registry);

bm_status_t bm_frontend_machine_open(
    const bm_frontend_adapter_t *adapter,
    const bm_frontend_asset_binding_t *bindings, size_t binding_count,
    bm_frontend_machine_t **out_machine);
const bm_machine_config_t *bm_frontend_machine_config(
    const bm_frontend_machine_t *machine);
bm_status_t bm_frontend_machine_diagnostics(
    const bm_frontend_machine_t *machine,
    bm_frontend_diagnostics_t *out_diagnostics);
/* Close only after destroying every session configured from this machine. */
void bm_frontend_machine_close(bm_frontend_machine_t *machine);

#ifdef __cplusplus
}
#endif

#endif
