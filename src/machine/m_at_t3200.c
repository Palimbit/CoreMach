/*
 * BluMach experimental Toshiba T3200 platform and PEGA2/AGS subset.
 * Copyright 2026 rtzor, Project BluMach.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Uses the 86Box AT and EGA cores with their original provenance retained.
 * See doc/machines/toshiba-t3200.md for evidence, assumptions and limitations.
 * The original T3200 is distinct from both the T3100e and T3200SX.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <wchar.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/keyboard.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/hdc.h>
#include <86box/video.h>
#include <86box/vid_ega.h>
#include <86box/machine.h>

#define T3200_AGS_ROM "roms/machines/t3200/AGS_IC8_035C.BIN"
#define T3200_LIM_FRAME 0xd0000
#define T3200_LIM_PAGE_SIZE 0x4000
#define T3200_LIM_PAGES 24

/* BIOS 4.61, F000:284D. The original T3200 has eight register latches;
   the additional T3100e 0258/0268 banks are not part of this model. */
static const uint16_t t3200_lim_ports[8] = {
    0x0208, 0x4208, 0x8208, 0xc208, 0x0218, 0x4218, 0x8218, 0xc218
};

typedef struct t3200_t {
    ega_t ega;
    mem_mapping_t lim_mapping[4];
    uint8_t lim_regs[8];
    uint32_t lim_base[4];
    unsigned lim_trace_count;
    int lim_card; /* Experimental 3 MiB option in PJ2 EMS-only mode. */
    uint8_t sram[0x800];
    uint8_t control;
    uint8_t mode[2];
    uint8_t crtc_control;
    uint8_t crtc_extension;
    uint16_t last_mode_port;
    uint64_t last_mode_sequence;
    uint64_t unlocked_sequence;
    unsigned trace_count;
    unsigned samples;
    int trace;
    int provisional_50;
    int provisional_display;
    int external_display;
    int last_display_request;
    uint32_t plasma16[256];
    uint32_t plasma64[256];
    pc_timer_t sample_timer;
} t3200_t;

/* UI requests are consumed by the original BIOS timer service through the
   T3200 high KBC status port, not by editing guest RAM or calling ROM code. */
static atomic_int t3200_display_active = -1;
static atomic_int t3200_display_target = 0;
static atomic_int t3200_extension_target = 0;
#ifndef T3200_CALLBACK_TEST
static t3200_t *t3200_platform;
#endif

int t3200_display_get(void) { return atomic_load(&t3200_display_active); }
void t3200_display_request(int external)
{
    if (t3200_display_get() >= 0)
        atomic_store(&t3200_display_target, !!external);
}

static uint8_t t3200_notification_status(uint8_t at_status)
{
    return (at_status & 0x0f) | (atomic_load(&t3200_display_target) ? 0x80 : 0) |
           (atomic_load(&t3200_extension_target) ? 0x20 : 0);
}

void t3200_display_extend(void)
{
    if (t3200_display_get() >= 0)
        atomic_fetch_xor(&t3200_extension_target, 1);
}

int t3200_display_hotkey(int down, uint16_t scan)
{
    static int swallowed[3];
    if (t3200_display_get() < 0) { swallowed[0] = swallowed[1] = swallowed[2] = 0; return 0; }
    int key = (scan == 0x147 || scan == 0x47) ? 0 :
              (scan == 0x14f || scan == 0x4f) ? 1 :
              (scan == 0x150 || scan == 0x50) ? 2 : -1;
    if (key < 0) return 0;
    if (!down && swallowed[key]) { swallowed[key] = 0; return 1; }
    if (down && keyboard_recv_ui(0x11d)) {
        if (swallowed[key]) return 1;
        swallowed[key] = 1;
        if (key == 2) t3200_display_extend();
        else t3200_display_request(key);
        return 1;
    }
    return 0;
}

/* AGS C000:1202 programs plasma attribute codes 0,2,4,6 from the
   CELT's upper two bits. XCHAD changes that table through INT10 AH73;
   the ROM remains responsible for selecting and programming each mode.
   Only the host RGB intensities are approximate, not the level ordering.
   External RGB comparison continues to use the ordinary EGA tables. */
static uint32_t
t3200_plasma_color(unsigned code)
{
    static const uint32_t levels[4] = { 0x000000, 0x9c400e, 0xe06018, 0xff8030 };
    return levels[(code >> 1) & 3];
}

static void
t3200_plasma_palette(t3200_t *dev)
{
    for (unsigned c = 0; c < 256; c++) {
        dev->plasma16[c] = dev->plasma64[c] = t3200_plasma_color(c);
    }
    dev->ega.output_palette16 = dev->plasma16;
    dev->ega.output_palette64 = dev->plasma64;
    dev->ega.pallook = dev->ega.vres ? dev->plasma16 : dev->plasma64;
    dev->ega.overscan_color = dev->ega.pallook[dev->ega.attrregs[0x11] & (dev->ega.vres ? 15 : 63)];
}

static uint8_t
t3200_lim_read(uint32_t addr, void *priv)
{
    t3200_t *dev = priv;
    if (addr < T3200_LIM_FRAME || addr >= T3200_LIM_FRAME + 0x10000)
        return 0xff;
    uint32_t base = dev->lim_base[(addr >> 14) & 3];
    return base ? ram[base + (addr & (T3200_LIM_PAGE_SIZE - 1))] : 0xff;
}

static void
t3200_lim_write(uint32_t addr, uint8_t val, void *priv)
{
    t3200_t *dev = priv;
    if (addr < T3200_LIM_FRAME || addr >= T3200_LIM_FRAME + 0x10000)
        return;
    uint32_t base = dev->lim_base[(addr >> 14) & 3];
    if (base)
        ram[base + (addr & (T3200_LIM_PAGE_SIZE - 1))] = val;
}

static uint8_t
t3200_lim_in(uint16_t port, void *priv)
{
    t3200_t *dev = priv;
    unsigned reg = ((port >> 14) & 3) | ((port & 0x10) >> 2);
    return dev->lim_regs[reg];
}

static void
t3200_lim_out(uint16_t port, uint8_t val, void *priv)
{
    t3200_t *dev = priv;
    unsigned reg = ((port >> 14) & 3) | ((port & 0x10) >> 2);
    unsigned slot = reg & 3;

    /* The base 24 pages are BIOS-verified. The optional card currently uses
       an explicit compatible approximation of the T3100e top-down backing
       order (m_at_t3100e.c): bank bit extends the 7-bit page number. This is
       not a measured T3200 physical-address decode. Intermediate extended/EMS
       splits and simultaneous bank enable are pending. */
    dev->lim_regs[reg] = val;
    unsigned page = (val & 0x7f) + ((reg >> 2) * 128);
    uint32_t base = 0;
    if (val & 0x80) {
        if (page < T3200_LIM_PAGES)
            base = 0xa0000 + page * T3200_LIM_PAGE_SIZE;
        else if (dev->lim_card && page < T3200_LIM_PAGES + 192)
            base = 0x400000 - (page - T3200_LIM_PAGES + 1) * T3200_LIM_PAGE_SIZE;
    }
    dev->lim_base[slot] = base;
    mem_mapping_set_exec(&dev->lim_mapping[slot], base ? ram + base : NULL);
    if (base)
        mem_mapping_enable(&dev->lim_mapping[slot]);
    else
        mem_mapping_disable(&dev->lim_mapping[slot]);
    if (dev->trace && dev->lim_trace_count++ < 512)
        pclog("T3200 LIM %04X:%04X OUT %04X=%02X slot=%u backing=%05X\n",
              CS, cpu_state.pc, port, val, slot, base);
}

#ifndef T3200_CALLBACK_TEST
static video_timings_t t3200_video_timing = {
    .type = VIDEO_ISA, .write_b = 8, .write_w = 16, .write_l = 32,
    .read_b = 8, .read_w = 16, .read_l = 32
};
#endif

/* Presentation correction for the observed BIOS underline shapes. The AGS
   eight-line table and its wrapped end=0 are not a sixteen-line block.
   Align these shapes with the loaded fourteen-line glyph. Other programmed
   shapes keep EGA behavior until PEGA2 cursor semantics are documented. */
static int
t3200_cursor_scanline(ega_t *ega)
{
    t3200_t *dev = ega->priv_parent;
    int start = ega->crtc[10] & 31;
    int end = ega->crtc[11] & 31;
    if (dev->provisional_display && !dev->external_display &&
        ega->dispend == 400 && ega->rowcount == 15 &&
        !(ega->gdcreg[6] & 1) && (ega->crtc[9] & 31) == 7 &&
        ((start == 6 && (end == 0 || end == 7)) || (start == 15 && end == 0)))
        return ega->scanline == 12 || ega->scanline == 13;
    return ega->cursorvisible;
}

static void
t3200_recalctimings(ega_t *ega)
{
    t3200_t *dev = ega->priv_parent;
    ega->cursor_scanline = t3200_cursor_scanline;
    /* Maintenance manual1-11: pixel pitch H=.30mm,V=.36mm. A640x400
       active image is physically4:3; the full720x400 panel is3:2.
       Preserve the programmed raster; never stretch640 columns into720. */
    /* AGS1955 toggles CR1A bits4/5 while retaining a350-line source.
       Present the documented350-to400 expansion without changing VRAM or
       guest counters. Exact PEGA2 duplicated-line placement is approximate. */
    int panel_lines = (ega->crtc[0x12] | ((ega->crtc[7] & 2) << 7)) + 1;
    double ratio = dev->external_display ? 0.0 : 1.2;
    if (!dev->external_display && dev->provisional_display &&
        panel_lines == 350 && (dev->crtc_extension & 0x30) == 0x30)
        ratio = 480.0 / 350.0;
    if (monitors[0].mon_pixel_height_ratio != ratio) {
        monitors[0].mon_pixel_height_ratio = ratio;
        monitors[0].mon_force_resize = 1;
    }

    /* AGS programs CR7=3F in its 400-line table. A VGA interpretation of
       bit 5 gives 929 lines, outside the EGA counter's nine-bit range.
       The PEGA2-specific upper-bit functions remain unresolved. */
    ega->vtotal = (ega->crtc[6] | ((ega->crtc[7] & 1) << 8)) + 2;
    ega->dispend = (ega->crtc[0x12] | ((ega->crtc[7] & 2) << 7)) + 1;
    ega->vsyncstart = (ega->crtc[0x10] | ((ega->crtc[7] & 4) << 6)) + 1;

    if (dev->provisional_display && !dev->external_display &&
        (ega->dispend == 350 || ega->dispend == 400)) {
        /* Explicit display approximation for the observed AGS panel table:
           do not apply the IBM 200-line monitor's host line duplication.
           Its text table uses CR9=7 while loading a 14-line font. Present
           complete glyphs in 16-line cells (25 rows over 400 lines).
           Guest register values are retained; this is not a claimed decode
           of the still-undocumented PEGA2 panel/extension controls. */
        ega->vres = 0;
        if (ega->dispend == 400 && !(ega->gdcreg[6] & 1) && (ega->crtc[9] & 0x1f) == 7)
            ega->rowcount = 15;
    } else
        ega->vres = !(ega->miscout & 0x80);
}

static void
t3200_trace(t3200_t *dev, uint16_t port, uint8_t val, int unlocked)
{
    if (dev->trace && dev->trace_count++ < 2048)
        pclog("T3200 AGS %04X:%04X OUT %04X=%02X unlock=%d crtc=%02X seq=%02X\n",
              CS, cpu_state.pc, port, val, unlocked,
              dev->ega.crtcreg, dev->ega.seqaddr);
}

static int
t3200_ega_mapped(t3200_t *dev, uint32_t addr)
{
    switch (dev->ega.gdcreg[6] & 0x0c) {
        case 0x04: return addr < 0xb0000;
        case 0x08: return addr >= 0xb0000 && addr < 0xb8000;
        case 0x0c: return addr >= 0xb8000;
        default:   return 1;
    }
}

static uint8_t
t3200_video_read(uint32_t addr, void *priv)
{
    t3200_t *dev = priv;
    if ((dev->control == 0x40 || dev->control == 0x41 || (dev->provisional_50 && dev->control == 0x50)) &&
        addr >= 0xa0000 && addr < 0xa0800) {
        cycles -= video_timing_read_b;
        return dev->sram[addr - 0xa0000];
    }
    return t3200_ega_mapped(dev, addr) ? ega_read(addr, &dev->ega) : 0xff;
}

static void
t3200_video_write(uint32_t addr, uint8_t val, void *priv)
{
    t3200_t *dev = priv;
    if ((dev->control == 0x40 || dev->control == 0x41 || (dev->provisional_50 && dev->control == 0x50)) &&
        addr >= 0xa0000 && addr < 0xa0800) {
        cycles -= video_timing_write_b;
        dev->sram[addr - 0xa0000] = val;
    } else if (t3200_ega_mapped(dev, addr))
        ega_write(addr, val, &dev->ega);
}

static uint8_t
t3200_video_in(uint16_t port, void *priv)
{
    t3200_t *dev = priv;
    if (port == 0x3b8 || port == 0x3d8) {
        /* The manual specifies two successive mode-register reads. Only the
           observed same-port byte sequence is supported in this pilot. */
        if (io_access_width == 1 && dev->last_mode_port == port &&
            dev->last_mode_sequence + 1 == io_access_sequence) {
            dev->unlocked_sequence = io_access_sequence + 1;
            dev->last_mode_port = 0;
        } else {
            dev->last_mode_port = io_access_width == 1 ? port : 0;
            dev->unlocked_sequence = 0;
        }
        dev->last_mode_sequence = io_access_sequence;
        return dev->mode[port == 0x3d8];
    }
    return ega_in(port, &dev->ega);
}

static void
t3200_video_out(uint16_t port, uint8_t val, void *priv)
{
    t3200_t *dev = priv;
    int unlocked = io_access_width == 1 &&
                   dev->unlocked_sequence == io_access_sequence;

    if (port == 0x3df) {
        t3200_trace(dev, port, val, unlocked);
        if (unlocked) {
            /* 00 and 40 are the evidence-supported diagnostic sequence.
               Keep an unsupported selector visible; do not equate 50 to 40. */
            dev->control = val;
            if (val != 0x00 && val != 0x01 && val != 0x40 && val != 0x41 && !(val == 0x50 && dev->provisional_50))
                pclog("T3200: unsupported AGS SRAM selector %02X\n", val);
        }
        return;
    }
    if (unlocked && (port == 0x3b5 || port == 0x3d5)) {
        /* The 85/8A transactions bracket CRTC programming through helper
           C000:035A. Keep these extension commands out of horizontal total.
           Their bank/protection semantics are not implemented yet. */
        if (dev->ega.crtcreg == 0 && (val == 0x85 || val == 0x8a)) {
            t3200_trace(dev, port, val, unlocked);
            dev->crtc_control = val;
            return;
        }
        if (dev->ega.crtcreg == 0x1a) {
            t3200_trace(dev, port, val, unlocked);
            dev->crtc_extension = val;
            ega_recalctimings(&dev->ega);
            if (dev->trace) pclog("T3200 extension=%02X lines=%d ratio=%.6f internal=%d\n", val, dev->ega.dispend, monitors[0].mon_pixel_height_ratio, !dev->external_display);
            return;
        }
    }
    if (port == 0x3de || port == 0x3bb || port == 0x3db) {
        /* Log both writes of the observed 05/parameter transaction, including
           the second write without a new unlock. Electrical effect unknown. */
        t3200_trace(dev, port, val, unlocked);
        return;
    }
    if (port == 0x3b8 || port == 0x3d8) {
        dev->mode[port == 0x3d8] = val;
        t3200_trace(dev, port, val, unlocked);
        return;
    }
    if (unlocked || port == 0x3b5 || port == 0x3d5 || port == 0x3c2)
        t3200_trace(dev, port, val, unlocked);
    ega_out(port, val, &dev->ega);
    /* Composite ownership: the callback selects the exact SRAM range and
       delegates only the active EGA aperture. Generic EGA may change its map. */
    mem_mapping_set_addr(&dev->ega.mapping, 0xa0000, 0x20000);
}

#ifndef T3200_CALLBACK_TEST
/* Platform construction is excluded only from the isolated callback test;
   the production build and firmware run exercise it normally. */
void t3200_display_commit(int external)
{
    t3200_t *dev = t3200_platform;
    if (!dev) return;
    extern uint32_t pallook16[256], pallook64[256];
    dev->external_display = !!external;
    atomic_store(&t3200_display_active, !!external);
    if (!external && machine_get_config_int("provisional_plasma"))
        t3200_plasma_palette(dev);
    else {
        dev->ega.output_palette16 = dev->ega.output_palette64 = NULL;
        dev->ega.pallook = dev->ega.vres ? pallook16 : pallook64;
    }
    ega_recalctimings(&dev->ega);
    dev->ega.pallook = dev->ega.vres ?
        (dev->ega.output_palette16 ? dev->ega.output_palette16 : pallook16) :
        (dev->ega.output_palette64 ? dev->ega.output_palette64 : pallook64);
    dev->ega.overscan_color = dev->ega.pallook[dev->ega.attrregs[0x11] & (dev->ega.vres ? 15 : 63)];
    pclog("T3200 display committed by KBC command: %s\n", external ? "external RGB" : "internal plasma");
}

static uint8_t
t3200_kbc_alias_in(uint16_t port, void *priv)
{
    (void) priv;
    uint8_t val = inb(port & 0x7fff);
    t3200_t *dev = priv;
    /* BIOS4.61 F000:9D66 reads bit7 as the requested display. Bits4..6
       are Toshiba notification/status lines, not AT timeout flags. */
    if (port == 0x8064) {
        if (dev->last_display_request != atomic_load(&t3200_display_target)) {
            dev->last_display_request = atomic_load(&t3200_display_target);
            pclog("T3200 display request=%d BDA415=%02X BDA493=%02X at %04X:%04X\n",
                  dev->last_display_request, ram[0x415], ram[0x493], CS, cpu_state.pc);
        }
        /* Unlike B4's active-plasma bit, this notification bit means End
           (external) when set; Home (internal) clears it. */
        val = t3200_notification_status(val);
    }
    return val;
}

static void
t3200_kbc_alias_out(uint16_t port, uint8_t val, void *priv)
{
    t3200_t *dev = priv;
    t3200_trace(dev, port, val, 0);
    /* Shared KBC state; its T3200 variant implements the B4 status response. */
    outb(port & 0x7fff, val);
}

static void
t3200_sample(void *priv)
{
    t3200_t *dev = priv;
    timer_advance_u64(&dev->sample_timer, 1000000 * TIMER_USEC);
    if (dev->samples++ < 120) {
        pclog("T3200 SAMPLE %u CS:IP=%04X:%04X AX=%04X DS=%04X ES=%04X "
              "SP=%04X NMI=%02X%02X:%02X%02X BDA-mode=%02X RAM=%u "
              "SRAM=%02X video=%.0fx%.0f\n", dev->samples,
              CS, cpu_state.pc, AX, DS, ES, SP, ram[11], ram[10], ram[9], ram[8],
              ram[0x449], ram[0x413] | (ram[0x414] << 8), dev->control,
              video_res_x, video_res_y);
        pclog("T3200 EGA hdisp=%d dispend=%d vtotal=%d sync=%d row=%d "
              "blank=%02X pal=%02X seq=%02X,%02X,%02X,%02X,%02X "
              "gdc=%02X,%02X,%02X,%02X latch=%04X selector=%02X sram7=%02X\n",
              dev->ega.hdisp, dev->ega.dispend, dev->ega.vtotal, dev->ega.vsyncstart,
              dev->ega.rowoffset, dev->ega.scrblank, dev->ega.attr_palette_enable,
              dev->ega.seqregs[0],dev->ega.seqregs[1],dev->ega.seqregs[2],
              dev->ega.seqregs[3],dev->ega.seqregs[4],dev->ega.gdcreg[4],
              dev->ega.gdcreg[5],dev->ega.gdcreg[6],dev->ega.gdcreg[8],
              dev->ega.memaddr_latch, ram[0x488], dev->sram[7]);
    }
    if (dev->samples == 25) {
        pclog("T3200 DISPLAY misc=%02X vres=%d rowcount=%d linedbl=%d "
              "host=%dx%d BDA-columns=%u rows=%u char-height=%u\n",
              dev->ega.miscout, dev->ega.vres, dev->ega.rowcount, dev->ega.linedbl,
              xsize, ysize, ram[0x44a] | (ram[0x44b] << 8), ram[0x484] + 1,
              ram[0x485] | (ram[0x486] << 8));
        pclog("T3200 DISPLAY approximation=%d crtc-control=%02X extension=%02X\n",
              dev->provisional_display, dev->crtc_control, dev->crtc_extension);
        for (unsigned reg = 0; reg < 0x19; reg++)
            pclog("T3200 CRTC %02X=%02X\n", reg, dev->ega.crtc[reg]);
        for (unsigned line = 0; line < 32; line++)
            pclog("T3200 FONT E %02u=%02X\n", line,
                  dev->ega.vram[dev->ega.charseta + ('E' * 0x80) + (line << 2)]);
        for (unsigned row = 0; row < 25; row++) {
            char line[81];
            for (unsigned col = 0; col < 80; col++) {
                uint8_t ch = dev->ega.vram[(row * 80 + col) * 8];
                line[col] = ch >= 32 && ch < 127 ? ch : '.';
            }
            line[80] = 0;
            pclog("T3200 VRAM text candidate %02u: %s\n", row, line);
        }
    }
}

static void *
t3200_init(const device_t *info)
{
    (void) info;
    t3200_t *dev = calloc(1, sizeof(*dev));
    dev->lim_card = mem_size == 4096 && machine_get_config_int("memory_card_mode") == 0;
    if (dev->lim_card)
        mem_mapping_disable(&ram_high_mapping); /* PJ2: EMS-only option. */
    dev->trace = machine_get_config_int("trace");
    dev->provisional_50 = machine_get_config_int("provisional_50");
    dev->provisional_display = machine_get_config_int("provisional_display");
    memset(dev->sram, 0xff, sizeof(dev->sram));
    video_inform(VIDEO_FLAG_TYPE_SPECIAL, &t3200_video_timing);
    ega_set_type(&dev->ega, EGA_TOSHIBA);
    dev->ega.priv_parent = dev;
    dev->ega.timing_override = t3200_recalctimings;
    ega_init(&dev->ega, 9, 0);
    t3200_platform = dev;
    atomic_store(&t3200_display_target, 0);
    atomic_store(&t3200_extension_target, 0);
    atomic_store(&t3200_display_active, 0);
    if (machine_get_config_int("provisional_plasma"))
        t3200_plasma_palette(dev);
    dev->ega.x_add = 8;
    dev->ega.y_add = 14;
    overscan_x = 16;
    overscan_y = 28;
    rom_init(&dev->ega.bios_rom, T3200_AGS_ROM,
             0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
    mem_mapping_add(&dev->ega.mapping, 0xa0000, 0x20000,
                    t3200_video_read, NULL, NULL, t3200_video_write, NULL, NULL,
                    NULL, MEM_MAPPING_EXTERNAL, dev);
    for (unsigned slot = 0; slot < 4; slot++) {
        /* Byte callbacks let the memory core split wide accesses at its page
           boundaries, without crossing into the next bank's backing store. */
        mem_mapping_add(&dev->lim_mapping[slot],
                        T3200_LIM_FRAME + slot * T3200_LIM_PAGE_SIZE,
                        T3200_LIM_PAGE_SIZE,
                        t3200_lim_read, NULL, NULL, t3200_lim_write, NULL, NULL,
                        NULL, MEM_MAPPING_EXTERNAL, dev);
        mem_mapping_disable(&dev->lim_mapping[slot]);
    }
    for (unsigned reg = 0; reg < 8; reg++)
        io_sethandler(t3200_lim_ports[reg], 1, t3200_lim_in, NULL, NULL,
                      t3200_lim_out, NULL, NULL, dev);
    io_sethandler(0x3b0, 0x30, t3200_video_in, NULL, NULL,
                  t3200_video_out, NULL, NULL, dev);
    io_sethandler(0x8060, 1, t3200_kbc_alias_in, NULL, NULL,
                  t3200_kbc_alias_out, NULL, NULL, dev);
    io_sethandler(0x8064, 1, t3200_kbc_alias_in, NULL, NULL,
                  t3200_kbc_alias_out, NULL, NULL, dev);
    if (dev->trace) {
        timer_add(&dev->sample_timer, t3200_sample, dev, 0);
        timer_set_delay_u64(&dev->sample_timer, 1000000 * TIMER_USEC);
    }
    pclog("T3200 experimental: readable EGA approximation, SRAM 40h, selector 9, "
          "AT KBC/high alias with T3200 B4 status, 384 KiB LIM; AGS capture/NMI, plasma, "
          "3 MiB card EMS-only uses provisional backing order; native MFM is experimental.\n");
    if (dev->provisional_50)
        pclog("T3200: provisional 50h SRAM aperture enabled; bit-4 effects unmodeled.\n");
    if (dev->provisional_display)
        pclog("T3200: provisional 400-line display and 16-line text cells enabled.\n");
    return dev;
}

static void
t3200_close(void *priv)
{
    t3200_t *dev = priv;
    atomic_store(&t3200_display_active, -1);
    monitors[0].mon_pixel_height_ratio = 0.0;
    t3200_platform = NULL;
    timer_disable(&dev->ega.timer);
    if (dev->trace)
        timer_disable(&dev->sample_timer);
    free(dev->ega.vram);
    free(dev);
}

static void
t3200_speed_changed(void *priv)
{
    t3200_t *dev = priv;
    ega_recalctimings(&dev->ega);
}

static const device_t t3200_platform_device = {
    .name = "Toshiba T3200 experimental EGA/AGS",
    .internal_name = "t3200_ags",
    .flags = DEVICE_ISA,
    .init = t3200_init,
    .close = t3200_close,
    .speed_changed = t3200_speed_changed
};

static const device_config_t t3200_config[] = {
    {
        .name = "bios", .description = "BIOS Version", .type = CONFIG_BIOS,
        .default_string = "v461",
        .bios = {
            {
                .name = "Toshiba 4.61", .internal_name = "v461",
                .bios_type = BIOS_NORMAL, .files_no = 3, .size = 65536,
                .files = { "roms/machines/t3200/IC22_033E.BIN",
                           "roms/machines/t3200/IC24_034E.BIN", T3200_AGS_ROM }
            },
            { .files_no = 0 }
        }
    },
    { .name = "trace", .description = "Log experimental firmware progress",
      .type = CONFIG_BINARY, .default_int = 0 },
    { .name = "provisional_50", .description = "Provisional AGS 50h SRAM aperture",
      .type = CONFIG_BINARY, .default_int = 1 },
    { .name = "provisional_display", .description = "Provisional 400-line display geometry",
      .type = CONFIG_BINARY, .default_int = 1 },
    { .name = "provisional_plasma", .description = "Approximate orange plasma palette",
      .type = CONFIG_BINARY, .default_int = 1 },
    {
        .name = "memory_card_mode", .description = "3 MB memory card mode",
        .type = CONFIG_SELECTION, .default_int = 0,
        .selection = {
            { .description = "Expanded memory only (EMS)", .value = 0 },
            { .description = "3 MB extended + 384 KB EMS", .value = 1 },
            { .description = "" }
        }
    },
    { .name = "", .type = CONFIG_END }
};

const device_t t3200_device = {
    .name = "Toshiba T3200", .internal_name = "t3200", .config = t3200_config
};

int
machine_at_t3200_init(const machine_t *model)
{
    int ret;
    if (!device_available(model->device))
        return 0;
    device_context(model->device);
    const char *bios = device_get_config_bios("bios");
    const char *lo = device_get_bios_file(model->device, bios, 0);
    const char *hi = device_get_bios_file(model->device, bios, 1);
    ret = bios_load_interleaved(lo, hi, 0xf0000, 65536, 0);
    device_context_restore();
    if (bios_only || !ret)
        return ret;
    machine_at_init(model);
    video_reset(gfxcard[0]);
    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);
    if (hdc_current[0] == HDC_INTERNAL)
        device_add(&st506_xt_toshiba_t3200_device);
    device_add(&t3200_platform_device);
    return ret;
}
#endif
