/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 BluMach contributors
 */
#ifndef BM_DESCRIPTOR_286_H
#define BM_DESCRIPTOR_286_H

#include <stdbool.h>
#include <stdint.h>

/* Private 80286 interpretation helpers, not an engine or runtime ABI. */
typedef enum bm_286_pm_kind {
    BM_286_PM_INVALID, BM_286_PM_DATA, BM_286_PM_CODE,
    BM_286_PM_TSS_AVAILABLE, BM_286_PM_LDT, BM_286_PM_TSS_BUSY,
    BM_286_PM_CALL_GATE, BM_286_PM_TASK_GATE,
    BM_286_PM_INTERRUPT_GATE, BM_286_PM_TRAP_GATE
} bm_286_pm_kind_t;

typedef struct bm_286_pm_selector {
    uint16_t index, table_offset;
    uint8_t rpl;
    bool local, null_selector;
} bm_286_pm_selector_t;

typedef struct bm_286_pm_descriptor {
    bm_286_pm_kind_t kind;
    uint32_t base;
    uint16_t limit, reserved;
    uint8_t access, dpl;
    bool present, readable, writable, expand_down, conforming, accessed;
} bm_286_pm_descriptor_t;

bm_286_pm_selector_t bm_286_pm_selector_decode(uint16_t raw);
/* bytes points to eight readable bytes, possibly unaligned. Gate payloads
 * are deliberately not decoded yet. Reserved bytes are diagnostic only. */
bm_286_pm_descriptor_t bm_286_pm_descriptor_decode(const uint8_t bytes[8]);
/* Bounds only: does NOT validate presence, privilege or access permissions.
 * descriptor must be non-NULL. Empty ranges are rejected. */
bool bm_286_pm_segment_contains(const bm_286_pm_descriptor_t *descriptor,
                                uint32_t offset, uint32_t length);

#endif
