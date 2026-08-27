/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the Olivetti PCS 386SX.
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

/* Memory remap: 0x60000-0x7FFFF is aliased to 0x80000-0x9FFFF.
   Phoenix v1.42 POST test 6 (MEMORY CONTROLLER) writes the 13-byte
   string "RAM-REMAPPING" at 0x60000 and verifies the same bytes are
   readable at 0x80000. The Headland HT18 has a remap feature
   controlled by KBC P2 bit 2, but 86Box does not implement that
   side effect. We add a static alias mapping (same trick used by
   the Olivetti PCS 286 driver).

   IMPORTANT: mem_read_ram() and mem_write_ram() ignore their `priv`
   pointer completely — they always access ram[addr] directly. Using
   them here would make reads/writes land at 0x60000, not 0x80000.
   We therefore supply custom handlers that translate addr→addr+0x20000
   and call addreadlookup/addwritelookup so the x86 fast-path cache
   is also pointed at the correct physical page. */
static mem_mapping_t olivetti_pcs386sx_remap_mapping;

static uint8_t
olivetti_386sx_remap_read8(uint32_t addr, UNUSED(void *priv))
{
    addreadlookup(mem_logical_addr, addr + 0x20000);
    return ram[addr + 0x20000];
}

static uint16_t
olivetti_386sx_remap_read16(uint32_t addr, UNUSED(void *priv))
{
    addreadlookup(mem_logical_addr, addr + 0x20000);
    return *(uint16_t *)&ram[addr + 0x20000];
}

static uint32_t
olivetti_386sx_remap_read32(uint32_t addr, UNUSED(void *priv))
{
    addreadlookup(mem_logical_addr, addr + 0x20000);
    return *(uint32_t *)&ram[addr + 0x20000];
}

static void
olivetti_386sx_remap_write8(uint32_t addr, uint8_t val, UNUSED(void *priv))
{
    addwritelookup(mem_logical_addr, addr + 0x20000);
    mem_write_ramb_page(addr + 0x20000, val, &pages[(addr + 0x20000) >> 12]);
}

static void
olivetti_386sx_remap_write16(uint32_t addr, uint16_t val, UNUSED(void *priv))
{
    addwritelookup(mem_logical_addr, addr + 0x20000);
    mem_write_ramw_page(addr + 0x20000, val, &pages[(addr + 0x20000) >> 12]);
}

static void
olivetti_386sx_remap_write32(uint32_t addr, uint32_t val, UNUSED(void *priv))
{
    addwritelookup(mem_logical_addr, addr + 0x20000);
    mem_write_raml_page(addr + 0x20000, val, &pages[(addr + 0x20000) >> 12]);
}

int
machine_at_olivetti_pcs386sx_init(const machine_t *model)
{
    int ret;

    /* Main system BIOS: Phoenix v1.14, 128KB byte-interleaved across
       two 27C256 EPROMs. The byte-interleaved layout is verified:
       byte 2i of the resulting 128KB image comes from the LOW chip,
       byte 2i+1 from the HIGH chip. After 86Box's `bios_load_interleaved`
       runs, the in-memory image at 0xE0000-0xFFFFF matches what the
       original motherboard would present to the CPU. */
    ret = bios_load_interleaved("roms/machines/olivetti_pcs386sx/olivetti_pcs386sx_v114_lo.bin",
                                "roms/machines/olivetti_pcs386sx/olivetti_pcs386sx_v114_hi.bin",
                                0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_ide_init(model);

    /* Top 384K of the 1MB address space is mapped to RAM.
       The Phoenix v1.14 expects this layout for shadowing. */
    mem_remap_top(384);

    /* Phoenix v1.14 diagnostic compare expects BDA equip word 0x410
     * to match NVR 0x14. The IBM AT encoding is:
     *   bit 5 = 80x25 color (1), bit 4 = 40x25 color (0)
     *   bit 0 = IPL (boot from FDD)
     *   bit 6 = 2 FDDs (0 = 1 FDD)
     *   bit 1 = FPU installed
     * 86Box's generic BDA init writes 0x10 (bit 4 set, which is
     * 40x25 color) without the IPL bit. The Olivetti has 80x25 color
     * and 1 FDD, so the BDA seed is 0x20 (bit 5 = 80x25
     * color). Phoenix updates the coprocessor bit after detecting the
     * socket; setting it before POST diverts video initialization. Without
     * this override Phoenix reads BDA 0x10, compares
     * against NVR 0x20, finds a "mismatch", and sets bit 5 of
     * CMOS 0x0E (Equipment Error), which keeps the SET-UP showing
     * on every boot.
     *
     * This is a ONE-SHOT write at machine init (not a per-poll
     * force — that was a hack and is now removed). Same trick
     * as the Olivetti PCS 286 driver. */
    extern uint8_t *ram;
    if (ram != NULL) {
        ram[0x0410] = 0x20;
        ram[0x0411] = 0x00;
}
    /* Olivetti IOC02 glue chip. Same gate array as the PCS 286, but
       the BIOS version is Phoenix v1.14 (instead of v1.42 on the
       PCS 286). v1.14 does a write-then-read verification of reg
       0x6A and aborts with "I/O CONTROLLER ERROR 2" if the first
       read returns 0x04 (the legacy PCS 286 hack). Disable the
       hack for this machine. */
    device_add(&olivetti_ioc02_device);
    olivetti_ioc02_set_first_read_hack(0);

    /* Olivetti-specific port 0x61/0x62/0x63 handler (A20 gate).
       KBC_P2 has custom semantics for the Olivetti firmware. */
    device_add(&port_6x_olivetti_device);

    /* Headland HT18-B + KBC AT (Olivetti firmware) + AT FDC.
       machine_at_headland_common_init(model, 2) wires all three:
         - kbc_at_device with model->kbc_params (KBC_VEN_OLIVETTI)
         - fdc_at_device (if fdc_current[0] == FDC_INTERNAL)
         - headland_ht18b_device
       The 386SX motherboard has PC87310 visible but its functionality
       is largely unused by Phoenix v1.14 — the Olivetti KBC handles
       port 0x64 directly, the HT18 handles memory/EMS, the PC87310
       provides serial/parallel/floppy which we still want for cfg
       compatibility. */
    machine_at_headland_common_init(model, 2);

    /* PC87310 super I/O (visible on the motherboard). Use PC87310_ALI
       to match the AMA-932J reference (also HT18 + PC87310). The
       ALI param configures the DENSEL polarity bits the way the
       FDC NSC variant expects. */
    device_add_params(&pc87310_device, (void *) PC87310_ALI);

    /* Memory remap 0x60000-0x7FFFF → 0x80000-0x9FFFF.
       Same trick as PCS 286: Phoenix POST test 6 writes 13 bytes
       ("RAM-REMAPPING") to 0x60000 then reads them back from 0x80000
       (REPE CMPSB). Custom handlers redirect every access to
       addr+0x20000 and prime the 86Box fast-path cache so subsequent
       bytes in the same REP MOVSB / REPE CMPSB also land at the
       right location. */
    mem_mapping_add(&olivetti_pcs386sx_remap_mapping, 0x60000, 0x20000,
                    olivetti_386sx_remap_read8,  olivetti_386sx_remap_read16,  olivetti_386sx_remap_read32,
                    olivetti_386sx_remap_write8, olivetti_386sx_remap_write16, olivetti_386sx_remap_write32,
                    NULL, 0, NULL);
    mem_mapping_enable(&olivetti_pcs386sx_remap_mapping);
    flushmmucache();

    /* Onboard Paradise PVGA1A + IMS G176P-40 RAMDAC, 256KB VRAM.
       Its OVC 1.06 firmware is embedded in the system ROM; the dedicated
       device therefore does not load an external C0000 option ROM. */
    if (gfxcard[0] == VID_INTERNAL)
        device_add(&paradise_pvga1a_pcs386sx_device);

    return ret;
}
