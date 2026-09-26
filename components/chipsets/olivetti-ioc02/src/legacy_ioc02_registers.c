/* SPDX-License-Identifier: GPL-2.0-or-later
 * Selective port of src/chipset/olivetti_ioc02.c at
 * 4769e40524bc194747b142f3e7ec908ae4df0897 (BluMach / 86Box).
 * Authors: EngiNerd <webmaster.crrc@yahoo.it>
 *          rtzor, BluMach PCS/Dario extensions.
 * Copyright 2020-2023 EngiNerd.
 * Copyright 2026 rtzor, BluMach contributors.
 *
 * Retains only latches, selector write gating and steady-state readback.
 * The classic first-read POST override and all machine/global dependencies
 * are deliberately excluded. See doc/architecture/pcs286-headland.md.
 */
#include "legacy_ioc02_registers.h"

bm_status_t
bm_ioc02_legacy_initialize(bm_ioc02_legacy_registers_t *registers)
{
    const bm_ioc02_legacy_registers_t initial = {0x04U, 0x04U, 0xffU};
    if (registers == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    *registers = initial;
    return BM_STATUS_OK;
}

bm_status_t
bm_ioc02_legacy_access(bm_ioc02_legacy_registers_t *registers,
                       uint16_t port, uint32_t width,
                       bm_bus_operation_t operation, int debug,
                       uint16_t *value, bm_ioc02_legacy_effect_t *effect)
{
    bm_ioc02_legacy_effect_t next = {0U, 0U};
    uint8_t *latch;
    uint8_t mask;
    if (registers == NULL || value == NULL || effect == NULL || width != 1U ||
        (operation != BM_BUS_READ && operation != BM_BUS_WRITE) ||
        (debug != 0 && debug != 1) ||
        (operation == BM_BUS_WRITE && *value > 0xffU))
        return BM_STATUS_INVALID_ARGUMENT;
    switch (port) {
        case 0x68U: latch = &registers->select; mask = BM_IOC02_LEGACY_SELECT; break;
        case 0x6aU: latch = &registers->data; mask = BM_IOC02_LEGACY_DATA; break;
        case 0x6cU: latch = &registers->control; mask = BM_IOC02_LEGACY_CONTROL; break;
        default: return BM_STATUS_UNMAPPED;
    }
    if (debug && operation == BM_BUS_WRITE)
        return BM_STATUS_READ_ONLY;
    if (operation == BM_BUS_READ) {
        *value = port == 0x6aU ? (*latch ^ 0x20U) : *latch;
    } else if (port != 0x6aU || (registers->select & 0x1fU) != 0U) {
        next.written = mask;
        if (*latch != (uint8_t)*value)
            next.changed = mask;
        *latch = (uint8_t)*value;
    }
    *effect = next;
    return BM_STATUS_OK;
}
