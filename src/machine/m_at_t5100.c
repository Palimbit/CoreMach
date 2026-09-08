/*
 * BluMach experimental Toshiba T5100 platform subset.
 * Copyright 2026 rtzor, Project BluMach.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This models only contracts supported by the T5100 maintenance manual and
 * BIOS V2.30 observation.  The proprietary AGS/CELT display and its separate
 * C0000 ROM are intentionally not substituted with another Toshiba machine.
 * See doc/machines/toshiba-t5100.md for evidence and current limitations.
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
#include <86box/machine.h>

#define T5100_LIM_FRAME     0xd0000
#define T5100_LIM_PAGE_SIZE 0x4000
#define T5100_LIM_PAGES     24

/* BIOS V2.30 F000:3536.  The first and second groups select the same four
   16 KiB frame slots; only one bank may provide a slot at a time. */
static const uint16_t t5100_lim_ports[8] = {
    0x0208, 0x4208, 0x8208, 0xc208, 0x0218, 0x4218, 0x8218, 0xc218
};

typedef struct t5100_t {
    mem_mapping_t lim_mapping[4];
    uint8_t       lim_regs[8];
    uint32_t      lim_base[4];
    uint8_t       system_regs[16];
    uint8_t       kbc2_data;
    uint8_t       kbc2_status;
    uint8_t       post_code;
    int           post_seen;
    unsigned      samples;
    pc_timer_t    sample_timer;
    unsigned      trace_count;
    int           trace;
} t5100_t;

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
       configuration (10001100b).  The missing AGS ROM still forces the BIOS'
       separate external-video fallback path. */
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

    pclog("T5100 experimental: external-video BIOS fallback, independent "
          "8060/8064 KBC2, 8080-808F latches and 384 KiB LIM subset; "
          "AGS/CELT and optional-card decode unavailable.\n");
    return dev;
}

static void
t5100_close(void *priv)
{
    t5100_t *dev = priv;
    if (dev->trace)
        timer_disable(&dev->sample_timer);
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
                .name = "Toshiba 2.30", .internal_name = "v230",
                .bios_type = BIOS_NORMAL, .files_no = 2, .size = 65536,
                .files = {
                    "roms/machines/t5100/Toshiba T5100 - BIOS ROM - V2.30 - EVEN - 042F - 27C256.bin",
                    "roms/machines/t5100/Toshiba T5100 - BIOS ROM - V2.30 - ODD - 043F - 27C256.bin"
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
        device_add(&fdc_at_device);
    if (hdc_current[0] == HDC_INTERNAL)
        device_add(&ide_isa_device);
    device_add(&t5100_platform_device);
    return ret;
}
#endif
