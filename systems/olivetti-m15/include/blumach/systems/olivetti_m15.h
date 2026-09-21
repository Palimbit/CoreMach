/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_SYSTEMS_OLIVETTI_M15_H
#define BLUMACH_SYSTEMS_OLIVETTI_M15_H

#include <blumach/components/cpu_808x.h>
#include <blumach/components/floppy_drive.h>
#include <blumach/runtime/runtime.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BM_M15_FIRMWARE_SIZE 65536U
#define BM_M15_CONFIG_TYPE "blumach.system.olivetti-m15.config"
#define BM_M15_CONFIG_VERSION 2U

/* This is an internal, non-catalogued platform bring-up contract. It does not
 * yet provide the V6355D display or live keyboard input. */
typedef struct bm_m15_config {
    bm_blob_view_t firmware;
    uint32_t ram_kib;
    uint8_t startup_display_switches; /* 10h: 40 columns; 20h: 80 columns. */
    /* Optional caller-owned 16-nibble battery-backed RTC state. */
    const uint8_t *rtc_initial_state;
    size_t rtc_initial_state_size;
    bm_floppy_drive_config_t floppy[2];
    bm_808x_trace_fn trace;
    void *trace_context;
    bm_808x_timing_fn timing;
    void *timing_context;
} bm_m15_config_t;

const bm_machine_definition_t *bm_m15_machine_definition(void);
bm_machine_config_t bm_m15_machine_config(const bm_m15_config_t *configuration);

#ifdef __cplusplus
}
#endif

#endif
