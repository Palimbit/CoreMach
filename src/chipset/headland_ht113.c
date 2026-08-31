/*
 * Headland HT113 memory controller used with HT101SX and GC102-PC.
 *
 * No exact HT113 data sheet is currently public.  This implementation is a
 * deliberately small functional contract derived from:
 *   - the PCS 386SX Phoenix 1.14 I/O sequence;
 *   - an independent HT101SX/HT113/GC102 AMI BIOS;
 *   - the contemporary Headland HT21 and GC103 register descriptions.
 *
 * Family-derived behaviour is kept in this file so it can be replaced when
 * an exact HT113 reference or hardware trace is preserved.  In particular,
 * this device does not inherit an HT18 revision, CR5/CR6, sleep state or
 * fast-A20 port 92h.
 */
#include <stdint.h>
#include <stdlib.h>

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/plat_unused.h>
#include <86box/chipset.h>

#include "headland_pcs386sx.h"

#ifdef ENABLE_HEADLAND_PCS386SX_LOG
#    define ht113_log(...) pclog(__VA_ARGS__)
#else
#    define ht113_log(...)
#endif

typedef struct headland_ht113_t headland_ht113_t;

typedef struct headland_ht113_mr_t {
    uint8_t            valid;
    uint8_t            enabled;
    uint8_t            write_protected;
    uint16_t           value;
    uint32_t           virt_base;
    headland_ht113_t  *owner;
} headland_ht113_mr_t;

struct headland_ht113_t {
    headland_pcs386sx_t *board;

    uint8_t cri;
    uint8_t mar;
    uint8_t cr[5];

    headland_ht113_mr_t null_mr;
    headland_ht113_mr_t mr[64];

    mem_mapping_t low_mapping;
    mem_mapping_t upper_mapping[24];
    mem_mapping_t ems_mapping[64];
    mem_mapping_t mid_mapping;
    mem_mapping_t high_mapping;
    mem_mapping_t shadow_mapping[2];
};

/* Strap readback used by the established Headland family core.  The PCS
   386SX public configuration currently admits 1 MB steps up to 8 MB, so the
   complete family table is retained instead of assuming only powers of two. */
static const uint8_t ht113_mem_conf_cr0[41] = {
    0x00, 0x00, 0x20, 0x40, 0x60, 0xa0, 0x40, 0xe0,
    0xa0, 0xc0, 0xe0, 0xe0, 0xc0, 0xe0, 0xe0, 0xe0,
    0xe0, 0x20, 0x40, 0x40, 0xa0, 0xc0, 0xe0, 0xe0,
    0xc0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0,
    0x20, 0x40, 0x60, 0x60, 0xc0, 0xe0, 0xe0, 0xe0,
    0xe0
};

static const uint8_t ht113_mem_conf_cr1[41] = {
    0x00, 0x40, 0x00, 0x00, 0x00, 0x40, 0x40, 0x40,
    0x00, 0x40, 0x40, 0x40, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x40, 0x40, 0x40, 0x00, 0x00, 0x00, 0x00,
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
    0x00, 0x00, 0x40, 0x40, 0x00, 0x00, 0x00, 0x00,
    0x40
};

static uint8_t
ht113_memory_index(void)
{
    uint32_t index = (uint32_t) mem_size >> 9;

    if (index > 40)
        index = 40;
    return (uint8_t) index;
}

static uint32_t
ht113_get_addr(headland_ht113_t *dev, uint32_t addr, headland_ht113_mr_t *mr)
{
    uint32_t bank_base[4];
    uint32_t bank_shift[4];
    uint32_t shift;
    uint32_t other_shift;
    uint32_t bank;

    if ((addr >= 0x0e0000) && (addr <= 0x0fffff))
        return addr;
    if ((addr >= 0xfe0000) && (addr <= 0xffffff))
        return addr & 0x0fffff;

    shift       = (dev->cr[0] & 0x80) ? 21 : 19;
    other_shift = (dev->cr[0] & 0x80) ? 19 : 21;

    bank_shift[0] = bank_shift[1] = shift;
    bank_base[0]                       = 0x00000000;
    bank_base[1]                       = bank_base[0] + (1U << shift);

    if (dev->cr[1] & 0x40) {
        bank_shift[2] = bank_shift[3] = other_shift;
        bank_base[2]                  = bank_base[1] + (1U << other_shift);
        bank_base[3]                  = bank_base[2] + (1U << other_shift);
    } else {
        bank_shift[2] = bank_shift[3] = shift;
        bank_base[2]                  = bank_base[1] + (1U << shift);
        bank_base[3]                  = bank_base[2] + (1U << shift);
    }

    if ((mr != NULL) && mr->valid && (dev->cr[0] & 0x02) && (mr->value & 0x0200)) {
        addr = (addr & 0x3fff) | ((mr->value & 0x001f) << 14);
        bank = (mr->value >> 7) & 3;

        if (bank_shift[bank] >= 21)
            addr |= (mr->value & 0x0060) << 14;

        addr |= bank_base[bank];
    } else if (((mr == NULL) || !mr->valid) && (mem_size >= 1024) &&
               (addr >= 0x100000) && ((dev->cr[0] & 0x04) == 0)) {
        /* CR0.2=0 exposes the 384 KiB hidden by A0000h-FFFFFh immediately
           above 1 MiB.  This is owned here; no generic remap is installed. */
        addr -= 0x60000;
    }

    return addr;
}

static uint8_t
ht113_mem_read_b(uint32_t addr, void *priv)
{
    headland_ht113_mr_t *mr  = (headland_ht113_mr_t *) priv;
    headland_ht113_t    *dev = mr->owner;

    addr = ht113_get_addr(dev, addr, mr);
    return (addr < ((uint32_t) mem_size << 10)) ? ram[addr] : 0xff;
}

static uint16_t
ht113_mem_read_w(uint32_t addr, void *priv)
{
    headland_ht113_mr_t *mr  = (headland_ht113_mr_t *) priv;
    headland_ht113_t    *dev = mr->owner;

    addr = ht113_get_addr(dev, addr, mr);
    return ((addr + 1) < ((uint32_t) mem_size << 10)) ? *(uint16_t *) &ram[addr] : 0xffff;
}

static uint32_t
ht113_mem_read_l(uint32_t addr, void *priv)
{
    headland_ht113_mr_t *mr  = (headland_ht113_mr_t *) priv;
    headland_ht113_t    *dev = mr->owner;

    addr = ht113_get_addr(dev, addr, mr);
    return ((addr + 3) < ((uint32_t) mem_size << 10)) ? *(uint32_t *) &ram[addr] : 0xffffffff;
}

static void
ht113_mem_write_b(uint32_t addr, uint8_t val, void *priv)
{
    headland_ht113_mr_t *mr  = (headland_ht113_mr_t *) priv;
    headland_ht113_t    *dev = mr->owner;

    if (mr->valid && mr->enabled && mr->write_protected)
        return;

    addr = ht113_get_addr(dev, addr, mr);
    if (addr < ((uint32_t) mem_size << 10))
        ram[addr] = val;
}

static void
ht113_mem_write_w(uint32_t addr, uint16_t val, void *priv)
{
    headland_ht113_mr_t *mr  = (headland_ht113_mr_t *) priv;
    headland_ht113_t    *dev = mr->owner;

    if (mr->valid && mr->enabled && mr->write_protected)
        return;

    addr = ht113_get_addr(dev, addr, mr);
    if ((addr + 1) < ((uint32_t) mem_size << 10))
        *(uint16_t *) &ram[addr] = val;
}

static void
ht113_mem_write_l(uint32_t addr, uint32_t val, void *priv)
{
    headland_ht113_mr_t *mr  = (headland_ht113_mr_t *) priv;
    headland_ht113_t    *dev = mr->owner;

    if (mr->valid && mr->enabled && mr->write_protected)
        return;

    addr = ht113_get_addr(dev, addr, mr);
    if ((addr + 3) < ((uint32_t) mem_size << 10))
        *(uint32_t *) &ram[addr] = val;
}

static void
ht113_ems_disable(headland_ht113_t *dev, uint8_t mar, uint32_t base_addr, uint8_t index)
{
    if (base_addr < ((uint32_t) mem_size << 10))
        mem_mapping_set_exec(&dev->ems_mapping[mar & 0x3f], ram + base_addr);
    else
        mem_mapping_set_exec(&dev->ems_mapping[mar & 0x3f], NULL);

    mem_mapping_disable(&dev->ems_mapping[mar & 0x3f]);

    if (index < 24) {
        mem_set_mem_state(base_addr, 0x4000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
        mem_mapping_enable(&dev->upper_mapping[index]);
    } else {
        mem_set_mem_state(base_addr, 0x4000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
    }
}

static void
ht113_ems_update(headland_ht113_t *dev, uint8_t mar)
{
    headland_ht113_mr_t *mr = &dev->mr[mar & 0x3f];
    uint32_t base_addr;
    uint32_t virt_addr;
    uint8_t  index = mar & 0x1f;

    base_addr = (index + 16) << 14;
    if (index >= 24)
        base_addr += 0x20000;

    ht113_ems_disable(dev, mar, base_addr, index);
    mr->enabled   = 0;
    mr->virt_base = base_addr;

    if ((dev->cr[0] & 0x02) &&
        ((dev->cr[0] & 0x01) == ((mar & 0x20) >> 5)) &&
        (mr->value & 0x0200)) {
        mem_set_mem_state(base_addr, 0x4000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
        virt_addr     = ht113_get_addr(dev, base_addr, mr);
        mr->enabled   = 1;
        mr->virt_base = virt_addr;

        if (index < 24)
            mem_mapping_disable(&dev->upper_mapping[index]);

        mem_mapping_set_exec(&dev->ems_mapping[mar & 0x3f],
                             (virt_addr < ((uint32_t) mem_size << 10)) ? ram + virt_addr : NULL);
        mem_mapping_enable(&dev->ems_mapping[mar & 0x3f]);
    }
}

static void
ht113_ems_update_all(headland_ht113_t *dev)
{
    uint8_t active_context = (dev->cr[0] & 0x01) << 5;

    for (uint8_t i = 0; i < 32; i++) {
        ht113_ems_update(dev, i | (active_context ^ 0x20));
        ht113_ems_update(dev, i | active_context);
    }
}

static void
ht113_memmap_default(headland_ht113_t *dev)
{
    mem_mapping_disable(&dev->mid_mapping);

    mem_set_mem_state(0x0e0000, 0x10000, MEM_READ_ROMCS | MEM_WRITE_ROMCS);
    mem_set_mem_state(0x0f0000, 0x10000, MEM_READ_ROMCS | MEM_WRITE_ROMCS);
    mem_set_mem_state(0xfe0000, 0x20000, MEM_READ_ROMCS | MEM_WRITE_ROMCS);

    mem_mapping_disable(&dev->shadow_mapping[0]);
    mem_mapping_disable(&dev->shadow_mapping[1]);
}

static void
ht113_memmap_update(headland_ht113_t *dev)
{
    uint8_t  effective_cr0 = dev->cr[0];
    uint32_t addr;

    /* Family documentation requires the extra 384 KiB to be disabled before
       E/F shadow can take ownership of its backing pages. */
    if (!(dev->cr[0] & 0x04))
        effective_cr0 &= ~0x18;

    for (uint8_t i = 0; i < 24; i++) {
        addr = ht113_get_addr(dev, 0x40000 + (i << 14), NULL);
        mem_mapping_set_exec(&dev->upper_mapping[i],
                             (addr < ((uint32_t) mem_size << 10)) ? ram + addr : NULL);
    }

    ht113_memmap_default(dev);

    if (mem_size > 640) {
        if (effective_cr0 & 0x04) {
            mem_mapping_set_addr(&dev->mid_mapping, 0x0a0000, 0x40000);
            mem_mapping_set_exec(&dev->mid_mapping, ram + 0x0a0000);
            mem_mapping_disable(&dev->mid_mapping);

            if (mem_size > 1024) {
                mem_set_mem_state((uint32_t) mem_size << 10, 0x60000,
                                  MEM_READ_EXTANY | MEM_WRITE_EXTANY);
                mem_mapping_set_addr(&dev->high_mapping, 0x100000,
                                     (mem_size - 1024) << 10);
                mem_mapping_set_exec(&dev->high_mapping, ram + 0x100000);
            } else {
                mem_set_mem_state(0x100000, (mem_size - 640) << 10,
                                  MEM_READ_EXTANY | MEM_WRITE_EXTANY);
            }
        } else {
            mem_mapping_set_addr(&dev->mid_mapping, 0x100000,
                                 (mem_size > 1024) ? 0x60000 : (mem_size - 640) << 10);
            mem_mapping_set_exec(&dev->mid_mapping, ram + 0x0a0000);

            if (mem_size > 1024) {
                mem_set_mem_state((uint32_t) mem_size << 10, 0x60000,
                                  MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
                mem_mapping_set_addr(&dev->high_mapping, 0x160000,
                                     (mem_size - 1024) << 10);
                mem_mapping_set_exec(&dev->high_mapping, ram + 0x100000);
            } else {
                mem_set_mem_state(0x100000, (mem_size - 640) << 10,
                                  MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
            }
        }
    }

    switch (effective_cr0 & 0x18) {
        case 0x18:
            if (((uint32_t) mem_size << 10) > 0x0e0000) {
                mem_set_mem_state(0x0e0000, 0x20000, MEM_READ_INTERNAL | MEM_WRITE_DISABLED);
                mem_set_mem_state(0xfe0000, 0x20000, MEM_READ_INTERNAL | MEM_WRITE_DISABLED);
                mem_mapping_set_addr(&dev->shadow_mapping[0], 0x0e0000, 0x20000);
                mem_mapping_set_exec(&dev->shadow_mapping[0], ram + 0x0e0000);
                mem_mapping_set_addr(&dev->shadow_mapping[1], 0xfe0000, 0x20000);
                mem_mapping_set_exec(&dev->shadow_mapping[1], ram + 0x0e0000);
            }
            break;

        case 0x10:
            if (((uint32_t) mem_size << 10) > 0x0f0000) {
                mem_set_mem_state(0x0f0000, 0x10000, MEM_READ_INTERNAL | MEM_WRITE_DISABLED);
                mem_set_mem_state(0xff0000, 0x10000, MEM_READ_INTERNAL | MEM_WRITE_DISABLED);
                mem_mapping_set_addr(&dev->shadow_mapping[0], 0x0f0000, 0x10000);
                mem_mapping_set_exec(&dev->shadow_mapping[0], ram + 0x0f0000);
                mem_mapping_set_addr(&dev->shadow_mapping[1], 0xff0000, 0x10000);
                mem_mapping_set_exec(&dev->shadow_mapping[1], ram + 0x0f0000);
            }
            break;

        case 0x08:
            if (((uint32_t) mem_size << 10) > 0x0e0000) {
                mem_set_mem_state(0x0e0000, 0x10000, MEM_READ_INTERNAL | MEM_WRITE_DISABLED);
                mem_set_mem_state(0xfe0000, 0x10000, MEM_READ_INTERNAL | MEM_WRITE_DISABLED);
                mem_mapping_set_addr(&dev->shadow_mapping[0], 0x0e0000, 0x10000);
                mem_mapping_set_exec(&dev->shadow_mapping[0], ram + 0x0e0000);
                mem_mapping_set_addr(&dev->shadow_mapping[1], 0xfe0000, 0x10000);
                mem_mapping_set_exec(&dev->shadow_mapping[1], ram + 0x0e0000);
            }
            break;

        default:
            break;
    }

    ht113_ems_update_all(dev);
    flushmmucache();

    ht113_log("HT113: CR0=%02X CR1=%02X CR2=%02X CR3=%02X CR4=%02X\n",
              dev->cr[0], dev->cr[1], dev->cr[2], dev->cr[3], dev->cr[4]);
}

static void
ht113_advance_mar(headland_ht113_t *dev)
{
    uint8_t next;

    if (!(dev->mar & 0x80))
        return;

    /* HT21 assigns D6 to page write-protect, so only CTX:PAGE (D5-D0)
       advances.  The 64th access clears AUTO and returns MAR to zero. */
    next = (dev->mar & 0x3f) + 1;
    if (next & 0x40)
        dev->mar = 0x00;
    else
        dev->mar = (dev->mar & 0xc0) | next;
}

static void
ht113_program_mr(headland_ht113_t *dev, uint16_t value)
{
    headland_ht113_mr_t *mr = &dev->mr[dev->mar & 0x3f];

    mr->value           = value & 0x03ff;
    mr->write_protected = !!(dev->mar & 0x40);
    ht113_ems_update(dev, dev->mar & 0x3f);
    ht113_advance_mar(dev);
    flushmmucache();
}

static void
ht113_write_cr(headland_ht113_t *dev, uint8_t value)
{
    uint8_t index = dev->cri & 0x07;
    uint8_t mem_index;

    if (index > 4)
        return;

    mem_index = ht113_memory_index();
    switch (index) {
        case 0:
            dev->cr[0] = (value & 0x1f) | ht113_mem_conf_cr0[mem_index];
            break;
        case 1:
            dev->cr[1] = (value & 0xbf) | ht113_mem_conf_cr1[mem_index];
            break;
        case 4:
            /* HT21 documents D3-D1 only.  Do not synthesize an HT18
               revision nibble or accept the reserved D0. */
            dev->cr[4] = value & 0x0e;
            break;
        default:
            dev->cr[index] = value;
            break;
    }

    ht113_memmap_update(dev);
}

static uint8_t
ht113_read_cr(headland_ht113_t *dev)
{
    uint8_t index = dev->cri & 0x07;

    return (index <= 4) ? dev->cr[index] : 0xff;
}

static void
ht113_write(uint16_t addr, uint8_t value, void *priv)
{
    headland_ht113_t *dev = (headland_ht113_t *) priv;

    ht113_log("HT113: out %04X,%02X\n", addr, value);

    switch (addr) {
        case 0x01ec: {
            headland_ht113_mr_t *mr = &dev->mr[dev->mar & 0x3f];
            ht113_program_mr(dev, (mr->value & 0x0300) | value);
            break;
        }
        case 0x01ed:
            dev->cri = value & 0x07;
            break;
        case 0x01ee:
            dev->mar = value;
            break;
        case 0x01ef:
            ht113_write_cr(dev, value);
            break;
        default:
            break;
    }
}

static void
ht113_writew(uint16_t addr, uint16_t value, void *priv)
{
    headland_ht113_t *dev = (headland_ht113_t *) priv;

    if (addr == 0x01ec) {
        ht113_log("HT113: outw 01EC,%04X\n", value);
        ht113_program_mr(dev, value);
    }
}

static void
ht113_writel(uint16_t addr, uint32_t value, void *priv)
{
    if (addr == 0x01ec)
        ht113_writew(addr, (uint16_t) value, priv);
}

static uint8_t
ht113_read(uint16_t addr, void *priv)
{
    headland_ht113_t *dev = (headland_ht113_t *) priv;
    uint8_t ret = 0xff;

    switch (addr) {
        case 0x01ec:
            ret = (uint8_t) dev->mr[dev->mar & 0x3f].value;
            ht113_advance_mar(dev);
            break;
        case 0x01ed:
            ret = dev->cri & 0x07;
            break;
        case 0x01ee:
            ret = dev->mar;
            break;
        case 0x01ef:
            ret = ht113_read_cr(dev);
            break;
        default:
            break;
    }

    ht113_log("HT113: in %04X -> %02X\n", addr, ret);
    return ret;
}

static uint16_t
ht113_readw(uint16_t addr, void *priv)
{
    headland_ht113_t *dev = (headland_ht113_t *) priv;
    uint16_t ret = 0xffff;

    if (addr == 0x01ec) {
        ret = dev->mr[dev->mar & 0x3f].value & 0x03ff;
        ht113_advance_mar(dev);
    }

    ht113_log("HT113: inw %04X -> %04X\n", addr, ret);
    return ret;
}

static uint32_t
ht113_readl(uint16_t addr, void *priv)
{
    return (addr == 0x01ec) ? (0xffff0000U | ht113_readw(addr, priv)) : 0xffffffffU;
}

static void
headland_ht113_close(void *priv)
{
    headland_ht113_t *dev = (headland_ht113_t *) priv;

    if (dev == NULL)
        return;

    io_removehandler(0x01ec, 4,
                     ht113_read, ht113_readw, ht113_readl,
                     ht113_write, ht113_writew, ht113_writel, dev);

    if (dev->board != NULL) {
        dev->board->ht113 = NULL;
        headland_pcs386sx_release(dev->board);
    }
    free(dev);
}

static void *
headland_ht113_init(UNUSED(const device_t *info))
{
    headland_pcs386sx_t *board = (headland_pcs386sx_t *) device_get_common_priv();
    headland_ht113_t    *dev;
    uint8_t              mem_index;

    if (board == NULL)
        return NULL;

    dev = (headland_ht113_t *) calloc(1, sizeof(headland_ht113_t));
    if (dev == NULL)
        return NULL;

    dev->board    = board;
    board->ht113  = dev;
    headland_pcs386sx_retain(board);
    mem_index     = ht113_memory_index();
    dev->cr[0]    = ht113_mem_conf_cr0[mem_index];
    dev->cr[1]    = ht113_mem_conf_cr1[mem_index];
    dev->cr[2]    = 0x00;
    dev->cr[3]    = 0x00;
    dev->cr[4]    = 0x00;
    dev->cri      = 0x00;
    dev->mar      = 0x00;

    dev->null_mr.valid = 0;
    dev->null_mr.owner = dev;

    for (uint8_t i = 0; i < 64; i++) {
        dev->mr[i].valid           = 1;
        dev->mr[i].value           = 0;
        dev->mr[i].write_protected = 0;
        dev->mr[i].owner           = dev;
    }

    io_sethandler(0x01ec, 4,
                  ht113_read, ht113_readw, ht113_readl,
                  ht113_write, ht113_writew, ht113_writel, dev);

    /* HT113 is the only owner of these regions. */
    mem_mapping_disable(&ram_low_mapping);
    mem_mapping_disable(&ram_mid_mapping);
    mem_mapping_disable(&ram_high_mapping);

    mem_mapping_add(&dev->low_mapping, 0x000000, 0x40000,
                    ht113_mem_read_b, ht113_mem_read_w, ht113_mem_read_l,
                    ht113_mem_write_b, ht113_mem_write_w, ht113_mem_write_l,
                    ram, MEM_MAPPING_INTERNAL, &dev->null_mr);

    mem_mapping_add(&dev->mid_mapping, 0x0a0000, 0x60000,
                    ht113_mem_read_b, ht113_mem_read_w, ht113_mem_read_l,
                    ht113_mem_write_b, ht113_mem_write_w, ht113_mem_write_l,
                    ram + 0x0a0000, MEM_MAPPING_INTERNAL, &dev->null_mr);
    mem_mapping_disable(&dev->mid_mapping);

    if (mem_size > 1024) {
        mem_mapping_add(&dev->high_mapping, 0x100000, (mem_size - 1024) << 10,
                        ht113_mem_read_b, ht113_mem_read_w, ht113_mem_read_l,
                        ht113_mem_write_b, ht113_mem_write_w, ht113_mem_write_l,
                        ram + 0x100000, MEM_MAPPING_INTERNAL, &dev->null_mr);
        mem_mapping_enable(&dev->high_mapping);
    }

    for (uint8_t i = 0; i < 24; i++) {
        uint32_t addr = 0x40000 + (i << 14);

        mem_mapping_add(&dev->upper_mapping[i], addr, 0x4000,
                        ht113_mem_read_b, ht113_mem_read_w, ht113_mem_read_l,
                        ht113_mem_write_b, ht113_mem_write_w, ht113_mem_write_l,
                        (addr < ((uint32_t) mem_size << 10)) ? ram + addr : NULL,
                        MEM_MAPPING_INTERNAL, &dev->null_mr);
        mem_mapping_enable(&dev->upper_mapping[i]);
    }

    mem_mapping_add(&dev->shadow_mapping[0], 0x0e0000, 0x20000,
                    ht113_mem_read_b, ht113_mem_read_w, ht113_mem_read_l,
                    ht113_mem_write_b, ht113_mem_write_w, ht113_mem_write_l,
                    (((uint32_t) mem_size << 10) > 0x0e0000) ? ram + 0x0e0000 : NULL,
                    MEM_MAPPING_INTERNAL, &dev->null_mr);
    mem_mapping_disable(&dev->shadow_mapping[0]);

    mem_mapping_add(&dev->shadow_mapping[1], 0xfe0000, 0x20000,
                    ht113_mem_read_b, ht113_mem_read_w, ht113_mem_read_l,
                    ht113_mem_write_b, ht113_mem_write_w, ht113_mem_write_l,
                    (((uint32_t) mem_size << 10) > 0x0e0000) ? ram + 0x0e0000 : NULL,
                    MEM_MAPPING_INTERNAL, &dev->null_mr);
    mem_mapping_disable(&dev->shadow_mapping[1]);

    for (uint8_t i = 0; i < 64; i++) {
        uint8_t index = i & 0x1f;
        uint32_t addr = (index + 16) << 14;

        if (index >= 24)
            addr += 0x20000;

        mem_mapping_add(&dev->ems_mapping[i], addr, 0x4000,
                        ht113_mem_read_b, ht113_mem_read_w, ht113_mem_read_l,
                        ht113_mem_write_b, ht113_mem_write_w, ht113_mem_write_l,
                        (addr < ((uint32_t) mem_size << 10)) ? ram + addr : NULL,
                        MEM_MAPPING_INTERNAL, &dev->mr[i]);
        mem_mapping_disable(&dev->ems_mapping[i]);
    }

    ht113_memmap_update(dev);
    return dev;
}

const device_t headland_ht113_device = {
    .name          = "Headland HT113 Memory Controller",
    .internal_name = "headland_ht113",
    .flags         = 0,
    .local         = 0,
    .init          = headland_ht113_init,
    .close         = headland_ht113_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
