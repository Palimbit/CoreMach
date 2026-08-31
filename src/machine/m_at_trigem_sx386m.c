/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the TriGem SX386M.
 *
 * Author:  rtzor
 *
 *          Copyright 2026 rtzor.
 *          SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdint.h>
#include <stdio.h>

#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/chipset.h>
#include <86box/keyboard.h>
#include <86box/mem.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/hdc.h>
#include <86box/nvr.h>
#include <86box/lpt.h>
#include <86box/serial.h>
#include <86box/rom.h>
#include <86box/machine.h>

int
machine_at_trigem_sx386m_init(const machine_t *model)
{
    int ret = bios_load_linear("roms/machines/sx386m/EESX386.BIN",
                               0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_ide_init(model);

    /* Board photographs and the AMI BIOS register sequence identify the
       three-chip Headland HT101SX + HT113 + GC102-PC implementation. */
    device_add(&headland_ht101sx_chipset_device);

    /* Exact mask-ROM and peripheral-controller markings have not yet been
       preserved. These compatible devices model only documented interfaces. */
    device_add_params(machine_get_kbc_device(machine),
                      (void *) model->kbc_params);
    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);
    device_add_inst(&ns16450_device, 1);
    device_add_inst(&ns16450_device, 2);
    device_add_inst(&lpt_port_device, 1);

    return ret;
}
