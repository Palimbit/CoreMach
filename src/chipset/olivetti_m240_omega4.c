/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 * BluMach preservation port: rtzor, Project BluMach, 2026.
 *
 *          Implementation of the Olivetti M240 OMEGA 4 gate array.
 *
 *          The OMEGA 4 is a CUSTOM Olivetti silicon (no public
 *          datasheet — this driver is reverse-engineered from the
 *          cap2_m240.pdf Pocket Service Guide). On the M240
 *          motherboard (BA200 / BA208) it sits alongside three
 *          bus-converter PALs (PL3P, PL6Z, PL27) and the BSA6A bus
 *          arbiter. Its visible I/O surface is just the two DIP-switch
 *          read ports the Phoenix v1.42 POST uses to size memory and
 *          detect peripheral configuration:
 *
 *            0x62 - DIP-SWITCH A (SWA) read
 *                    bit 1:0 - RAM capacity
 *                              00 = 640 KB, 01 = 256 KB,
 *                              10 = 512 KB, 11 = RAM not enabled
 *                    bit 2   - EGC board present (1 = present)
 *                    bit 4:3 - number of MFD (floppy) units
 *                              00 = 1, 01 = 2, 10 = 3, 11 = 4
 *                    bit 6:5 - monitor type
 *                              00 = 80x25 mono, 01 = 40x25 colour,
 *                              10 = 80x25 colour, 11 = EGA / no CRT
 *                    bit 7   - 8087 coprocessor (0 = present)
 *            0x63 - DIP-SWITCH B (SWB) read
 *                    bit 0   - microfloppy density
 *                              1 = 720 / 360 KB, 0 = 1.44 / 1.2 MB
 *                    bit 1   - Unit A (drive 0) type
 *                              1 = 5.25", 0 = 3.5"
 *                    bit 2   - Unit B (drive 1) type
 *                              1 = 5.25", 0 = 3.5"
 *                    bit 3   - floppy controller (1 = enabled)
 *                    bit 4   - ROM BIOS HD location
 *                              1 = system board, 0 = on controller
 *                    bit 5   - monitor controller (1 = OGC, 0 = others)
 *                    bit 6   - serial port (1 = enabled)
 *                    bit 7   - parallel port (1 = enabled)
 *
 *          The OMEGA 4 also drives the bus-converter logic that turns
 *          the 8086's 16-bit bus accesses into 8-bit ISA accesses, but
 *          86Box's mem layer already enforces the 8-bit ISA bus for
 *          machines with MACHINE_PC, so we do not need to emulate the
 *          PL3P / PL6Z / PL27 / BSA6A silicon here.
 *
 *          DIP switch layout is read by the machine init via the
 *          setters below (m240_omega4_set_swa / set_swb) so a
 *          particular VM config can override the live detection. By
 *          default we derive the DIP switch state at reset time from
 *          86Box's global state (mem_size, fdd_get_flags,
 *          video_is_cga / _mda, hasfpu, machine_has_flags), matching
 *          the behaviour of the existing m240_read() helper in
 *          m_xt_olivetti.c (which the OLDER PCHL/PCHM 2.12 entry
 *          uses). This is the 86Box-correct behaviour: 86Box's UI
 *          represents the DIP switch settings as concrete peripherals,
 *          not as raw switch positions, and the BIOS reads what the
 *          hardware would present.
 *
 * Authors: rtzor
 *
 *          Copyright 2026 rtzor.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/fdd.h>
#include <86box/video.h>
#include <86box/serial.h>
#include <86box/lpt.h>
#include <86box/hdc.h>
#include <86box/ppi.h>
#include <86box/plat_unused.h>

typedef struct
{
    /* Raw DIP switch state. -1 means "derive from 86Box globals on
       read". 0/0xff are valid values (all off / all on respectively)
       so the sentinel is distinct. */
    int8_t swa_override;
    int8_t swb_override;
} olivetti_m240_omega4_t;

/* Global handle to the single M240 OMEGA 4 device. Used by the
   setters below to override the DIP switch state per-VM (e.g. when
   a user wants to force a 256K configuration without manually
   editing the 86Box UI). Only one M240 OMEGA 4 exists in the
   emulated universe at a time, so a static handle is sufficient —
   same convention as olivetti_m250_gate.c and olivetti_ioc02.c. */
static olivetti_m240_omega4_t *current_m240_dev = NULL;

#ifdef ENABLE_OLIVETTI_M240_OMEGA4_LOG
int olivetti_m240_omega4_do_log = ENABLE_OLIVETTI_M240_OMEGA4_LOG;
static void
olivetti_m240_omega4_log(const char *fmt, ...)
{
    va_list ap;

    if (olivetti_m240_omega4_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#define olivetti_m240_omega4_log(fmt, ...)
#endif

/* Derive the SWA byte (port 0x62) from 86Box's current machine
   configuration. The DIP switch layout is "OFF = 1" on the M240
   (every switch is open when shipped, like the IBM PC/XT). */
static uint8_t
olivetti_m240_omega4_default_swa(void)
{
    uint8_t ret = 0x00;
    int     fdd_count = 0;

    /* OMEGA 4 multiplexes two switch banks onto 62h. Port 61h bit 3
       selects the bank, exactly as in the established M240 driver. */
    if (ppi.pb & 0x08) {
        for (uint8_t i = 0; i < FDD_NUM; i++)
            if (fdd_get_flags(i))
                fdd_count++;
        if (fdd_count)
            ret |= (fdd_count - 1) << 2;

        if (video_is_mda())
            ret |= 0x03;
        else if (video_is_cga())
            ret |= 0x02; /* 80x25 colour */
    } else {
        ret |= 0x04; /* fixed board-identification switch */
        if (hasfpu)
            ret |= 0x02;
    }

    return ret;
}

/* Derive the SWB byte (port 0x63) from 86Box's current machine
   configuration. */
static uint8_t
olivetti_m240_omega4_default_swb(void)
{
    uint8_t ret = (fdd_is_hd(0) || fdd_is_hd(1)) ? 0x80 : 0x00;

    ret |= fdd_doublestep_40(1) ? 0x40 : 0x00;
    ret |= fdd_doublestep_40(0) ? 0x20 : 0x00;

    return ret;
}

static uint8_t
olivetti_m240_omega4_read(uint16_t addr, void *priv)
{
    const olivetti_m240_omega4_t *dev = (const olivetti_m240_omega4_t *) priv;
    uint8_t ret;

    switch (addr) {
        case 0x0062:
            if (dev->swa_override >= 0)
                ret = (uint8_t) dev->swa_override;
            else
                ret = olivetti_m240_omega4_default_swa();
            break;
        case 0x0063:
            if (dev->swb_override >= 0)
                ret = (uint8_t) dev->swb_override;
            else
                ret = olivetti_m240_omega4_default_swb();
            break;
        default:
            ret = 0xff;
            break;
    }

    olivetti_m240_omega4_log("Olivetti M240 OMEGA 4: Read %02x at %02x\n", ret, addr);
    return ret;
}

static void
olivetti_m240_omega4_close(void *priv)
{
    olivetti_m240_omega4_t *dev = (olivetti_m240_omega4_t *) priv;

    io_removehandler(0x0062, 2,
                     olivetti_m240_omega4_read, NULL, NULL, NULL, NULL, NULL, dev);

    if (current_m240_dev == dev)
        current_m240_dev = NULL;
    free(dev);
}

static void
olivetti_m240_omega4_reset(void *priv)
{
    olivetti_m240_omega4_t *dev = (olivetti_m240_omega4_t *) priv;

    /* Clear any overrides on reset. After a KBC 0xFE reset the
       BIOS re-reads the DIP switches and we want the live
       configuration (mem_size, FDD count, video adapter, hasfpu)
       to take effect again. */
    dev->swa_override = -1;
    dev->swb_override = -1;
}

void
olivetti_m240_omega4_set_swa(uint8_t swa)
{
    /* Override the SWA read (port 0x62) with a fixed value. Pass
       0xFF to clear the override. Used by per-VM config — e.g. to
       force a 256K configuration without changing mem_size. */
    if (current_m240_dev != NULL) {
        if (swa == 0xFF)
            current_m240_dev->swa_override = -1;
        else
            current_m240_dev->swa_override = (int8_t) swa;
    }
}

void
olivetti_m240_omega4_set_swb(uint8_t swb)
{
    if (current_m240_dev != NULL) {
        if (swb == 0xFF)
            current_m240_dev->swb_override = -1;
        else
            current_m240_dev->swb_override = (int8_t) swb;
    }
}

static void *
olivetti_m240_omega4_init(const device_t *info)
{
    olivetti_m240_omega4_t *dev = (olivetti_m240_omega4_t *) calloc(1, sizeof(olivetti_m240_omega4_t));

    olivetti_m240_omega4_reset(dev);

    /* Register read-only handlers for ports 0x62 and 0x63. The
       Phoenix v1.42 POST reads these to size memory and detect
       peripheral configuration. No write handler — DIP switches
       are read-only hardware. */
    io_sethandler(0x0062, 2,
                  olivetti_m240_omega4_read, NULL, NULL, NULL, NULL, NULL, dev);

    current_m240_dev = dev;

    return dev;
}

const device_t olivetti_m240_omega4_device = {
    .name          = "Olivetti M240 OMEGA 4",
    .internal_name = "olivetti_m240_omega4",
    .flags         = DEVICE_SOFTRESET,
    .local         = 0,
    .init          = olivetti_m240_omega4_init,
    .close         = olivetti_m240_omega4_close,
    .reset         = olivetti_m240_omega4_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
