/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_SYSTEMS_OLIVETTI_PCS86_H
#define BLUMACH_SYSTEMS_OLIVETTI_PCS86_H

#include <blumach/components/cpu_808x.h>
#include <blumach/components/floppy_drive.h>
#include <blumach/components/xta.h>
#include <blumach/runtime/runtime.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BM_PCS86_FIRMWARE_HALF_SIZE 32768U
#define BM_PCS86_MEMORY_SIZE (640U * 1024U)
#define BM_PCS86_EMS_NONE_KIB 0U
#define BM_PCS86_EMS_384_KIB 384U
#define BM_PCS86_EMS_1920_KIB 1920U
#define BM_PCS86_ROM_BASE 0xf0000U
#define BM_PCS86_ROM_SIZE 65536U
#define BM_PCS86_CONFIG_TYPE "blumach.system.olivetti-pcs86.config"
#define BM_PCS86_CONFIG_VERSION 5U

typedef struct bm_pcs86_hard_disk_config {
    int present;
    bm_xta_geometry_t geometry;
    bm_block_media_t media;
} bm_pcs86_hard_disk_config_t;

typedef struct bm_pcs86_io_trace {
    bm_bus_operation_t operation;
    uint16_t port;
    uint8_t value;
} bm_pcs86_io_trace_t;

typedef void (*bm_pcs86_io_trace_fn)(void *context, const bm_pcs86_io_trace_t *trace);

typedef struct bm_pcs86_memory_trace {
    bm_bus_operation_t operation;
    uint32_t address;
    uint64_t value;
    uint8_t size;
} bm_pcs86_memory_trace_t;

typedef void (*bm_pcs86_memory_trace_fn)(
    void *context, const bm_pcs86_memory_trace_t *trace);

typedef void (*bm_pcs86_interrupt_trace_fn)(void *context, uint8_t vector);

typedef struct bm_pcs86_config {
    bm_blob_view_t firmware_even;
    bm_blob_view_t firmware_odd;
    bm_808x_trace_fn trace;
    void *trace_context;
    bm_pcs86_io_trace_fn io_trace;
    void *io_trace_context;
    bm_pcs86_memory_trace_fn memory_trace;
    void *memory_trace_context;
    bm_pcs86_interrupt_trace_fn interrupt_trace;
    void *interrupt_trace_context;
    bm_floppy_drive_config_t floppy[2];
    uint32_t ems_kib;
    bm_pcs86_hard_disk_config_t hard_disk;
} bm_pcs86_config_t;

typedef struct bm_pcs86_firmware_identity {
    const char *role;
    const char *asset_id;
    size_t size;
    const char *sha256;
} bm_pcs86_firmware_identity_t;

const bm_pcs86_firmware_identity_t *bm_pcs86_expected_firmware(size_t *count);
const bm_machine_definition_t *bm_pcs86_machine_definition(void);
bm_machine_config_t bm_pcs86_machine_config(const bm_pcs86_config_t *configuration);

#ifdef __cplusplus
}
#endif

#endif
