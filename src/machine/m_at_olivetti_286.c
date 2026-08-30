/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the Olivetti PCS 286.
 *
 * Authors: <rauto>
 *
 *          Copyright 2026 <rauto>.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/plat_unused.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/chipset.h>
#include <86box/keyboard.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/hdc.h>
#include <86box/nvr.h>
#include <86box/port_6x.h>
#include <86box/sio.h>
#include <86box/serial.h>
#include <86box/video.h>
#include <86box/machine.h>

/* Enable only for directed TACT/OLIMCU firmware research. */
#define OLIMCU_LOG(...) do { } while (0)
#define OLIMCU_MEM_LOG(...) do { } while (0)

static const device_config_t olivetti_pcs286_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "v142",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "BIOS / Resident Diagnostics 1.37 (11/20/89; archive label 1.34)",
                .internal_name = "v137",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/olivetti_pcs286/olivetti_pcs286_bios_v137.bin", "" }
            },
            {
                .name          = "BIOS / Resident Diagnostics 1.42 (05/03/90)",
                .internal_name = "v142",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 2,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/olivetti_pcs286/olivetti_pcs286_bios_v142_low.bin",
                                   "roms/machines/olivetti_pcs286/olivetti_pcs286_bios_v142_high.bin", "" }
            },
            { .files_no = 0 }
        },
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t olivetti_pcs286_device = {
    .name          = "Olivetti PCS 286",
    .internal_name = "olivetti_pcs286",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = olivetti_pcs286_config
};

/* Triumph-Adler sold the same motherboard as the Dario 286/P35.  Keep a
   distinct device identity so its BIOS selection and saved configuration are
   presented under the historical brand while sharing the verified ROM set. */
const device_t ta_dario286_device = {
    .name          = "Triumph-Adler Dario 286 (P35)",
    .internal_name = "ta_dario286",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = olivetti_pcs286_config
};

static const device_config_t olivetti_pcs286s_ti_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "v206",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "BIOS / Resident Diagnostics 2.06 (05/20/91)",
                .internal_name = "v206",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 2,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/olivetti_pcs286s/pcs286s_v206_lo.bin",
                                   "roms/machines/olivetti_pcs286s/pcs286s_v206_hi.bin", "" }
            },
            { .files_no = 0 }
        },
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t olivetti_pcs286s_ti_device = {
    .name          = "Olivetti PCS 286/S (TI/OLIMCU16)",
    .internal_name = "olivetti_pcs286s_ti",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = olivetti_pcs286s_ti_config
};

/* Memory remap: 0x60000-0x7FFFF is aliased to 0x80000-0x9FFFF.
   The Phoenix v1.42 POST test 6 (MEMORY CONTROLLER) writes the 13-byte
   string "RAM-REMAPPING" at 0x60000 and verifies the same bytes are
   readable at 0x80000.  The Headland GC101A has this remap feature
   controlled by KBC P2 bit 2, but 86Box does not implement that side
   effect.  We add a static alias mapping instead.

   IMPORTANT: mem_read_ram() and mem_write_ram() ignore their `priv`
   pointer completely — they always access ram[addr] directly.  Using
   them here would make reads/writes land at 0x60000, not 0x80000.
   We therefore supply custom handlers that translate addr→addr+0x20000
   and call addreadlookup/addwritelookup so the x86 fast-path cache
   is also pointed at the correct physical page. */
static mem_mapping_t olivetti_pcs286_remap_mapping;

/* OLIMCU16/TI memory paging interface used by the PCS 286S BIOS.

   BIOS 2.06 scans the standard EMS base candidates 0208h, 0218h, 0258h,
   0268h, 02A8h, 02B8h and 02E8h by writing A5h and selecting the first
   port that does not echo it.  It then uses four page/control pairs at
   base + 0000h/+4000h/+8000h/+C000h.  The previous two-byte echo latch
   made 0208h appear occupied and caused the BIOS to select 0218h.

   The POST uses the first window as an EEMS-style low-memory mapper while
   determining installed RAM.  The observed register pairs select offsets
   000000h, 080000h, 100000h, 200000h and 400000h and map them over the
   corresponding 16 KiB logical window.  Addresses beyond populated RAM
   wrap at the installed-memory boundary, reproducing the alias test used
   by the firmware.  This is intentionally limited to the BIOS-observed
   protocol; shadow and the final DOS EMS/XMS allocation policy remain
   separate work. */
#define OLIMCU_PAGE_SIZE 0x4000
#define OLIMCU_WINDOWS   4

typedef struct olivetti_pcs286s_olimcu_t olivetti_pcs286s_olimcu_t;

typedef struct olivetti_pcs286s_olimcu_window_t {
    olivetti_pcs286s_olimcu_t *dev;
    mem_mapping_t              mapping;
    uint32_t                   logical;
    uint32_t                   source;
    uint8_t                    page;
    uint8_t                    control;
} olivetti_pcs286s_olimcu_window_t;

typedef struct olivetti_pcs286s_olimcu_t {
    uint16_t                            base;
    uint16_t                            probe_base;
    uint8_t                             selected;
    uint8_t                             probe_seen;
    uint8_t                             reg_index;
    uint8_t                             unlock_state;
    uint8_t                             regs_unlocked;
    uint8_t                             regs[256];
    uint8_t                             bank;
    uint32_t                            bank_base[OLIMCU_WINDOWS];
    uint32_t                            bank_size[OLIMCU_WINDOWS];
    mem_mapping_t                       regs_mapping;
    olivetti_pcs286s_olimcu_window_t    window[OLIMCU_WINDOWS];
} olivetti_pcs286s_olimcu_t;

static olivetti_pcs286s_olimcu_t olivetti_pcs286s_olimcu;

static uint32_t
olivetti_pcs286s_olimcu_phys(const olivetti_pcs286s_olimcu_window_t *window, uint32_t addr)
{
    const olivetti_pcs286s_olimcu_t *dev  = window->dev;
    uint32_t                         bank = dev->bank & 3;
    uint32_t                         size = dev->bank_size[bank];

    if (!size)
        return 0;

    return dev->bank_base[bank] +
           ((window->source + (addr - window->logical)) % size);
}

static uint8_t
olivetti_pcs286s_olimcu_mem_read8(uint32_t addr, void *priv)
{
    const olivetti_pcs286s_olimcu_window_t *window = (olivetti_pcs286s_olimcu_window_t *) priv;
    uint32_t                                phys   = olivetti_pcs286s_olimcu_phys(window, addr);

    if ((addr - window->logical) < 12)
        OLIMCU_MEM_LOG("[OLIMCU-MEM] R bank=%u slot=%u logical=%05X phys=%06X value=%02X\n",
                       window->dev->bank & 3, (unsigned) (window - window->dev->window),
                       addr, phys, ram[phys]);
    addreadlookup(mem_logical_addr, phys);
    return ram[phys];
}

static uint16_t
olivetti_pcs286s_olimcu_mem_read16(uint32_t addr, void *priv)
{
    uint16_t ret = olivetti_pcs286s_olimcu_mem_read8(addr, priv);
    ret |= (uint16_t) olivetti_pcs286s_olimcu_mem_read8(addr + 1, priv) << 8;
    return ret;
}

static uint32_t
olivetti_pcs286s_olimcu_mem_read32(uint32_t addr, void *priv)
{
    uint32_t ret = olivetti_pcs286s_olimcu_mem_read16(addr, priv);
    ret |= (uint32_t) olivetti_pcs286s_olimcu_mem_read16(addr + 2, priv) << 16;
    return ret;
}

static void
olivetti_pcs286s_olimcu_mem_write8(uint32_t addr, uint8_t val, void *priv)
{
    const olivetti_pcs286s_olimcu_window_t *window = (olivetti_pcs286s_olimcu_window_t *) priv;
    uint32_t                                phys   = olivetti_pcs286s_olimcu_phys(window, addr);

    if ((addr - window->logical) < 12)
        OLIMCU_MEM_LOG("[OLIMCU-MEM] W bank=%u slot=%u logical=%05X phys=%06X value=%02X\n",
                       window->dev->bank & 3, (unsigned) (window - window->dev->window),
                       addr, phys, val);
    addwritelookup(mem_logical_addr, phys);
    mem_write_ramb_page(phys, val, &pages[phys >> 12]);
}

static void
olivetti_pcs286s_olimcu_mem_write16(uint32_t addr, uint16_t val, void *priv)
{
    olivetti_pcs286s_olimcu_mem_write8(addr, val, priv);
    olivetti_pcs286s_olimcu_mem_write8(addr + 1, val >> 8, priv);
}

static void
olivetti_pcs286s_olimcu_mem_write32(uint32_t addr, uint32_t val, void *priv)
{
    olivetti_pcs286s_olimcu_mem_write16(addr, val, priv);
    olivetti_pcs286s_olimcu_mem_write16(addr + 2, val >> 16, priv);
}

static void
olivetti_pcs286s_olimcu_update(olivetti_pcs286s_olimcu_window_t *window)
{
    olivetti_pcs286s_olimcu_t *dev  = window->dev;
    uint32_t                  page = window->page & 0x7f;
    uint32_t                  bank = dev->bank & 3;

    if (!(window->page & 0x80) || !dev->bank_size[bank]) {
        OLIMCU_LOG("[OLIMCU-MAP] disable bank=%u slot=%u page=%02X ctl=%02X bank_size=%X\n",
                   bank, (unsigned) (window - dev->window), window->page, window->control,
                   dev->bank_size[bank]);
        mem_mapping_disable(&window->mapping);
        flushmmucache();
        return;
    }

    /* BIOS 2.06 uses these extended encodings while looking for aliases at
       2 MiB and 4 MiB.  Values below 2 MiB use the ordinary seven-bit
       16-KiB page number in the mapping register. */
    if ((page == 0x00) && (window->control == 0x21))
        window->source = 0x00200000;
    else if ((page == 0x00) && (window->control == 0x34))
        window->source = 0x00400000;
    else
        window->source = page * OLIMCU_PAGE_SIZE;

    OLIMCU_LOG("[OLIMCU-MAP] enable bank=%u slot=%u logical=%05X page=%02X ctl=%02X source=%06X bank_base=%06X bank_size=%X\n",
               bank, (unsigned) (window - dev->window), window->logical, window->page,
               window->control, window->source, dev->bank_base[bank],
               dev->bank_size[bank]);
    mem_mapping_enable(&window->mapping);
    flushmmucache();
}

static void
olivetti_pcs286s_olimcu_update_all(olivetti_pcs286s_olimcu_t *dev)
{
    for (unsigned slot = 0; slot < OLIMCU_WINDOWS; slot++)
        olivetti_pcs286s_olimcu_update(&dev->window[slot]);
}

/* BIOS 2.06 accesses the TACT/OLIMCU configuration through three bytes at
   the very top of the system-ROM window.  FFFECh selects a register and
   FFFEAh transfers its data; FFFEEh is the configuration lock/status latch.
   Register 10h selects one of four physical memory banks and register 1Bh
   reports bit 2 when that bank is populated. */
static uint8_t
olivetti_pcs286s_olimcu_reg_read(uint32_t addr, void *priv)
{
    olivetti_pcs286s_olimcu_t *dev = (olivetti_pcs286s_olimcu_t *) priv;

    /* OLIMCU16 only decodes the register triplet after the AA/55 unlock
       sequence.  Once FF locks it again these addresses are ordinary ROM;
       this is essential because the POST subsequently checksums the whole
       system BIOS, including FFFEAh/FFFECh/FFFEEh. */
    if (!dev->regs_unlocked)
        return bios_read(addr, NULL);

    switch (addr) {
        case 0x000fffea:
            if (dev->reg_index == 0x1b) {
                uint8_t val = dev->bank_size[dev->bank & 3] ? 0x04 : 0x00;
                OLIMCU_LOG("[OLIMCU-REG] R data index=%02X bank=%u -> %02X\n",
                           dev->reg_index, dev->bank & 3, val);
                return val;
            }
            OLIMCU_LOG("[OLIMCU-REG] R data index=%02X bank=%u -> %02X\n",
                       dev->reg_index, dev->bank & 3, dev->regs[dev->reg_index]);
            return dev->regs[dev->reg_index];
        case 0x000fffec:
            OLIMCU_LOG("[OLIMCU-REG] R index -> %02X\n", dev->reg_index);
            return dev->reg_index;
        case 0x000fffee:
            OLIMCU_LOG("[OLIMCU-REG] R lock -> %02X\n", dev->regs[0xff]);
            return dev->regs[0xff];
        default:
            return bios_read(addr, NULL);
    }
}

static uint16_t
olivetti_pcs286s_olimcu_reg_read16(uint32_t addr, void *priv)
{
    uint16_t ret = olivetti_pcs286s_olimcu_reg_read(addr, priv);
    ret |= (uint16_t) olivetti_pcs286s_olimcu_reg_read(addr + 1, priv) << 8;
    return ret;
}

static uint32_t
olivetti_pcs286s_olimcu_reg_read32(uint32_t addr, void *priv)
{
    uint32_t ret = olivetti_pcs286s_olimcu_reg_read16(addr, priv);
    ret |= (uint32_t) olivetti_pcs286s_olimcu_reg_read16(addr + 2, priv) << 16;
    return ret;
}

static void
olivetti_pcs286s_olimcu_reg_write(uint32_t addr, uint8_t val, void *priv)
{
    olivetti_pcs286s_olimcu_t *dev = (olivetti_pcs286s_olimcu_t *) priv;

    if (addr == 0x000fffee) {
        OLIMCU_LOG("[OLIMCU-REG] W lock=%02X\n", val);

        if (val == 0xaa) {
            dev->unlock_state = 1;
        } else if ((val == 0x55) && (dev->unlock_state == 1)) {
            dev->unlock_state = 0;
            dev->regs_unlocked = 1;
        } else {
            dev->unlock_state = 0;
            if (val == 0xff)
                dev->regs_unlocked = 0;
        }
        return;
    }

    if (!dev->regs_unlocked)
        return;

    switch (addr) {
        case 0x000fffea:
            OLIMCU_LOG("[OLIMCU-REG] W data index=%02X bank=%u value=%02X\n",
                       dev->reg_index, dev->bank & 3, val);
            dev->regs[dev->reg_index] = val;
            if (dev->reg_index == 0x10) {
                dev->bank = val & 3;
                olivetti_pcs286s_olimcu_update_all(dev);
            }
            break;
        case 0x000fffec:
            OLIMCU_LOG("[OLIMCU-REG] W index=%02X\n", val);
            dev->reg_index = val;
            break;
        default:
            break;
    }
}

static void
olivetti_pcs286s_olimcu_reg_write16(uint32_t addr, uint16_t val, void *priv)
{
    olivetti_pcs286s_olimcu_reg_write(addr, val, priv);
    olivetti_pcs286s_olimcu_reg_write(addr + 1, val >> 8, priv);
}

static void
olivetti_pcs286s_olimcu_reg_write32(uint32_t addr, uint32_t val, void *priv)
{
    olivetti_pcs286s_olimcu_reg_write16(addr, val, priv);
    olivetti_pcs286s_olimcu_reg_write16(addr + 2, val >> 16, priv);
}

static void
olivetti_pcs286s_olimcu_banks_init(olivetti_pcs286s_olimcu_t *dev)
{
    uint32_t remaining = mem_size << 10;
    uint32_t base      = 0;

    /* BIOS 2.06 sizes each bank by comparing aliases at 512 KiB, 1 MiB and
       2 MiB.  A 4 MiB monolithic bank never aliases during that sequence and
       is rejected, causing the POST to restart.  Therefore each selectable
       logical bank is capped at 2 MiB.  On the photographed 16-bit board each
       logical bank is a pair of 1 MiB SIMMs, so four modules are represented
       as two populated 2 MiB banks. */
    for (unsigned bank = 0; bank < OLIMCU_WINDOWS; bank++) {
        uint32_t size = 0;

        if (remaining >= 0x200000)
            size = 0x200000;
        else if (remaining >= 0x100000)
            size = 0x100000;
        else if (remaining >= 0x080000)
            size = 0x080000;

        dev->bank_base[bank] = base;
        dev->bank_size[bank] = size;
        base                 += size;
        remaining            -= size;
        OLIMCU_LOG("[OLIMCU-BANK] bank=%u base=%06X size=%X remaining=%X\n",
                   bank, dev->bank_base[bank], dev->bank_size[bank], remaining);
    }
}

static uint8_t
olivetti_pcs286s_olimcu_read(uint16_t port, void *priv)
{
    olivetti_pcs286s_olimcu_t *dev = (olivetti_pcs286s_olimcu_t *) priv;
    uint16_t                    base = port & (OLIMCU_PAGE_SIZE - 1);
    unsigned                    slot = (port >> 14) & 3;

    if (!dev->selected || (base & 0xfffe) != dev->base)
        return 0xff;

    return (port & 1) ? 0xff : dev->window[slot].page;
}

static void
olivetti_pcs286s_olimcu_write(uint16_t port, uint8_t val, void *priv)
{
    olivetti_pcs286s_olimcu_t *dev = (olivetti_pcs286s_olimcu_t *) priv;
    uint16_t                    base = (port & (OLIMCU_PAGE_SIZE - 1)) & 0xfffe;
    unsigned                    slot = (port >> 14) & 3;

    OLIMCU_LOG("[OLIMCU-IO] W port=%04X base=%04X slot=%u value=%02X selected=%u bank=%u\n",
               port, base, slot, val, dev->selected, dev->bank & 3);

    /* The A5h transaction is a search for a free EMS base.  BIOS 2.06 runs
       this negotiation twice: the first pass uses the power-on base 0208h
       while sizing RAM, then the final EMS setup sees 0208h occupied and
       relocates the interface to 0218h.  Remember probes at inactive bases
       so that the following high-window write can perform that relocation.
       A probe of the currently active base must still echo A5h. */
    if ((slot == 0) && !(port & 1) && (val == 0xa5)) {
        if (dev->selected && (base == dev->base)) {
            dev->window[slot].page = val;
            olivetti_pcs286s_olimcu_update(&dev->window[slot]);
        } else {
            dev->probe_base = base;
            dev->probe_seen = 1;
        }
        return;
    }

    if (dev->probe_seen && (base == dev->probe_base) && (slot != 0)) {
        OLIMCU_LOG("[OLIMCU-BASE] relocate %04X -> %04X\n",
                   dev->selected ? dev->base : 0xffff, dev->probe_base);
        dev->base       = dev->probe_base;
        dev->selected   = 1;
        dev->probe_seen = 0;
    }

    if (!dev->selected) {
        if (!dev->probe_seen || (base != dev->probe_base))
            return;
        dev->base       = dev->probe_base;
        dev->selected   = 1;
        dev->probe_seen = 0;
    }

    if (base != dev->base)
        return;

    if (port & 1)
        dev->window[slot].control = val;
    else
        dev->window[slot].page = val;

    olivetti_pcs286s_olimcu_update(&dev->window[slot]);
}

static uint8_t
olivetti_remap_read8(uint32_t addr, UNUSED(void *priv))
{
    addreadlookup(mem_logical_addr, addr + 0x20000);
    return ram[addr + 0x20000];
}

static uint16_t
olivetti_remap_read16(uint32_t addr, UNUSED(void *priv))
{
    addreadlookup(mem_logical_addr, addr + 0x20000);
    return *(uint16_t *)&ram[addr + 0x20000];
}

static uint32_t
olivetti_remap_read32(uint32_t addr, UNUSED(void *priv))
{
    addreadlookup(mem_logical_addr, addr + 0x20000);
    return *(uint32_t *)&ram[addr + 0x20000];
}

static void
olivetti_remap_write8(uint32_t addr, uint8_t val, UNUSED(void *priv))
{
    addwritelookup(mem_logical_addr, addr + 0x20000);
    mem_write_ramb_page(addr + 0x20000, val, &pages[(addr + 0x20000) >> 12]);
}

static void
olivetti_remap_write16(uint32_t addr, uint16_t val, UNUSED(void *priv))
{
    addwritelookup(mem_logical_addr, addr + 0x20000);
    mem_write_ramw_page(addr + 0x20000, val, &pages[(addr + 0x20000) >> 12]);
}

static void
olivetti_remap_write32(uint32_t addr, uint32_t val, UNUSED(void *priv))
{
    addwritelookup(mem_logical_addr, addr + 0x20000);
    mem_write_raml_page(addr + 0x20000, val, &pages[(addr + 0x20000) >> 12]);
}

int
machine_at_olivetti_pcs286_init(const machine_t *model)
{
    int         files_no;
    int         ret;
    const char *bios;
    const char *fn[2];

    /* The v1.42 system BIOS is 128KB byte-interleaved across two
       27C512 chips. The byte-interleaved
       layout is the way the original ROMs were dumped: byte 2i of
       the resulting 128KB image comes from the LOW chip, byte 2i+1
       from the HIGH chip. After 86Box's `bios_load_interleaved` runs,
       the in-memory image at 0xE0000-0xFFFFF matches what the
       original motherboard would present to the CPU. The Don
       Rizzelli archive.org dump ships both halves as
       "Olivetti_PCS_286_Version_1_42-LOW-3DF6.BIN" and
       "Olivetti_PCS_286_Version_1_42-HIGH-3DF6.BIN" (each 64KB, with
       the bytes pre-arranged for that exact interleave — at first
       glance each half looks like garbled VGA font data, but
       interleaving them produces the real v1.42 POST + SETUP
       including "Resident Diagnostics", "Parity Circuitry", "OLIVETTI
       05/03/90" etc.). The Don Rizzelli archive ALSO ships a
       separate 64KB "Video at E0000.ROM" and 64KB "BIOS at F0000.ROM".
       Despite those historical names, they are the sequential E0000
       and F0000 halves of the older Olivetti 1.37 system BIOS, not an
       independent video ROM. Do not replace v142_low/high with them.

       The older set, labelled 1.34 by its archive but identifying
       itself as BIOS and Resident Diagnostics 1.37, is supplied as
       a canonical 128KB combined image and is therefore loaded
       linearly. */
    device_context(model->device);
    bios     = device_get_config_bios("bios");
    files_no = device_get_bios_num_files(model->device, bios);
    fn[0]    = device_get_bios_file(model->device, bios, 0);

    if (files_no == 2) {
        fn[1] = device_get_bios_file(model->device, bios, 1);
        ret   = bios_load_interleaved(fn[0], fn[1], 0x000e0000, 131072, 0);
    } else
        ret = bios_load_linear(fn[0], 0x000e0000, 131072, 0);

    device_context_restore();

    if (bios_only || !ret)
        return ret;

    machine_at_common_ide_init(model);

    /* Top 384K of the 1MB address space is mapped to RAM. Mirrors
       the commit 37386e6 official driver. The Phoenix v1.42 expects
       this layout for shadowing. */
    mem_remap_top(384);

    /* Phoenix v1.42 diagnostic compare expects BDA equip word 0x410
     * to use the same encoding as NVR 0x14. The IBM AT encoding is:
     *   bit 5 = 80x25 color (1), bit 4 = 40x25 color (0)
     *   bit 0 = IPL (boot from FDD)
     *   bit 6 = 2 FDDs (0 = 1 FDD)
     *   bit 1 = FPU installed
     * 86Box's generic BDA init writes 0x10 (bit 4 set, which is
     * 40x25 color) without the IPL bit. The Olivetti has 80x25 color
     * and 1 FDD, so the correct BDA value is 0x20 (bit 5 = 80x25
     * color). Without this override Phoenix reads BDA 0x10, compares
     * against NVR 0x20, finds a "mismatch", and sets bit 5 of
     * CMOS 0x0E (Equipment Error), which keeps the SET-UP showing
     * on every boot.
     *
     * This is a ONE-SHOT write at machine init (not a per-poll
     * force — that was a hack and is now removed). 86Box's BDA
     * init runs during mem.c setup before this machine init, so
     * we overwrite the generic value with the Olivetti-specific
     * one. Phoenix will then read 0x20, find it matches NVR 0x14,
     * and skip the equipment-error flag. */
    extern uint8_t *ram;
    if (ram != NULL) {
        ram[0x0410] = 0x20;  /* equip: 80x25 color, 1 FDD, no FPU, no IPL */
        ram[0x0411] = 0x00;
    }

    /* The Olivetti-specific NVR factory defaults (IPL + 80x25 + 1 FDD,
     * 640K base, 384K ext, 0x0E clean) are set inside nvr_at.c when
     * it detects this machine, following the same FLAG_*_HACK pattern
     * as FLAG_SPITFIRE_HACK / FLAG_BX6_HACK. Together with the BDA
     * override above, this makes Phoenix v1.42's diagnostic compare
     * pass and skip the SET-UP on subsequent boots. */
    /* Olivetti IOC02 glue chip. My v0.2 fix forces bit 2 of reg 0x6A
       to 1 (I/O subsystem ready) so the Phoenix POST test 2 passes. */
    device_add(&olivetti_ioc02_device);

    /* Olivetti-specific port 0x61/0x62/0x63 handler (A20 gate). The
       generic port_6x_device is NOT added because machine_at_common_init
       skips it for this machine. */
    device_add(&port_6x_olivetti_device);

    /* AT-style PS/2 KBC with Olivetti firmware (vendor 0x0B). */
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    /* FDC AT is always present on the Olivetti PCS 286 motherboard. */
    device_add(&fdc_at_device);

    /* Headland GC101A + GC102 (rebranded G2) - same family as GC103.
       Provides memory mapping + EMS. Does NOT implement the
       0x60000-0x7FFFF ↔ 0x80000-0x9FFFF remap that the Phoenix v1.42
       POST test 6 expects — we add that mapping below. */
    device_add(&headland_gc10x_device);

    /* Memory remap 0x60000-0x7FFFF → 0x80000-0x9FFFF.
       The Phoenix POST test 6 writes 13 bytes ("RAM-REMAPPING") to
       0x60000 then reads them back from 0x80000 (REPE CMPSB).
       Custom handlers redirect every access to addr+0x20000 and
       prime the 86Box fast-path cache so subsequent bytes in the
       same REP MOVSB / REPE CMPSB also land at the right location.
       flushmmucache() invalidates any stale lookup entries that may
       have been established during earlier boot-time memory clears. */
    mem_mapping_add(&olivetti_pcs286_remap_mapping, 0x60000, 0x20000,
                    olivetti_remap_read8,  olivetti_remap_read16,  olivetti_remap_read32,
                    olivetti_remap_write8, olivetti_remap_write16, olivetti_remap_write32,
                    NULL, 0, NULL);
    mem_mapping_enable(&olivetti_pcs286_remap_mapping);
    flushmmucache();

    /* Onboard Paradise PVGA1A + IMS G176P-40 RAMDAC, 256KB VRAM.
       Video firmware is integrated in the 128KB motherboard BIOS; neither
       preserved revision contains a valid separate C000 option-ROM image. */
    if (gfxcard[0] == VID_INTERNAL)
        device_add(&paradise_pvga1a_pcs286_device);

    return ret;
}

/* TI-board PCS 286S.  The preserved 2.06 firmware and the physical board
   identify a TACT82300-family three-chip set, the Olivetti OLIMCU16 ASIC,
   PC87310 Super I/O and onboard Paradise VGA.  TACT/OLIMCU16 memory control
   is deliberately not substituted with Headland: until that ASIC is modeled
   this target is experimental, but it provides an honest firmware/peripheral
   integration point for POST tracing and incremental implementation. */
int
machine_at_olivetti_pcs286s_ti_init(const machine_t *model)
{
    int         ret;
    const char *bios;
    const char *fn[2];

    device_context(model->device);
    bios  = device_get_config_bios("bios");
    fn[0] = device_get_bios_file(model->device, bios, 0);
    fn[1] = device_get_bios_file(model->device, bios, 1);
    ret   = bios_load_interleaved(fn[0], fn[1], 0x000e0000, 131072, 0);
    device_context_restore();

    if (bios_only || !ret)
        return ret;

    machine_at_common_ide_init(model);

    /* Murdock's preservation branch established the board-level devices
       below.  PC87310 owns FDC, two UARTs and LPT and is configured through
       port 03F3h, which is also visible in the 2.06 reset path. */
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add(&pc87310_device);

    memset(&olivetti_pcs286s_olimcu, 0, sizeof(olivetti_pcs286s_olimcu));
    olivetti_pcs286s_olimcu_banks_init(&olivetti_pcs286s_olimcu);
    static const uint16_t olimcu_bases[] = {
        0x0208, 0x0218, 0x0258, 0x0268, 0x02a8, 0x02b8, 0x02e8
    };

    for (unsigned slot = 0; slot < OLIMCU_WINDOWS; slot++) {
        olivetti_pcs286s_olimcu_window_t *window = &olivetti_pcs286s_olimcu.window[slot];

        window->dev     = &olivetti_pcs286s_olimcu;
        window->logical = slot * OLIMCU_PAGE_SIZE;
        mem_mapping_add(&window->mapping, window->logical, OLIMCU_PAGE_SIZE,
                        olivetti_pcs286s_olimcu_mem_read8,
                        olivetti_pcs286s_olimcu_mem_read16,
                        olivetti_pcs286s_olimcu_mem_read32,
                        olivetti_pcs286s_olimcu_mem_write8,
                        olivetti_pcs286s_olimcu_mem_write16,
                        olivetti_pcs286s_olimcu_mem_write32,
                        NULL, MEM_MAPPING_INTERNAL, window);
        mem_mapping_disable(&window->mapping);

        for (unsigned base = 0; base < (sizeof(olimcu_bases) / sizeof(olimcu_bases[0])); base++)
            io_sethandler(olimcu_bases[base] + (slot * OLIMCU_PAGE_SIZE), 2,
                          olivetti_pcs286s_olimcu_read, NULL, NULL,
                          olivetti_pcs286s_olimcu_write, NULL, NULL,
                          &olivetti_pcs286s_olimcu);
    }

    /* The chipset register triplet is decoded inside the last 4 KiB BIOS
       page.  Memory mappings have 4 KiB granularity, so overlay the complete
       page, pass ordinary reads through to the BIOS ROM and intercept only
       FFFEAh/FFFECh/FFFEEh.  Marking the old five-byte mapping as internal RAM
       meant ROMCS accesses never reached it and bank 0 was tested four times. */
    mem_mapping_add(&olivetti_pcs286s_olimcu.regs_mapping,
                    0x000ff000, 0x1000,
                    olivetti_pcs286s_olimcu_reg_read,
                    olivetti_pcs286s_olimcu_reg_read16,
                    olivetti_pcs286s_olimcu_reg_read32,
                    olivetti_pcs286s_olimcu_reg_write,
                    olivetti_pcs286s_olimcu_reg_write16,
                    olivetti_pcs286s_olimcu_reg_write32,
                    NULL, MEM_MAPPING_EXTERNAL, &olivetti_pcs286s_olimcu);

    /* Temporary top-memory exposure remains until OLIMCU16 shadow and the
       final EMS/XMS split are understood.  Low-memory bank aliasing is now
       owned by the mapper above instead of the old 0208h echo latch. */
    mem_remap_top(384);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(&paradise_pvga1a_pcs286_device);

    return ret;
}
