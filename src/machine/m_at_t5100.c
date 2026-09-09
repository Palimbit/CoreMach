/*
 * BluMach experimental Toshiba T5100 platform subset.
 * Copyright 2026 rtzor, Project BluMach.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This models contracts supported by the T5100 maintenance manual and BIOS
 * V2.30 observation.  The proprietary AGS ROM remains unavailable; the
 * optional compatibility path adapts a locally supplied IBM EGA BIOS at run
 * time and never presents it as recovered Toshiba firmware.
 * See doc/machines/toshiba-t5100.md for current operation and
 * doc/machines/toshiba-t5100-implementation.md for the engineering record.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
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

#define T5100_COMPAT_EGA_ROM "roms/video/ega/ibm_6277356_ega_card_u44_27128.bin"
#define T5100_LIM_FRAME     0xd0000
#define T5100_LIM_PAGE_SIZE 0x4000
#define T5100_LIM_PAGES     24

/* BIOS V2.30 F000:3536.  The first and second groups select the same four
   16 KiB frame slots; only one bank may provide a slot at a time. */
static const uint16_t t5100_lim_ports[8] = {
    0x0208, 0x4208, 0x8208, 0xc208, 0x0218, 0x4218, 0x8218, 0xc218
};

typedef struct t5100_t {
    ega_t         ega;
    mem_mapping_t lim_mapping[4];
    uint8_t       lim_regs[8];
    uint32_t      lim_base[4];
    uint8_t       system_regs[16];
    uint8_t       sram[0x800];
    uint8_t       ags_control;
    uint8_t       video_mode[2];
    uint8_t       crtc_control;
    uint8_t       crtc_extension;
    uint16_t      last_mode_port;
    uint64_t      last_mode_sequence;
    uint64_t      unlocked_sequence;
    uint32_t      plasma16[256];
    uint32_t      plasma64[256];
    uint8_t       kbc2_data;
    uint8_t       kbc2_status;
    uint8_t       post_code;
    int           post_seen;
    unsigned      samples;
    pc_timer_t    sample_timer;
    unsigned      trace_count;
    int           trace;
} t5100_t;

/* BIOS V2.30 uses a Toshiba-specific pre-RAM handoff rather than the normal
   option-ROM entry: it checks "AGS" at offset 000Ah, builds a far-return frame
   at F000:213Ah and jumps through the far pointer at 3FF0h.  Later, after it
   selects the internal panel at F000:399Dh, it calls a second AGS hook through
   3FF4h.  Both undocumented hooks can conservatively return to the system BIOS
   while the unmodified option entry at offset 0003h installs the standard EGA
   INT 10h services.

   The transformation is deliberately in-memory.  The source ROM remains an
   external local test input and no derived firmware bytes enter the tree. */
static int
t5100_prepare_compat_rom(uint8_t *rom, size_t size)
{
    if (rom == NULL || size < 0x4000 || rom[0] != 0x55 || rom[1] != 0xaa)
        return 0;

    rom[0x000a] = 'A';
    rom[0x000b] = 'G';
    rom[0x000c] = 'S';

    rom[0x3fe0] = 0xcb; /* RETF to F000:2185 on the ROM-resident frame. */
    rom[0x3ff0] = 0xe0;
    rom[0x3ff1] = 0x3f;
    rom[0x3ff2] = 0x00;
    rom[0x3ff3] = 0xc0;
    rom[0x3ff4] = 0xe0;
    rom[0x3ff5] = 0x3f;
    rom[0x3ff6] = 0x00;
    rom[0x3ff7] = 0xc0;

    /* The IBM header declares 20h 512-byte blocks.  Keep that boundary and
       repair only its final checksum byte after the compatibility additions. */
    rom[0x3fff] = 0;
    uint8_t sum = 0;
    for (size_t offset = 0; offset < 0x4000; offset++)
        sum += rom[offset];
    rom[0x3fff] = (uint8_t) (0 - sum);
    return 1;
}

static void
t5100_post_out(uint16_t port, uint8_t val, void *priv)
{
    t5100_t *dev = priv;
    if (!dev->post_seen || dev->post_code != val) {
        dev->post_seen = 1;
        dev->post_code = val;
        pclog("T5100 POST %04X:%04X port=%04X code=%02X\n",
              CS, cpu_state.pc, port, val);
    }
}

static uint8_t
t5100_hardware_status(void)
{
    /* Maintenance manual, table 3-7: plasma fitted, 16 MHz, 2 MB-format
       (1.44 MB) internal drive, one internal drive, normal A/B assignment,
       no external drive, and internal 2HD.  This is the documented sample
       configuration (10001100b). */
    return 0x8c;
}

static void
t5100_kbc2_command(t5100_t *dev, uint8_t command)
{
    switch (command) {
        case 0xbb:
            /* BIOS V2.30 F000:0D52 treats response bit 2 as an error/Fn
               condition.  Zero represents the observed normal POST path. */
            dev->kbc2_data = 0x00;
            dev->kbc2_status |= 0x01;
            break;

        case 0xb4:
            dev->kbc2_data = t5100_hardware_status();
            dev->kbc2_status |= 0x01;
            break;

        default:
            /* Unknown controller commands are deliberately not acknowledged. */
            break;
    }
}

static uint8_t
t5100_kbc2_in(uint16_t port, void *priv)
{
    t5100_t *dev = priv;
    if (port == 0x8064)
        return dev->kbc2_status;

    uint8_t val = dev->kbc2_status & 0x01 ? dev->kbc2_data : 0xff;
    dev->kbc2_status &= ~0x01;
    return val;
}

static void
t5100_kbc2_out(uint16_t port, uint8_t val, void *priv)
{
    t5100_t *dev = priv;
    if (dev->trace && dev->trace_count++ < 512)
        pclog("T5100 KBC2 %04X:%04X OUT %04X=%02X\n",
              CS, cpu_state.pc, port, val);
    if (port == 0x8064)
        t5100_kbc2_command(dev, val);
}

static uint8_t
t5100_system_in(uint16_t port, void *priv)
{
    t5100_t *dev = priv;
    uint8_t val = dev->system_regs[port & 0x0f];
    if (dev->trace && dev->trace_count++ < 512)
        pclog("T5100 SYS %04X:%04X IN %04X=%02X\n",
              CS, cpu_state.pc, port, val);
    return val;
}

static void
t5100_system_out(uint16_t port, uint8_t val, void *priv)
{
    t5100_t *dev = priv;
    /* Read/write latches are a bounded approximation.  BIOS V2.30 proves
       coherent read-modify-write use of 8084; electrical side effects other
       than the AGS ROM gate remain unknown and therefore unimplemented. */
    dev->system_regs[port & 0x0f] = val;
    if (dev->trace && dev->trace_count++ < 512)
        pclog("T5100 SYS %04X:%04X OUT %04X=%02X\n",
              CS, cpu_state.pc, port, val);
}

static uint8_t
t5100_lim_read(uint32_t addr, void *priv)
{
    t5100_t *dev = priv;
    if (addr < T5100_LIM_FRAME || addr >= T5100_LIM_FRAME + 0x10000)
        return 0xff;
    uint32_t base = dev->lim_base[(addr >> 14) & 3];
    return base ? ram[base + (addr & (T5100_LIM_PAGE_SIZE - 1))] : 0xff;
}

static void
t5100_lim_write(uint32_t addr, uint8_t val, void *priv)
{
    t5100_t *dev = priv;
    if (addr < T5100_LIM_FRAME || addr >= T5100_LIM_FRAME + 0x10000)
        return;
    uint32_t base = dev->lim_base[(addr >> 14) & 3];
    if (base)
        ram[base + (addr & (T5100_LIM_PAGE_SIZE - 1))] = val;
}

static uint8_t
t5100_lim_in(uint16_t port, void *priv)
{
    t5100_t *dev = priv;
    unsigned reg = ((port >> 14) & 3) | ((port & 0x10) >> 2);
    return dev->lim_regs[reg];
}

static void
t5100_lim_out(uint16_t port, uint8_t val, void *priv)
{
    t5100_t *dev = priv;
    unsigned reg  = ((port >> 14) & 3) | ((port & 0x10) >> 2);
    unsigned slot = reg & 3;
    unsigned page = (val & 0x7f) + ((reg >> 2) * 128);
    uint32_t base = 0;

    dev->lim_regs[reg] = val;
    /* BIOS V2.30 directly tests the 24 base pages.  Optional-card physical
       decode is not yet documented, so bank 1 and pages >=24 stay absent. */
    if ((val & 0x80) && page < T5100_LIM_PAGES)
        base = 0xa0000 + page * T5100_LIM_PAGE_SIZE;
    dev->lim_base[slot] = base;
    mem_mapping_set_exec(&dev->lim_mapping[slot], base ? ram + base : NULL);
    if (base)
        mem_mapping_enable(&dev->lim_mapping[slot]);
    else
        mem_mapping_disable(&dev->lim_mapping[slot]);
}

#ifndef T5100_CALLBACK_TEST
static video_timings_t t5100_video_timing = {
    .type = VIDEO_ISA, .write_b = 8, .write_w = 16, .write_l = 32,
    .read_b = 8, .read_w = 16, .read_l = 32
};

/* The manual documents four plasma levels produced by CELT.  The electrical
   transfer curve is unavailable, so these RGB values are presentation-only;
   the ordering and four-level reduction are the emulated contract. */
static uint32_t
t5100_plasma_color(unsigned code)
{
    static const uint32_t levels[4] = {
        0x000000, 0x783008, 0xc85010, 0xff7c28
    };
    return levels[(code >> 1) & 3];
}

static void
t5100_plasma_palette(t5100_t *dev)
{
    for (unsigned color = 0; color < 256; color++)
        dev->plasma16[color] = dev->plasma64[color] =
            t5100_plasma_color(color);
    dev->ega.output_palette16 = dev->plasma16;
    dev->ega.output_palette64 = dev->plasma64;
    dev->ega.pallook = dev->ega.vres ? dev->plasma16 : dev->plasma64;
    dev->ega.overscan_color =
        dev->ega.pallook[dev->ega.attrregs[0x11] & (dev->ega.vres ? 15 : 63)];
}

static void
t5100_recalctimings(ega_t *ega)
{
    int panel_lines =
        (ega->crtc[0x12] | ((ega->crtc[7] & 2) << 7)) + 1;

    /* The original panel is 640x400.  This compatibility path preserves the
       generic EGA guest counters; exact AGS line expansion remains unknown. */

    ega->vtotal = (ega->crtc[6] | ((ega->crtc[7] & 1) << 8)) + 2;
    ega->dispend = panel_lines;
    ega->vsyncstart =
        (ega->crtc[0x10] | ((ega->crtc[7] & 4) << 6)) + 1;
    ega->vres = !(ega->miscout & 0x80);
}

static int
t5100_ega_mapped(t5100_t *dev, uint32_t addr)
{
    switch (dev->ega.gdcreg[6] & 0x0c) {
        case 0x04: return addr < 0xb0000;
        case 0x08: return addr >= 0xb0000 && addr < 0xb8000;
        case 0x0c: return addr >= 0xb8000;
        default:   return 1;
    }
}

static uint8_t
t5100_video_read(uint32_t addr, void *priv)
{
    t5100_t *dev = priv;
    if ((dev->ags_control == 0x40 || dev->ags_control == 0x41 ||
         dev->ags_control == 0x50) &&
        addr >= 0xa0000 && addr < 0xa0800) {
        cycles -= video_timing_read_b;
        return dev->sram[addr - 0xa0000];
    }
    return t5100_ega_mapped(dev, addr) ? ega_read(addr, &dev->ega) : 0xff;
}

static void
t5100_video_write(uint32_t addr, uint8_t val, void *priv)
{
    t5100_t *dev = priv;
    if ((dev->ags_control == 0x40 || dev->ags_control == 0x41 ||
         dev->ags_control == 0x50) &&
        addr >= 0xa0000 && addr < 0xa0800) {
        cycles -= video_timing_write_b;
        dev->sram[addr - 0xa0000] = val;
    } else if (t5100_ega_mapped(dev, addr)) {
        ega_write(addr, val, &dev->ega);
    }
}

static uint8_t
t5100_video_in(uint16_t port, void *priv)
{
    t5100_t *dev = priv;
    if (port == 0x3b8 || port == 0x3d8) {
        if (io_access_width == 1 && dev->last_mode_port == port &&
            dev->last_mode_sequence + 1 == io_access_sequence) {
            dev->unlocked_sequence = io_access_sequence + 1;
            dev->last_mode_port = 0;
        } else {
            dev->last_mode_port = io_access_width == 1 ? port : 0;
            dev->unlocked_sequence = 0;
        }
        dev->last_mode_sequence = io_access_sequence;
        return dev->video_mode[port == 0x3d8];
    }
    return ega_in(port, &dev->ega);
}

static void
t5100_video_out(uint16_t port, uint8_t val, void *priv)
{
    t5100_t *dev = priv;
    int unlocked = io_access_width == 1 &&
                   dev->unlocked_sequence == io_access_sequence;

    if (port == 0x3df) {
        if (unlocked) {
            dev->ags_control = val;
            if (dev->trace && val != 0x00 && val != 0x01 &&
                val != 0x40 && val != 0x41 && val != 0x50)
                pclog("T5100: unsupported AGS SRAM selector %02X\n", val);
        }
        return;
    }
    if (unlocked && (port == 0x3b5 || port == 0x3d5)) {
        if (dev->ega.crtcreg == 0 && (val == 0x85 || val == 0x8a)) {
            dev->crtc_control = val;
            return;
        }
        if (dev->ega.crtcreg == 0x1a) {
            dev->crtc_extension = val;
            ega_recalctimings(&dev->ega);
            return;
        }
    }
    if (port == 0x3de || port == 0x3bb || port == 0x3db)
        return;
    if (port == 0x3b8 || port == 0x3d8) {
        dev->video_mode[port == 0x3d8] = val;
        return;
    }

    ega_out(port, val, &dev->ega);
    mem_mapping_set_addr(&dev->ega.mapping, 0xa0000, 0x20000);
}

static void
t5100_sample(void *priv)
{
    t5100_t *dev = priv;
    timer_advance_u64(&dev->sample_timer, 1000000 * TIMER_USEC);
    if (dev->samples++ < 30)
        pclog("T5100 SAMPLE %u CS:IP=%04X:%04X AX=%04X DL=%02X "
              "BDA-mode=%02X RAM=%u POST=%02X\n",
              dev->samples, CS, cpu_state.pc, AX, DL,
              ram[0x449], ram[0x413] | (ram[0x414] << 8), dev->post_code);
}

static void *
t5100_init(const device_t *info)
{
    (void) info;
    t5100_t *dev = calloc(1, sizeof(*dev));
    dev->trace = machine_get_config_int("trace");
    memset(dev->sram, 0xff, sizeof(dev->sram));

    video_inform(VIDEO_FLAG_TYPE_SPECIAL, &t5100_video_timing);
    ega_set_type(&dev->ega, EGA_TOSHIBA);
    dev->ega.priv_parent = dev;
    dev->ega.timing_override = t5100_recalctimings;
    ega_init(&dev->ega, 9, 0);
    t5100_plasma_palette(dev);
    /* The compatibility handoff returns before the option-ROM entry runs.
       Establish a safe initial renderer for that short pre-scan interval. */
    ega_recalctimings(&dev->ega);
    dev->ega.x_add = 8;
    dev->ega.y_add = 14;
    overscan_x = 16;
    overscan_y = 28;

    if (rom_init(&dev->ega.bios_rom, T5100_COMPAT_EGA_ROM,
                 0xc0000, 0x8000, 0x7fff, 0,
                 MEM_MAPPING_EXTERNAL) < 0 ||
        !t5100_prepare_compat_rom(dev->ega.bios_rom.rom,
                                  dev->ega.bios_rom.sz)) {
        pclog("T5100: unable to prepare the AGS compatibility ROM\n");
        free(dev->ega.vram);
        free(dev);
        return NULL;
    }
    mem_mapping_add(&dev->ega.mapping, 0xa0000, 0x20000,
                    t5100_video_read, NULL, NULL,
                    t5100_video_write, NULL, NULL,
                    NULL, MEM_MAPPING_EXTERNAL, dev);

    for (unsigned slot = 0; slot < 4; slot++) {
        mem_mapping_add(&dev->lim_mapping[slot],
                        T5100_LIM_FRAME + slot * T5100_LIM_PAGE_SIZE,
                        T5100_LIM_PAGE_SIZE,
                        t5100_lim_read, NULL, NULL,
                        t5100_lim_write, NULL, NULL,
                        NULL, MEM_MAPPING_EXTERNAL, dev);
        mem_mapping_disable(&dev->lim_mapping[slot]);
    }
    for (unsigned reg = 0; reg < 8; reg++)
        io_sethandler(t5100_lim_ports[reg], 1,
                      t5100_lim_in, NULL, NULL,
                      t5100_lim_out, NULL, NULL, dev);
    io_sethandler(0x8060, 1, t5100_kbc2_in, NULL, NULL,
                  t5100_kbc2_out, NULL, NULL, dev);
    io_sethandler(0x8064, 1, t5100_kbc2_in, NULL, NULL,
                  t5100_kbc2_out, NULL, NULL, dev);
    io_sethandler(0x8080, 0x10, t5100_system_in, NULL, NULL,
                  t5100_system_out, NULL, NULL, dev);
    io_sethandler(0x3b0, 0x30, t5100_video_in, NULL, NULL,
                  t5100_video_out, NULL, NULL, dev);
    if (dev->trace) {
        /* The system BIOS mirrors its diagnostic byte to both printer bases.
           I/O dispatch is shared, so this listener observes without replacing
           any configured LPT device. */
        io_sethandler(0x0378, 1, NULL, NULL, NULL,
                      t5100_post_out, NULL, NULL, dev);
        io_sethandler(0x03bc, 1, NULL, NULL, NULL,
                      t5100_post_out, NULL, NULL, dev);
        timer_add(&dev->sample_timer, t5100_sample, dev, 0);
        timer_set_delay_u64(&dev->sample_timer, 1000000 * TIMER_USEC);
    }

    pclog("T5100 experimental: PEGA2/EGA compatibility video, four-level "
          "plasma, 2 KiB AGS SRAM, independent 8060/8064 KBC2, "
          "8080-808F latches and 384 KiB LIM subset; exact AGS/CELT, "
          "firmware and optional-card decode remain unavailable.\n");
    return dev;
}

static void
t5100_close(void *priv)
{
    t5100_t *dev = priv;
    if (dev->trace)
        timer_disable(&dev->sample_timer);
    monitors[0].mon_pixel_height_ratio = 0.0;
    free(dev->ega.bios_rom.rom);
    free(dev->ega.vram);
    free(dev);
}

static const device_t t5100_platform_device = {
    .name          = "Toshiba T5100 experimental platform",
    .internal_name = "t5100_platform",
    .flags         = DEVICE_ISA,
    .init          = t5100_init,
    .close         = t5100_close
};

static const device_config_t t5100_config[] = {
    {
        .name = "bios", .description = "BIOS Version", .type = CONFIG_BIOS,
        .default_string = "v230",
        .bios = {
            {
                .name = "Toshiba 2.30 + compatible AGS", .internal_name = "v230",
                .bios_type = BIOS_NORMAL, .files_no = 3, .size = 65536,
                .files = {
                    "roms/machines/t5100/Toshiba T5100 - BIOS ROM - V2.30 - EVEN - 042F - 27C256.bin",
                    "roms/machines/t5100/Toshiba T5100 - BIOS ROM - V2.30 - ODD - 043F - 27C256.bin",
                    T5100_COMPAT_EGA_ROM
                }
            },
            { .files_no = 0 }
        }
    },
    { .name = "trace", .description = "Log experimental firmware progress",
      .type = CONFIG_BINARY, .default_int = 0 },
    { .name = "", .type = CONFIG_END }
};

const device_t t5100_device = {
    .name = "Toshiba T5100", .internal_name = "t5100", .config = t5100_config
};

int
machine_at_t5100_init(const machine_t *model)
{
    if (!device_available(model->device))
        return 0;

    device_context(model->device);
    const char *bios = device_get_config_bios("bios");
    const char *lo = device_get_bios_file(model->device, bios, 0);
    const char *hi = device_get_bios_file(model->device, bios, 1);
    int ret = bios_load_interleaved(lo, hi, 0xf0000, 65536, 0);
    device_context_restore();
    if (bios_only || !ret)
        return ret;

    machine_at_init(model);
    video_reset(gfxcard[0]);
    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_xt_t1x00_device);
    if (hdc_current[0] == HDC_INTERNAL)
        device_add(&ide_t5100_device);
    device_add(&t5100_platform_device);
    return ret;
}
#endif
