/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          Olivetti M380 / M380 C (BA806 / BA818).
 *
 * BluMach preservation port: rtzor, Project BluMach, 2026.
 *
 *          The first implementation intentionally models only hardware
 *          demonstrated by the service guide and the PBUQ/PBUZ 1.09 ROMs.
 *          The proprietary memory and 32-bit expansion-bus logic is not yet
 *          emulated; standard AT services provide a bring-up baseline.
 */
#include <stdint.h>
#include <stdio.h>
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/machine.h>
#include <86box/rom.h>

int
machine_at_olivetti_m380_init(const machine_t *model)
{
    int ret;

    /* Two 27C256 EPROMs form a 64 KiB byte-interleaved image. PBUQ supplies
       even bytes and PBUZ odd bytes. The reset vector is EA 5B E0 00 F0. */
    ret = bios_load_interleaved(
        "roms/machines/olivetti_m380/Olivetti M380 BIOS Version 1_09 PBUQ.BIN",
        "roms/machines/olivetti_m380/Olivetti M380 BIOS Version 1_09 PBUZ.BIN",
        0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_init(model);

    /* The physical machine connects its floppy controller through the IF614
       and the proprietary backplane. Treat 86Box's "internal" selection as
       that controller board until its Olivetti glue logic is implemented. */
    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}
