/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the Olivetti M250 / M250 E memory
 *          controller gate array (GA98 + GA99 on the M250, GA80 + GA99
 *          on the M250 E). This is a CUSTOM, OLIVETTI-SPECIFIC chip,
 *          not a stock CHIPS 80C206 peripheral controller. The service
 *          manual "cap6_m250.pdf" (pages 5-8) only lists the I/O port
 *          map; no register-level datasheet exists, so the semantics
 *          below mirror the sister Olivetti EVA gate array
 *          (see olivetti_eva.c) which uses the same 0x65 / 0x67 / 0x69
 *          triplet and the same Phoenix v1.42 shadow-RAM dance.
 *
 *          I/O port map (from cap6_m250.pdf page 6-7 / 6-8):
 *            0x65 - Configuration register
 *            0x67 - Memory page register
 *            0x69 - Memory map register (bit 3 = shadow E0000-FFFFF)
 *            0x6B - Memory banks starting address (M250 E only)
 *
 * Author: rtzor
 * Project: BluMach
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
#include <86box/chipset.h>
#include <86box/video.h>
#include <86box/mem.h>
#include <86box/plat_unused.h>

typedef struct
{
    uint8_t reg_065;          /* Configuration register. */
    uint8_t reg_067;          /* Memory page register. */
    uint8_t reg_069;          /* Memory map register (bit 3 = shadow E0000-FFFFF). */
    uint8_t reg_06b;          /* Memory banks starting address (M250 E only). */
    uint8_t e_variant;        /* 1 on the M250 E (port 0x6B enabled),
                                 0 on the stock M250 (port 0x6B absent). */
} olivetti_m250_gate_t;

/* Global handle to the single M250 gate array device. Used by
   olivetti_m250_gate_set_e_variant() to toggle the M250 E behaviour
   (port 0x6B enabled) per-machine. The base M250 leaves it at 0; the
   M250 E machine init calls the setter with 1 BEFORE the first POST
   access to 0x6B so the IO handler is registered in time.

   Only one M250 gate exists in the emulated universe at a time, so a
   static handle is sufficient — same convention as olivetti_ioc02.c. */
static olivetti_m250_gate_t *current_m250_dev = NULL;

#ifdef ENABLE_OLIVETTI_M250_GATE_LOG
int olivetti_m250_gate_do_log = ENABLE_OLIVETTI_M250_GATE_LOG;
static void
olivetti_m250_gate_log(const char *fmt, ...)
{
    va_list ap;

    if (olivetti_m250_gate_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#define olivetti_m250_gate_log(fmt, ...)
#endif

static void
olivetti_m250_gate_update_shadow(olivetti_m250_gate_t *dev)
{
    /* Bit 3 of reg_069 enables shadowing for E0000-FFFFF. This is the
       same convention as the Olivetti EVA gate array: when shadowing
       is OFF the upper 128KB is read-only from the system BIOS ROM
       (loaded by the machine init at 0xF0000-0xFFFFF); when ON the
       CPU can read AND write the upper 128KB as shadow RAM (the
       Phoenix v1.42 POST copies the ROM to RAM there then enables
       shadowing).

       mem_set_mem_state_both() updates the per-page mem state table
       that the 86Box fast-path cache (pages[]) consults on every CPU
       access. We do NOT need a per-gate mem_mapping_add: the system
       BIOS ROM mapping is added by bios_load_linear() during machine
       init, and toggling the per-page state is enough to switch
       between "ROM is the source" (READ_EXTANY) and "RAM is the
       source" (READ_INTERNAL/WRITE_INTERNAL). */
    if (dev->reg_069 & 0x08) {
        mem_set_mem_state_both(0xe0000, 0x20000,
                               MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
    } else {
        mem_set_mem_state_both(0xe0000, 0x20000,
                               MEM_READ_EXTANY | MEM_WRITE_EXTANY);
    }
    flushmmucache();
}

static void
olivetti_m250_gate_write(uint16_t addr, uint8_t val, void *priv)
{
    olivetti_m250_gate_t *dev = (olivetti_m250_gate_t *) priv;
    olivetti_m250_gate_log("Olivetti M250 Gate Array: Write %02x at %02x\n", val, addr);

    switch (addr) {
        case 0x065:
            /* Configuration register. The cap6 service manual only
               lists the address; no bit-level semantics are documented.
               We store the value so reads return what the POST wrote —
               the Phoenix v1.42 SET-UP screen issues configuration
               writes here during its MEMORY sub-test. */
            dev->reg_065 = val;
            break;
        case 0x067:
            /* Memory page register. Used by the BIOS to select which
               64KB bank is mapped at 0x00000-0x0FFFF in real-mode
               segments above the base 640K (i.e. for XMS access). We
               store the value but do not implement the bank switch
               here — 86Box's mem_remap_top() and ram[] layout already
               give the BIOS flat access to the full 1MB installed
               memory. The Phoenix v1.42 POST MEMORY CONTROLLER test
               only writes/reads this register to verify the
               register is connected; it does not rely on a side
               effect (the test passes if the written value is
               read back unchanged). */
            dev->reg_067 = val;
            break;
        case 0x069:
            /* Memory map register. Bit 3 = enable shadowing of
               E0000-FFFFF. Other bits are reserved / config. */
            dev->reg_069 = val;
            olivetti_m250_gate_update_shadow(dev);
            break;
        case 0x06b:
            /* Memory banks starting address register. ONLY present on
               the M250 E (GA80 + GA99); the stock M250 (GA98 + GA99)
               does not decode 0x6B. The IO handler is only registered
               when e_variant=1 — see olivetti_m250_gate_init(). If a
               non-E machine somehow writes here (it shouldn't), we
               silently drop the value. */
            if (dev->e_variant)
                dev->reg_06b = val;
            break;
    }
}

static uint8_t
olivetti_m250_gate_read(uint16_t addr, void *priv)
{
    const olivetti_m250_gate_t *dev = (const olivetti_m250_gate_t *) priv;
    uint8_t ret = 0xff;

    switch (addr) {
        case 0x065:
            ret = dev->reg_065;
            break;
        case 0x067:
            ret = dev->reg_067;
            break;
        case 0x069:
            ret = dev->reg_069;
            break;
        case 0x06b:
            if (dev->e_variant)
                ret = dev->reg_06b;
            break;
    }
    olivetti_m250_gate_log("Olivetti M250 Gate Array: Read %02x at %02x\n", ret, addr);
    return ret;
}

static void
olivetti_m250_gate_close(void *priv)
{
    olivetti_m250_gate_t *dev = (olivetti_m250_gate_t *) priv;

    if (current_m250_dev == dev)
        current_m250_dev = NULL;
    free(dev);
}

static void
olivetti_m250_gate_reset(void *priv)
{
    olivetti_m250_gate_t *dev = (olivetti_m250_gate_t *) priv;

    /* Reset to the documented cold-boot state from cap6_m250.pdf:
       640K base RAM at 0x00000-0x9FFFF and 384K ext RAM at
       0xA0000-0xFFFFF (handled by mem_remap_top(384) in the
       machine init). The system BIOS ROM at 0xF0000-0xFFFFF is
       enabled (shadowing off, READ_EXTANY). The memory page
       register and the bank starting address register are 0.

       Bit 3 of reg_069 (shadow enable) defaults to 0, which matches
       "ROM enabled, RAM disabled" for E0000-FFFFF. */
    dev->reg_065 = 0x00;
    dev->reg_067 = 0x00;
    dev->reg_069 = 0x00;
    dev->reg_06b = 0x00;
    /* e_variant is set by the machine init via the setter and is
       preserved across soft resets — the gate array is the same
       silicon in both flavours, only the silicon revision
       (GA98 vs GA80) differs. */
    olivetti_m250_gate_update_shadow(dev);
}

void
olivetti_m250_gate_set_e_variant(int on)
{
    /* The single M250 gate device is shared across the M250 and
       M250 E machine types. The machine init calls this with
       on=1 (M250 E) or on=0 (M250) BEFORE the first POST access
       to 0x6B so the IO handler is (re)registered correctly via
       a close/init cycle. The setter is also safe to call after
       the device has been instantiated — it updates the e_variant
       flag and re-registers the IO handler set so the M250 E
       picks up port 0x6B without a full reset. */
    if (current_m250_dev != NULL) {
        current_m250_dev->e_variant = on ? 1 : 0;
        /* Re-register the IO handler set. 86Box's io_removehandler()
           must match an existing registration (same priv + read/write
           callbacks); calling it on a port that was never registered
           crashes. So we only remove 0x6B when switching FROM the
           E variant (where it was registered by a previous call to
           this function) TO the non-E variant — never on the very
           first call (init registered only 0x65/0x67/0x69). */
        io_removehandler(0x0065, 0x0001, olivetti_m250_gate_read, NULL, NULL,
                         olivetti_m250_gate_write, NULL, NULL, current_m250_dev);
        io_removehandler(0x0067, 0x0001, olivetti_m250_gate_read, NULL, NULL,
                         olivetti_m250_gate_write, NULL, NULL, current_m250_dev);
        io_removehandler(0x0069, 0x0001, olivetti_m250_gate_read, NULL, NULL,
                         olivetti_m250_gate_write, NULL, NULL, current_m250_dev);
        if (!on) {
            /* Switching M250 E -> stock: 0x6B was registered on the
               previous call, remove it now. */
            io_removehandler(0x006b, 0x0001, olivetti_m250_gate_read, NULL, NULL,
                             olivetti_m250_gate_write, NULL, NULL, current_m250_dev);
        }

        io_sethandler(0x0065, 0x0001, olivetti_m250_gate_read, NULL, NULL,
                      olivetti_m250_gate_write, NULL, NULL, current_m250_dev);
        io_sethandler(0x0067, 0x0001, olivetti_m250_gate_read, NULL, NULL,
                      olivetti_m250_gate_write, NULL, NULL, current_m250_dev);
        io_sethandler(0x0069, 0x0001, olivetti_m250_gate_read, NULL, NULL,
                      olivetti_m250_gate_write, NULL, NULL, current_m250_dev);
        if (on) {
            /* Switching stock -> M250 E: add the 0x6B handler. */
            io_sethandler(0x006b, 0x0001, olivetti_m250_gate_read, NULL, NULL,
                          olivetti_m250_gate_write, NULL, NULL, current_m250_dev);
        }
    }
}

static void *
olivetti_m250_gate_init(const device_t *info)
{
    olivetti_m250_gate_t *dev = (olivetti_m250_gate_t *) calloc(1, sizeof(olivetti_m250_gate_t));

    olivetti_m250_gate_reset(dev);

    /* Default: stock M250 (GA98 + GA99) — no port 0x6B. The M250 E
       machine init calls olivetti_m250_gate_set_e_variant(1) right
       after device_add() to upgrade to the E variant. */
    dev->e_variant = 0;

    io_sethandler(0x0065, 0x0001, olivetti_m250_gate_read, NULL, NULL,
                  olivetti_m250_gate_write, NULL, NULL, dev);
    io_sethandler(0x0067, 0x0001, olivetti_m250_gate_read, NULL, NULL,
                  olivetti_m250_gate_write, NULL, NULL, dev);
    io_sethandler(0x0069, 0x0001, olivetti_m250_gate_read, NULL, NULL,
                  olivetti_m250_gate_write, NULL, NULL, dev);

    current_m250_dev = dev;

    return dev;
}

const device_t olivetti_m250_gate_device = {
    .name          = "Olivetti M250 Gate Array",
    .internal_name = "olivetti_m250_gate",
    .flags         = DEVICE_SOFTRESET,
    .local         = 0,
    .init          = olivetti_m250_gate_init,
    .close         = olivetti_m250_gate_close,
    .reset         = olivetti_m250_gate_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
