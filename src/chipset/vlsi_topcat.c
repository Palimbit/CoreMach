/*
 * BluMach, a preservation-focused fork of 86Box.
 *
 *          VLSI TOPCAT VL82C320 / VL82C331 (initial implementation).
 *
 * Author: rtzor
 * Project: BluMach
 *
 * Copyright 2026 rtzor.
 *
 * The initial register model was recovered from the surviving development
 * object and checked against Olivetti firmware behaviour.  Unknown bits are
 * deliberately retained as firmware-visible register state.
 */

#include <stdint.h>
#include <stdlib.h>

#include <86box/86box.h>
#include "cpu.h"
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>

typedef struct vlsi_topcat_bank_t {
    uint32_t      virt_base;
    uint32_t      logical_size;
    uint32_t      phys_base;
    uint32_t      phys_size;
    uint32_t      split_virt_base;
    uint32_t      split_phys_offset;
    uint32_t      split_size;
    mem_mapping_t mapping;
    mem_mapping_t split_mapping;
} vlsi_topcat_bank_t;

typedef struct vlsi_topcat_t {
    uint8_t  index;
    uint8_t  regs[256];
    uint8_t  config_enabled;
    uint8_t  ems_index;
    uint8_t  ems_active_set;
    uint16_t ems_page[2][36];
    uint32_t phys_base[4];
    uint32_t phys_size[4];
    vlsi_topcat_bank_t bank[4];
    mem_mapping_t ems_mapping[36];
} vlsi_topcat_t;

static const uint8_t topcat_phys_to_logical[16][4] = {
    { 0, 1, 2, 3 }, { 1, 2, 0, 3 }, { 0, 2, 1, 3 }, { 2, 1, 0, 3 },
    { 2, 0, 1, 3 }, { 1, 0, 3, 2 }, { 3, 0, 1, 2 }, { 3, 1, 0, 2 },
    { 0, 2, 3, 1 }, { 0, 3, 2, 1 }, { 3, 2, 0, 1 }, { 1, 2, 3, 0 },
    { 1, 3, 2, 0 }, { 3, 1, 2, 0 }, { 2, 3, 1, 0 }, { 3, 2, 1, 0 }
};

static const uint32_t topcat_logical_sizes[24][4] = {
    { 0x080000, 0,        0,        0        },
    { 0x080000, 0x080000, 0,        0        },
    { 0x080000, 0x080000, 0x080000, 0        },
    { 0x080000, 0x080000, 0x080000, 0x080000 },
    { 0x200000, 0,        0,        0        },
    { 0x080000, 0x200000, 0,        0        },
    { 0x080000, 0x080000, 0x200000, 0        },
    { 0x200000, 0x200000, 0,        0        },
    { 0x200000, 0x200000, 0x080000, 0        },
    { 0x080000, 0x080000, 0x200000, 0x200000 },
    { 0x200000, 0x200000, 0x200000, 0        },
    { 0x200000, 0x200000, 0x200000, 0x200000 },
    { 0x800000, 0,        0,        0        },
    { 0x080000, 0x800000, 0,        0        },
    { 0x080000, 0x080000, 0x800000, 0        },
    { 0x200000, 0x800000, 0,        0        },
    { 0x200000, 0x200000, 0x800000, 0        },
    { 0x800000, 0x800000, 0,        0        },
    { 0x800000, 0x800000, 0x080000, 0        },
    { 0x080000, 0x080000, 0x800000, 0x800000 },
    { 0x800000, 0x800000, 0x200000, 0        },
    { 0x200000, 0x200000, 0x800000, 0x800000 },
    { 0x800000, 0x800000, 0x800000, 0        },
    { 0x800000, 0x800000, 0x800000, 0x800000 }
};

static uint32_t
vlsi_topcat_bank_addr(uint32_t addr, const vlsi_topcat_bank_t *bank)
{
    uint32_t offset;

    if (bank->split_size && (addr >= bank->split_virt_base))
        offset = bank->split_phys_offset + (addr - bank->split_virt_base);
    else
        offset = addr - bank->virt_base;

    if (bank->logical_size == 0x800000) {
        if (bank->phys_size == 0x080000)
            offset &= ~0x000400;
        else if (bank->phys_size == 0x200000)
            offset &= ~0x000800;
    }

    return bank->phys_base + (offset % bank->phys_size);
}

static uint8_t
vlsi_topcat_bank_readb(uint32_t addr, void *priv)
{
    const vlsi_topcat_bank_t *bank = (const vlsi_topcat_bank_t *) priv;
    return ram[vlsi_topcat_bank_addr(addr, bank)];
}

static uint16_t
vlsi_topcat_bank_readw(uint32_t addr, void *priv)
{
    return vlsi_topcat_bank_readb(addr, priv) |
           ((uint16_t) vlsi_topcat_bank_readb(addr + 1, priv) << 8);
}

static uint32_t
vlsi_topcat_bank_readl(uint32_t addr, void *priv)
{
    return vlsi_topcat_bank_readw(addr, priv) |
           ((uint32_t) vlsi_topcat_bank_readw(addr + 2, priv) << 16);
}

static void
vlsi_topcat_bank_writeb(uint32_t addr, uint8_t val, void *priv)
{
    const vlsi_topcat_bank_t *bank = (const vlsi_topcat_bank_t *) priv;
    const uint32_t phys = vlsi_topcat_bank_addr(addr, bank);
    mem_write_ramb_page(phys, val, &pages[phys >> 12]);
}

static void
vlsi_topcat_bank_writew(uint32_t addr, uint16_t val, void *priv)
{
    vlsi_topcat_bank_writeb(addr, val, priv);
    vlsi_topcat_bank_writeb(addr + 1, val >> 8, priv);
}

static void
vlsi_topcat_bank_writel(uint32_t addr, uint32_t val, void *priv)
{
    vlsi_topcat_bank_writew(addr, val, priv);
    vlsi_topcat_bank_writew(addr + 2, val >> 16, priv);
}

static void
vlsi_topcat_banks_recalc(vlsi_topcat_t *dev)
{
    uint8_t  logical_to_phys[4] = { 0xff, 0xff, 0xff, 0xff };
    uint32_t virt_base = 0;
    const uint8_t order = dev->regs[4] & 0x0f;
    const uint8_t layout = dev->regs[3] & 0x1f;

    for (uint8_t phys = 0; phys < 4; phys++)
        logical_to_phys[topcat_phys_to_logical[order][phys]] = phys;

    for (uint8_t logical = 0; logical < 4; logical++) {
        vlsi_topcat_bank_t *bank = &dev->bank[logical];
        const uint8_t phys = logical_to_phys[logical];
        uint32_t logical_size;
        uint32_t regular_size;

        if (layout == 0x1f)
            logical_size = (logical < 2) ? 0x080000 : 0;
        else if (layout == 0x1e)
            logical_size = (logical == 0) ? 0x200000 : 0;
        else
            logical_size = (layout < 24) ? topcat_logical_sizes[layout][logical] : 0;

        mem_mapping_disable(&bank->mapping);
        mem_mapping_disable(&bank->split_mapping);
        bank->virt_base = virt_base;
        bank->logical_size = logical_size;
        bank->phys_base = (phys < 4) ? dev->phys_base[phys] : 0;
        bank->phys_size = (phys < 4) ? dev->phys_size[phys] : 0;
        bank->split_virt_base = 0;
        bank->split_phys_offset = 0;
        bank->split_size = 0;
        virt_base += logical_size;

        if (bank->logical_size && bank->phys_size) {
            regular_size = bank->logical_size;
            if ((layout == 0x1e) && (logical == 0))
                regular_size = 0x0a0000;
            mem_mapping_set_addr(&bank->mapping, bank->virt_base, regular_size);
            mem_mapping_set_mask(&bank->mapping,
                                 (regular_size & (regular_size - 1)) ?
                                 0xffffffff : regular_size - 1);
            mem_mapping_set_exec(&bank->mapping,
                                 (regular_size == bank->phys_size) ?
                                 ram + bank->phys_base : NULL);
            mem_mapping_enable(&bank->mapping);
        }
    }

    if ((layout == 0x1f) && dev->bank[1].phys_size) {
        vlsi_topcat_bank_t *bank = &dev->bank[1];
        bank->split_virt_base = 0x100000;
        bank->split_phys_offset = 0x020000;
        bank->split_size = 0x060000;
        mem_mapping_set_addr(&bank->split_mapping, bank->split_virt_base,
                             bank->split_size);
        mem_mapping_set_mask(&bank->split_mapping, 0xffffffff);
        mem_mapping_set_exec(&bank->split_mapping,
                             ram + bank->phys_base + bank->split_phys_offset);
        mem_mapping_enable(&bank->split_mapping);
    } else if ((layout == 0x1e) && dev->bank[0].phys_size) {
        vlsi_topcat_bank_t *bank = &dev->bank[0];
        bank->split_virt_base = 0x100000;
        bank->split_phys_offset = 0x0a0000;
        bank->split_size = 0x160000;
        mem_mapping_set_addr(&bank->split_mapping, bank->split_virt_base,
                             bank->split_size);
        mem_mapping_set_mask(&bank->split_mapping, 0xffffffff);
        mem_mapping_set_exec(&bank->split_mapping,
                             ram + bank->phys_base + bank->split_phys_offset);
        mem_mapping_enable(&bank->split_mapping);
    }

    mem_set_access(ACCESS_ALL, 0, 0x000000, 0x0a0000,
                   MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
    mem_set_access(ACCESS_ALL, 0, 0x0a0000, 0x040000,
                   MEM_READ_ROMCS | MEM_WRITE_ROMCS);
    mem_set_access(ACCESS_ALL, 0, 0x0e0000, 0x020000,
                   MEM_READ_ROMCS | MEM_WRITE_ROMCS);
    mem_set_access(ACCESS_ALL, 0, 0x100000, 0xee0000,
                   MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
    mem_set_access(ACCESS_ALL, 0, 0xfe0000, 0x020000,
                   MEM_READ_ROMCS | MEM_WRITE_ROMCS);
    flushmmucache_nopc();
}

static uint32_t
vlsi_topcat_ems_addr(const vlsi_topcat_t *dev, uint8_t slot)
{
    if (slot >= 12)
        return 0x10000 + ((uint32_t) slot << 14);
    if (!(dev->regs[11] & 0x10))
        return 0xc0000 + ((uint32_t) slot << 14);
    if (slot < 4)
        return 0xa0000 + ((uint32_t) slot << 14);
    if (slot < 8)
        return 0xc0000 + ((uint32_t) slot << 14);
    return 0x90000 + ((uint32_t) slot << 14);
}

static int
vlsi_topcat_ems_enabled(const vlsi_topcat_t *dev, uint8_t slot, uint16_t page)
{
    const uint32_t end = ((uint32_t) page << 14) + 0x4000;

    if (end > ((uint32_t) mem_size << 10))
        return 0;
    if (slot >= 12)
        return (dev->regs[11] & 0x40) != 0;
    if (!(dev->regs[11] & 0x80))
        return 0;
    if (slot < 8)
        return (dev->regs[12] & (1 << slot)) != 0;
    return (dev->regs[11] & (1 << (slot - 8))) != 0;
}

static void
vlsi_topcat_ems_recalc(vlsi_topcat_t *dev)
{
    for (uint8_t slot = 0; slot < 36; slot++) {
        mem_mapping_t *mapping = &dev->ems_mapping[slot];
        const uint32_t addr = vlsi_topcat_ems_addr(dev, slot);
        const uint16_t page = dev->ems_page[dev->ems_active_set][slot];

        mem_mapping_disable(mapping);
        mem_set_access(ACCESS_ALL, 0, addr, 0x4000,
                       (addr <= 0x9ffff) ?
                       (MEM_READ_INTERNAL | MEM_WRITE_INTERNAL) :
                       (MEM_READ_ROMCS | MEM_WRITE_ROMCS));

        if (vlsi_topcat_ems_enabled(dev, slot, page)) {
            mem_mapping_set_addr(mapping, addr, 0x4000);
            mem_mapping_set_mask(mapping, 0x3fff);
            mem_mapping_set_exec(mapping, ram + ((uint32_t) page << 14));
            mem_mapping_enable(mapping);
            mem_set_access(ACCESS_ALL, 0, addr, 0x4000,
                           MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
        }
    }
    flushmmucache_nopc();
}

static void
vlsi_topcat_ems_advance(vlsi_topcat_t *dev)
{
    if (dev->ems_index & 0x40)
        dev->ems_index = (dev->ems_index & 0xc0) |
                         (((dev->ems_index & 0x3f) + 1) & 0x3f);
}

static void
vlsi_topcat_write(uint16_t port, uint8_t val, void *priv)
{
    vlsi_topcat_t *dev = (vlsi_topcat_t *) priv;
    uint8_t slot;
    uint8_t set;

    switch (port) {
        case 0x00e8:
            if (dev->config_enabled)
                dev->ems_index = val;
            break;
        case 0x00e9:
            if (dev->config_enabled) {
                dev->ems_active_set = 1;
                vlsi_topcat_ems_recalc(dev);
            }
            break;
        case 0x00ea:
            if (!dev->config_enabled)
                break;
            slot = dev->ems_index & 0x3f;
            set = (dev->ems_index >> 7) & 1;
            if (slot < 36)
                dev->ems_page[set][slot] =
                    (dev->ems_page[set][slot] & 0x0700) | val;
            break;
        case 0x00eb:
            if (!dev->config_enabled)
                break;
            slot = dev->ems_index & 0x3f;
            set = (dev->ems_index >> 7) & 1;
            if (slot < 36) {
                dev->ems_page[set][slot] =
                    (dev->ems_page[set][slot] & 0x00ff) |
                    (((uint16_t) val << 8) & 0x0700);
                vlsi_topcat_ems_advance(dev);
                if (set == dev->ems_active_set)
                    vlsi_topcat_ems_recalc(dev);
            } else
                vlsi_topcat_ems_advance(dev);
            break;
        case 0x00ec:
            if (dev->config_enabled)
                dev->index = val;
            break;
        case 0x00ed:
            if (!dev->config_enabled)
                break;
            dev->regs[dev->index] = val;
            if ((dev->index == 3) || (dev->index == 4))
                vlsi_topcat_banks_recalc(dev);
            if ((dev->index == 3) || (dev->index == 4) ||
                (dev->index == 11) || (dev->index == 12))
                vlsi_topcat_ems_recalc(dev);
            break;
        case 0x00ee:
            mem_a20_alt = 0;
            mem_a20_recalc();
            break;
        case 0x00f9:
            dev->config_enabled = 0;
            break;
        case 0x00fb:
            dev->config_enabled = 1;
            break;
        default:
            break;
    }
}

static uint8_t
vlsi_topcat_read(uint16_t port, void *priv)
{
    vlsi_topcat_t *dev = (vlsi_topcat_t *) priv;
    uint8_t slot;
    uint8_t set;
    uint8_t ret = 0xff;

    switch (port) {
        case 0x00e8:
            if (dev->config_enabled)
                ret = dev->ems_index;
            break;
        case 0x00e9:
            if (dev->config_enabled) {
                dev->ems_active_set = 0;
                vlsi_topcat_ems_recalc(dev);
            }
            break;
        case 0x00ea:
            if (dev->config_enabled) {
                slot = dev->ems_index & 0x3f;
                set = (dev->ems_index >> 7) & 1;
                if (slot < 36)
                    ret = dev->ems_page[set][slot] & 0xff;
            }
            break;
        case 0x00eb:
            if (dev->config_enabled) {
                slot = dev->ems_index & 0x3f;
                set = (dev->ems_index >> 7) & 1;
                if (slot < 36)
                    ret = (dev->ems_page[set][slot] >> 8) | 0xf8;
                vlsi_topcat_ems_advance(dev);
            }
            break;
        case 0x00ec:
            if (dev->config_enabled)
                ret = dev->index;
            break;
        case 0x00ed:
            if (dev->config_enabled)
                ret = dev->regs[dev->index];
            break;
        case 0x00ee:
            mem_a20_alt = 2;
            mem_a20_recalc();
            break;
        case 0x00ef:
            softresetx86();
            cpu_set_edx();
            break;
        default:
            break;
    }
    return ret;
}

static void
vlsi_topcat_close(void *priv)
{
    free(priv);
}

static void *
vlsi_topcat_init(const device_t *info)
{
    vlsi_topcat_t *dev = (vlsi_topcat_t *) calloc(1, sizeof(vlsi_topcat_t));
    (void) info;

    dev->regs[0] = 0xe0;
    dev->regs[1] = 0xff;
    dev->regs[2] = 0xff;
    dev->regs[3] = 0xe0;
    dev->regs[4] = 0xf0;
    dev->regs[5] = 0x3c;
    dev->regs[7] = 0xff;
    dev->regs[8] = 0xb7;
    dev->regs[9] = 0xff;
    dev->regs[10] = 0xb7;
    dev->regs[19] = 0x01;
    dev->regs[20] = 0x06;

    switch (mem_size) {
        case 2048:
            dev->regs[3] = 0xe4;
            dev->phys_size[0] = 0x200000;
            break;
        case 4096:
            dev->regs[3] = 0xe7;
            dev->phys_size[0] = 0x200000;
            dev->phys_size[1] = 0x200000;
            break;
        case 10240:
            dev->regs[3] = 0xef;
            dev->phys_size[0] = 0x200000;
            dev->phys_size[1] = 0x800000;
            break;
        default:
            dev->regs[3] = 0xe0;
            dev->phys_size[0] = (uint32_t) mem_size << 10;
            break;
    }
    for (uint8_t bank = 1; bank < 4; bank++)
        dev->phys_base[bank] = dev->phys_base[bank - 1] +
                               dev->phys_size[bank - 1];

    mem_mapping_disable(&ram_low_mapping);
    mem_mapping_disable(&ram_mid_mapping);
    mem_mapping_disable(&ram_high_mapping);

    for (uint8_t bank = 0; bank < 4; bank++) {
        mem_mapping_add(&dev->bank[bank].mapping, 0, 0x4000,
                        vlsi_topcat_bank_readb, vlsi_topcat_bank_readw,
                        vlsi_topcat_bank_readl, vlsi_topcat_bank_writeb,
                        vlsi_topcat_bank_writew, vlsi_topcat_bank_writel,
                        NULL, MEM_MAPPING_INTERNAL, &dev->bank[bank]);
        mem_mapping_disable(&dev->bank[bank].mapping);
        mem_mapping_add(&dev->bank[bank].split_mapping, 0, 0x4000,
                        vlsi_topcat_bank_readb, vlsi_topcat_bank_readw,
                        vlsi_topcat_bank_readl, vlsi_topcat_bank_writeb,
                        vlsi_topcat_bank_writew, vlsi_topcat_bank_writel,
                        NULL, MEM_MAPPING_INTERNAL, &dev->bank[bank]);
        mem_mapping_disable(&dev->bank[bank].split_mapping);
    }
    vlsi_topcat_banks_recalc(dev);

    for (uint8_t slot = 0; slot < 36; slot++) {
        const uint32_t addr = vlsi_topcat_ems_addr(dev, slot);
        mem_mapping_add(&dev->ems_mapping[slot], addr, 0x4000,
                        mem_read_ram, mem_read_ramw, mem_read_raml,
                        mem_write_ram, mem_write_ramw, mem_write_raml,
                        ram + addr, MEM_MAPPING_INTERNAL, NULL);
        mem_mapping_disable(&dev->ems_mapping[slot]);
    }

    io_sethandler(0x00e8, 8, vlsi_topcat_read, NULL, NULL,
                  vlsi_topcat_write, NULL, NULL, dev);
    io_sethandler(0x00f9, 1, NULL, NULL, NULL,
                  vlsi_topcat_write, NULL, NULL, dev);
    io_sethandler(0x00fb, 1, NULL, NULL, NULL,
                  vlsi_topcat_write, NULL, NULL, dev);
    return dev;
}

const device_t vlsi_topcat_device = {
    .name          = "VLSI TOPCAT VL82C320/VL82C331",
    .internal_name = "vlsi_topcat",
    .flags         = 0,
    .local         = 0,
    .init          = vlsi_topcat_init,
    .close         = vlsi_topcat_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
