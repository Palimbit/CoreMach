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
#include <86box/lpt.h>
#include <86box/port_6x.h>
#include <86box/serial.h>
#include <86box/video.h>
#include <86box/machine.h>

int
machine_at_olivetti_pcs386sx_init(const machine_t *model)
{
    int         ret;
    const char *rom_lo;
    const char *rom_hi;

    /* Main system BIOS: Phoenix v1.14, 128KB byte-interleaved across
       two 27C512 EPROMs. The byte-interleaved layout is verified:
       byte 2i of the resulting 128KB image comes from the LOW chip,
       byte 2i+1 from the HIGH chip. After 86Box's `bios_load_interleaved`
       runs, the in-memory image at 0xE0000-0xFFFFF matches what the
       original motherboard would present to the CPU. */
    /* The TA and Olivetti executable regions are byte-identical.  Preserve
       the original dump from each badge anyway: their erased padding differs
       (FFh on the Dario, 00h on the PCS) and is useful provenance. */
    if (!strcmp(model->internal_name, "ta_dario386sx")) {
        rom_lo = "roms/machines/ta_dario386sx/dario386sx_v114_lo.bin";
        rom_hi = "roms/machines/ta_dario386sx/dario386sx_v114_hi.bin";
    } else {
        rom_lo = "roms/machines/olivetti_pcs386sx/olivetti_pcs386sx_v114_lo.bin";
        rom_hi = "roms/machines/olivetti_pcs386sx/olivetti_pcs386sx_v114_hi.bin";
    }
    ret = bios_load_interleaved(rom_lo, rom_hi,
                                0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_ide_init(model);

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

    /* The physical board uses the three-chip Headland HT101SX + HT113 +
       GC102 set.  PCS 386SX Phoenix 1.14 and a second surviving HT101SX
       BIOS both program CR0-CR4 and EMS maps through 1ECh-1EFh, which is
       the interface supplied by this dedicated profile.  Unlike the HT18-B
       approximation previously used here, it does not install fast A20 at
       port 92h; neither BIOS accesses that port. */
    device_add(&headland_ht101sx_device);

    /* Real discrete I/O population: Mitsubishi M5L8042 keyboard controller,
       WD37C65C floppy controller and TI TL16C451FN serial/parallel controller.
       The preserved M5L8042 mask ROM is still missing, so the existing
       Olivetti 8042 protocol profile remains the closest executable model.
       WD37C65C uses the standard AT FDC register model.  TI documents the
       TL16C451 as one TL16C450-compatible UART plus a Centronics interface,
       hence one 16450-compatible UART and one standard bidirectional LPT are
       a direct functional decomposition rather than a borrowed Super I/O. */
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_wd37c65_device);
    device_add_inst(&ns16450_device, 1);
    device_add_inst(&lpt_port_device, 1);

    /* HT101SX owns conventional, relocated, shadow and EMS memory mapping.
       Do not install the PCS 286's 0x60000 -> 0x80000 diagnostic alias or
       the generic mem_remap_top() mapping here: both overlap the HT101SX maps,
       collapse two distinct 128 KiB conventional-memory ranges and can
       corrupt EMS/shadow behavior. The PCS 386SX BIOS 1.14 contains neither
       the PCS 286's RAM-REMAPPING string nor its 6000h/8000h segment test. */

    /* Onboard Paradise PVGA1A + IMS G176P-40 RAMDAC, 256KB VRAM.
       Its OVC 1.06 firmware is embedded in the system ROM; the dedicated
       device therefore does not load an external C0000 option ROM. */
    if (gfxcard[0] == VID_INTERNAL)
        device_add(&paradise_pvga1a_pcs386sx_device);

    return ret;
}
