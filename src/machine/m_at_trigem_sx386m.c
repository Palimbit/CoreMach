/*
 * BluMach, a preservation-focused fork of 86Box.
 *
 * Implementation of the TriGem SX386M OEM motherboard.
 *
 * Author: rtzor
 *
 * Copyright 2026 rtzor.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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

static const device_config_t sx386m_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "emerson_020491",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "AMI DG2X-6080-020491-KB (Emerson OEM, 02/04/91)",
                .internal_name = "emerson_020491",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 65536,
                .files         = { "roms/machines/sx386m/EESX386.BIN", "" }
            },
            { .files_no = 0 }
        },
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t sx386m_device = {
    .name          = "TriGem SX386M",
    .internal_name = "sx386m",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = sx386m_config
};

typedef struct sx386m_io_t {
    serial_t *uart[2];
    lpt_t    *lpt;
    uint8_t   config;
} sx386m_io_t;

/*
 * The AMI BIOS first probes the addresses selected in CMOS byte 36h to find
 * add-in cards.  Only afterwards does it write that byte to port 03F3h to
 * enable the motherboard ports.  Keeping the generic UART/LPT handlers active
 * from reset therefore makes the BIOS correctly report them as conflicts.
 *
 * The low two bits select 378h, 3BCh, 278h or disabled for LPT.  Bits 2 and 3
 * disable COM1 and COM2 respectively.  This mapping comes directly from the
 * probe and configuration paths at F000:94C0-F000:953B in EESX386.BIN.
 */
static void
sx386m_io_apply(sx386m_io_t *dev, uint8_t config)
{
    serial_remove(dev->uart[0]);
    serial_remove(dev->uart[1]);
    lpt_port_remove(dev->lpt);

    if (!(config & 0x04))
        serial_setup(dev->uart[0], COM1_ADDR, COM1_IRQ);
    if (!(config & 0x08))
        serial_setup(dev->uart[1], COM2_ADDR, COM2_IRQ);

    switch (config & 0x03) {
        case 0x00:
            lpt_port_setup(dev->lpt, LPT1_ADDR);
            break;
        case 0x01:
            lpt_port_setup(dev->lpt, LPT_MDA_ADDR);
            break;
        case 0x02:
            lpt_port_setup(dev->lpt, LPT2_ADDR);
            break;
        default:
            break;
    }

    dev->config = config;
}

static void
sx386m_io_write(uint16_t port, uint8_t val, void *priv)
{
    sx386m_io_t *dev = (sx386m_io_t *) priv;

    if (port == 0x03f3)
        sx386m_io_apply(dev, val);
}

static void
sx386m_io_reset(void *priv)
{
    /* All motherboard ports are electrically absent until configured. */
    sx386m_io_apply((sx386m_io_t *) priv, 0x0f);
}

static void *
sx386m_io_init(const device_t *info)
{
    sx386m_io_t *dev = (sx386m_io_t *) calloc(1, sizeof(sx386m_io_t));

    (void) info;
    if (dev == NULL)
        return NULL;

    dev->uart[0] = (serial_t *) device_add_inst(&ns16450_device, 1);
    dev->uart[1] = (serial_t *) device_add_inst(&ns16450_device, 2);
    dev->lpt     = (lpt_t *) device_add_inst(&lpt_port_device, 1);

    sx386m_io_reset(dev);
    io_sethandler(0x03f3, 1, NULL, NULL, NULL,
                  sx386m_io_write, NULL, NULL, dev);

    return dev;
}

static void
sx386m_io_close(void *priv)
{
    sx386m_io_t *dev = (sx386m_io_t *) priv;

    io_removehandler(0x03f3, 1, NULL, NULL, NULL,
                     sx386m_io_write, NULL, NULL, dev);
    free(dev);
}

static const device_t sx386m_io_device = {
    .name          = "TriGem SX386M Onboard I/O Latch",
    .internal_name = "sx386m_io",
    .flags         = 0,
    .local         = 0,
    .init          = sx386m_io_init,
    .close         = sx386m_io_close,
    .reset         = sx386m_io_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

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
    device_add(&sx386m_io_device);

    return ret;
}
