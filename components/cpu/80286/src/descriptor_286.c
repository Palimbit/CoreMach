/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 BluMach contributors
 * Intel 80286/80287 Programmer's Reference Manual (1987), chapters 6, 7,
 * 11 and appendix B. See doc/architecture/pcs286-protected-mode-plan.md.
 */
#include "descriptor_286.h"

bm_286_pm_selector_t bm_286_pm_selector_decode(uint16_t raw)
{
    bm_286_pm_selector_t result;
    result.index = (uint16_t)(raw >> 3);
    result.table_offset = (uint16_t)(raw & 0xfff8u);
    result.rpl = (uint8_t)(raw & 3u);
    result.local = (raw & 4u) != 0;
    result.null_selector = (raw & 0xfffcu) == 0;
    return result;
}

bm_286_pm_descriptor_t bm_286_pm_descriptor_decode(const uint8_t bytes[8])
{
    bm_286_pm_descriptor_t result = {0};
    const uint8_t type = bytes[5] & 15u;
    result.access = bytes[5];
    result.dpl = (uint8_t)((bytes[5] >> 5) & 3u);
    result.present = (bytes[5] & 0x80u) != 0;
    result.reserved = (uint16_t)((uint16_t)bytes[6] | ((uint16_t)bytes[7] << 8));
    if ((bytes[5] & 0x10u) != 0) {
        result.kind = (type & 8u) ? BM_286_PM_CODE : BM_286_PM_DATA;
        result.accessed = (type & 1u) != 0;
        result.readable = result.kind == BM_286_PM_DATA || (type & 2u) != 0;
        result.writable = result.kind == BM_286_PM_DATA && (type & 2u) != 0;
        result.expand_down = result.kind == BM_286_PM_DATA && (type & 4u) != 0;
        result.conforming = result.kind == BM_286_PM_CODE && (type & 4u) != 0;
    } else {
        switch (type) {
        case 1: result.kind = BM_286_PM_TSS_AVAILABLE; break;
        case 2: result.kind = BM_286_PM_LDT; break;
        case 3: result.kind = BM_286_PM_TSS_BUSY; break;
        case 4: result.kind = BM_286_PM_CALL_GATE; break;
        case 5: result.kind = BM_286_PM_TASK_GATE; break;
        case 6: result.kind = BM_286_PM_INTERRUPT_GATE; break;
        case 7: result.kind = BM_286_PM_TRAP_GATE; break;
        default: result.kind = BM_286_PM_INVALID; break;
        }
    }
    if (result.kind == BM_286_PM_DATA || result.kind == BM_286_PM_CODE ||
        result.kind == BM_286_PM_LDT || result.kind == BM_286_PM_TSS_AVAILABLE ||
        result.kind == BM_286_PM_TSS_BUSY) {
        result.limit = (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
        result.base = (uint32_t)bytes[2] | ((uint32_t)bytes[3] << 8) |
                      ((uint32_t)bytes[4] << 16);
    }
    return result;
}

bool bm_286_pm_segment_contains(const bm_286_pm_descriptor_t *descriptor,
                                uint32_t offset, uint32_t length)
{
    if ((descriptor->kind != BM_286_PM_DATA && descriptor->kind != BM_286_PM_CODE) ||
        length == 0 || offset > 0xffffu || length - 1u > 0xffffu - offset)
        return false;
    if (descriptor->expand_down)
        return offset > descriptor->limit;
    return offset + length - 1u <= descriptor->limit;
}
