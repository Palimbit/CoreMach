/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Paradise VGA emulation
 *           PC2086, PC3086 use PVGA1A
 *           MegaPC uses W90C11A
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2008-2019 Sarah Walker.
 *          Copyright 2016-2019 Miran Grca.
 *
 * BluMach modifications: rtzor, Project BluMach, 2026.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/nvr.h>
#include <86box/machine.h>
#include <86box/video.h>
#include <86box/vid_cga.h>
#include <86box/vid_xga.h>
#include <86box/vid_svga.h>
#include <86box/vid_svga_render.h>

#define VAR_BYTE_MODE      (0 << 0)
#define VAR_WORD_MODE_MA13 (1 << 0)
#define VAR_WORD_MODE_MA15 (2 << 0)
#define VAR_DWORD_MODE     (3 << 0)
#define VAR_MODE_MASK      (3 << 0)
#define VAR_ROW0_MA13      (1 << 2)
#define VAR_ROW1_MA14      (1 << 3)

typedef struct paradise_t {
    svga_t svga;
    cga_t  t5200_cga;

    rom_t bios_rom;

    enum {
        PVGA1A = 0,
        WD90C11,
        WD90C30
    } type;

    uint32_t vram_mask;
    uint32_t memory;

    mem_mapping_t t5200_cga_mapping;

    uint32_t read_bank[4], write_bank[4];

    int interlace;
    int board_enabled;
    int board_mapping_enabled;
    int t5200_pdc;
    int t5200_internal_plasma;
    int t5200_dual_output;
    int t5200_secondary_monitor;
    int t5200_panel_override;
    int t5200_cmos_crt_only;
    int t5200_cga_pending;
    unsigned int t5200_disable_count;
    uint8_t t5200_pdc_unlock;
    uint8_t t5200_pdc_regs[4];
    uint8_t t5200_setup_control;
    uint8_t t5200_awake;
    uint8_t t5200_vga_init_probe;
    uint8_t t5200_lut_stream[64];
    uint8_t t5200_lut_stream_pos;
    uint8_t t5200_vga_mode;
    uint32_t t5200_external_palette[256];
    uint32_t t5200_plasma_palette[256];

    struct {
        uint8_t reg_block_ptr;
        uint8_t reg_idx;
        uint8_t disable_autoinc;

        uint16_t int_status;
        uint16_t blt_ctrl1, blt_ctrl2;
        uint16_t srclow, srchigh;
        uint16_t dstlow, dsthigh;

        uint32_t srcaddr, dstaddr;

        int invalid_block;
    } accel;
} paradise_t;

static void
paradise_t5200_cga_poll(void *priv)
{
    paradise_t *paradise = (paradise_t *) priv;
    const int   previous_monitor = monitor_index_global;

    monitor_index_global = 0;
    cga_poll(&paradise->t5200_cga);
    monitor_index_global = previous_monitor;
}

static uint8_t
paradise_t5200_cga_read(uint32_t addr, void *priv)
{
    paradise_t *paradise = (paradise_t *) priv;

    return paradise->svga.vram[addr & 0x7fff];
}

static uint16_t
paradise_t5200_cga_readw(uint32_t addr, void *priv)
{
    return paradise_t5200_cga_read(addr, priv) |
           ((uint16_t) paradise_t5200_cga_read(addr + 1, priv) << 8);
}

static uint32_t
paradise_t5200_cga_readl(uint32_t addr, void *priv)
{
    return paradise_t5200_cga_readw(addr, priv) |
           ((uint32_t) paradise_t5200_cga_readw(addr + 2, priv) << 16);
}

static void
paradise_t5200_cga_write(uint32_t addr, uint8_t val, void *priv)
{
    paradise_t *paradise = (paradise_t *) priv;
    svga_t     *svga     = &paradise->svga;
    const uint32_t offset = addr & 0x7fff;

    svga->vram[offset] = val;
    svga->changedvram[offset >> 12] = svga->monitor->mon_changeframecount;
}

static void
paradise_t5200_cga_writew(uint32_t addr, uint16_t val, void *priv)
{
    paradise_t5200_cga_write(addr, val, priv);
    paradise_t5200_cga_write(addr + 1, val >> 8, priv);
}

static void
paradise_t5200_cga_writel(uint32_t addr, uint32_t val, void *priv)
{
    paradise_t5200_cga_writew(addr, val, priv);
    paradise_t5200_cga_writew(addr + 2, val >> 16, priv);
}

/*
 * The T5200 PDC-GA converts the PVGA color stream to sixteen orange intensity
 * levels for the internal gas-plasma panel. Toshiba documents the number of
 * levels and maximum levels 15 (bright) and 11 (semi-bright), but not the
 * orange transfer curve. Preserve luminance with a conventional weighted
 * conversion and quantize only the panel output. The external VGA path
 * continues to use the unmodified DAC palette.
 *
 * In VGA mode the system BIOS writes a 40-entry Color or 64-entry Monochrome
 * conversion program to PVGA sequencer index 07h. Recognizing those byte-exact
 * V1.xx streams lets the display follow the firmware choice without reaching
 * into CMOS from the video device. VCHAD 1.10 independently confirms that this
 * is a 64-byte read/write PDC table and derives its index from the VGA DAC as
 * (3R + 6G + B) / 16, with a three-way distinction at index 15. The PDC
 * register 15h bits used by CGA mode come directly from the BIOS programming
 * routine at F000:1E22.
 */
static void
paradise_t5200_update_palette(paradise_t *paradise)
{
    svga_t *svga = &paradise->svga;
    nvr_t  *nvr;
    int     crt_only;
    int     panel_enabled;

    if (!paradise->t5200_internal_plasma)
        return;

    nvr = (nvr_t *) device_get_priv(&nvr_at_device);
    crt_only = !!(nvr && (nvr->regs[0x38] & 0x04));

    /* A new Setup selection supersedes a previous temporary keyboard action. */
    if (crt_only != paradise->t5200_cmos_crt_only) {
        paradise->t5200_cmos_crt_only = crt_only;
        paradise->t5200_panel_override = -1;
    }
    panel_enabled = (paradise->t5200_panel_override >= 0) ?
                    paradise->t5200_panel_override : !crt_only;

    for (int index = 0; index < 256; index++) {
        /* TEST3 stores Plasma=0 and CRT-only=1 in CMOS 38h bit 2. */
        if (!panel_enabled) {
            paradise->t5200_external_palette[index] = svga->pallook[index];
            paradise->t5200_plasma_palette[index]   = makecol32(0, 0, 0);
            if (!paradise->t5200_dual_output)
                svga->pallook[index] = paradise->t5200_plasma_palette[index];
            continue;
        }

        const int red       = video_6to8[svga->vgapal[index].r & 0x3f];
        const int green     = video_6to8[svga->vgapal[index].g & 0x3f];
        const int blue      = video_6to8[svga->vgapal[index].b & 0x3f];
        const int luminance = ((30 * red) + (59 * green) + (11 * blue) + 50) / 100;
        int       level;

        if (!paradise->t5200_vga_mode && (paradise->t5200_pdc_regs[3] & 0x04)) {
            const int intense = !!(index & 0x08);
            const int doubled = !!(paradise->t5200_pdc_regs[3] & (1 << intense));

            /* T3100-compatible CGA mode: one visible level per intensity class. */
            level = luminance ? (doubled ? 15 : 11) : 0;
        } else if (paradise->t5200_vga_mode) {
            const int red6  = svga->vgapal[index].r & 0x3f;
            const int blue6 = svga->vgapal[index].b & 0x3f;
            int       lut_index = ((3 * red6) +
                                   (6 * (svga->vgapal[index].g & 0x3f)) + blue6) / 16;

            if (lut_index == 15) {
                if (!red6 && !blue6)
                    lut_index = 14;
                else if (red6 && blue6)
                    lut_index = 16;
            }

            level = paradise->t5200_lut_stream[lut_index & 0x3f] & 0x0f;
        } else {
            /* CGA 16-gray-scale mode. */
            level = ((15 * luminance) + 127) / 255;
        }

        paradise->t5200_external_palette[index] = svga->pallook[index];
        paradise->t5200_plasma_palette[index] =
            makecol32((255 * level + 7) / 15,
                      (128 * level + 7) / 15,
                      (48 * level + 7) / 15);

        if (!paradise->t5200_dual_output)
            svga->pallook[index] = paradise->t5200_plasma_palette[index];
    }

    svga->fullchange = changeframecount;
}

static uint32_t
paradise_t5200_plasma_pixel(const paradise_t *paradise, uint32_t pixel,
                           int *palette_index)
{
    for (int index = 0; index < 256; index++) {
        if (pixel == paradise->t5200_external_palette[index]) {
            *palette_index = index;
            return paradise->t5200_plasma_palette[index];
        }
    }

    /* Border/direct-color fallback; normal T5200 VGA modes are palette based. */
    const int luminance = ((30 * getcolr(pixel)) + (59 * getcolg(pixel)) +
                           (11 * getcolb(pixel)) + 50) / 100;
    const int level = ((15 * luminance) + 127) / 255;

    *palette_index = 0;
    return makecol32((255 * level + 7) / 15,
                     (128 * level + 7) / 15,
                     (48 * level + 7) / 15);
}

static void
paradise_t5200_dual_vsync(svga_t *svga)
{
    paradise_t *paradise = (paradise_t *) svga->priv;
    monitor_t  *plasma   = &monitors[1];
    const int   width    = svga->monitor->mon_xsize;
    const int   height   = svga->monitor->mon_ysize;
    uint32_t    previous = 0xffffffff;
    uint32_t    converted = 0;
    uint32_t    previous_converted = 0;
    int         palette_index = 0;
    int         widen_previous = 0;
    const int   text_mode = !(svga->gdcreg[6] & 0x01) &&
                            !(svga->attrregs[0x10] & 0x01);

    if (!paradise->t5200_secondary_monitor || plasma->target_buffer == NULL)
        return;

    video_wait_for_buffer_monitor(1);

    if (plasma->mon_xsize != width || plasma->mon_ysize != height ||
        video_force_resize_get_monitor(1)) {
        plasma->mon_xsize = width;
        plasma->mon_ysize = height;
        set_screen_size_monitor(width, height, 1);
        video_force_resize_set_monitor(0, 1);
    }

    for (int y = 0; y < height; y++) {
        const uint32_t *source = svga->monitor->target_buffer->line[y];
        uint32_t       *target = plasma->target_buffer->line[y];

        previous = 0xffffffff;
        widen_previous = 0;
        for (int x = 0; x < width; x++) {
            if (source[x] != previous) {
                previous  = source[x];
                converted = paradise_t5200_plasma_pixel(paradise, previous,
                                                        &palette_index);
            }

            if (widen_previous && converted == paradise->t5200_plasma_palette[0])
                target[x] = previous_converted;
            else
                target[x] = converted;

            /*
             * Approximate the saved Single/Double strokes on the panel copy.
             * Exact regional glyph shapes require the undumped 64 KiB CG-ROM.
             */
            widen_previous = text_mode && !paradise->t5200_vga_mode &&
                             palette_index != 0 &&
                             (paradise->t5200_pdc_regs[3] &
                              (1 << !!(palette_index & 0x08)));
            previous_converted = converted;
        }
    }

    video_blit_memtoscreen_monitor(0, 0, width, height, 1);
}

static const uint8_t t5200_color_lut_bright[40] = {
    0x00, 0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02,
    0x03, 0x03, 0x03, 0x04, 0x04, 0x04, 0x05, 0x06,
    0x07, 0x08, 0x08, 0x09, 0x09, 0x09, 0x0a, 0x0a,
    0x0a, 0x0f, 0x0f, 0x0f, 0x0c, 0x0c, 0x0c, 0x0d,
    0x0d, 0x0d, 0x0e, 0x0e, 0x0e, 0x0f, 0x0f, 0x0f
};

static const uint8_t t5200_color_lut_semibright[40] = {
    0x00, 0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02,
    0x03, 0x03, 0x03, 0x04, 0x04, 0x04, 0x05, 0x06,
    0x07, 0x08, 0x08, 0x09, 0x09, 0x09, 0x0a, 0x0a,
    0x0a, 0x0b, 0x0b, 0x0b, 0x0c, 0x0c, 0x0c, 0x0d,
    0x0d, 0x0d, 0x0e, 0x0e, 0x0e, 0x0b, 0x0b, 0x0b
};

static void
paradise_t5200_lut_write(paradise_t *paradise, uint8_t val)
{
    if (!paradise->t5200_pdc || paradise->t5200_lut_stream_pos >= 64)
        return;

    paradise->t5200_lut_stream[paradise->t5200_lut_stream_pos++] = val;

    if (paradise->t5200_lut_stream_pos == 40) {
        if (!memcmp(paradise->t5200_lut_stream, t5200_color_lut_bright, 40)) {
            paradise->t5200_vga_mode = 1;
            paradise_t5200_update_palette(paradise);
        } else if (!memcmp(paradise->t5200_lut_stream, t5200_color_lut_semibright, 40)) {
            paradise->t5200_vga_mode = 1;
            paradise_t5200_update_palette(paradise);
        }
    } else if (paradise->t5200_lut_stream_pos == 64) {
        /* VCHAD writes complete user-defined tables, not just BIOS presets. */
        paradise->t5200_vga_mode = 1;
        paradise_t5200_update_palette(paradise);
    }
}

void
paradise_t5200_panel_set(void *priv, int enabled)
{
    paradise_t *paradise = (paradise_t *) priv;

    if (paradise == NULL || !paradise->t5200_pdc ||
        !paradise->t5200_internal_plasma)
        return;

    paradise->t5200_panel_override = !!enabled;
    paradise_t5200_update_palette(paradise);
    paradise->svga.fullchange = changeframecount;
    pclog("T5200 documented Ctrl+Home approximation: internal plasma enabled; external VGA unchanged\n");
}

static uint8_t
paradise_t5200_setup_in(uint16_t addr, void *priv)
{
    const paradise_t *paradise = (const paradise_t *) priv;

    return (addr == 0x0102) ? paradise->t5200_awake
                            : paradise->t5200_setup_control;
}

static void
paradise_t5200_setup_out(uint16_t addr, uint8_t val, void *priv)
{
    paradise_t *paradise = (paradise_t *) priv;

    if (addr == 0x0102) {
        if (paradise->t5200_setup_control & 0x10)
            paradise->t5200_awake = val & 0x01;
        return;
    }

    paradise->t5200_setup_control = val;
    /*
     * The Toshiba/Phoenix ROM brackets its search for a pre-existing MDA/CGA
     * adapter with 46E8h values 16h and 0Eh.  The onboard PVGA must not echo
     * the CRTC 0Fh 55h/AAh probe in that window or the ROM records itself as
     * the old display and INT 10h/1A00h subsequently reports VGA mono (07h).
     */
    if (val == 0x16)
        paradise->t5200_vga_init_probe = 1;
    else if (val == 0x0e)
        paradise->t5200_vga_init_probe = 0;

}

static video_timings_t timing_paradise_pvga1a = { .type = VIDEO_ISA, .write_b = 6, .write_w = 8, .write_l = 16, .read_b = 6, .read_w = 8, .read_l = 16 };
static video_timings_t timing_paradise_wd90c  = { .type = VIDEO_ISA, .write_b = 3, .write_w = 3, .write_l =  6, .read_b = 5, .read_w = 5, .read_l = 10 };

void paradise_remap(paradise_t *paradise);

uint8_t
paradise_in(uint16_t addr, void *priv)
{
    paradise_t *paradise = (paradise_t *) priv;
    svga_t     *svga     = &paradise->svga;
    uint8_t     max_sr   = (paradise->type >= WD90C30) ? 0x15 : 0x12;

    if (paradise->t5200_vga_init_probe &&
        (addr == 0x3b4 || addr == 0x3b5 || addr == 0x3d4 || addr == 0x3d5))
        return 0xff;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) &&
        !(svga->miscout & 1) &&
        !(paradise->t5200_pdc && !paradise->board_enabled &&
          (addr & 0xfff0) == 0x3d0) &&
        !(paradise->t5200_pdc && (addr == 0x3d4 || addr == 0x3d5)))
        addr ^= 0x60;

    switch (addr) {
        case 0x3c3:
            if (paradise->t5200_pdc)
                return paradise->board_enabled ? 0x01 : 0x00;
            break;

        case 0x3c5:
            if (paradise->t5200_pdc && svga->seqaddr == 0x07 &&
                (svga->gdcreg[0x0f] & 0x07) == 0x05 &&
                (svga->seqregs[0x05] & 0x04) && svga->seqregs[0x06] == 0x00) {
                const uint8_t value = paradise->t5200_lut_stream[paradise->t5200_lut_stream_pos & 0x3f];

                paradise->t5200_lut_stream_pos = (paradise->t5200_lut_stream_pos + 1) & 0x3f;
                return value;
            }
            if (svga->seqaddr > 7) {
                if (paradise->type < WD90C11 || svga->seqregs[6] != 0x48)
                    return 0xff;
                if (svga->seqaddr > max_sr)
                    return 0xff;
                return svga->seqregs[svga->seqaddr & 0x1f];
            }
            break;

        case 0x3c6:
        case 0x3c7:
        case 0x3c8:
        case 0x3c9:
            if (paradise->type == WD90C30)
                return sc1148x_ramdac_in(addr, 0, svga->ramdac, svga);
            return svga_in(addr, svga);

        case 0x3cf:
            if (svga->gdcaddr >= 9 && svga->gdcaddr <= 0x0e) {
                if (svga->gdcreg[0x0f] & 0x10)
                    return 0xff;
            }
            switch (svga->gdcaddr) {
                case 0x0f:
                    return (svga->gdcreg[0x0f] & 0x17) | 0x80;

                default:
                    break;
            }
            break;

        case 0x3D4:
            return (!paradise->board_enabled && paradise->t5200_pdc) ?
                   cga_in(addr, &paradise->t5200_cga) : svga->crtcreg;
        case 0x3D5:
            if (paradise->t5200_pdc && paradise->t5200_pdc_unlock == 2 &&
                svga->crtcreg >= 0x12 && svga->crtcreg <= 0x15)
                return paradise->t5200_pdc_regs[svga->crtcreg - 0x12];
            if (!paradise->board_enabled && paradise->t5200_pdc)
                return cga_in(addr, &paradise->t5200_cga);
            if ((paradise->type == PVGA1A) && (svga->crtcreg & 0x20))
                return 0xff;
            if (svga->crtcreg > 0x29 && svga->crtcreg < 0x30 && (svga->crtc[0x29] & 0x88) != 0x80)
                return 0xff;
            return svga->crtc[svga->crtcreg];

        default:
            break;
    }
    if (!paradise->board_enabled && paradise->t5200_pdc &&
        addr >= 0x3d0 && addr <= 0x3df)
        return cga_in(addr, &paradise->t5200_cga);

    return svga_in(addr, svga);
}

void
paradise_out(uint16_t addr, uint8_t val, void *priv)
{
    paradise_t *paradise = (paradise_t *) priv;
    svga_t     *svga     = &paradise->svga;

    xga_t      *xga      = (xga_t *) svga->xga;
    uint8_t     old;

    if (paradise->t5200_vga_init_probe &&
        (addr == 0x3b4 || addr == 0x3b5 || addr == 0x3d4 || addr == 0x3d5))
        return;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) &&
        !(svga->miscout & 1) &&
        !(paradise->t5200_pdc && !paradise->board_enabled &&
          (addr & 0xfff0) == 0x3d0) &&
        !(paradise->t5200_pdc && (addr == 0x3d4 || addr == 0x3d5)))
        addr ^= 0x60;

    switch (addr) {
        case 0x3c3:
            if (paradise->t5200_pdc) {
                /*
                 * Award V1.xx switches the onboard PVGA and its option ROM
                 * together through 3C3h. It first hides the ROM, scans the
                 * C0000h-C7FFFh window, enables and initializes VGA, then
                 * disables it again when CGA-compatible output was selected.
                 * Leaving the ROM visible after that final disable makes POST
                 * reject the saved CGA equipment byte as a bad configuration.
                 * The PDC-GA register window remains independently reachable.
                 */
                paradise->board_enabled = !!(val & 0x01);
                if (paradise->board_enabled) {
                    mem_mapping_enable(&paradise->bios_rom.mapping);
                    mem_mapping_disable(&paradise->t5200_cga_mapping);
                    timer_disable(&paradise->t5200_cga.timer);
                    timer_set_delay_u64(&svga->timer, 1);
                    paradise->t5200_cga_pending = 0;
                } else {
                    paradise->t5200_disable_count++;
                    mem_mapping_disable(&paradise->bios_rom.mapping);
                    /*
                     * The undocumented BGS keeps its 32 KiB CGA window at
                     * B8000h available after PVGA is switched off. The common
                     * CGA core supplies its documented register and timing
                     * contract; its VRAM is shared with the output conversion
                     * until BGS and CG-ROM dumps permit a dedicated model.
                     */
                    mem_mapping_enable(&paradise->t5200_cga_mapping);
                    /* Two discovery passes precede the final CGA hand-off. */
                    if (paradise->t5200_disable_count >= 3) {
                        timer_disable(&svga->timer);
                        paradise->t5200_cga_pending = 1;
                    }
                }
            }
            break;

        case 0x3c4:
            if (paradise->t5200_pdc && val == 0x07) {
                paradise->t5200_lut_stream_pos = 0;
            }
            break;

        case 0x3c5:
            if (paradise->t5200_pdc && svga->seqaddr == 0x07 &&
                (svga->gdcreg[0x0f] & 0x07) == 0x05 &&
                (svga->seqregs[0x05] & 0x04) && svga->seqregs[0x06] == 0x00) {
                paradise_t5200_lut_write(paradise, val);
            }
            if (svga->seqaddr > 7) {
                if (paradise->type < WD90C11 || svga->seqregs[6] != 0x48)
                    return;
                svga->seqregs[svga->seqaddr & 0x1f] = val;
                if (svga->seqaddr == 0x11) {
                    paradise_remap(paradise);
                }
                return;
            }
            break;

        case 0x3c6:
        case 0x3c7:
        case 0x3c8:
        case 0x3c9:
            if (paradise->type == WD90C30)
                sc1148x_ramdac_out(addr, 0, val, svga->ramdac, svga);
            else {
                svga_out(addr, val, svga);
                paradise_t5200_update_palette(paradise);
            }
            return;

        case 0x3cf:
            if (svga->gdcaddr >= 9 && svga->gdcaddr <= 0x0e) {
                if ((svga->gdcreg[0x0f] & 7) != 5)
                    return;
            }

            switch (svga->gdcaddr) {
                case 6:
                    if ((svga->gdcreg[6] & 0x0c) != (val & 0xc)) {
                        switch (val & 0x0c) {
                            case 0x0: /*128k at A0000*/
                                mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x20000);
                                svga->banked_mask = 0xffff;
                                break;
                            case 0x4: /*64k at A0000*/
                                mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x10000);
                                svga->banked_mask = 0xffff;
                                if (xga_active && (svga->xga != NULL)) {
                                    xga->on = 0;
                                    mem_mapping_set_handler(&svga->mapping, svga->read, svga->readw, svga->readl, svga->write, svga->writew, svga->writel);
                                }
                                break;
                            case 0x8: /*32k at B0000*/
                                mem_mapping_set_addr(&svga->mapping, 0xb0000, 0x08000);
                                svga->banked_mask = 0x7fff;
                                break;
                            case 0xC: /*32k at B8000*/
                                mem_mapping_set_addr(&svga->mapping, 0xb8000, 0x08000);
                                svga->banked_mask = 0x7fff;
                                break;

                            default:
                                break;
                        }
                    }
                    svga->gdcreg[6] = val;
                    paradise_remap(paradise);
                    return;

                case 9:
                case 0x0a:
                    svga->gdcreg[svga->gdcaddr] = val;
                    paradise_remap(paradise);
                    return;
                case 0x0b:
                    svga->gdcreg[0x0b] = val;
                    svga->gdcreg[0x0b] &= ~0xc0;
                    if (paradise->memory == 1024)
                        svga->gdcreg[0x0b] |= 0xc0;
                    else if (paradise->memory == 512)
                        svga->gdcreg[0x0b] |= 0x80;
                    else
                        svga->gdcreg[0x0b] |= 0x40;

                    if (svga->crtc[0x2f] & 0x02)
                        svga->decode_mask = 0x3ffff;
                    else  switch (svga->gdcreg[0x0b] & 0xc0) {
                        case 0x00: case 0x40:
                            svga->decode_mask = 0x3ffff;
                            break;
                        case 0x80:
                            svga->decode_mask = 0x7ffff;
                            break;
                        case 0xc0:
                            svga->decode_mask = 0xfffff;
                            break;
                    }

                    paradise_remap(paradise);
                    return;
                case 0x0e:
                    svga->gdcreg[0x0e] = val;
                    svga_recalctimings(svga);
                    return;

                default:
                    break;
            }
            break;

        case 0x3D4:
            svga->crtcreg = val & 0x3f;
            if (!paradise->board_enabled && paradise->t5200_pdc)
                cga_out(addr, val, &paradise->t5200_cga);
            if (paradise->t5200_pdc && paradise->t5200_pdc_unlock == 2 &&
                svga->crtcreg != 0x08 &&
                (svga->crtcreg < 0x12 || svga->crtcreg > 0x15))
                paradise->t5200_pdc_unlock = 0;
            return;
        case 0x3D5:
            if (paradise->t5200_pdc) {
                if (svga->crtcreg == 0x08) {
                    /* A fresh password may be written while the bank is open. */
                    if (val == 0x4d) {
                        paradise->t5200_pdc_unlock = 1;
                        return;
                    }
                    if (paradise->t5200_pdc_unlock == 1 && val == 0x53) {
                        paradise->t5200_pdc_unlock = 2;
                        return;
                    }
                    paradise->t5200_pdc_unlock = 0;
                } else if (paradise->t5200_pdc_unlock == 2 &&
                           svga->crtcreg >= 0x12 && svga->crtcreg <= 0x15) {
                    paradise->t5200_pdc_regs[svga->crtcreg - 0x12] = val;
                    if (svga->crtcreg == 0x15)
                        paradise_t5200_update_palette(paradise);
                    return;
                } else if (paradise->t5200_pdc_unlock == 1) {
                    paradise->t5200_pdc_unlock = 0;
                }
            }
            if (!paradise->board_enabled && paradise->t5200_pdc) {
                cga_out(addr, val, &paradise->t5200_cga);
                return;
            }
            if ((paradise->type == PVGA1A) && (svga->crtcreg & 0x20))
                return;
            if ((svga->crtcreg < 7) && (svga->crtc[0x11] & 0x80))
                return;
            if ((svga->crtcreg == 7) && (svga->crtc[0x11] & 0x80))
                val = (svga->crtc[7] & ~0x10) | (val & 0x10);
            if (svga->crtcreg > 0x29 && (svga->crtc[0x29] & 7) != 5)
                return;
            if (svga->crtcreg >= 0x31 && svga->crtcreg <= 0x37)
                return;
            old                       = svga->crtc[svga->crtcreg];
            svga->crtc[svga->crtcreg] = val;

            if (old != val) {
                if (svga->crtcreg < 0xe || svga->crtcreg > 0x10) {
                    if (svga->crtcreg == 0x2f) {
                        if (svga->crtc[0x2f] & 0x02)
                            svga->decode_mask = 0x3ffff;
                        else  switch (svga->gdcreg[0x0b] & 0xc0) {
                            case 0x00: case 0x40:
                                svga->decode_mask = 0x3ffff;
                                break;
                            case 0x80:
                                svga->decode_mask = 0x7ffff;
                                break;
                            case 0xc0:
                                svga->decode_mask = 0xfffff;
                                break;
                        }
                    }
                    if ((svga->crtcreg == 0xc) || (svga->crtcreg == 0xd)) {
                        svga->fullchange = 3;
                        svga->memaddr_latch   = ((svga->crtc[0xc] << 8) | svga->crtc[0xd]) + ((svga->crtc[8] & 0x60) >> 5);
                    } else {
                        svga->fullchange = changeframecount;
                        svga_recalctimings(svga);
                    }
                }
            }
            break;

        default:
            break;
    }

    if (!paradise->board_enabled && paradise->t5200_pdc &&
        addr >= 0x3d0 && addr <= 0x3df) {
        cga_out(addr, val, &paradise->t5200_cga);
        if (addr == CGA_REGISTER_MODE_CONTROL &&
            paradise->t5200_cga_pending) {
            paradise->t5200_cga_pending = 0;
            timer_set_delay_u64(&paradise->t5200_cga.timer, 1);
        }
        return;
    }

    svga_out(addr, val, svga);
}

void
paradise_remap(paradise_t *paradise)
{
    svga_t *svga    = &paradise->svga;

    if (svga->seqregs[0x11] & 0x80) {
        paradise->read_bank[0] = svga->gdcreg[9] << 12;
        paradise->read_bank[1] = paradise->read_bank[0] + 0x8000;

        paradise->write_bank[0] = svga->gdcreg[0x0a] << 12;
        paradise->write_bank[1] = paradise->write_bank[0] + 0x8000;

        if ((svga->gdcreg[6] & 0x0c) == 0x00) {
            paradise->read_bank[2] = paradise->read_bank[1] + 0x8000;
            paradise->read_bank[3] = paradise->read_bank[2] + 0x8000;

            paradise->write_bank[2] = paradise->write_bank[1] + 0x8000;
            paradise->write_bank[3] = paradise->write_bank[2] + 0x8000;
        } else {
            if (svga->gdcreg[6] & 0x08) {
                paradise->read_bank[1] = paradise->read_bank[0];

                paradise->write_bank[1] = paradise->write_bank[0];
            }

            paradise->read_bank[2] = paradise->read_bank[0];
            paradise->read_bank[3] = paradise->read_bank[1];

            paradise->write_bank[2] = paradise->write_bank[0];
            paradise->write_bank[3] = paradise->write_bank[1];
        }
    } else if (svga->gdcreg[0x0b] & 0x08) {
        if ((svga->gdcreg[6] & 0x0c) == 0x00) {
            paradise->read_bank[0] = svga->gdcreg[0x0a] << 12;
            paradise->read_bank[1] = paradise->read_bank[0] + 0x8000;
            paradise->read_bank[2] = svga->gdcreg[9] << 12;
            paradise->read_bank[3] = paradise->read_bank[2] + 0x8000;

            paradise->write_bank[0] = svga->gdcreg[0x0a] << 12;
            paradise->write_bank[1] = paradise->write_bank[0] + 0x8000;
            paradise->write_bank[2] = svga->gdcreg[9] << 12;
            paradise->write_bank[3] = paradise->write_bank[2] + 0x8000;
       } else if ((svga->gdcreg[6] & 0x0c) == 0x04) {
            paradise->read_bank[0] = svga->gdcreg[0x0a] << 12;
            paradise->read_bank[1] = (svga->gdcreg[9] << 12) + 0x8000;
            paradise->read_bank[2] = paradise->read_bank[0];
            paradise->read_bank[3] = paradise->read_bank[1];

            paradise->write_bank[0] = svga->gdcreg[0x0a] << 12;
            paradise->write_bank[1] = (svga->gdcreg[9] << 12) + 0x8000;
            paradise->write_bank[2] = paradise->write_bank[0];
            paradise->write_bank[3] = paradise->write_bank[1];
       } else {
            paradise->read_bank[0] = svga->gdcreg[0x0a] << 12;
            paradise->read_bank[1] = paradise->read_bank[0];
            paradise->read_bank[2] = paradise->read_bank[0];
            paradise->read_bank[3] = paradise->read_bank[0];

            paradise->write_bank[0] = svga->gdcreg[0x0a] << 12;
            paradise->write_bank[1] = paradise->write_bank[0];
            paradise->write_bank[2] = paradise->write_bank[0];
            paradise->write_bank[3] = paradise->write_bank[0];
       }
    } else {
        paradise->read_bank[0] = svga->gdcreg[9] << 12;
        paradise->read_bank[1] = paradise->read_bank[0] + 0x8000;

        paradise->write_bank[0] = svga->gdcreg[9] << 12;
        paradise->write_bank[1] = paradise->write_bank[0] + 0x8000;

        if ((svga->gdcreg[6] & 0x0c) == 0x00) {
            paradise->read_bank[2] = paradise->read_bank[1] + 0x8000;
            paradise->read_bank[3] = paradise->read_bank[2] + 0x8000;

            paradise->write_bank[2] = paradise->write_bank[1] + 0x8000;
            paradise->write_bank[3] = paradise->write_bank[2] + 0x8000;
        } else {
            if (svga->gdcreg[6] & 0x08) {
                paradise->read_bank[1] = paradise->read_bank[0];

                paradise->write_bank[1] = paradise->write_bank[0];
            }

            paradise->read_bank[2] = paradise->read_bank[0];
            paradise->read_bank[3] = paradise->read_bank[1];

            paradise->write_bank[2] = paradise->write_bank[0];
            paradise->write_bank[3] = paradise->write_bank[1];
        }
   }

    /* There are separate drivers for 1M and 512K/256K versions of the PVGA chips. */
    if ((svga->gdcreg[0x0b] & 0xc0) < 0xc0) {
        paradise->read_bank[1] &= 0x7ffff;
        paradise->write_bank[1] &= 0x7ffff;
    } else {
        paradise->read_bank[1] &= 0xfffff;
        paradise->write_bank[1] &= 0xfffff;
    }
}

void
paradise_render_4bpp_word_highres(svga_t *svga)
{
    int       x;
    int       oddeven;
    uint32_t  addr;
    uint32_t *p;
    uint8_t   edat[4];
    uint8_t   dat;
    uint32_t  changed_addr;

    if ((svga->displine + svga->y_add) < 0)
        return;

    changed_addr = ((svga->memaddr & 0x3fffc) << 1);

    if (svga->changedvram[changed_addr >> 12] || svga->changedvram[(changed_addr >> 12) + 1] || svga->fullchange) {
        p = &svga->monitor->target_buffer->line[svga->displine + svga->y_add][svga->x_add];

        if (svga->firstline_draw == 2000)
            svga->firstline_draw = svga->displine;
        svga->lastline_draw = svga->displine;

        for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 8) {
            addr    = ((svga->memaddr & 0x3fffc) << 1);
            oddeven = 0;

            oddeven = (svga->memaddr & 2) ? 1 : 0;
            *(uint32_t *) (&edat[0]) = *(uint32_t *) (&svga->vram[addr | oddeven]) & 0x00ff00ff;
            svga->memaddr = (svga->memaddr + 2) & svga->vram_mask;

            dat  = edatlookup[edat[0] >> 6][edat[1] >> 6] | (edatlookup[edat[2] >> 6][edat[3] >> 6] << 2);
            p[0] = svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask]];
            p[1] = svga->pallook[svga->egapal[dat & svga->plane_mask]];
            dat  = edatlookup[(edat[0] >> 4) & 3][(edat[1] >> 4) & 3] |
                   (edatlookup[(edat[2] >> 4) & 3][(edat[3] >> 4) & 3] << 2);
            p[2] = svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask]];
            p[3] = svga->pallook[svga->egapal[dat & svga->plane_mask]];
            dat  = edatlookup[(edat[0] >> 2) & 3][(edat[1] >> 2) & 3] |
                   (edatlookup[(edat[2] >> 2) & 3][(edat[3] >> 2) & 3] << 2);
            p[4] = svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask]];
            p[5] = svga->pallook[svga->egapal[dat & svga->plane_mask]];
            dat  = edatlookup[edat[0] & 3][edat[1] & 3] | (edatlookup[edat[2] & 3][edat[3] & 3] << 2);
            p[6] = svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask]];
            p[7] = svga->pallook[svga->egapal[dat & svga->plane_mask]];

            p += 8;
        }
    }
}

static int
paradise_mode_is_word(svga_t *svga)
{
    int func_nr;

    if (svga->fb_only)
        func_nr = 0;
    else {
        if (svga->force_dword_mode)
            func_nr = VAR_DWORD_MODE;
        else if (svga->crtc[0x14] & 0x40)
            func_nr = svga->packed_chain4 ? VAR_BYTE_MODE : VAR_DWORD_MODE;
        else if (svga->crtc[0x17] & 0x40)
            func_nr = VAR_BYTE_MODE;
        else if (svga->crtc[0x17] & 0x20)
            func_nr = VAR_WORD_MODE_MA15;
        else
            func_nr = VAR_WORD_MODE_MA13;

        if (!(svga->crtc[0x17] & 0x01))
            func_nr |= VAR_ROW0_MA13;
        if (!(svga->crtc[0x17] & 0x02))
            func_nr |= VAR_ROW1_MA14;
    }

    return (func_nr == 2);
}

void
paradise_recalctimings(svga_t *svga)
{
    paradise_t *paradise = (paradise_t *) svga->priv;
    int clk_sel = 0;

    svga->lowres = !(svga->gdcreg[0x0e] & 0x01);

    if (paradise->type == WD90C30) {
        if (svga->crtc[0x3e] & 0x01)
            svga->vtotal |= 0x400;
        if (svga->crtc[0x3e] & 0x02)
            svga->dispend |= 0x400;
        if (svga->crtc[0x3e] & 0x04)
            svga->vsyncstart |= 0x400;
        if (svga->crtc[0x3e] & 0x08)
            svga->vblankstart |= 0x400;
        if (svga->crtc[0x3e] & 0x10)
            svga->split |= 0x400;
    }

    if (paradise->type >= WD90C11)
        svga->interlace = !!(svga->crtc[0x2d] & 0x20);

    if (paradise->type < WD90C30) {
        if ((svga->gdcreg[6] & 1) || (svga->attrregs[0x10] & 1)) {
            if ((svga->bpp >= 8) && !svga->lowres) {
                svga->render = svga_render_8bpp_highres;
                if (paradise->type < WD90C11)
                    svga->vram_display_mask = (svga->crtc[0x2f] & 0x02) ? 0x3ffff : paradise->vram_mask;
            }
        }
        if (paradise->type >= WD90C11)  switch (svga->crtc[0x2f] & 0x60) {
            case 0x60: case 0x40:
                svga->vram_display_mask = 0x3ffff;
                break;
            case 0x20:
                svga->vram_display_mask = 0x7ffff;
                break;
            case 0x00:
                svga->vram_display_mask = 0xfffff;
                break;
        }
    } else {
        clk_sel = ((svga->miscout >> 2) & 0x03);
        if (!(svga->gdcreg[0x0c] & 0x02))
            clk_sel |= 0x04;

        svga->clock = (cpuclock * (double) (1ULL << 32)) / svga->getclock(clk_sel, svga->clock_gen);
        if ((svga->gdcreg[6] & 1) || (svga->attrregs[0x10] & 1)) {
            if ((svga->bpp >= 8) && !svga->lowres) {
                if (svga->bpp == 16) {
                    svga->render = svga_render_16bpp_highres;
                    svga->hdisp >>= 1;
                    if (svga->hdisp == 788)
                        svga->hdisp += 12;
                } else if (svga->bpp == 15) {
                    svga->render = svga_render_15bpp_highres;
                    svga->hdisp >>= 1;
                    if (svga->hdisp == 788)
                        svga->hdisp += 12;
                } else
                    svga->render = svga_render_8bpp_highres;

                svga->vram_display_mask = (svga->crtc[0x2f] & 0x02) ? 0x3ffff : paradise->vram_mask;
            } else if ((svga->bpp <= 8) && svga->lowres && !svga->interlace && (svga->hdisp >= 1024) &&
                       (svga->miscout >= 0x27) && (svga->miscout <= 0x2f))
                svga->interlace = 1; /*Horrible tweak to re-enable the interlace after returning to
                                       a windowed DOS box in Win3.x*/
        }
    }

    /*
       Yes, this is basically hack but I'm going to look at a proper rewrite in
       86Box 6.0.
     */
    if ((paradise->type == WD90C11) && (svga->hdisp == 1024) &&
        (svga->render == svga_render_4bpp_highres) && paradise_mode_is_word(svga))
        svga->render = paradise_render_4bpp_word_highres;
}

uint32_t
paradise_decode_addr(paradise_t *paradise, uint32_t addr, int write)
{
    svga_t   *svga            = &paradise->svga;
    int       memory_map_mode = (svga->gdcreg[6] >> 2) & 3;

    addr &= 0x1ffff;

    switch (memory_map_mode) {
        case 0:
            break;
        case 1:
            if (addr >= 0x10000)
                return 0xffffffff;
            break;
        case 2:
            addr -= 0x10000;
            if (addr >= 0x8000)
                return 0xffffffff;
            break;
        default:
        case 3:
            addr -= 0x18000;
            if (addr >= 0x8000)
                return 0xffffffff;
            break;
    }

    if (write)
        addr = (addr & 0x7fff) + paradise->write_bank[(addr >> 15) & 3];
    else
        addr = (addr & 0x7fff) + paradise->read_bank[(addr >> 15) & 3];


    return addr;
}

static void
paradise_write(uint32_t addr, uint8_t val, void *priv)
{
    paradise_t *paradise = (paradise_t *) priv;
    svga_t     *svga     = &paradise->svga;
    uint32_t    prev_addr;
    uint32_t    prev_addr2;

    addr = paradise_decode_addr(paradise, addr, 1);
    if (addr == 0xffffffff)
        return;

    /*Could be done in a better way but it works.*/
    if (!svga->lowres || (svga->attrregs[0x10] & 0x40)) {
        if (((svga->gdcreg[6] & 0x0c) == 0x04) && (svga->crtc[0x14] & 0x40) && ((svga->gdcreg[0x0b] & 0xc0) == 0xc0) && !svga->chain4) {
            prev_addr  = addr & 3;
            prev_addr2 = addr & 0xfffc;
            if (prev_addr == 3) {
                if ((addr & 0x30000) != 0x30000)
                    addr = (addr >> 16) | (prev_addr << 16) | prev_addr2;
            } else if (prev_addr == 2) {
                if ((addr & 0x30000) != 0x20000)
                    addr = (addr >> 16) | (prev_addr << 16) | prev_addr2;
            } else if (prev_addr == 1) {
                if ((addr & 0x30000) != 0x10000)
                    addr = (addr >> 16) | (prev_addr << 16) | prev_addr2;
            } else {
                if (addr & 0x30000)
                    addr = (addr >> 16) | (prev_addr << 16) | prev_addr2;
            }
        }
    }
    svga_write_linear(addr, val, svga);
}

static void
paradise_writew(uint32_t addr, uint16_t val, void *priv)
{
    paradise_t *paradise = (paradise_t *) priv;
    svga_t     *svga     = &paradise->svga;
    uint32_t    prev_addr;
    uint32_t    prev_addr2;

    addr = paradise_decode_addr(paradise, addr, 1);
    if (addr == 0xffffffff)
        return;

    /*Could be done in a better way but it works.*/
    if (!svga->lowres || (svga->attrregs[0x10] & 0x40)) {
        if (((svga->gdcreg[6] & 0x0c) == 0x04) && (svga->crtc[0x14] & 0x40) && ((svga->gdcreg[0x0b] & 0xc0) == 0xc0) && !svga->chain4) {
            prev_addr  = addr & 3;
            prev_addr2 = addr & 0xfffc;
            if (prev_addr == 3) {
                if ((addr & 0x30000) != 0x30000)
                    addr = (addr >> 16) | (prev_addr << 16) | prev_addr2;
            } else if (prev_addr == 2) {
                if ((addr & 0x30000) != 0x20000)
                    addr = (addr >> 16) | (prev_addr << 16) | prev_addr2;
            } else if (prev_addr == 1) {
                if ((addr & 0x30000) != 0x10000)
                    addr = (addr >> 16) | (prev_addr << 16) | prev_addr2;
            } else {
                if (addr & 0x30000)
                    addr = (addr >> 16) | (prev_addr << 16) | prev_addr2;
            }
        }
    }
    svga_writew_linear(addr, val, svga);
}

static uint8_t
paradise_read(uint32_t addr, void *priv)
{
    paradise_t *paradise = (paradise_t *) priv;
    svga_t     *svga     = &paradise->svga;
    uint32_t    prev_addr;
    uint32_t    prev_addr2;

    addr = paradise_decode_addr(paradise, addr, 0);
    if (addr == 0xffffffff)
        return 0xff;

    /*Could be done in a better way but it works.*/
    if (!svga->lowres || (svga->attrregs[0x10] & 0x40)) {
        if (((svga->gdcreg[6] & 0x0c) == 0x04) && (svga->crtc[0x14] & 0x40) && ((svga->gdcreg[0x0b] & 0xc0) == 0xc0) && !svga->chain4) {
            prev_addr  = addr & 3;
            prev_addr2 = addr & 0xfffc;
            if (prev_addr == 3) {
                if ((addr & 0x30000) != 0x30000)
                    addr = (addr >> 16) | (prev_addr << 16) | prev_addr2;
            } else if (prev_addr == 2) {
                if ((addr & 0x30000) != 0x20000)
                    addr = (addr >> 16) | (prev_addr << 16) | prev_addr2;
            } else if (prev_addr == 1) {
                if ((addr & 0x30000) != 0x10000)
                    addr = (addr >> 16) | (prev_addr << 16) | prev_addr2;
            } else {
                if (addr & 0x30000)
                    addr = (addr >> 16) | (prev_addr << 16) | prev_addr2;
            }
        }
    }
    return svga_read_linear(addr, svga);
}

static uint16_t
paradise_readw(uint32_t addr, void *priv)
{
    paradise_t *paradise = (paradise_t *) priv;
    svga_t     *svga     = &paradise->svga;
    uint32_t    prev_addr;
    uint32_t    prev_addr2;

    addr = paradise_decode_addr(paradise, addr, 0);
    if (addr == 0xffffffff)
        return 0xffff;

    /*Could be done in a better way but it works.*/
    if (!svga->lowres || (svga->attrregs[0x10] & 0x40)) {
        if (((svga->gdcreg[6] & 0x0c) == 0x04) && (svga->crtc[0x14] & 0x40) && ((svga->gdcreg[0x0b] & 0xc0) == 0xc0) && !svga->chain4) {
            prev_addr  = addr & 3;
            prev_addr2 = addr & 0xfffc;
            if (prev_addr == 3) {
                if ((addr & 0x30000) != 0x30000)
                    addr = (addr >> 16) | (prev_addr << 16) | prev_addr2;
            } else if (prev_addr == 2) {
                if ((addr & 0x30000) != 0x20000)
                    addr = (addr >> 16) | (prev_addr << 16) | prev_addr2;
            } else if (prev_addr == 1) {
                if ((addr & 0x30000) != 0x10000)
                    addr = (addr >> 16) | (prev_addr << 16) | prev_addr2;
            } else {
                if (addr & 0x30000)
                    addr = (addr >> 16) | (prev_addr << 16) | prev_addr2;
            }
        }
    }
    return svga_readw_linear(addr, svga);
}

void *
paradise_init(const device_t *info, uint32_t memory)
{
    paradise_t *paradise = calloc(1, sizeof(paradise_t));
    svga_t     *svga     = &paradise->svga;

    if (info->local == PVGA1A)
        video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_paradise_pvga1a);
    else
        video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_paradise_wd90c);

    paradise->memory = memory;

    switch (info->local) {
        case PVGA1A:
            svga_init(info, svga, paradise, (memory << 10), /*256kb default*/
                      paradise_recalctimings,
                      paradise_in, paradise_out,
                      NULL,
                      NULL);
            paradise->vram_mask = (memory << 10) - 1;
            svga->decode_mask   = (memory << 10) - 1;
            break;
        case WD90C11:
            svga_init(info, svga, paradise, (memory << 10), /*512kb default*/
                      paradise_recalctimings,
                      paradise_in, paradise_out,
                      NULL,
                      NULL);
            paradise->vram_mask = (memory << 10) - 1;
            svga->decode_mask   = (memory << 10) - 1;
            break;
        case WD90C30:
            svga_init(info, svga, paradise, (memory << 10),
                      paradise_recalctimings,
                      paradise_in, paradise_out,
                      NULL,
                      NULL);
            paradise->vram_mask = (memory << 10) - 1;
            svga->decode_mask   = (memory << 10) - 1;
            svga->ramdac        = device_add(&sc11487_ramdac_device); /*Actually a Winbond W82c487-80, probably a clone.*/
            svga->clock_gen     = device_add(&ics90c64a_903_device);
            svga->getclock      = ics90c64a_vclk_getclock;
            break;

        default:
            break;
    }

    svga->read = paradise_read;
    svga->readw = paradise_readw;
    svga->readl = NULL;
    svga->write = paradise_write;
    svga->writew = paradise_writew;
    svga->writel = NULL;
    mem_mapping_set_handler(&svga->mapping, paradise_read, paradise_readw, NULL, paradise_write, paradise_writew, NULL);
    mem_mapping_set_p(&svga->mapping, paradise);

    io_sethandler(0x03c0, 0x0020, paradise_in, NULL, NULL, paradise_out, NULL, NULL, paradise);
    paradise->board_enabled         = 1;
    paradise->board_mapping_enabled = 1;

    /* Common to all three types. */
    svga->crtc[0x31] = 'W';
    svga->crtc[0x32] = 'D';
    svga->crtc[0x33] = '9';
    svga->crtc[0x34] = '0';
    svga->crtc[0x35] = 'C';

    switch (info->local) {
        case WD90C11:
            svga->crtc[0x36] = '1';
            svga->crtc[0x37] = '1';
            break;
        case WD90C30:
            svga->crtc[0x36] = '3';
            svga->crtc[0x37] = '0';
            break;

        default:
            break;
    }

    svga->bpp     = 8;
    svga->miscout = 1;

    paradise->type = info->local;

    svga->hoverride = 1;

    return paradise;
}

static void *
paradise_pvga1a_ncr3302_init(const device_t *info)
{
    paradise_t *paradise = paradise_init(info, 256);

    if (paradise)
        rom_init(&paradise->bios_rom, "roms/machines/3302/c000-wd_1987-1989-740011-003058-019c.bin", 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);

    return paradise;
}

static void *
paradise_pvga1a_pc2086_init(const device_t *info)
{
    paradise_t *paradise = paradise_init(info, 256);

    if (paradise)
        rom_init(&paradise->bios_rom, "roms/machines/pc2086/40186.ic171", 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);

    return paradise;
}

static void *
paradise_pvga1a_pc3086_init(const device_t *info)
{
    paradise_t *paradise = paradise_init(info, 256);

    if (paradise)
        rom_init(&paradise->bios_rom, "roms/machines/pc3086/c000.bin", 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);

    return paradise;
}

void
paradise_pcs86_set_enabled(void *priv, int enabled)
{
    paradise_t *paradise = (paradise_t *) priv;
    svga_t     *svga;

    if (paradise == NULL)
        return;
    enabled = !!enabled;
    if (enabled == paradise->board_enabled)
        return;

    svga = &paradise->svga;
    if (enabled) {
        io_sethandler(0x03c0, 0x0020, paradise_in, NULL, NULL,
                      paradise_out, NULL, NULL, paradise);
        if (paradise->board_mapping_enabled)
            mem_mapping_enable(&svga->mapping);
    } else {
        paradise->board_mapping_enabled = svga->mapping.enable;
        io_removehandler(0x03c0, 0x0020, paradise_in, NULL, NULL,
                         paradise_out, NULL, NULL, paradise);
        mem_mapping_disable(&svga->mapping);
    }
    svga->vga_enabled       = enabled;
    paradise->board_enabled = enabled;
}

static void *
paradise_pvga1a_pcs86_init(const device_t *info)
{
    /* The PCS86 VGA firmware is embedded in the motherboard BIOS. */
    return paradise_init(info, 256);
}

static void *
paradise_pvga1a_pcs286_init(const device_t *info)
{
    /* Video firmware is embedded in the 128 KiB motherboard BIOS. */
    return paradise_init(info, 256);
}

static void *
paradise_pvga1a_pcs386sx_init(const device_t *info)
{
    /* Olivetti OVC 1.06 video firmware is embedded in the system BIOS. */
    return paradise_init(info, 256);
}

static void *
paradise_pvga1a_t5200_init(const device_t *info)
{
    paradise_t *paradise = paradise_init(info, 256);

    if (paradise) {
        paradise->t5200_pdc             = 1;
        paradise->t5200_dual_output      = (machine_get_config_int("display_output") == 0);
        paradise->t5200_internal_plasma = paradise->t5200_dual_output;
        paradise->t5200_panel_override   = -1;
        paradise->t5200_cmos_crt_only    = -1;
        if (paradise->t5200_dual_output && monitors[1].target_buffer == NULL) {
            video_monitor_init(1);
            video_inform_monitor(VIDEO_FLAG_TYPE_SPECIAL, &timing_paradise_pvga1a, 1);
            paradise->t5200_secondary_monitor = 1;
            paradise->svga.vsync_callback = paradise_t5200_dual_vsync;
        }
        paradise_t5200_update_palette(paradise);

        /* Award V1.xx is documented to require the original 24 KiB VGA ROM. */
        rom_init(&paradise->bios_rom, "roms/machines/t5200/t5200-vga-1988.bin",
                 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
        mem_mapping_add(&paradise->t5200_cga_mapping, 0xb8000, 0x08000,
                        paradise_t5200_cga_read, paradise_t5200_cga_readw,
                        paradise_t5200_cga_readl, paradise_t5200_cga_write,
                        paradise_t5200_cga_writew, paradise_t5200_cga_writel,
                        NULL, MEM_MAPPING_EXTERNAL, paradise);
        mem_mapping_disable(&paradise->t5200_cga_mapping);
        paradise->t5200_cga.vram         = paradise->svga.vram;
        paradise->t5200_cga.composite    = 0;
        paradise->t5200_cga.snow_enabled = 0;
        paradise->t5200_cga.double_type  = 1;
        paradise->t5200_cga.monitor_used = 0;
        timer_add(&paradise->t5200_cga.timer, paradise_t5200_cga_poll,
                  paradise, 1);
        timer_disable(&paradise->t5200_cga.timer);
        io_sethandler(0x0102, 1, paradise_t5200_setup_in, NULL, NULL,
                      paradise_t5200_setup_out, NULL, NULL, paradise);
        io_sethandler(0x46e8, 1, paradise_t5200_setup_in, NULL, NULL,
                      paradise_t5200_setup_out, NULL, NULL, paradise);
    }

    return paradise;
}

static void *
paradise_pvga1a_standalone_init(const device_t *info)
{
    paradise_t *paradise;
    uint32_t memsize = device_get_config_int("memory");

    paradise = paradise_init(info, memsize);

    if (paradise)
        rom_init(&paradise->bios_rom, "roms/video/pvga1a/BIOS.BIN", 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);

    return paradise;
}

static int
paradise_pvga1a_standalone_available(void)
{
    return rom_present("roms/video/pvga1a/BIOS.BIN");
}

static void *
paradise_wd90c11_megapc_init(const device_t *info)
{
    paradise_t *paradise = paradise_init(info, 512);

    if (paradise)
        rom_init_interleaved(&paradise->bios_rom,
                             "roms/machines/megapc/41651-bios lo.u18",
                             "roms/machines/megapc/211253-bios hi.u19",
                             0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);

    return paradise;
}

static void *
paradise_wd90c11_standalone_init(const device_t *info)
{
    paradise_t *paradise = paradise_init(info, 512);

    if (paradise)
        rom_init(&paradise->bios_rom, "roms/video/wd90c11/WD90C11.VBI", 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);

    return paradise;
}

static void *
paradise_wd90c11_ba013_init(const device_t *info)
{
    /* OVC 1.06 is part of the motherboard image at E0000. */
    return paradise_init(info, 512);
}

static void
paradise_wd90c11_m290sp_power_on(paradise_t *paradise)
{
    static const uint8_t seq[]  = { 0x03, 0x00, 0x03, 0x00, 0x02 };
    static const uint8_t gdc[]  = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0e, 0x00, 0xff };
    static const uint8_t attr[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
        0x0c, 0x00, 0x0f, 0x08, 0x00
    };
    static const uint8_t crtc_10_18[] = {
        0x9c, 0x0e, 0x8f, 0x28, 0x1f, 0x96, 0xb9, 0xa3, 0xff
    };
    svga_t *svga = &paradise->svga;
    FILE   *font;

    /*
     * The BA08 resident diagnostics only programs the 6845-compatible half
     * of the WD90C11.  The real on-board controller supplies the remaining
     * VGA text-mode state at power-on; a zeroed svga_t leaves the display
     * disabled and every DAC entry black.
     */
    for (uint8_t i = 0; i < sizeof(seq); i++) {
        paradise_out(0x3c4, i, paradise);
        paradise_out(0x3c5, seq[i], paradise);
    }

    for (uint8_t i = 0; i < sizeof(gdc); i++) {
        paradise_out(0x3ce, i, paradise);
        paradise_out(0x3cf, gdc[i], paradise);
    }

    for (uint8_t i = 0; i < sizeof(attr); i++) {
        svga->attrff = 0;
        paradise_out(0x3c0, i, paradise);
        paradise_out(0x3c0, attr[i], paradise);
    }
    svga->attrff = 0;
    paradise_out(0x3c0, 0x20, paradise);

    memcpy(&svga->crtc[0x10], crtc_10_18, sizeof(crtc_10_18));
    for (uint8_t i = 0; i < 16; i++) {
        svga->vgapal[i]  = cgapal[i + 16];
        svga->pallook[i] = makecol32(video_6to8[svga->vgapal[i].r],
                                     video_6to8[svga->vgapal[i].g],
                                     video_6to8[svga->vgapal[i].b]);
    }

    /* The BA08 BIOS stores its 256-character 8x16 font at offset 1739h. */
    font = rom_fopen("roms/machines/m290sp/BIOS-1.08.ROM", "rb");
    if (font) {
        fseek(font, 0x1739, SEEK_SET);
        for (uint16_t chr = 0; chr < 256; chr++)
            for (uint8_t row = 0; row < 16; row++)
                svga->vram[svga->charseta + (chr * 128) + (row * 4)] =
                    fgetc(font) & 0xff;
        fclose(font);
    }

    svga->fullchange = svga->monitor->mon_changeframecount;
    svga_recalctimings(svga);
}

static void *
paradise_wd90c11_m290sp_init(const device_t *info)
{
    paradise_t *paradise = paradise_init(info, 512);

    if (paradise)
        paradise_wd90c11_m290sp_power_on(paradise);

    return paradise;
}

static int
paradise_wd90c11_standalone_available(void)
{
    return rom_present("roms/video/wd90c11/WD90C11.VBI");
}

static void *
paradise_wd90c30_standalone_init(const device_t *info)
{
    paradise_t *paradise;
    uint32_t memsize = device_get_config_int("memory");

    paradise = paradise_init(info, memsize);

    if (paradise)
        rom_init(&paradise->bios_rom, "roms/video/wd90c30/90C30-LR.VBI", 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);

    return paradise;
}

static void *
paradise_wd90c31_m30030_init(const device_t *info)
{
    paradise_t *paradise;
    const char *fn;

    /*
     * OVC 1.09 occupies the first 24 KB of the selected 128 KB motherboard
     * image. The firmware scans it as an option ROM at C0000, so expose the
     * first 32 KB there while retaining the full image at E0000.
     */
    paradise = paradise_init(info, 1024);
    if (paradise) {
        device_context(machine_get_device(machine));
        fn = device_get_bios_file(machine_get_device(machine),
                                  device_get_config_bios("bios"), 0);
        rom_init(&paradise->bios_rom, fn, 0xc0000, 0x8000, 0x7fff, 0,
                 MEM_MAPPING_EXTERNAL);
        device_context_restore();
    }

    return paradise;
}

static int
paradise_wd90c30_standalone_available(void)
{
    return rom_present("roms/video/wd90c30/90C30-LR.VBI");
}

void
paradise_close(void *priv)
{
    paradise_t *paradise = (paradise_t *) priv;

    svga_close(&paradise->svga);

    if (paradise->t5200_secondary_monitor && monitors[1].target_buffer != NULL)
        video_monitor_close(1);

    free(paradise);
}

void
paradise_speed_changed(void *priv)
{
    paradise_t *paradise = (paradise_t *) priv;

    svga_recalctimings(&paradise->svga);
}

void
paradise_force_redraw(void *priv)
{
    paradise_t *paradise = (paradise_t *) priv;

    paradise->svga.fullchange = changeframecount;
}

const device_t paradise_pvga1a_pc2086_device = {
    .name          = "Paradise PVGA1A On-Board (Amstrad PC2086)",
    .internal_name = "pvga1a_pc2086",
    .flags         = 0,
    .local         = PVGA1A,
    .init          = paradise_pvga1a_pc2086_init,
    .close         = paradise_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = paradise_speed_changed,
    .force_redraw  = paradise_force_redraw,
    .machine       = "Amstrad PC2086",
    .config        = NULL
};

const device_t paradise_pvga1a_pc3086_device = {
    .name          = "Paradise PVGA1A On-Board (Amstrad PC3086)",
    .internal_name = "pvga1a_pc3086",
    .flags         = 0,
    .local         = PVGA1A,
    .init          = paradise_pvga1a_pc3086_init,
    .close         = paradise_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = paradise_speed_changed,
    .force_redraw  = paradise_force_redraw,
    .machine       = "Amstrad PC3086",
    .config        = NULL
};

const device_t paradise_pvga1a_pcs86_device = {
    .name          = "Paradise PVGA1A On-Board (Olivetti PCS86)",
    .internal_name = "pvga1a_pcs86",
    .flags         = 0,
    .local         = PVGA1A,
    .init          = paradise_pvga1a_pcs86_init,
    .close         = paradise_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = paradise_speed_changed,
    .force_redraw  = paradise_force_redraw,
    .machine       = "Olivetti PCS86",
    .config        = NULL
};

const device_t paradise_pvga1a_pcs286_device = {
    .name          = "Paradise PVGA1A On-Board (Olivetti PCS 286)",
    .internal_name = "pvga1a_pcs286",
    .flags         = 0,
    .local         = PVGA1A,
    .init          = paradise_pvga1a_pcs286_init,
    .close         = paradise_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = paradise_speed_changed,
    .force_redraw  = paradise_force_redraw,
    .machine       = "Olivetti PCS 286",
    .config        = NULL
};

const device_t paradise_pvga1a_pcs386sx_device = {
    .name          = "Paradise PVGA1A On-Board (Olivetti PCS 386SX)",
    .internal_name = "pvga1a_pcs386sx",
    .flags         = 0,
    .local         = PVGA1A,
    .init          = paradise_pvga1a_pcs386sx_init,
    .close         = paradise_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = paradise_speed_changed,
    .force_redraw  = paradise_force_redraw,
    .machine       = "Olivetti PCS 386SX",
    .config        = NULL
};

const device_t paradise_pvga1a_t5200_device = {
    .name          = "Paradise PVGA1A On-Board (Toshiba T5200)",
    .internal_name = "pvga1a_t5200",
    .flags         = 0,
    .local         = PVGA1A,
    .init          = paradise_pvga1a_t5200_init,
    .close         = paradise_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = paradise_speed_changed,
    .force_redraw  = paradise_force_redraw,
    .machine       = "Toshiba T5200",
    .config        = NULL
};

static const device_config_t paradise_pvga1a_config[] = {
  // clang-format off
    {
        .name           = "memory",
        .description    = "Memory size",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 512,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "256 KB", .value = 256 },
            { .description = "512 KB", .value = 512 },
            { .description = ""                     }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t paradise_pvga1a_ncr3302_device = {
    .name          = "Paradise PVGA1A On-Board (NCR 3302)",
    .internal_name = "pvga1a_ncr3302",
    .flags         = 0,
    .local         = PVGA1A,
    .init          = paradise_pvga1a_ncr3302_init,
    .close         = paradise_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = paradise_speed_changed,
    .force_redraw  = paradise_force_redraw,
    .machine       = "NCR 3302",
    .config        = paradise_pvga1a_config
};

const device_t paradise_pvga1a_device = {
    .name          = "Paradise PVGA1A",
    .internal_name = "pvga1a",
    .flags         = DEVICE_ISA,
    .local         = PVGA1A,
    .init          = paradise_pvga1a_standalone_init,
    .close         = paradise_close,
    .reset         = NULL,
    .available     = paradise_pvga1a_standalone_available,
    .speed_changed = paradise_speed_changed,
    .force_redraw  = paradise_force_redraw,
    .config        = paradise_pvga1a_config
};

const device_t paradise_wd90c11_megapc_device = {
    .name          = "Paradise WD90C11 On-Board (Amstrad MegaPC)",
    .internal_name = "wd90c11_megapc",
    .flags         = 0,
    .local         = WD90C11,
    .init          = paradise_wd90c11_megapc_init,
    .close         = paradise_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = paradise_speed_changed,
    .force_redraw  = paradise_force_redraw,
    .machine       = "Amstrad MegaPC",
    .config        = NULL
};

const device_t paradise_wd90c11_device = {
    .name          = "Paradise WD90C11-LR",
    .internal_name = "wd90c11",
    .flags         = DEVICE_ISA,
    .local         = WD90C11,
    .init          = paradise_wd90c11_standalone_init,
    .close         = paradise_close,
    .reset         = NULL,
    .available     = paradise_wd90c11_standalone_available,
    .speed_changed = paradise_speed_changed,
    .force_redraw  = paradise_force_redraw,
    .config        = NULL
};

const device_t paradise_wd90c11_ba013_device = {
    .name          = "Western Digital WD90C11 On-Board (Olivetti BA013)",
    .internal_name = "wd90c11_ba013",
    .flags         = 0,
    .local         = WD90C11,
    .init          = paradise_wd90c11_ba013_init,
    .close         = paradise_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = paradise_speed_changed,
    .force_redraw  = paradise_force_redraw,
    .machine       = "Olivetti BA013",
    .config        = NULL
};

const device_t paradise_wd90c11_m290sp_device = {
    .name          = "Western Digital WD90C11 On-Board (Olivetti M290 SP)",
    .internal_name = "wd90c11_m290sp",
    .flags         = 0,
    .local         = WD90C11,
    .init          = paradise_wd90c11_m290sp_init,
    .close         = paradise_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = paradise_speed_changed,
    .force_redraw  = paradise_force_redraw,
    .machine       = "Olivetti M290 SP",
    .config        = NULL
};


static const device_config_t paradise_wd90c30_config[] = {
  // clang-format off
    {
        .name           = "memory",
        .description    = "Memory size",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 1024,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "256 KB", .value =  256 },
            { .description = "512 KB", .value =  512 },
            { .description = "1 MB",   .value = 1024 },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t paradise_wd90c30_device = {
    .name          = "Paradise WD90C30-LR",
    .internal_name = "wd90c30",
    .flags         = DEVICE_ISA,
    .local         = WD90C30,
    .init          = paradise_wd90c30_standalone_init,
    .close         = paradise_close,
    .reset         = NULL,
    .available     = paradise_wd90c30_standalone_available,
    .speed_changed = paradise_speed_changed,
    .force_redraw  = paradise_force_redraw,
    .config        = paradise_wd90c30_config
};

const device_t paradise_wd90c31_m30030_device = {
    .name          = "Western Digital WD90C31 On-Board (Olivetti M300-30)",
    .internal_name = "wd90c31_m30030",
    .flags         = 0,
    .local         = WD90C30,
    .init          = paradise_wd90c31_m30030_init,
    .close         = paradise_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = paradise_speed_changed,
    .force_redraw  = paradise_force_redraw,
    .machine       = "Olivetti M300-30 / M300-30P",
    .config        = NULL
};
