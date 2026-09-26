/* SPDX-License-Identifier: GPL-2.0-or-later
 * Selective port of src/chipset/headland.c at
 * 4769e40524bc194747b142f3e7ec908ae4df0897 (BluMach / 86Box / PCem).
 * Authors: Sarah Walker, https://pcem-emulator.co.uk/
 *          Fred N. van Kempen, decwiz@yahoo.com
 *          Original by GreatPsycho for PCem.
 *          Miran Grca, mgrca8@gmail.com
 * Copyright 2010-2019 Sarah Walker.
 * Copyright 2017-2019 Fred N. van Kempen, Miran Grca, GreatPsycho.
 * Copyright 2026 BluMach contributors.
 *
 * Only the GC103 variant selected by the classic PCS286 is migrated here.
 * No assertion of GC101/GC102/GC103 equivalence, ROM workaround or timing.
 */
#include "legacy_gc103_registers.h"
#include <string.h>

/* Manufacturer register definitions pp5-7 take precedence over the terse
 * pin table p12 (which duplicates a bank-count row and names MR instead of CR).
 * This resolves readback, not the decoder for physically tied inputs. */
static uint8_t strapped_readback(const bm_gc103_registers_t *r)
{
    uint8_t value = r->cr_written;
    if (r->pins.ram1m != BM_GC103_PIN_FLOATING) {
        value &= (uint8_t)~0x80U;
        if (r->pins.ram1m == BM_GC103_PIN_LOW) value |= 0x80U;
    }
    if (r->pins.ramsw1 == BM_GC103_PIN_LOW) value |= 0x20U;
    if (r->pins.ramsw2 == BM_GC103_PIN_LOW) value |= 0x40U;
    if (r->pins.splsw == BM_GC103_PIN_LOW) value |= 0x04U;
    return value;
}

static int valid_pin(bm_gc103_pin_level_t pin)
{
    return pin >= BM_GC103_PIN_FLOATING && pin <= BM_GC103_PIN_HIGH;
}

bm_status_t
bm_gc103_registers_initialize_strapped(bm_gc103_registers_t *r,
                                       const bm_gc103_straps_t *pins)
{
    bm_gc103_registers_t initial;
    if (r == NULL || pins == NULL || !valid_pin(pins->ram1m) ||
        !valid_pin(pins->ramsw1) || !valid_pin(pins->ramsw2) || !valid_pin(pins->splsw))
        return BM_STATUS_INVALID_ARGUMENT;
    memset(&initial, 0, sizeof(initial));
    initial.control_profile = BM_GC103_CONTROL_STRAP_READBACK;
    initial.pins = *pins;
    initial.cr0 = strapped_readback(&initial);
    *r = initial;
    return BM_STATUS_OK;
}

bm_status_t
bm_gc103_registers_initialize(bm_gc103_registers_t *registers, uint32_t ram_bytes)
{
    /* Entries 2/4/6/8 of classic mem_conf_cr0, in whole MiB order. */
    static const uint8_t straps[4] = {0x20U, 0x60U, 0x40U, 0xa0U};
    bm_gc103_registers_t initial;
    if (registers == NULL || ram_bytes < 0x100000U || ram_bytes > 0x400000U ||
        (ram_bytes & 0xfffffU) != 0U)
        return BM_STATUS_INVALID_ARGUMENT;
    memset(&initial, 0, sizeof(initial));
    initial.ram_straps = straps[(ram_bytes >> 20) - 1U];
    /* Classic initialization leaves raw CR0 zero; only readback includes
     * straps until the first CR0 write. Preserve this distinction. */
    *registers = initial;
    return BM_STATUS_OK;
}

bm_status_t
bm_gc103_registers_access(bm_gc103_registers_t *registers,
                          uint16_t port, uint32_t width,
                          bm_bus_operation_t operation, int debug,
                          uint16_t *value, bm_gc103_register_effect_t *effect)
{
    bm_gc103_register_effect_t next = {BM_GC103_MAPPING_UNCHANGED, 0U};
    uint16_t result;
    uint8_t slot;
    if (registers == NULL || value == NULL || effect == NULL ||
        (width != 1U && width != 2U) ||
        (operation != BM_BUS_READ && operation != BM_BUS_WRITE) ||
        (debug != 0 && debug != 1) ||
        (operation == BM_BUS_WRITE && width == 1U && *value > 0xffU))
        return BM_STATUS_INVALID_ARGUMENT;
    if (port < 0x1ecU || port > 0x1efU)
        return BM_STATUS_UNMAPPED;
    if (debug && operation == BM_BUS_WRITE)
        return BM_STATUS_READ_ONLY;

    slot = registers->mar & 0x3fU;
    if (operation == BM_BUS_READ) {
        result = width == 1U ? 0xffU : 0xffffU;
        if (port == 0x1ecU) {
            result = width == 1U ? (registers->ems[slot] & 0xffU) :
                                  (registers->ems[slot] | 0xfc00U);
            if (!debug && (registers->mar & 0x80U))
                registers->mar = (uint8_t)(registers->mar + 1U);
        } else if (width == 1U && port == 0x1eeU) {
            result = registers->mar;
        } else if (width == 1U && port == 0x1efU) {
            result = registers->control_profile == BM_GC103_CONTROL_STRAP_READBACK ?
                strapped_readback(registers) :
                ((registers->cr0 & 0x1fU) | registers->ram_straps);
        }
        *value = result;
    } else if (port == 0x1ecU) {
        registers->ems[slot] = width == 1U ? (*value | 0xff00U) : *value;
        next.mapping = BM_GC103_MAPPING_EMS_SLOT;
        next.slot = slot;
        if (registers->mar & 0x80U)
            registers->mar = (uint8_t)(registers->mar + 1U);
    } else if (width == 1U && port == 0x1eeU) {
        registers->mar = (uint8_t)*value;
    } else if (width == 1U && port == 0x1efU) {
        if (registers->control_profile == BM_GC103_CONTROL_STRAP_READBACK) {
            registers->cr_written = (uint8_t)*value;
            registers->cr0 = strapped_readback(registers);
        } else {
            registers->cr0 = (*value & 0x1fU) | registers->ram_straps;
        }
        next.mapping = BM_GC103_MAPPING_ALL;
    }
    *effect = next;
    return BM_STATUS_OK;
}
