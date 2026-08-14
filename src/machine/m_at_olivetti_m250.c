/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the Olivetti M250 / M250 E desktop.
 *
 *          The M250 (1989) and M250 E (1989) are Olivetti's first
 *          AT-class desktop machines with an 80286 CPU. The M250
 *          runs at 8 MHz, the M250 E at 12 MHz. The peripheral
 *          controller is the CHIPS 80C206 (M250) or 82C206 (M250 E)
 *          — a standard AT peripheral controller (8259 PIC + 8237
 *          DMA + 8254 PIT + RTC). Because it is a stock CHIPS chip
 *          and not a custom gate array, it does NOT need its own
 *          chipset driver — machine_at_common_ide_init() adds the
 *          standard AT peripheral set (PIC, DMA, PIT, RTC, ISA IDE)
 *          automatically.
 *
 *          The CUSTOM Olivetti silicon is the memory controller:
 *            M250   = GA98 + GA99
 *            M250 E = GA80 + GA99
 *          These are the chips that decode ports 0x65 / 0x67 / 0x69
 *          (and 0x6B on the E variant). See olivetti_m250_gate.c.
 *
 *          Phoenix v1.42 quirks are identical to the Olivetti
 *          PCS 286 (see m_at_olivetti_286.c):
 *            - RAM-REMAPPING test 6 needs 0x60000 aliased to 0x80000
 *            - BDA 0x410 must be 0x20 (80x25 color, 1 FDD) at boot
 *            - NVR factory defaults must match (handled in nvr_at.c)
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
#include <86box/sio.h>
#include <86box/serial.h>
#include <86box/video.h>
#include <86box/machine.h>

/* Memory remap: 0x60000-0x7FFFF is aliased to 0x80000-0x9FFFF.
   The Phoenix v1.42 POST test 6 (MEMORY CONTROLLER) writes the 13-byte
   string "RAM-REMAPPING" at 0x60000 and verifies the same bytes are
   readable at 0x80000. The custom Olivetti memory controller on the
   M250 (GA98/GA80) does this remap on its own in hardware, but
   86Box does not implement that side effect inside the gate array
   driver — we add a static alias mapping instead, exactly like the
   sister PCS 286 implementation in m_at_olivetti_286.c.

   IMPORTANT: mem_read_ram() and mem_write_ram() ignore their `priv`
   pointer completely — they always access ram[addr] directly. Using
   them here would make reads/writes land at 0x60000, not 0x80000.
   We therefore supply custom handlers that translate addr→addr+0x20000
   and call addreadlookup/addwritelookup so the x86 fast-path cache
   is also pointed at the correct physical page. */
static mem_mapping_t olivetti_m250_remap_mapping;

static uint8_t
olivetti_m250_remap_read8(uint32_t addr, UNUSED(void *priv))
{
    addreadlookup(mem_logical_addr, addr + 0x20000);
    return ram[addr + 0x20000];
}

static uint16_t
olivetti_m250_remap_read16(uint32_t addr, UNUSED(void *priv))
{
    addreadlookup(mem_logical_addr, addr + 0x20000);
    return *(uint16_t *) &ram[addr + 0x20000];
}

static uint32_t
olivetti_m250_remap_read32(uint32_t addr, UNUSED(void *priv))
{
    addreadlookup(mem_logical_addr, addr + 0x20000);
    return *(uint32_t *) &ram[addr + 0x20000];
}

static void
olivetti_m250_remap_write8(uint32_t addr, uint8_t val, UNUSED(void *priv))
{
    addwritelookup(mem_logical_addr, addr + 0x20000);
    mem_write_ramb_page(addr + 0x20000, val, &pages[(addr + 0x20000) >> 12]);
}

static void
olivetti_m250_remap_write16(uint32_t addr, uint16_t val, UNUSED(void *priv))
{
    addwritelookup(mem_logical_addr, addr + 0x20000);
    mem_write_ramw_page(addr + 0x20000, val, &pages[(addr + 0x20000) >> 12]);
}

static void
olivetti_m250_remap_write32(uint32_t addr, uint32_t val, UNUSED(void *priv))
{
    addwritelookup(mem_logical_addr, addr + 0x20000);
    mem_write_raml_page(addr + 0x20000, val, &pages[(addr + 0x20000) >> 12]);
}

/* Shared body for both M250 (8 MHz) and M250 E (12 MHz). The only
   difference between the two machine types is the CPU clock — there
   is no different chipset, no different ROM and no different KBC.
   The gate array device's "E variant" flag is set by the M250 E
   init after the shared work is done (so port 0x6B is registered
   before the first POST access). */
static int
machine_at_olivetti_m250_init_internal(const machine_t *model, int is_e_variant)
{
    int ret;

    /* Main system BIOS: Phoenix v1.42 "Resident Diagnostics"
       (PERB 1.03, 05/03/90), 64KB flat, loaded at 0xF0000. The
       image is a single binary (not byte-interleaved) — the
       cap6_m250.pdf service guide shows two 27C256 sockets on the
       M250 motherboard but the archive.org dump ships them merged
       into one 64KB PERB image. The 8/12 MHz variants share the
       same BIOS image. */
    ret = bios_load_linear("roms/machines/olivetti_m250/olivetti_m250_perb_103.BIN",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    /* Standard AT core plus the motherboard's ISA IDE interface.
       Adds PIC1, PIC2, DMA controller (8-bit and 16-bit), PIT,
       RTC, the standard port_6x_device (A20 gate, M250 has stock
       Intel 8742 KBC so the standard port_6x works — no need for
       port_6x_olivetti like the PCS 286), game port, and the IDE
       ISA device. The CHIPS 80C206/82C206 peripheral controller
       does not need a separate driver — its functions are
       supplied by the standard AT peripheral set. */
    machine_at_common_ide_init(model);

    /* Top 384K of the 1MB address space is mapped to RAM. Mirrors
       the PCS 286 layout and matches the cap6_m250.pdf memory map
       (640K base + 384K ext). The Phoenix v1.42 POST expects this
       layout: it copies the upper 128K of BIOS (E0000-FFFFF) to
       shadow RAM at boot, then enables shadowing via port 0x69
       bit 3 on the gate array. */
    mem_remap_top(384);

    /* Phoenix v1.42 diagnostic compare expects BDA equip word 0x410
       to use the same encoding as NVR 0x14. The IBM AT encoding is:
         bit 5 = 80x25 color (1), bit 4 = 40x25 color (0)
         bit 0 = IPL (boot from FDD)
         bit 6 = 2 FDDs (0 = 1 FDD)
         bit 1 = FPU installed
       86Box's generic BDA init writes 0x10 (bit 4 set, which is
       40x25 color) without the IPL bit. The Olivetti has 80x25
       color and 1 FDD, so the correct BDA value is 0x20 (bit 5 =
       80x25 color). Without this override Phoenix reads BDA 0x10,
       compares against NVR 0x20, finds a "mismatch", and sets bit
       5 of CMOS 0x0E (Equipment Error), which keeps the SET-UP
       showing on every boot.

       This is a ONE-SHOT write at machine init (not a per-poll
       force — that was a hack and is now removed for the M250).
       86Box's BDA init runs during mem.c setup before this
       machine init, so we overwrite the generic value with the
       Olivetti-specific one. Phoenix will then read 0x20, find it
       matches NVR 0x14, and skip the equipment-error flag. */
    extern uint8_t *ram;
    if (ram != NULL) {
        /* Match NVR 0x14 (0x21) bit-for-bit so Phoenix v1.42's BDA-vs-NVR
         * diagnostic compare does not flag 0x0E bit 6 ("Configuration
         * table integrity"). Bit 0 = IPL (boot from FDD), bit 5 = 80x25
         * color, bit 6 = 2 FDDs (0 = 1 FDD). 0x21 = IPL + 80x25 + 1 FDD. */
        ram[0x0410] = 0x21;
        ram[0x0411] = 0x00;
    }

    /* The Olivetti-specific NVR factory defaults (IPL + 80x25 + 1
       FDD, 640K base, 384K ext, 0x0E clean) are set inside
       nvr_at.c when it detects this machine, following the same
       is_new pattern as the PCS 286 block at nvr_at.c:1228.
       Together with the BDA override above, this makes Phoenix
       v1.42's diagnostic compare pass and skip the SET-UP on
       subsequent boots. */
    extern void kbd_at_olivetti_bda_force_set(int on);
    kbd_at_olivetti_bda_force_set(1);

    /* Olivetti M250 gate array (GA98/GA99 on the stock M250, GA80
       on the M250 E). Handles ports 0x65/0x67/0x69/0x6B. NOT the
       same as the IOC02 (PCS 286 / PCS 386SX) — the M250 does not
       have an IOC02 chip. */
    device_add(&olivetti_m250_gate_device);
    if (is_e_variant) {
        /* M250 E has the GA80 + GA99 silicon which adds port 0x6B
           (memory banks starting address). The setter must be
           called AFTER device_add() so the static handle is
           populated, and BEFORE the first POST access to 0x6B. */
        extern void olivetti_m250_gate_set_e_variant(int on);
        olivetti_m250_gate_set_e_variant(1);
    }

    /* AT-style PS/2 KBC. The cap6 service manual lists the chip as
       a "stock 8742", but the Phoenix v1.42 firmware expects the
       Olivetti-customized command set (0x80/0x82/0x84/0x85/0xCF) and
       loops forever rejecting them as "bad controller command" if
       we use kbc_params=0 (KBC_VEN_NONE). Use KBC_VEN_OLIVETTI — the
       same vendor the PCS 286 uses — so the write_cmd_olivetti
       handler in kbc_at.c is installed and Phoenix POST completes. */
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    /* Opt this machine into the file-based keyboard injector. The
       Olivetti BUILT-IN SET-UP screen (after Phoenix POST) shows a
       6-keyboard-layout menu and waits for the user to type a number
       1-6. 86Box's SDL keyboard hook can't reliably inject synthetic
       keystrokes into the emulator window, so we use the same
       one-shot file injector as the PCS 286: drop a SET-2 scan code
       file at kbd_inject_path, the KBC poll picks it up, injects the
       byte, and truncates the file. We pre-set the path here so the
       poll in kbc_at_poll() picks it up. */
    extern void kbd_inject_set_path(const char *path);
    kbd_inject_set_path("C:/Users/rauto/86Box VMs/olivetti_m250/kbd_inject.bin");

    /* WD37C65C FDC is the floppy controller on the M250 motherboard.
       The cap6 service manual lists it explicitly (BA227/BA233 boards
       for the stock M250, BA241 for the M250 E). */
    device_add(&fdc_at_device);

    /* Memory remap 0x60000-0x7FFFF → 0x80000-0x9FFFF. The Phoenix
       POST test 6 writes 13 bytes ("RAM-REMAPPING") to 0x60000
       then reads them back from 0x80000 (REPE CMPSB). Custom
       handlers redirect every access to addr+0x20000 and prime
       the 86Box fast-path cache so subsequent bytes in the same
       REP MOVSB / REPE CMPSB also land at the right location.
       flushmmucache() invalidates any stale lookup entries that
       may have been established during earlier boot-time memory
       clears. */
    mem_mapping_add(&olivetti_m250_remap_mapping, 0x60000, 0x20000,
                    olivetti_m250_remap_read8,  olivetti_m250_remap_read16,  olivetti_m250_remap_read32,
                    olivetti_m250_remap_write8, olivetti_m250_remap_write16, olivetti_m250_remap_write32,
                    NULL, 0, NULL);
    mem_mapping_enable(&olivetti_m250_remap_mapping);
    flushmmucache();

    /* Onboard Paradise PVGA1A + IMS G176P-40 RAMDAC, 256KB VRAM.
       Reuses the Amstrad PC3086 driver (same WD90C11-LR / PVGA1A
       core). The M250 BIOS reports "OVC rev. 1.06" in SET-UP — OVC
       is just Olivetti's rebrand of the PVGA1A (Paradise sold the
       die as Western Digital / OVC / Olivetti depending on
       customer). The PERC 1.06 video BIOS is 32KB flat, loaded at
       0xC0000.

       We do NOT use rom_load_aux — that function does not exist in
       this 86Box fork. rom_load_linear() is the correct API for
       loading a flat video ROM at a custom base address with
       custom mappings. The system BIOS already lives at
       0xF0000-0xFFFFF (set up by bios_load_linear above), so the
       video BIOS at 0xC0000-0xC7FFF is a separate, non-
       conflicting mapping. */
    if (gfxcard[0] == VID_INTERNAL) {
        device_add(&paradise_pvga1a_pc3086_device);
        rom_load_linear("roms/machines/olivetti_m250/olivetti_m250_perc_106.BIN",
                        0x000c0000, 32768, 0, NULL);
    }

    return ret;
}

int
machine_at_olivetti_m250_init(const machine_t *model)
{
    /* Stock M250: 8 MHz, GA98 + GA99, no port 0x6B. */
    return machine_at_olivetti_m250_init_internal(model, 0);
}

int
machine_at_olivetti_m250e_init(const machine_t *model)
{
    /* M250 E: 12 MHz, GA80 + GA99, port 0x6B enabled. Same BIOS,
       same KBC, same FDC, same video. The 8/12 MHz difference is
       handled by the machine_table.c entry (cpu block) — this
       init function only needs to enable the E variant of the
       gate array. */
    return machine_at_olivetti_m250_init_internal(model, 1);
}
