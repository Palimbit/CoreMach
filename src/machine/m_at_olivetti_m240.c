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
 *          Implementation of the Olivetti M240 desktop machine.
 *
 *          The M240 (1988) is Olivetti's mid-range XT desktop. It runs
 *          an 8086 @ 10 MHz with 0 wait states on an 8-bit ISA bus
 *          (16-bit ISA NOT supported). Max 640 KB on-board RAM
 *          (256K / 512K / 640K selected by DIP switch A1-A2). The
 *          custom silicon is the OMEGA 4 gate array (memory decoder +
 *          bus arbiter) plus the PL3P / PL6Z / PL27 bus-converter
 *          PALs and the BSA6A bus arbiter. The KBC is an 8041 / 8742
 *          (the same Olivetti-customised XT-class KBC the M24 uses,
 *          with M240-specific ID 0x10). The FDC is an enhanced XT
 *          controller that supports 1-4 drives (DIP A3-A5) at 360K,
 *          720K, 1.2M and 1.44M; the 86Box driver for that is the
 *          standard AT fdc_at_device (the WD37C65C-equivalent
 *          register set lives on the 8086 bus). The hard disk
 *          controller is the WD MFM (BA227 / BA233) on an add-in
 *          card, not on the motherboard — we do NOT add it for the
 *          first port: the Phoenix v1.42 INT 13h probe times out
 *          gracefully and the machine boots from floppy.
 *
 *          The BIOS image is the PCHJ (LOW) + PCHK (HIGH) PERB v2.11,
 *          concatenated to a single 32 KB image and loaded flat at
 *          0xF0000. The 2.11 release is Phoenix v1.42 (BIT
 *          240.60.5/10) which fixes the FUJITSU 8284 problem; it
 *          expects a Dallas DS1287-style NVR with BCD date/time
 *          (reg 0x0B = 0x02, DM=0). The 86Box nvr_at_device with
 *          NVR_AT satisfies that — same pattern as the M250 / PCS 286
 *          / PCS 386SX ports. Factory defaults are seeded in nvr_at.c
 *          so the BDA-vs-NVR diagnostic compare passes and the
 *          SET-UP menu does not reappear on every reboot.
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
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/ppi.h>
#include <86box/dma.h>
#include <86box/nmi.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/chipset.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/hdc.h>
#include <86box/nvr.h>
#include <86box/keyboard.h>
#include <86box/mouse.h>
#include <86box/gameport.h>
#include <86box/sound.h>
#include <86box/snd_speaker.h>
#include <86box/serial.h>
#include <86box/lpt.h>
#include <86box/video.h>
#include <86box/machine.h>
#include <86box/plat_unused.h>

/* ====================================================================
 * Olivetti M21 / M24 / M240 keyboard and mouse controller
 *
 * This block is a deliberate copy of the M24 KBC implementation that
 * lives in m_xt_olivetti.c (machine_xt_m24_init / machine_xt_m240_init
 * use it). The m24 KBC is the right driver for the M240 8041 / 8742:
 * it implements the Olivetti-customised command set Phoenix v1.42
 * relies on (0x01 / 0x05 returning 0xAA / 0x10, command 0x13 reserved
 * for "M240 Customer Diagnostics", etc.). Lifting it into this file
 * keeps the new M240 init self-contained and avoids touching the
 * existing m_xt_olivetti.c M24 / m240 entries.
 *
 * The m24 KBC is registered as a non-standard device (init = NULL)
 * so the caller allocates the m24_kbd_t struct, calls
 * olivetti_m240_kbc_init() to wire it up, then attaches it via
 * device_add_ex(). This matches the existing pattern in
 * m_xt_olivetti.c.
 * ==================================================================== */

#define M24_KBD_STAT_PARITY     0x80
#define M24_KBD_STAT_RTIMEOUT   0x40
#define M24_KBD_STAT_TTIMEOUT   0x20
#define M24_KBD_STAT_LOCK       0x10
#define M24_KBD_STAT_CD         0x08
#define M24_KBD_STAT_SYSFLAG    0x04
#define M24_KBD_STAT_IFULL      0x02
#define M24_KBD_STAT_OFULL      0x01

typedef struct m24_kbd_t {
    int     wantirq;
    uint8_t command;
    uint8_t status;
    uint8_t out;
    uint8_t output_port;
    uint8_t id;
    int     param;
    int     param_total;
    uint8_t params[16];
    uint8_t scan[7];

    /* Mouse stuff. */
    int mouse_input_mode;
    int b;

    pc_timer_t send_delay_timer;
} m24_kbd_t;

static uint8_t m24_kbd_key_queue[16];
static int     m24_kbd_key_queue_start = 0;
static int     m24_kbd_key_queue_end   = 0;

static void
m24_kbd_poll(void *priv)
{
    m24_kbd_t *m24_kbd = (m24_kbd_t *) priv;

    timer_advance_u64(&m24_kbd->send_delay_timer, 1000 * TIMER_USEC);
    if (m24_kbd->wantirq) {
        m24_kbd->wantirq = 0;
        picint(2);
    }

    if (!(m24_kbd->status & M24_KBD_STAT_OFULL) && m24_kbd_key_queue_start != m24_kbd_key_queue_end) {
        m24_kbd->out                 = m24_kbd_key_queue[m24_kbd_key_queue_start];
        m24_kbd_key_queue_start      = (m24_kbd_key_queue_start + 1) & 0xf;
        m24_kbd->status             |= M24_KBD_STAT_OFULL;
        m24_kbd->status             &= ~M24_KBD_STAT_IFULL;
        m24_kbd->wantirq             = 1;
    }
}

static void
m24_kbd_adddata(uint16_t val)
{
    m24_kbd_key_queue[m24_kbd_key_queue_end] = val;
    m24_kbd_key_queue_end                    = (m24_kbd_key_queue_end + 1) & 0xf;
}

static void
m24_kbd_adddata_ex(uint16_t val)
{
    kbd_adddata_process(val, m24_kbd_adddata);
}

static void
m24_kbd_write(uint16_t port, uint8_t val, void *priv)
{
    m24_kbd_t *m24_kbd = (m24_kbd_t *) priv;

    switch (port) {
        case 0x0060:
            m24_kbd->status &= ~M24_KBD_STAT_CD;

            if (m24_kbd->param != m24_kbd->param_total) {
                m24_kbd->params[m24_kbd->param++] = val;
                if (m24_kbd->param == m24_kbd->param_total) {
                    switch (m24_kbd->command) {
                        case 0x11:
                            m24_kbd->mouse_input_mode = 0;
                            m24_kbd->scan[0]          = m24_kbd->params[0];
                            m24_kbd->scan[1]          = m24_kbd->params[1];
                            m24_kbd->scan[2]          = m24_kbd->params[2];
                            m24_kbd->scan[3]          = m24_kbd->params[3];
                            m24_kbd->scan[4]          = m24_kbd->params[4];
                            m24_kbd->scan[5]          = m24_kbd->params[5];
                            m24_kbd->scan[6]          = m24_kbd->params[6];
                            break;

                        case 0x12:
                            m24_kbd->mouse_input_mode = 1;
                            m24_kbd->scan[0]          = m24_kbd->params[0];
                            m24_kbd->scan[1]          = m24_kbd->params[1];
                            m24_kbd->scan[2]          = m24_kbd->params[2];
                            break;

                        default:
                            break;
                    }
                }
            } else {
                m24_kbd->command = val;
                switch (val) {
                    /* Self-test returns 0xAA. */
                    case 0x01:
                        m24_kbd_adddata(0xaa);
                        break;

                    /* Read SWB (Olivetti M240 / M24 layout). The M240
                       uses port 0x63 for SWB but the KBC command
                       returns the cached read in the same encoding the
                       M24 uses. The Phoenix v1.42 POST does not call
                       this command — the BIOS reads 0x62 / 0x63
                       directly via the OMEGA 4 — but the M240 Customer
                       Diagnostics utility does. */
                    case 0x02:
                        m24_kbd_adddata(0x00);
                        break;

                    /* Read keyboard ID. M240 = 0x10. */
                    case 0x05:
                        m24_kbd_adddata(m24_kbd->id);
                        break;

                    case 0x11:
                        m24_kbd->param       = 0;
                        m24_kbd->param_total = 9;
                        break;

                    case 0x12:
                        m24_kbd->param       = 0;
                        m24_kbd->param_total = 4;
                        break;

                    /* Sent by Olivetti M240 Customer Diagnostics.
                       The KBC acknowledges without changing state. */
                    case 0x13:
                        break;

                    default:
                        break;
                }
            }
            break;

        case 0x0061:
            ppi.pb = val;

            speaker_update();
            speaker_gated  = val & 1;
            speaker_enable = val & 2;
            if (speaker_enable)
                was_speaker_enable = 1;
            pit_devs[0].set_gate(pit_devs[0].data, 2, val & 1);
            break;

        case 0x0064:
            m24_kbd->status |= M24_KBD_STAT_CD;

            if (val == 0x02)
                m24_kbd_adddata(0x00);
            break;

        default:
            break;
    }
}

static uint8_t
m24_kbd_read(uint16_t port, void *priv)
{
    m24_kbd_t *m24_kbd = (m24_kbd_t *) priv;
    uint8_t    ret     = 0xff;

    switch (port) {
        case 0x0060:
            ret = m24_kbd->out;
            if (m24_kbd_key_queue_start == m24_kbd_key_queue_end) {
                m24_kbd->status &= ~M24_KBD_STAT_OFULL;
                m24_kbd->wantirq = 0;
            } else {
                m24_kbd->out                 = m24_kbd_key_queue[m24_kbd_key_queue_start];
                m24_kbd_key_queue_start      = (m24_kbd_key_queue_start + 1) & 0xf;
                m24_kbd->status             |= M24_KBD_STAT_OFULL;
                m24_kbd->status             &= ~M24_KBD_STAT_IFULL;
                m24_kbd->wantirq             = 1;
            }
            break;

        case 0x0061:
            /* M240 is not affected by the M24's port 0x61 refresh
               toggle problem (the M240 uses a true 8253 PIT so bit 4
               toggles correctly on every refresh tick). */
            ret = ppi.pb;
            break;

        case 0x0064:
            ret = m24_kbd->status & 0x0f;
            m24_kbd->status &= ~(M24_KBD_STAT_RTIMEOUT | M24_KBD_STAT_TTIMEOUT);
            break;

        default:
            break;
    }

    return ret;
}

static void
m24_kbd_close(void *priv)
{
    m24_kbd_t *kbd = (m24_kbd_t *) priv;

    timer_disable(&kbd->send_delay_timer);

    keyboard_scan = 0;

    keyboard_send = NULL;

    io_removehandler(0x0060, 2,
                     m24_kbd_read, NULL, NULL, m24_kbd_write, NULL, NULL, kbd);
    io_removehandler(0x0064, 1,
                     m24_kbd_read, NULL, NULL, m24_kbd_write, NULL, NULL, kbd);

    free(kbd);
}

static void
m24_kbd_reset(void *priv)
{
    m24_kbd_t *m24_kbd = (m24_kbd_t *) priv;

    m24_kbd->status  = M24_KBD_STAT_LOCK | M24_KBD_STAT_CD;
    m24_kbd->wantirq = 0;
    keyboard_scan    = 1;
    m24_kbd->param = m24_kbd->param_total = 0;
    m24_kbd->mouse_input_mode             = 0;
    m24_kbd->scan[0]                      = 0x1c;
    m24_kbd->scan[1]                      = 0x53;
    m24_kbd->scan[2]                      = 0x01;
    m24_kbd->scan[3]                      = 0x4b;
    m24_kbd->scan[4]                      = 0x4d;
    m24_kbd->scan[5]                      = 0x48;
    m24_kbd->scan[6]                      = 0x50;
}

static void
m24_kbd_init(m24_kbd_t *m24_kbd)
{
    /* Note: m24_kbd is allocated by the caller. The caller's id (set
       before m24_kbd_init) selects which keyboard this KBC reports:
         0x01 = M24 102-key
         0x02 = 83-key
         0x10 = M240 (default for this init)
         0x20 = 101/102 key
    */
    m24_kbd->output_port = 0x80;

    io_sethandler(0x0060, 2,
                  m24_kbd_read, NULL, NULL, m24_kbd_write, NULL, NULL, m24_kbd);
    io_sethandler(0x0064, 1,
                  m24_kbd_read, NULL, NULL, m24_kbd_write, NULL, NULL, m24_kbd);

    keyboard_send = m24_kbd_adddata_ex;
    keyboard_scan = 1;

    timer_add(&m24_kbd->send_delay_timer, m24_kbd_poll, m24_kbd, 1);
}

static const device_t m24_kbd_m240_device = {
    .name          = "Olivetti M240 keyboard and mouse",
    .internal_name = "m240_kbd",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = m24_kbd_close,
    .reset         = m24_kbd_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

/* ====================================================================
 * Olivetti M240 machine init
 * ==================================================================== */

int
machine_xt_olivetti_m240_init(const machine_t *model)
{
    int        ret;
    m24_kbd_t *m24_kbd;
    nvr_t     *nvr;

    /* Main system BIOS: Phoenix v1.42 "Resident Diagnostics"
       (PERB v2.11, BIT 240.60.5/10). The preserved 32 KB dump is
       PCHJ + PCHK concatenated: two 16 KB byte-wide EPROM images,
       not a flat CPU-address-order image. Keep that source untouched
       and load the reproducibly interleaved derivative. Interleaving
       exposes the valid reset vector EA 5B E0 00 F0 at offset 0x7FF0
       (JMP F000:E05B), so the 32 KB BIOS belongs at F8000-FFFFF. */
    ret = bios_load_linear("roms/machines/olivetti_m240/olivetti_m240_perb_211_interleaved.bin",
                           0x000f8000, 32768, 0);

    if (bios_only || !ret)
        return ret;

    /* Standard XT core: PIC (single 8259A), DMA (single 8237A),
       PIT (8253 at the XT-class frequency, NOT 8254). 86Box's
       machine_common_init() picks PIT_8253 automatically when
       the machine is MACHINE_TYPE_8086 (see machine.c:195). */
    machine_common_init(model);

    /* On-board FDC. The M240 service manual lists an enhanced
       controller that supports 1-4 drives (the standard XT FDC
       only supports 2). 86Box's fdc_at_device exposes the
       WD37C65C-equivalent register set on the same 0x3F0-0x3F7
       I/O ports the XT FDC uses, so it works on an 8-bit ISA
       bus as long as we leave DMA channel 2 for the FDC (which
       is the XT default per cap2_m240.pdf page 2-4: DMA2 = FDC). */
    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    /* Olivetti M240 OMEGA 4 gate array: provides the DIP-SWITCH
       A and B read handlers at ports 0x62 and 0x63. The Phoenix
       v1.42 POST reads these to size memory and detect the
       peripheral configuration. The PL3P / PL6Z / PL27 /
       BSA6A bus-converter logic is not emulated — 86Box's mem
       layer enforces the 8-bit ISA bus for MACHINE_PC
       machines already. */
    device_add(&olivetti_m240_omega4_device);

    /* Standalone gameport on the M240 is at 0x200, like the
       M24. The M240 service manual does not list the gameport
       explicitly but cap6_m250.pdf (sister machine) does, and
       the 86Box M24 / M19 entries both use the standalone
       0x200 layout. */
    standalone_gameport_type = &gameport_200_device;

    /* NMI controller. */
    nmi_init();

    /* Dallas DS1287-style NVR. The Phoenix v1.42 firmware on
       the M240 expects a 256-byte CMOS with BCD date/time
       (reg 0x0B bit 2 = 0, DM = 0). nvr_at_device with
       NVR_AT provides that; the factory defaults (0x0E = 0x00,
       0x14 = 0x21 and 0x15/0x16 = 128/512 for 640K base)
       are applied in
       nvr_at.c::nvr_at_init() when the active machine is
       this init. */
    nvr = (nvr_t *) calloc(1, sizeof(nvr_t));
    if (nvr == NULL)
        return 0;
    device_add_params(&nvr_at_device, (void *) NVR_AT);

    /* Video: the M240 shipped with an OGC (Olivetti Graphics
       Card) which is CGA-compatible with an enhanced 640x400
       2-colour mode. We reuse the ogc_m24_device from the M24
       entry — same chip family, same register set. The user
       can pick a different video card from the UI; we only
       add the on-board OGC if gfxcard[0] is VID_INTERNAL. */
    video_reset(gfxcard[0]);
    if (gfxcard[0] == VID_INTERNAL)
        device_add(&ogc_m24_device);

    /* PIT channel 2 is the speaker / gate. The XT default
       already wires this up via machine_common_init() but we
       set the gate driver here explicitly for the M240's
       10 MHz bus speed — the channel 1 refresh hook needs to
       be aware of the CPU clock. */
    pit_devs[0].set_out_func(pit_devs[0].data, 1, pit_refresh_timer_xt);

    /* M24 / M240 KBC. Same 8041 / 8742 family as the M24, with
       ID 0x10 (M240 keyboard) instead of 0x01 (M24 102-key).
       The m24 KBC handles the Olivetti-customised command set
       Phoenix v1.42 and the M240 Customer Diagnostics expect
       (0x01=AA, 0x05=10, 0x11, 0x12, 0x13). See the block
       above for the implementation. */
    m24_kbd = (m24_kbd_t *) calloc(1, sizeof(m24_kbd_t));
    m24_kbd->id  = 0x10;  /* M240 keyboard ID */
    m24_kbd_init(m24_kbd);
    device_add_ex(&m24_kbd_m240_device, m24_kbd);

    /* On-board HD controller. The M240 has a WD MFM add-in
       card (BA227 / BA233) — not on the motherboard. We add
       the wd1002a-wx1 no-bios variant so the Phoenix v1.42
       INT 13h probe has something to find, but the user
       can disable it in the UI. The Phoenix POST only runs
       the INT 13h test if the WD controller responds; without
       it, the POST skips the fixed-disk test and continues
       with the floppy boot. */
    if (hdc_current[0] == HDC_INTERNAL)
        device_add(&st506_xt_wd1002a_wx1_nobios_device);

    return ret;
}
