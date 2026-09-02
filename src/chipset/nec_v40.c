/*
 * BluMach, a preservation-focused fork of 86Box.
 *
 * Initial NEC V40 integrated-peripheral model.
 *
 * The execution engine is BluMach's NEC V20 core. This file models the V40
 * system I/O registers, peripheral relocation state, reset behaviour and the
 * 4.77/8 MHz clock selection used by the Olivetti Prodest PC 1.
 *
 * Behavioural reference: NEC uPD70208 documentation and the BSD-3-Clause
 * MAME v40_device by Patrick Mackinlay. No MAME source code is copied here.
 *
 * Author: rtzor
 * Project: BluMach
 * Copyright 2026 rtzor
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/machine.h>
#include <86box/nec_v40.h>

#define V40_SFR_BASE 0xfff0
#define V40_SFR_SIZE 0x0010

#define V40_REG_TCKS  0x00
#define V40_REG_RFC   0x02
#define V40_REG_WMB   0x04
#define V40_REG_WCY1  0x05
#define V40_REG_WCY2  0x06
#define V40_REG_SULA  0x08
#define V40_REG_TULA  0x09
#define V40_REG_IULA  0x0a
#define V40_REG_DULA  0x0b
#define V40_REG_OPHA  0x0c
#define V40_REG_OPSEL 0x0d
#define V40_REG_OPCN  0x0e

#define V40_OPSEL_SCU 0x08

#define V40_SPEED_SLOW 4772728
#define V40_SPEED_FAST 8000000

typedef struct nec_v40_t {
    uint8_t regs[V40_SFR_SIZE];

    uint8_t scu_data;
    uint8_t scu_mode;
    uint8_t scu_command;
    uint8_t scu_status;
    uint16_t scu_base;
    uint8_t scu_mapped;

    int cpu_slow_index;
    int cpu_fast_index;
} nec_v40_t;

static nec_v40_t *nec_v40_active;

static int
nec_v40_find_cpu(int speed)
{
    int index = 0;

    while (cpu_f->cpus[index].cpu_type) {
        if (cpu_is_eligible(cpu_f, index, machine) &&
            cpu_f->cpus[index].rspeed == speed)
            return index;
        index++;
    }

    return -1;
}

static void
nec_v40_set_speed(nec_v40_t *dev, int slow)
{
    const int index = slow ? dev->cpu_slow_index : dev->cpu_fast_index;

    if (index >= 0)
        cpu_dynamic_switch(index);
}

static uint8_t
nec_v40_scu_read(uint16_t port, void *priv)
{
    nec_v40_t *dev = (nec_v40_t *) priv;

    switch ((port - dev->scu_base) & 0x03) {
        case 0x00:
            dev->scu_status &= ~0x02;
            return dev->scu_data;
        case 0x01:
            return dev->scu_status;
        case 0x02:
            return dev->scu_mode;
        case 0x03:
            return dev->scu_command;
        default:
            return 0xff;
    }
}

static void
nec_v40_scu_write(uint16_t port, uint8_t val, void *priv)
{
    nec_v40_t *dev = (nec_v40_t *) priv;

    switch ((port - dev->scu_base) & 0x03) {
        case 0x00:
            dev->scu_data = val;
            dev->scu_status |= 0x05;
            break;
        case 0x02:
            dev->scu_mode = val;
            break;
        case 0x03:
            dev->scu_command = val;
            if (val & 0x40) {
                dev->scu_data = 0x00;
                dev->scu_mode = 0x00;
                dev->scu_status = 0x05;
            }
            break;
        default:
            break;
    }
}

static void
nec_v40_update_scu_mapping(nec_v40_t *dev)
{
    if (dev->scu_mapped) {
        io_removehandler(dev->scu_base, 4,
                         nec_v40_scu_read, NULL, NULL,
                         nec_v40_scu_write, NULL, NULL, dev);
        dev->scu_mapped = 0;
    }

    if (dev->regs[V40_REG_OPSEL] & V40_OPSEL_SCU) {
        dev->scu_base = ((uint16_t) dev->regs[V40_REG_OPHA] << 8) |
                        (dev->regs[V40_REG_SULA] & 0xfc);
        io_sethandler(dev->scu_base, 4,
                      nec_v40_scu_read, NULL, NULL,
                      nec_v40_scu_write, NULL, NULL, dev);
        dev->scu_mapped = 1;
    }
}

static void
nec_v40_update_clock(nec_v40_t *dev)
{
    const int slow = (dev->regs[V40_REG_RFC] == 0x84) &&
                     (dev->regs[V40_REG_WCY1] == 0xff) &&
                     (dev->regs[V40_REG_WCY2] == 0x0f);
    const int fast = (dev->regs[V40_REG_RFC] == 0x8d) &&
                     (dev->regs[V40_REG_WCY1] == 0xcc) &&
                     (dev->regs[V40_REG_WCY2] == 0x0f);

    if (slow)
        nec_v40_set_speed(dev, 1);
    else if (fast)
        nec_v40_set_speed(dev, 0);
}

static uint8_t
nec_v40_sfr_read(uint16_t port, void *priv)
{
    const nec_v40_t *dev = (const nec_v40_t *) priv;

    return dev->regs[port & 0x0f];
}

static void
nec_v40_sfr_write(uint16_t port, uint8_t val, void *priv)
{
    nec_v40_t *dev = (nec_v40_t *) priv;
    const uint8_t reg = port & 0x0f;

    dev->regs[reg] = val;

    switch (reg) {
        case V40_REG_RFC:
        case V40_REG_WCY1:
        case V40_REG_WCY2:
            nec_v40_update_clock(dev);
            break;
        case V40_REG_SULA:
        case V40_REG_OPHA:
        case V40_REG_OPSEL:
            nec_v40_update_scu_mapping(dev);
            break;
        default:
            break;
    }
}

uint8_t
nec_v40_reg_read(uint8_t reg)
{
    if (nec_v40_active == NULL)
        return 0xff;

    return nec_v40_active->regs[reg & 0x0f];
}

static void
nec_v40_reset(void *priv)
{
    nec_v40_t *dev = (nec_v40_t *) priv;

    if (dev->scu_mapped) {
        io_removehandler(dev->scu_base, 4,
                         nec_v40_scu_read, NULL, NULL,
                         nec_v40_scu_write, NULL, NULL, dev);
        dev->scu_mapped = 0;
    }

    memset(dev->regs, 0, sizeof(dev->regs));
    dev->scu_data = 0x00;
    dev->scu_mode = 0x00;
    dev->scu_command = 0x00;
    dev->scu_status = 0x05;

    /* The PC1 keeps the selected 4.77/8 MHz state across a warm reset. The
       first device initialization below establishes the cold-boot 8 MHz
       state; reset therefore does not force a CPU switch. */
}

static void *
nec_v40_init(const device_t *info)
{
    nec_v40_t *dev = (nec_v40_t *) calloc(1, sizeof(nec_v40_t));

    (void) info;

    if (dev == NULL)
        return NULL;

    dev->cpu_slow_index = nec_v40_find_cpu(V40_SPEED_SLOW);
    dev->cpu_fast_index = nec_v40_find_cpu(V40_SPEED_FAST);

    nec_v40_active = dev;
    nec_v40_reset(dev);
    nec_v40_set_speed(dev, 0);

    io_sethandler(V40_SFR_BASE, V40_SFR_SIZE,
                  nec_v40_sfr_read, NULL, NULL,
                  nec_v40_sfr_write, NULL, NULL, dev);

    return dev;
}

static void
nec_v40_close(void *priv)
{
    nec_v40_t *dev = (nec_v40_t *) priv;

    if (dev->scu_mapped)
        io_removehandler(dev->scu_base, 4,
                         nec_v40_scu_read, NULL, NULL,
                         nec_v40_scu_write, NULL, NULL, dev);

    io_removehandler(V40_SFR_BASE, V40_SFR_SIZE,
                     nec_v40_sfr_read, NULL, NULL,
                     nec_v40_sfr_write, NULL, NULL, dev);

    if (nec_v40_active == dev)
        nec_v40_active = NULL;

    free(dev);
}

const device_t nec_v40_device = {
    .name          = "NEC V40 integrated peripherals",
    .internal_name = "nec_v40",
    .flags         = 0,
    .local         = 0,
    .init          = nec_v40_init,
    .close         = nec_v40_close,
    .reset         = nec_v40_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
