/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * PCS 286 contracts only: NOT a runnable machine or the separate PCS 286S.
 */
#ifndef BLUMACH_SYSTEMS_OLIVETTI_PCS286_H
#define BLUMACH_SYSTEMS_OLIVETTI_PCS286_H
#include <blumach/runtime/runtime.h>
#include <blumach/components/cpu_80286.h>
#include <blumach/components/ata_pio.h>
#include <blumach/components/floppy_drive.h>
#ifdef __cplusplus
extern "C" {
#endif

#define BM_PCS286_CONFIG_VERSION 1U
#define BM_PCS286_CONFIG_TYPE "blumach.olivetti-pcs286.v1"
#define BM_PCS286_FIRMWARE_BYTES (128U * 1024U)
#define BM_PCS286_ONBOARD_RAM_MAX_KIB 4096U
#define BM_PCS286_CPU_HZ UINT64_C(12000000)

typedef enum bm_pcs286_firmware_layout {
    BM_PCS286_FIRMWARE_COMBINED = 0,
    BM_PCS286_FIRMWARE_LOW_HIGH
} bm_pcs286_firmware_layout_t;
typedef struct bm_pcs286_firmware {
    bm_pcs286_firmware_layout_t layout;
    /* combined: image[0] 128 KiB, image[1] empty;
     * low/high: image[0] even and image[1] odd, each 64 KiB. */
    bm_blob_view_t image[2];
} bm_pcs286_firmware_t;

typedef struct bm_pcs286_config {
    uint32_t size;
    uint32_t version;
    bm_pcs286_firmware_t firmware;
    uint32_t ram_kib;          /* candidate onboard matrix: 1024/2048/3072/4096 */
    bm_floppy_drive_config_t floppy[2];
    bm_ata_drive_config_t hard_disk[2];
    const uint8_t *initial_cmos;
    size_t initial_cmos_size;  /* 128 or zero; no inherited 256-byte NVR */
    int battery_valid;
    bm_286_trace_fn cpu_trace;
    void *cpu_trace_context;
} bm_pcs286_config_t;

/* Planned factory signatures only. This branch provides no implementation,
 * does not register the machine and cannot enable a Create/Start action.
 * Future definition MUST select BM_MACHINE_ENGINE_CLOCKED with
 * BM_MACHINE_CLOCKED_TICKS_PER_SECOND; CPU rate is independently 12 MHz.
 * Firmware ownership follows bm_blob_view_t; file paths stay in the frontend.
 * No implicit 6 MHz mode, >4 MiB expansion, ISA card or 80287 is advertised.
 * The documented possibilities remain explicit follow-up tasks. */
const bm_machine_definition_t *bm_pcs286_machine_definition(void);
bm_machine_config_t bm_pcs286_machine_config(const bm_pcs286_config_t *configuration);

#ifdef __cplusplus
}
#endif
#endif
