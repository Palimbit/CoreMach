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
 * GC103-only translation/map behavior with private, bounded route metadata.
 */
#include "legacy_gc103_memory.h"
#include <string.h>

enum { LOW = 0, MID = 1, HIGH = 2, UPPER = 3, SHADOW = 27, EMS = 29 };
enum { EXTERNAL = 0, INTERNAL = 1, ROMCS = 2, WRITE_DISABLED = 4 };

static void access_set(bm_gc103_memory_t *m, uint32_t base, uint32_t size, uint8_t access)
{
    uint32_t page;
    for (page = base >> 14; page < ((base + size) >> 14); ++page)
        m->access[page] = access;
}
static void window_set(bm_gc103_memory_t *m, unsigned index,
                        uint32_t base, uint32_t size, int enabled, int slot)
{
    bm_gc103_window_t *w = &m->window[index];
    w->base = base; w->size = size; w->enabled = enabled; w->ems_slot = slot;
}
static uint32_t get_address(const bm_gc103_memory_t *m, uint32_t address, int slot)
{
    uint32_t shift, bank, mapped;
    uint16_t mr;
    /* GC103 07-89 (01), p6 shadow pointer table: F/FF uses MR298..29B
     * (256K DRAM) or MR238..23B (1M), both backing E0000..EFFFF.
     * E/FE uses MR29C..29F or MR23C..23F, backing F0000..FFFFF.
     * These are fixed RAM sources, not a live EMS context or ROM copy. */
    if ((address >= 0xe0000U && address <= 0xfffffU) || address >= 0xfe0000U)
        return (address & 0xfffffU) ^ 0x10000U;
    if (slot >= 0 && (m->registers.cr0 & 2U)) {
        mr = m->registers.ems[slot];
        if (mr & 0x200U) {
            shift = (m->registers.cr0 & 0x80U) ? 21U : 19U;
            bank = (mr >> 7) & 3U;
            mapped = (address & 0x3fffU) | ((uint32_t)(mr & 0x1fU) << 14);
            if (shift == 21U) mapped |= (uint32_t)(mr & 0x60U) << 14;
            return mapped | (bank << shift);
        }
    }
    if (slot < 0 && address >= 0x100000U && !(m->registers.cr0 & 4U))
        address -= 0x60000U;
    return address;
}
static void ems_update(bm_gc103_memory_t *m, unsigned slot)
{
    unsigned index = slot & 31U;
    uint32_t base = (index + 16U) << 14;
    if (index >= 24U) base += 0x20000U;
    m->window[EMS + slot].enabled = 0;
    /* GC103 07-89 (01), pp2/3/7: MAR selects a context for I/O; CR.D0
     * independently selects it for memory. Retire an inactive window during
     * a CR rebuild, but never let an inactive MR write change live decode.
     * This corrects the classic upper-window disable-before-enable artifact. */
    if ((m->registers.cr0 & 1U) != (slot >> 5)) return;
    access_set(m, base, 0x4000U, index < 24U ? INTERNAL : EXTERNAL);
    if (index < 24U) m->window[UPPER + index].enabled = 1;
    if ((m->registers.cr0 & 2U) &&
        ((m->registers.cr0 & 1U) == (slot >> 5)) &&
        (m->registers.ems[slot] & 0x200U)) {
        access_set(m, base, 0x4000U, INTERNAL);
        if (index < 24U) m->window[UPPER + index].enabled = 0;
        m->window[EMS + slot].enabled = 1;
    }
}
static void map_update(bm_gc103_memory_t *m)
{
    unsigned i;
    uint8_t cr0 = m->registers.cr0;
    uint32_t shadow_base = 0, shadow_size = 0;
    m->window[MID].enabled = 0;
    m->window[SHADOW].enabled = m->window[SHADOW + 1].enabled = 0;
    access_set(m, 0xe0000U, 0x20000U, ROMCS);
    access_set(m, 0xfe0000U, 0x20000U, ROMCS);
    if (cr0 & 4U) {
        window_set(m, MID, 0xa0000U, 0x40000U, 0, -1);
        if (m->ram_bytes > 0x100000U) {
            access_set(m, m->ram_bytes, 0x60000U, EXTERNAL);
            window_set(m, HIGH, 0x100000U, m->ram_bytes - 0x100000U, 1, -1);
        } else {
            access_set(m, 0x100000U, 0x60000U, EXTERNAL);
        }
    } else {
        window_set(m, MID, 0x100000U, 0x60000U, 1, -1);
        if (m->ram_bytes > 0x100000U) {
            access_set(m, m->ram_bytes, 0x60000U, INTERNAL);
            window_set(m, HIGH, 0x160000U, m->ram_bytes - 0x100000U, 1, -1);
        } else {
            access_set(m, 0x100000U, 0x60000U, INTERNAL);
        }
        cr0 &= 0xe7U;
    }
    switch (cr0 & 0x18U) {
    case 0x08: shadow_base = 0xe0000U; shadow_size = 0x10000U; break;
    case 0x10: shadow_base = 0xf0000U; shadow_size = 0x10000U; break;
    case 0x18: shadow_base = 0xe0000U; shadow_size = 0x20000U; break;
    default: break;
    }
    if (shadow_size) {
        access_set(m, shadow_base, shadow_size, INTERNAL | WRITE_DISABLED);
        access_set(m, shadow_base + 0xf00000U, shadow_size, INTERNAL | WRITE_DISABLED);
        window_set(m, SHADOW, shadow_base, shadow_size, 1, -1);
        window_set(m, SHADOW + 1, shadow_base + 0xf00000U, shadow_size, 1, -1);
    }
    for (i = 0; i < 32U; ++i) {
        unsigned active = (m->registers.cr0 & 1U) << 5;
        ems_update(m, i | (active ^ 32U));
        ems_update(m, i | active);
    }
}

bm_status_t bm_gc103_memory_initialize(bm_gc103_memory_t *m, uint32_t ram_bytes)
{
    bm_gc103_registers_t registers;
    unsigned i;
    if (m == NULL || bm_gc103_registers_initialize(&registers, ram_bytes) != BM_STATUS_OK)
        return BM_STATUS_INVALID_ARGUMENT;
    memset(m, 0, sizeof(*m));
    m->registers = registers;
    m->ram_bytes = ram_bytes;
    access_set(m, 0, 0xa0000U, INTERNAL);
    access_set(m, 0x100000U, ram_bytes - 0x100000U, INTERNAL);
    window_set(m, LOW, 0, 0x40000U, 1, -1);
    for (i = 0; i < 24U; ++i)
        window_set(m, UPPER + i, 0x40000U + (i << 14), 0x4000U, 1, -1);
    for (i = 0; i < 64U; ++i)
        window_set(m, EMS + i, ((i & 31U) + ((i & 31U) >= 24U ? 24U : 16U)) << 14,
                   0x4000U, 0, (int)i);
    map_update(m);
    return BM_STATUS_OK;
}

bm_status_t bm_gc103_memory_initialize_configured(bm_gc103_memory_t *m,
                                                  const bm_gc103_memory_config_t *config)
{
    bm_gc103_registers_t registers;
    uint32_t bank_bytes;
    if (m == NULL || config == NULL ||
        (config->dram != BM_GC103_DRAM_256K && config->dram != BM_GC103_DRAM_1M) ||
        config->installed_banks < 1U || config->installed_banks > 4U ||
        bm_gc103_registers_initialize_strapped(&registers, &config->pins) != BM_STATUS_OK)
        return BM_STATUS_INVALID_ARGUMENT;
    /* Detailed pp5-7 unambiguously define software control when floating.
     * Do not conflate the OR readback of tied pins with effective decode. */
    if (config->pins.ramsw1 != BM_GC103_PIN_FLOATING ||
        config->pins.ramsw2 != BM_GC103_PIN_FLOATING ||
        config->pins.splsw != BM_GC103_PIN_FLOATING ||
        (config->pins.ram1m == BM_GC103_PIN_LOW && config->dram != BM_GC103_DRAM_1M) ||
        (config->pins.ram1m == BM_GC103_PIN_HIGH && config->dram != BM_GC103_DRAM_256K))
        return BM_STATUS_UNSUPPORTED;
    bank_bytes = config->dram == BM_GC103_DRAM_1M ? 0x200000U : 0x80000U;
    /* Read all borrowed config before replacing the caller-owned object. */
    {
        uint32_t ram_bytes = bank_bytes * config->installed_banks;
        memset(m, 0, sizeof(*m));
        m->registers = registers;
        m->ram_bytes = ram_bytes;
        m->physical_bank_bytes = bank_bytes;
    }
    return BM_STATUS_OK;
}

static int valid_profile(const bm_gc103_memory_t *m)
{
    return m->physical_bank_bytes == 0 ?
        m->registers.control_profile == BM_GC103_CONTROL_LEGACY :
        m->registers.control_profile == BM_GC103_CONTROL_STRAP_READBACK;
}

/* p2 linear top is selected by bank count; p5 MR has its own bank/page fields.
 * Retained inference: count limits linear decode, not enabled MR translation.
 * Reuse the corrected classic address formula.
 * No cached windows: shrinking a programmed extent cannot leave stale routes. */
static bm_status_t configured_resolve(const bm_gc103_memory_t *m, uint32_t address,
                                       bm_gc10x_route_t *r)
{
    uint8_t cr = m->registers.cr0;
    uint32_t bank_bytes = cr & 0x80U ? 0x200000U : 0x80000U;
    uint32_t selected_bytes = bank_bytes * (((cr >> 5) & 3U) + 1U);
    uint32_t top = selected_bytes + ((cr & 4U) ? 0U : 0x60000U);
    int slot = -1, shadow = 0;
    if ((address >= 0xe0000U && address <= 0xfffffU) || address >= 0xfe0000U) {
        shadow = (cr & 4U) && (cr & ((address & 0x10000U) ? 0x10U : 0x08U));
        if (!shadow) {
            r->target = BM_GC10X_FIRMWARE;
            r->offset = address & 0x1ffffU;
            return BM_STATUS_OK;
        }
    } else {
        if (cr & 2U) {
            if (address >= 0x40000U && address < 0xa0000U)
                slot = (int)((address - 0x40000U) >> 14);
            else if (address >= 0xc0000U && address < 0xe0000U)
                slot = 24 + (int)((address - 0xc0000U) >> 14);
            if (slot >= 0) {
                slot += (cr & 1U) * 32;
                if (!(m->registers.ems[slot] & 0x200U)) slot = -1;
            }
        }
        if (slot < 0) {
            if (address < 0xa0000U) {
                /* The manual's fixed 640KiB decode description does not
                 * resolve a selected 512KiB linear population. */
                if (selected_bytes < 0x100000U) return BM_STATUS_UNSUPPORTED;
            } else if (!(address >= 0x100000U && address < top)) {
                r->target = BM_GC10X_EXTERNAL;
                r->offset = address;
                r->writable = 1;
                return BM_STATUS_OK;
            }
        }
    }
    if (bank_bytes != m->physical_bank_bytes) return BM_STATUS_UNSUPPORTED;
    r->offset = get_address(m, address, slot);
    if (r->offset < m->ram_bytes) {
        uint32_t available = m->ram_bytes - r->offset;
        r->target = BM_GC10X_RAM;
        r->writable = !shadow;
        if (available < r->contiguous_bytes) r->contiguous_bytes = available;
    } else {
        r->target = BM_GC10X_OPEN_BUS;
        r->offset = 0;
    }
    return BM_STATUS_OK;
}

bm_status_t bm_gc103_memory_io(bm_gc103_memory_t *m, uint16_t port,
                              uint32_t width, bm_bus_operation_t operation,
                              int debug, uint16_t *value)
{
    bm_gc103_register_effect_t effect;
    bm_status_t status;
    if (m == NULL) return BM_STATUS_INVALID_ARGUMENT;
    if (!valid_profile(m))
        return BM_STATUS_UNSUPPORTED;
    status = bm_gc103_registers_access(&m->registers, port, width, operation, debug, value, &effect);
    if (status != BM_STATUS_OK) return status;
    if (m->physical_bank_bytes) return BM_STATUS_OK; /* Pure live decode. */
    if (effect.mapping == BM_GC103_MAPPING_EMS_SLOT) ems_update(m, effect.slot);
    else if (effect.mapping == BM_GC103_MAPPING_ALL) map_update(m);
    return BM_STATUS_OK;
}
bm_status_t bm_gc103_memory_resolve(const bm_gc103_memory_t *m,
                                   bm_gc10x_requester_t requester, int cpu_a20,
                                   uint32_t address, bm_bus_operation_t operation,
                                   bm_gc10x_route_t *out_route)
{
    bm_gc10x_route_t route = {0};
    uint8_t access;
    int i;
    if (m == NULL || out_route == NULL || address > 0xffffffU ||
        requester < BM_GC10X_CPU || requester > BM_GC10X_ISA_MASTER ||
        (cpu_a20 != 0 && cpu_a20 != 1) ||
        operation < BM_BUS_READ || operation > BM_BUS_FETCH)
        return BM_STATUS_INVALID_ARGUMENT;
    if (!valid_profile(m))
        return BM_STATUS_UNSUPPORTED;
    if (requester == BM_GC10X_CPU && !cpu_a20) address &= ~0x100000U;
    route.contiguous_bytes = 0x4000U - (address & 0x3fffU);
    route.wait_quality = BM_GC10X_WAIT_UNKNOWN;
    if (m->physical_bank_bytes) {
        bm_status_t status = configured_resolve(m, address, &route);
        if (status != BM_STATUS_OK) return status;
        *out_route = route;
        return BM_STATUS_OK;
    }
    access = m->access[address >> 14];
    route.target = BM_GC10X_OPEN_BUS;
    if ((access & 3U) == EXTERNAL) {
        route.target = BM_GC10X_EXTERNAL;
        route.offset = address;
        route.writable = 1; /* Downstream target decides actual write policy. */
    } else if ((access & 3U) == ROMCS) {
        route.target = BM_GC10X_FIRMWARE;
        route.offset = address & 0x1ffffU;
    } else {
        /* Classic mappings are registered in this order; later enabled
         * internal handlers take precedence. No fast-path host pointers. */
        for (i = 92; i >= 0; --i) {
            const bm_gc103_window_t *w = &m->window[i];
            if (!w->enabled || address < w->base || address - w->base >= w->size) continue;
            route.offset = get_address(m, address, w->ems_slot);
            if (route.offset < m->ram_bytes) {
                uint32_t available = m->ram_bytes - route.offset;
                route.target = BM_GC10X_RAM;
                route.writable = !(access & WRITE_DISABLED);
                if (available < route.contiguous_bytes) route.contiguous_bytes = available;
            } else route.offset = 0;
            break;
        }
    }
    *out_route = route;
    return BM_STATUS_OK;
}
