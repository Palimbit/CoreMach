/*
 * BluMach, a preservation-focused fork of 86Box.
 * Author: rtzor
 * Project: BluMach
 *
 * C&T 82C452 VGA subset for the Amstrad PC5286.
 * Register reference: September 1991 data sheet, revision 2.1.
 * See doc/machines/amstrad-pc5286-vga.md for board assumptions and limits.
 * Standard VGA registers, planar memory and the 6-bit DAC reuse 86Box.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/vid_vga.h>
#include "cpu.h"

#define PC5286_VGA_ROM "roms/machines/pc5286/C000.ROM"

typedef struct chips452_t {
    vga_t vga;
    uint8_t setup, awake, enable, index;
    uint8_t xr[128];
    unsigned frame_count;
    uint8_t interrupt_pending;
    unsigned cursor_frames;
    void (*background_render)(svga_t *);
    void (*blank_render)(svga_t *);
    uint32_t *cursor_line;
    int cursor_line_width;
} chips452_t;

/* The data sheet is inconsistent about the end pointer. This experimental
   interpretation uses its two counter steps per line and inclusive bits 9:2,
   independently of the physical A-H layout. See the machine documentation. */
static int
chips452_cursor_row(const chips452_t *dev, int y)
{
    int row = y - (((dev->xr[0x35] & 15) << 8) | dev->xr[0x36]);
    unsigned height = ((((unsigned) dev->xr[0x32] - dev->xr[0x31]) & 255) + 1) * 2;
    if (!(dev->xr[0x37] & 1) || row < 0 || (unsigned) row >= height)
        return -1;
    if ((dev->xr[0x37] & 8) &&
        (dev->cursor_frames & ((dev->xr[0x37] & 16) ? 16 : 8)))
        return -1;
    return row;
}

static uint8_t
chips452_cursor_pixel(const chips452_t *dev, int row, int x, uint8_t background)
{
    int left = (dev->xr[0x33] & 0x80) ? (dev->xr[0x34] & 63) - 64 :
               ((dev->xr[0x33] & 15) << 8) | dev->xr[0x34];
    int zoom = (dev->xr[0x37] & 4) ? 2 : 1;
    int pixel = x - left;
    if (row < 0 || pixel < 0 || pixel >= 32 * zoom)
        return background;
    pixel /= zoom;
    /* Interpret start as a plane address. Physical layout: ABCD, skip four,
       EFGH, skip four; most significant bit first in each eight-pixel group. */
    uint32_t start = ((dev->xr[0x30] << 8) | dev->xr[0x31]) << 4;
    uint32_t addr = start + row * 16 + (pixel / 16) * 8 + ((pixel / 8) & 1);
    unsigned shift = 7 - (pixel & 7);
    unsigned pattern = ((dev->vga.svga.vram[addr & 0x3ffff] >> shift) & 1) |
                       (((dev->vga.svga.vram[(addr + 2) & 0x3ffff] >> shift) & 1) << 1);
    uint8_t mask = dev->xr[0x38];
    if (!pattern)
        return background;
    if (pattern == 1)
        return background ^ mask;
    return (background & ~mask) | (dev->xr[0x39 + (pattern == 3)] & mask);
}

/* Reuse the VGA shifter/text renderer with an identity palette into a private
   scanline. Compose indices at physical pixel resolution, then apply the DAC
   mask and real palette. This preserves duplicated pixels and equal-colour
   palette entries without trying to reverse an RGB lookup. */
static void
chips452_render(svga_t *svga)
{
    chips452_t *dev = svga->priv;
    bitmap_t *target = svga->monitor->target_buffer;
    int y = svga->displine + svga->y_add;
    if (!target || y < 0 || y >= target->h || y >= 2048 ||
        !target->line[y] || target->w <= 0)
        return;
    /* VGA renderers fetch a complete final character and may retain pixels
       for horizontal panning. Give those writes private padding as well. */
    int padding = svga->x_add < 0 ? -svga->x_add : 0;
    int width = (svga->x_add & 2047) + svga->hdisp + svga->scrollcache + 64;
    if (svga->render_line_offset > 0)
        width += svga->render_line_offset * 18;
    if (width < target->w)
        width = target->w;
    width += padding;
    if (dev->cursor_line_width < width) {
        uint32_t *line = realloc(dev->cursor_line, (size_t) width * sizeof(uint32_t));
        if (!line) {
            dev->background_render(svga);
            return;
        }
        dev->cursor_line = line;
        dev->cursor_line_width = width;
    }
    memset(dev->cursor_line, 0xff, (size_t) width * sizeof(uint32_t));
    uint32_t *indices = dev->cursor_line + padding;
    bitmap_t scratch = *target;
    scratch.line[y] = indices;
    monitor_t monitor = {0};
    monitor.target_buffer = &scratch;
    monitor_t *saved_monitor = svga->monitor;
    uint32_t palette[512], identity[512];
    memcpy(palette, svga->pallook, sizeof(palette));
    for (unsigned i = 0; i < 512; i++)
        identity[i] = svga->pallook[i] = i;
    uint32_t *saved_map = svga->map8;
    uint8_t saved_mask = svga->dac_mask;
    int saved_change = svga->fullchange;
    svga->map8 = identity;
    svga->dac_mask = 255;
    svga->monitor = &monitor;
    svga->fullchange = 1; /* Also redraw when only the cursor pattern changed. */
    dev->background_render(svga);
    svga->monitor = saved_monitor;
    svga->fullchange = saved_change;
    svga->dac_mask = saved_mask;
    svga->map8 = saved_map;
    memcpy(svga->pallook, palette, sizeof(palette));
    int row = chips452_cursor_row(dev, svga->displine);
    for (int x = 0; x < target->w; x++) {
        if (indices[x] == UINT32_MAX)
            continue; /* Leave overscan and pixels the renderer did not write. */
        int active = x >= svga->x_add && x < svga->x_add + svga->hdisp;
        uint8_t index = chips452_cursor_pixel(dev, active ? row : -1,
                                             x - svga->x_add, indices[x]);
        target->line[y][x] = palette[index & saved_mask];
    }
}

static int
chips452_enabled(const chips452_t *dev)
{
    return (dev->setup & 0x18) == 8 && dev->awake;
}

static void
chips452_mapping(chips452_t *dev)
{
    svga_t *svga = &dev->vga.svga;
    if (chips452_enabled(dev) && (svga->miscout & 2))
        mem_mapping_enable(&svga->mapping);
    else
        mem_mapping_disable(&svga->mapping);
    /* ISA ROM decode is enabled at reset, before the VGA is awake (XR03). */
    if (dev->xr[3] & 1)
        mem_mapping_disable(&dev->vga.bios_rom.mapping);
    else
        mem_mapping_enable(&dev->vga.bios_rom.mapping);
}

/* BIOS220's font loader stores one byte per character at each 256-byte
   plane row, then enables XR0E bit0. The data sheet specifies one font.
   Selecting the normal (attribute bit3 clear) SR03 bank is an explicit
   approximation until the missing register description is recovered. */
static uint8_t
chips452_text_glyph(svga_t *svga, uint8_t chr)
{
    uint32_t addr = svga->charseta + ((uint32_t) chr << 2) +
                    ((svga->scanline & 31) << 10);
    return svga->vram[addr & svga->vram_display_mask];
}

static void
chips452_render_default(svga_t *svga)
{
    chips452_t *dev = svga->priv;
    bitmap_t *target = svga->monitor->target_buffer;
    int y = svga->displine + svga->y_add;
    if (!target || y < 0 || y >= target->h || y >= 2048 || !target->line[y])
        return;
    int left = svga->x_add;
    int right = left + svga->hdisp + svga->scrollcache;
    if (left < 0) left = 0;
    if (right > target->w) right = target->w;
    /* Conventional active-low DAC blank wiring is a board approximation.
       The default byte is a DAC index, not an attribute-palette index. */
    uint32_t color = (dev->xr[0x28] & 8) ? 0 :
                     svga->pallook[dev->xr[0x2b] & svga->dac_mask];
    for (int x = left; x < right; x++)
        target->line[y][x] = color;
    if (svga->firstline_draw == 2000)
        svga->firstline_draw = svga->displine;
    svga->lastline_draw = svga->displine;
}

static void
chips452_recalc(svga_t *svga)
{
    chips452_t *dev = svga->priv;
    svga->text_glyph = (dev->xr[0x0e] & 1) ? chips452_text_glyph : NULL;
    /* 452 p.44 plus 451 p.70: auxiliary LSB in word/dword addressing.
       One half of CR13's eight internal address units. Byte-mode gating
       follows the 451 definition; XR0D bit1 belongs to alternate timings. */
    svga->rowoffset_extra = (dev->xr[0x0d] & 1) &&
                           (!(svga->crtc[0x17] & 0x40) || (svga->crtc[0x14] & 0x40)) ? 4 : 0;
    unsigned select = (svga->miscout >> 2) & 3;
    /* CLK2 routing is unconfirmed: use the observed 32 MHz oscillator as
       an explicit board approximation for CLK2 and MCLK. */
    double clock = select == 0 ? 25175000.0 : select == 1 ? 28322000.0 : 32000000.0;
    if (dev->xr[5] & 1)
        clock = 32000000.0;
    if ((dev->xr[5] & 1) || select >= 2 ||
        (select == 0 && (dev->xr[5] & 0x40)) ||
        (select == 1 && (dev->xr[5] & 0x20)))
        clock /= 1 + ((dev->xr[5] >> 3) & 3);
    svga->clock = cpuclock * (double) (1ULL << 32) / clock;
    svga->memaddr_latch |= (dev->xr[0x0c] & 3) << 16;
    svga->ca_adj = (dev->xr[0x0a] & 3) << 16;
    svga->vram_display_mask = 0x3ffff;
    if (svga->render == chips452_render_default) {
        svga->hdisp /= svga->dots_per_clock;
        svga->dots_per_clock = 1;
        svga->render = dev->blank_render;
    }
    /* pp.81-82/103: default video under software screen-off. Only the
       normal BLANK polarity/output selection is modeled here; DE/inverted
       pin behavior and FIFO underrun require a separate signal model. */
    if (svga->scrblank == 0x20 && (dev->xr[0x28] & 7) == 4 &&
        (svga->crtc[0x17] & 0x80) && svga->attr_palette_enable) {
        /* The core leaves blanked hdisp in character clocks. Restore pixel
           units for clipping, overscan and frame submission as well. */
        svga->dots_per_clock = (svga->seqregs[1] & 1) ? 8 : 9;
        if (svga->seqregs[1] & 8)
            svga->dots_per_clock *= 2;
        svga->hdisp *= svga->dots_per_clock;
        dev->blank_render = svga->render;
        svga->render = chips452_render_default;
        return;
    }
    if (svga->render != chips452_render)
        dev->background_render = svga->render;
    if ((dev->xr[0x37] & 1) && !svga->scrblank && svga->attr_palette_enable &&
        (svga->crtc[0x17] & 0x80) && svga->bpp == 8 && dev->background_render)
        svga->render = chips452_render;
    else if (svga->render == chips452_render)
        svga->render = dev->background_render;
}

static void
chips452_vsync(svga_t *svga)
{
    chips452_t *dev = svga->priv;
    dev->cursor_frames = (dev->cursor_frames + 1) & 31;
    if (++dev->frame_count > (dev->xr[0x2a] & 31)) {
        dev->frame_count = 0;
        if ((svga->crtc[0x11] & 0x30) == 0x10)
            dev->interrupt_pending = 1;
    }
    /* The chip's pending flag is modeled; PC5286 IRQ wiring is unknown. */
}

static uint32_t
chips452_address(chips452_t *dev, uint32_t addr)
{
    svga_t *svga = &dev->vga.svga;
    unsigned mode = (svga->gdcreg[6] >> 2) & 3;
    unsigned bank = 0x10;
    addr &= 0x1ffff;
    if (mode >= 2)
        addr -= 0x10000;
    if (mode <= 1 && (dev->xr[0x0b] & 2)) {
        unsigned window = mode == 0 ? 0x10000 : 0x8000;
        if (addr & window)
            bank = 0x11;
        addr &= window - 1;
    }
    /* Map base is in plane addresses; divide-by-four exposes packed pixels. */
    return addr + ((dev->xr[bank] & 63) << ((dev->xr[0x0b] & 4) ? 14 : 12));
}

static uint8_t
chips452_read(uint32_t addr, void *priv)
{
    chips452_t *dev = priv;
    if (dev->xr[0x0b] & 1)
        return svga_read_linear(chips452_address(dev, addr), &dev->vga.svga);
    return svga_read(addr, &dev->vga.svga);
}

static void
chips452_write(uint32_t addr, uint8_t val, void *priv)
{
    chips452_t *dev = priv;
    if (dev->xr[0x0b] & 1)
        svga_write_linear(chips452_address(dev, addr), val, &dev->vga.svga);
    else
        svga_write(addr, val, &dev->vga.svga);
}

/* C&T pp.79-80/108: SUD replaces the byte rotation with a shift that
   takes carry from the previous write. Keeping the entire previous input
   in XR21-XR24, including masked planes, is an experimental interpretation
   of the carry-register readback; no independent silicon trace is available. */
static uint8_t
chips452_slide(uint8_t current, uint8_t previous, unsigned count, int right)
{
    if (!count)
        return current;
    return right ? (current >> count) | (previous << (8 - count)) :
                   (current << count) | (previous >> (8 - count));
}

static int
chips452_plane_write(svga_t *svga, uint32_t addr, uint8_t val, uint8_t planes)
{
    chips452_t *dev = svga->priv;
    unsigned mode = svga->writemode;
    if (!(dev->xr[0x20] & 1) || mode == 2 || mode > 3)
        return 0;
    unsigned shift = svga->gdcreg[3] & 7;
    int right = dev->xr[0x20] & 2;
    uint8_t shifted = 0;
    if (mode != 1) {
        shifted = chips452_slide(val, dev->xr[0x21], shift, right);
        dev->xr[0x21] = val;
    }
    for (unsigned plane = 0; plane < 4; plane++) {
        uint8_t data, mask = svga->gdcreg[8];
        uint8_t latched = svga->latch.b[plane];
        if (mode == 1) {
            data = chips452_slide(latched, dev->xr[0x21 + plane], shift, right);
            dev->xr[0x21 + plane] = latched;
        } else {
            data = (mode == 3 || (svga->gdcreg[1] & (1 << plane))) ?
                   ((svga->gdcreg[0] & (1 << plane)) ? 255 : 0) : shifted;
            if (mode == 3)
                mask &= shifted;
            switch (svga->gdcreg[3] & 0x18) {
                case 8:  data &= latched; break;
                case 16: data |= latched; break;
                case 24: data ^= latched; break;
            }
            data = (data & mask) | (latched & ~mask);
        }
        if (planes & (1 << plane))
            svga->vram[addr | plane] = data;
    }
    return 1;
}

static void
chips452_paging(chips452_t *dev)
{
    svga_t *svga = &dev->vga.svga;
    unsigned mode = (svga->gdcreg[6] >> 2) & 3;
    svga->packed_chain4 = !!(dev->xr[0x0b] & 4);
    if (dev->xr[0x0b] & 1)
        mem_mapping_set_addr(&svga->mapping, mode >= 2 ? 0xb0000 : 0xa0000,
                             mode == 0 ? 0x20000 : 0x10000);
    else {
        static const uint32_t bases[] = { 0xa0000, 0xa0000, 0xb0000, 0xb8000 };
        static const uint32_t sizes[] = { 0x20000, 0x10000, 0x8000, 0x8000 };
        mem_mapping_set_addr(&svga->mapping, bases[mode], sizes[mode]);
    }
    chips452_mapping(dev);
}

static uint8_t
chips452_in(uint16_t port, void *priv)
{
    chips452_t *dev = priv;
    svga_t *svga = &dev->vga.svga;
    if (port >= 0x102 && port <= 0x104) {
        if (!(dev->setup & 0x10))
            return 0xff;
        return port == 0x102 ? dev->awake : port == 0x103 ? dev->enable : 0xa5;
    }
    if (!chips452_enabled(dev))
        return 0xff;
    /* XR02 bit6 enables the additional palette select. The PC5286's
       two-address-bit VGA DAC uses the same four registers at either base. */
    if (port >= 0x83c6 && port <= 0x83c9) {
        if (!(dev->xr[2] & 0x40))
            return 0xff;
        port &= 0x7fff;
    }
    /* XR15 group6 suppresses PALRD as well as PALWR (data sheet p.75). */
    if ((dev->xr[0x15] & 0x20) && port >= 0x3c6 && port <= 0x3c9)
        return 0xff;
    uint16_t ext = (dev->enable & 0x40) ? 0x3b6 : 0x3d6;
    if (port == ext || port == ext + 1) {
        if (!(dev->enable & 0x80))
            return 0xff;
        if (port == ext)
            return dev->index;
        if (dev->index == 2)
            return dev->xr[2] | (svga->attrff ? 0x80 : 0);
        return dev->xr[dev->index];
    }
    if ((port & 0xfff0) == ((svga->miscout & 1) ? 0x3b0 : 0x3d0))
        return 0xff;
    /* C&T p.48 documents these read-only VGA state registers. Reading
       them must not load memory latches or advance the attribute phase. */
    if (port == 0x3b5 || port == 0x3d5) {
        if (svga->crtcreg == 0x22)
            return svga->latch.b[svga->gdcreg[4] & 3];
        if (svga->crtcreg == 0x24)
            return (svga->attraddr & 0x1f) | (svga->attr_palette_enable & 0x20) |
                   (svga->attrff ? 0x80 : 0);
    }
    uint8_t val = vga_in(port, &dev->vga);
    if (port == 0x3c2)
        val = (val & 0x7f) | (dev->interrupt_pending ? 0x80 : 0);
    return val;
}

static void
chips452_out(uint16_t port, uint8_t val, void *priv)
{
    chips452_t *dev = priv;
    svga_t *svga = &dev->vga.svga;
    if (port == 0x46e8) {
        dev->setup = val & 0x18;
        chips452_mapping(dev);
        return;
    }
    if (port >= 0x102 && port <= 0x104) {
        if (dev->setup & 0x10) {
            if (port == 0x102)
                dev->awake = val & 1;
            if (port == 0x103)
                dev->enable = val & 0xdf;
            chips452_mapping(dev);
        }
        return;
    }
    if (!chips452_enabled(dev))
        return;
    if (port >= 0x83c6 && port <= 0x83c9) {
        if (!(dev->xr[2] & 0x40))
            return;
        port &= 0x7fff;
    }
    uint16_t ext = (dev->enable & 0x40) ? 0x3b6 : 0x3d6;
    if (port == ext || port == ext + 1) {
        if (!(dev->enable & 0x80))
            return;
        if (port == ext) {
            dev->index = val & 0x7f;
            return;
        }
        static const uint8_t masks[0x18] = {
            0, 0, 0x7d, 1, 7, 0x7b, 0xff, 0, 15, 15, 3, 7,
            3, 3, 3, 0, 63, 63, 0, 0, 0xf3, 0x7f, 63, 63
        };
        if (dev->index < sizeof(masks)) {
            if (!masks[dev->index])
                return;
            val &= masks[dev->index];
        }
        /* Reserved bits read as zero (data sheet pp.79-86). Retaining a
           register does not implement its cursor, sliding or sync engine. */
        if (dev->index == 0x20)
            val &= 0x03;
        if (dev->index >= 0x27 && dev->index <= 0x37) {
            static const uint8_t extended_masks[] = {
                0x3f, 0x0f, 0xfc, 0x1f, 0xff, 0x0f, 0xff, 0x03,
                0xff, 0xff, 0xff, 0xff, 0x8f, 0xff, 0x0f, 0xff, 0x1f
            };
            val &= extended_masks[dev->index - 0x27];
        }
        dev->xr[dev->index] = val;
        /* In the separate index/data mapping the flip-flop is always reset,
           including the transition from VGA/EGA's data phase (p.67). */
        if (dev->index == 2 && (val & 0x18) == 8)
            svga->attrff = 0;
        if (dev->index == 0x0e || dev->index == 0x28 || dev->index == 0x2b ||
            (dev->index >= 0x30 && dev->index <= 0x3a))
            svga->fullchange = changeframecount;
        chips452_paging(dev);
        svga_recalctimings(svga);
        return;
    }
    if ((port & 0xfff0) == ((svga->miscout & 1) ? 0x3b0 : 0x3d0))
        return;
    if ((port == 0x3b5 || port == 0x3d5) &&
        (svga->crtcreg == 0x22 || svga->crtcreg == 0x24))
        return;
    /* Data sheet p.75: extension write-protect groups. Index ports remain
       writable so software can inspect a protected register group. */
    uint8_t protect = dev->xr[0x15];
    if ((protect & 1) && (port == 0x3c0 || port == 0x3c1 || port == 0x3c5 || port == 0x3cf))
        return;
    if ((protect & 0x10) && (port == 0x3c2 || port == 0x3ba || port == 0x3da))
        return;
    if ((protect & 0x20) && port >= 0x3c6 && port <= 0x3c9)
        return;
    if (port == 0x3b5 || port == 0x3d5) {
        unsigned reg = svga->crtcreg;
        uint8_t mask = 0;
        if (reg < 7 && (protect & 0x40))
            mask = 0xff;
        if (reg == 7 && (protect & 0x40))
            mask |= 0xef;
        if (protect & 2) {
            if (reg == 9) mask |= 0x1f;
            if (reg == 10 || reg == 11) mask = 0xff;
        }
        if (protect & 4) {
            if (reg == 7) mask |= 0x10;
            if (reg == 0x11) mask |= 0x30;
            if (reg == 0x17) mask |= 0xfb;
            if (reg == 8 || reg == 0x13 || reg == 0x14 || reg == 0x18) mask = 0xff;
        }
        if (protect & 8) {
            if (reg == 9) mask |= 0xe0;
            if (reg == 0x11) mask |= 0x4f;
            if (reg == 0x17) mask |= 4;
            if (reg == 0x10 || reg == 0x12 || reg == 0x15 || reg == 0x16) mask = 0xff;
        }
        val = (val & ~mask) | (svga->crtc[reg] & mask);
    }
    /* XR02 supports separate attribute index/data ports. */
    if ((dev->xr[2] & 0x18) == 8 && (port == 0x3c0 || port == 0x3c1)) {
        svga->attrff = port == 0x3c1;
        vga_out(0x3c0, val, &dev->vga);
        svga->attrff = 0;
        return;
    }
    if ((dev->xr[2] & 0x18) == 0x10 && port == 0x3c1)
        port = 0x3c0;
    if ((port == 0x3b5 || port == 0x3d5) && svga->crtcreg == 0x11 && !(val & 0x10))
        dev->interrupt_pending = 0;
    vga_out(port, val, &dev->vga);
    if (port == 0x3c2 || port == 0x3cf)
        chips452_paging(dev);
}

static void
chips452_reset(void *priv)
{
    chips452_t *dev = priv;
    dev->setup = dev->awake = dev->enable = dev->index = 0;
    dev->frame_count = dev->interrupt_pending = 0;
    dev->cursor_frames = 0;
    dev->vga.svga.fullchange = changeframecount;
    memset(dev->xr, 0, sizeof(dev->xr));
    dev->xr[0] = 0x14; /* Data sheet production stepping; board stepping unknown. */
    dev->xr[6] = 0x4a;
    dev->vga.svga.packed_chain4 = 0;
    dev->vga.svga.attrff = 0;
    chips452_paging(dev);
    svga_recalctimings(&dev->vga.svga);
}

static void *
chips452_init(const device_t *info)
{
    chips452_t *dev = calloc(1, sizeof(*dev));
    svga_t *svga = &dev->vga.svga;
    video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_vga);
    svga_init(info, svga, dev, 256 << 10, chips452_recalc,
              chips452_in, chips452_out, NULL, NULL);
    /* Own both mono and colour ranges; prevent core handler replacement. */
    svga->priv_parent = dev;
    svga->plane_write = chips452_plane_write;
    svga->bpp = 8;
    svga->decode_mask = 0x3ffff;
    svga->miscout = 1;
    svga->vsync_callback = chips452_vsync;
    mem_mapping_set_handler(&svga->mapping, chips452_read, NULL, NULL,
                             chips452_write, NULL, NULL);
    mem_mapping_set_p(&svga->mapping, dev);
    rom_init(&dev->vga.bios_rom, PC5286_VGA_ROM, 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
    io_sethandler(0x102, 3, chips452_in, NULL, NULL, chips452_out, NULL, NULL, dev);
    io_sethandler(0x3b0, 0x30, chips452_in, NULL, NULL, chips452_out, NULL, NULL, dev);
    io_sethandler(0x83c6, 4, chips452_in, NULL, NULL, chips452_out, NULL, NULL, dev);
    io_sethandler(0x46e8, 1, NULL, NULL, NULL, chips452_out, NULL, NULL, dev);
    chips452_reset(dev);
    return dev;
}

static int chips452_available(void) { return rom_present(PC5286_VGA_ROM); }
static void chips452_close(void *priv)
{
    chips452_t *dev = priv;
    svga_close(&dev->vga.svga);
    free(dev->cursor_line);
    free(dev);
}
static void chips452_speed_changed(void *priv)
{
    chips452_t *dev = priv;
    svga_recalctimings(&dev->vga.svga);
}
static void chips452_redraw(void *priv)
{
    chips452_t *dev = priv;
    dev->vga.svga.fullchange = changeframecount;
}

const device_t chips452_pc5286_device = {
    .name = "C&T 82C452 (Amstrad PC5286, experimental)",
    .internal_name = "chips452_pc5286",
    .flags = DEVICE_ISA,
    .init = chips452_init,
    .close = chips452_close,
    .reset = chips452_reset,
    .available = chips452_available,
    .speed_changed = chips452_speed_changed,
    .force_redraw = chips452_redraw
};
