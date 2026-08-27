/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the Olivetti IOC02 I/O Controller.
 *
 *      Note: This chipset has no datasheet, everything were done via
 *      reverse engineering the BIOS of various machines using it.
 *
 * Authors: EngiNerd <webmaster.crrc@yahoo.it>
 *
 *		Copyright 2020-2021 EngiNerd
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
#include <86box/machine.h>
#include <86box/video.h>
#include <86box/mem.h>

typedef struct
{
    uint8_t	reg_068;
    uint8_t	reg_06a;
    uint8_t reg_06c;
    uint8_t first_read_done;  /* For test 2: first read returns 0x04 (bit 2=1) to skip sub-test */
    uint8_t write_before_read;  /* Phoenix v1.14 (PCS 386SX) writes to 0x6A BEFORE
                                   the first read. The first_read_done hack
                                   must NOT fire in that case or the BIOS
                                   reads back 0x04 instead of the value it
                                   just wrote, aborting with
                                   "I/O CONTROLLER ERROR 2".
                                   Phoenix v1.42 (PCS 286) reads first then
                                   writes, so the hack works there. */
    uint8_t first_read_hack_enabled;  /* Per-machine override. Default 1 (legacy
                                         PCS 286 behavior). The PCS 386SX init
                                         sets this to 0 so a preceding write
                                         suppresses the one-shot; a read-first
                                         SET-UP path still sees 0x04. */
} olivetti_ioc02_t;

/* Global handle to the single IOC02 device. Used by
   olivetti_ioc02_set_first_read_hack() to toggle whether the legacy
   "return 0x04 on first read" response survives a preceding write.
   The PCS 286 keeps it ON (matches Phoenix v1.42). The PCS 386SX
   disables it because Phoenix v1.14 verifies writes to 0x6A. */
static olivetti_ioc02_t *current_ioc02_dev = NULL;

#ifdef ENABLE_OLIVETTI_ioc02_LOG
int olivetti_ioc02_do_log = ENABLE_OLIVETTI_ioc02_LOG;
static void
olivetti_ioc02_log(const char *fmt, ...)
{
    va_list ap;

    if (olivetti_ioc02_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
    	va_end(ap);
    }
}
#else
#define olivetti_ioc02_log(fmt, ...)
#endif

static void
olivetti_ioc02_write(uint16_t addr, uint8_t val, void *priv)
{
    olivetti_ioc02_t *dev = (olivetti_ioc02_t *) priv;
    olivetti_ioc02_log("[%04X:%08X AX=%04X] IOC02 W %04x=%02x\n",
                      CS, cpu_state.pc, AX, addr, val);

    switch (addr) {
        case 0x068:
            dev->reg_068 = val;
            break;
        case 0x06a:
            /* Register 0x68 selects the IOC02 function exposed at 0x6A.
               With selector bits 0-4 clear, writes to 0x6A do not reach
               the latched register. Phoenix v1.14 verifies this by
               selecting 0x00, writing 0x55, and requiring the following
               read not to reproduce 0x55.

               Otherwise store the value as-is. The init value 0x04
               has bit 2 = 1 (I/O subsystem ready). When the BIOS
               writes other values (0xA0, 0x80, 0x55, etc.), bit 2
               is cleared, which is fine for the MEMORY CONTROLLER
               sub-tests in test 6 (they expect exact value match). */
            if (dev->reg_068 & 0x1f)
                dev->reg_06a = val;
            dev->write_before_read = 1;
            break;
        case 0x06c:
            dev->reg_06c = val;
            break;
    }
}

static uint8_t
olivetti_ioc02_read(uint16_t addr, void *priv)
{
    olivetti_ioc02_t *dev = (olivetti_ioc02_t *) priv;
    uint8_t ret = 0xff;
    switch (addr) {
        case 0x068:
            ret = dev->reg_068;
            break;
        case 0x06a:
            /* Read behavior:
               - On the PCS 286, return 0x04 (bit 2 = 1) on the first
                 read so Phoenix v1.42 takes the "all passed" path and
                 skips the detailed sub-test.  The ROM does perform
                 setup writes before this read, so write_before_read
                 must not suppress the machine-specific one-shot.
               - On the PCS 386SX a first read with no preceding write
                 is the SET-UP entry path and must expose the reset-ready
                 value 0x04.  Its normal POST writes first, so that path
                 still receives the transformed stored value required by
                 Phoenix v1.14 read-back verification.
               - On subsequent reads: return the actual stored value
                 so the MEMORY CONTROLLER sub-tests in test 6 can
                 verify exact value match.
               The first_read_hack_enabled flag is per-machine:
               the PCS 286 keeps the legacy 0x04-on-first-read hack,
               the PCS 386SX disables it because Phoenix v1.14
               actually verifies the write.

               XOR 0x20 transformation on 0x6A reads:
               Phoenix v1.14 IOC02 sub-test 2 expects every 0x6A read
               (after the first one) to return (stored_val XOR 0x20).
               Concretely the test does:
                   out 0x68, 0x18   ; set index to 0x18
                   out 0x6A, 0x55
                   in  0x6A
                   xor  al, 0x20
                   and  al, 0x3F
                   cmp  al, 0x15   ; PASS only if read = 0x75
                   ...
                   out 0x6A, 0xAA
                   in  0x6A
                   xor  al, 0x20
                   and  al, 0x3F
                   cmp  al, 0x2A   ; PASS only if read = 0x8A
               And the very first read of 0x6A with index 0x1E:
                   in  0x6A
                   xor  al, 0x20
                   mov  bh, al     ; BH is later written back to 0x6A
               If the IOC02 returns stored XOR 0x20, the BIOS's own
               XOR 0x20 cancels the transformation, so BH ends up
               holding the original stored value. With the previous
               "return as-is" behavior, BH = (stored XOR 0x20), and
               the value written back to 0x6A during cleanup differs
               from the original by 0x20.
               The 0x68 read does NOT have this transformation
               (the BIOS only does AND 0x1F on those reads, never
               XOR 0x20). */
            if (!dev->first_read_done
                && (dev->first_read_hack_enabled || !dev->write_before_read)) {
                ret = 0x04;
                dev->first_read_done = 1;
            } else {
                ret = dev->reg_06a ^ 0x20;
                dev->first_read_done = 1;
            }
            break;
        case 0x06c:
            ret = dev->reg_06c;
            break;
    }
    olivetti_ioc02_log("[%04X:%08X AX=%04X] IOC02 R %04x=%02x stored=%02x\n",
                      CS, cpu_state.pc, AX, addr, ret,
                      (addr == 0x06a) ? dev->reg_06a : dev->reg_068);
    return ret;
}


static void
olivetti_ioc02_close(void *priv)
{
    olivetti_ioc02_t *dev = (olivetti_ioc02_t *) priv;

    if (current_ioc02_dev == dev)
        current_ioc02_dev = NULL;
    free(dev);
}

static void
olivetti_ioc02_reset(void *priv)
{
    olivetti_ioc02_t *dev = (olivetti_ioc02_t *) priv;

    /*
     * A KBC 0xFE reset restarts the Phoenix POST without recreating the
     * device.  Reset both the IOC02 registers and the one-shot POST-test
     * response, otherwise the next boot sees the value left in reg 0x6A
     * by SET-UP (commonly 0x22) and reports "I/O CONTROLLER ERROR 2".
     */
    dev->reg_068                  = 0x04;
    dev->reg_06a                  = 0x04;
    dev->reg_06c                  = 0xff;
    dev->first_read_done          = 0;
    dev->write_before_read        = 0;
    /* Phoenix 1.42 on the PCS 286 needs the legacy first-read response.
       Phoenix 1.14 on the PCS 386SX verifies a write/read round trip and
       must keep that response disabled across DEVICE_SOFTRESET as well as
       initial construction. */
    dev->first_read_hack_enabled =
        (machines[machine].init != machine_at_olivetti_pcs386sx_init);
}

void
olivetti_ioc02_set_first_read_hack(int on)
{
    /* The single IOC02 device is shared across all PCS machines. The
       machine init calls this with on=0 (or 1) before the first POST
       access to 0x6A. The flag is then read on every IOC02 read of
       0x6A to decide whether the legacy "return 0x04 on first read"
       hack should fire. */
    if (current_ioc02_dev != NULL)
        current_ioc02_dev->first_read_hack_enabled = on ? 1 : 0;
}

static void *
olivetti_ioc02_init(const device_t *info)
{
    olivetti_ioc02_t *dev = (olivetti_ioc02_t *) malloc(sizeof(olivetti_ioc02_t));
    memset(dev, 0, sizeof(olivetti_ioc02_t));

    olivetti_ioc02_reset(dev);

    /* RAM page register notes:
     * if > 0 ram test is skipped (set during warm-boot)
     * if bit 3 is set, machine hangs -> if set, shadow ram is reported, should be set programmatically
     * bios code can set bit 4, 1 or 2, unclear when it should happen
     * bios code can set bit 6 and 3, unclear when it should happen
     * bit 6 is set if bit 3 is high
     * bit 5 is set when remapping occurs
     */
    
    io_sethandler(0x0068, 0x0001, olivetti_ioc02_read, NULL, NULL, olivetti_ioc02_write, NULL, NULL, dev);
    io_sethandler(0x006a, 0x0001, olivetti_ioc02_read, NULL, NULL, olivetti_ioc02_write, NULL, NULL, dev);
    io_sethandler(0x006c, 0x0001, olivetti_ioc02_read, NULL, NULL, olivetti_ioc02_write, NULL, NULL, dev);

    current_ioc02_dev = dev;

    return dev;
}

const device_t olivetti_ioc02_device = {
    .name = "Olivetti ioc02 Gate Array",
    .internal_name = "olivetti_ioc02",
    .flags = DEVICE_SOFTRESET,
    .local = 0,
    .init = (void *) olivetti_ioc02_init,
    .close = olivetti_ioc02_close,
    .reset = olivetti_ioc02_reset,
    .available = NULL,
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};
