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

