/*
 * BluMach, a preservation-focused fork of 86Box.
 *
 *          Olivetti M300 modular system with IF378 80386SX CPU card.
 *
 * Author: rtzor
 * Project: BluMach
 *
 * Copyright 2026 rtzor.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/chipset.h>
#include <86box/io.h>
#include <86box/keyboard.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/serial.h>
#include <86box/machine.h>

static void
m300_if378_diag_write(uint16_t port, uint8_t val, void *priv)
{
    (void) port;
    (void) priv;

#ifdef ENABLE_OLIVETTI_M300_LOG
    pclog("Olivetti M300 IF378 diagnostic code: %02X\n", val);
#else
    (void) val;
#endif
}

static uint8_t
m300_if378_pit_wait_read(uint16_t port, void *priv)
{
    (void) port;
    (void) priv;
    cycles -= 5;
    return 0xff;
}

static void
m300_if378_pit_wait_write(uint16_t port, uint8_t val, void *priv)
{
    (void) port;
    (void) val;
    (void) priv;
    cycles -= 5;
}

static uint8_t
m300_if378_port61_wait_read(uint16_t port, void *priv)
{
    (void) port;
    (void) priv;
    cycles -= 5;
    return 0xff;
}

static uint8_t
m300_if378_cmos_wait_read(uint16_t port, void *priv)
{
    (void) port;
    (void) priv;
    cycles -= 160;
    return 0xff;
}

static void
m300_if378_cmos_wait_write(uint16_t port, uint8_t val, void *priv)
{
    (void) port;
    (void) val;
    (void) priv;
    cycles -= 160;
}

int
machine_at_olivetti_m300_if378_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/m300_if378/BIOS.ROM",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_ide_init(model);

    device_add(&intel_82335_device);
    device_add_params(machine_get_kbc_device(machine),
                      (void *) model->kbc_params);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);
    device_add_inst(&ns16450_device, 1);

    io_sethandler(0x0378, 1, NULL, NULL, NULL,
                  m300_if378_diag_write, NULL, NULL, NULL);
    io_sethandler(0x0040, 4,
                  m300_if378_pit_wait_read, NULL, NULL,
                  m300_if378_pit_wait_write, NULL, NULL, NULL);
    io_sethandler(0x0061, 1,
                  m300_if378_port61_wait_read, NULL, NULL,
                  NULL, NULL, NULL, NULL);
    io_sethandler(0x0070, 2,
                  m300_if378_cmos_wait_read, NULL, NULL,
                  m300_if378_cmos_wait_write, NULL, NULL, NULL);
    return ret;
}
