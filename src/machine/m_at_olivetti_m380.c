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
#include <86box/lpt.h>
#include <86box/machine.h>
#include <86box/rom.h>
#include <86box/serial.h>

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

    /* CAP7 p. 7-3 maps the first MiB as 640 KiB conventional RAM followed
       by video memory, VGA ROM/option space, video BIOS shadow and system
       BIOS shadow. The physical RAM backing that falls below A0000h-FFFFFh
       must be made visible above installed RAM: with a 4 MiB ME912 profile,
       that is 640 KiB conventional plus 3456 KiB extended, for the documented
       4096 KiB total. Without this remap Resident Diagnostics loses 384 KiB
       and reports the incorrect 3712 KiB total. */
    mem_remap_top(384);

    /* The physical machine connects its floppy controller through the IF614
       and the proprietary backplane. Treat 86Box's "internal" selection as
       that controller board until its Olivetti glue logic is implemented. */
    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    /* CAP7 p. 7-4: two fixed 16450-compatible serial interfaces.  Make them
       motherboard resources instead of relying on the optional standalone
       ports, so COM1 is 3F8h/IRQ4 and COM2 is 2F8h/IRQ3 in every M380. */
    com_ports[0].enabled = 1;
    com_ports[1].enabled = 1;

    serial_t *com1 = device_add_inst(&ns16450_device, 1);
    serial_setup(com1, COM1_ADDR, COM1_IRQ);

    serial_t *com2 = device_add_inst(&ns16450_device, 2);
    serial_setup(com2, COM2_ADDR, COM2_IRQ);

    /* No extra, generic UARTs belong to the documented motherboard. */
    serial_set_next_inst(SERIAL_MAX - 1);

    /* CAP7, p. 7-4 documents two motherboard parallel interfaces: LPT1 at
       378h on IRQ 7 and LPT2 at 278h on IRQ 5.  Their Olivetti glue logic is
       not yet known, so model each as a fixed, standard parallel interface.
       They are not optional add-in cards, therefore reserve both ports here
       rather than relying on the global standalone-port defaults. */
    lpt_ports[0].enabled = 1;
    lpt_ports[1].enabled = 1;

    lpt_t *lpt1 = device_add_inst(&lpt_port_device, 1);
    lpt_port_setup(lpt1, LPT1_ADDR);
    lpt_port_irq(lpt1, LPT1_IRQ);

    lpt_t *lpt2 = device_add_inst(&lpt_port_device, 2);
    lpt_port_setup(lpt2, LPT2_ADDR);
    lpt_port_irq(lpt2, LPT2_IRQ);

    /* Do not let the standalone initializer create further, generic ports. */
    lpt_set_next_inst(PARALLEL_MAX - 1);

    return ret;
}
