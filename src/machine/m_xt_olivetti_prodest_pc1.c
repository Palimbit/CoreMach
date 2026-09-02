/*
 * BluMach, a preservation-focused fork of 86Box.
 *
 * Experimental Olivetti Prodest PC 1 support.
 *
 * Author: rtzor
 * Project: BluMach
 * Copyright 2026 rtzor
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <86box/86box.h>
#include <86box/plat_unused.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/dma.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/gameport.h>
#include <86box/hdc.h>
#include <86box/io.h>
#include <86box/keyboard.h>
#include <86box/lpt.h>
#include <86box/machine.h>
#include <86box/mem.h>
#include <86box/nec_v40.h>
#include <86box/nmi.h>
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/ppi.h>
#include <86box/rom.h>
#include <86box/serial.h>
#include <86box/snd_speaker.h>
#include <86box/vid_v6355.h>

#include <86box/video.h>

typedef struct prodest_pc1_t {
    pc_timer_t    keyboard_timer;
#ifdef ENABLE_OLIVETTI_PC1_TRACE
    pc_timer_t    trace_timer;
    uint32_t      trace_polls;
    uint32_t      trace_vram_hash;
#endif
    uint8_t       video_control;
    uint32_t      v40_dma_base_addr[4];
    uint32_t      v40_dma_cur_addr[4];
    uint16_t      v40_dma_base_count[4];
    uint16_t      v40_dma_cur_count[4];
    uint8_t       v40_dma_mode[4];
    uint8_t       v40_dma_channel;
    uint8_t       v40_dma_base_only;
    uint8_t       v40_dma_device_control[2];
    uint8_t       v40_dma_mask;
    uint8_t       v40_dma_status;
    uint8_t       port61;
    uint8_t       keyboard_queue[32];
    uint8_t       keyboard_queue_start;
    uint8_t       keyboard_queue_end;
    uint8_t       keyboard_interface_enabled;
    uint8_t       keyboard_led_pending;
    mem_mapping_t bios_mirror[3];
} prodest_pc1_t;

#define PRODEST_PC1_V40_FDC_CHANNEL 1
#define PRODEST_PC1_8237_FDC_CHANNEL 2
#define PRODEST_PC1_V40_HDC_CHANNEL 2
#define PRODEST_PC1_8237_HDC_CHANNEL 3

static prodest_pc1_t *prodest_pc1_active;

static void
prodest_pc1_keyboard_poll(void *priv)
{
    prodest_pc1_t *dev = (prodest_pc1_t *) priv;

    timer_advance_u64(&dev->keyboard_timer, 1000 * TIMER_USEC);

    /* The MBL8042H presents keyboard data asynchronously.  Keep the UI-side
       key producer limited to filling the controller queue and assert IRQ1
       from the emulation thread, as the physical controller would. */
    if (dev->keyboard_queue_start != dev->keyboard_queue_end)
        picint(1 << 1);
}

static void
prodest_pc1_v40_dma_sync_channel(prodest_pc1_t *dev, uint8_t v40_channel,
                                 uint8_t dma8237_channel)
{
    dma_t        *dma_channel = &dma[dma8237_channel];
    const uint8_t mode        = dev->v40_dma_mode[v40_channel];

    dma_channel->ab   = dev->v40_dma_base_addr[v40_channel] & 0xfffff;
    dma_channel->ac   = dev->v40_dma_cur_addr[v40_channel] & 0xfffff;
    dma_channel->cb   = dev->v40_dma_base_count[v40_channel];
    dma_channel->cc   = dev->v40_dma_cur_count[v40_channel];
    dma_channel->size = 0;

    /* The V40 direction encoding is the reverse of the 8237 encoding used
       by dma_channel_read/write(). Preserve auto-init, decrement, and the
       demand/single/block transfer selection while translating it. */
    dma_channel->mode = dma8237_channel;
    if ((mode & 0x0c) == 0x04)
        dma_channel->mode |= 0x04;
    else if ((mode & 0x0c) == 0x08)
        dma_channel->mode |= 0x08;
    dma_channel->mode |= mode & 0x30;
    dma_channel->mode |= (mode & 0xc0);

    dma_e |= 1 << dma8237_channel;
    if (dev->v40_dma_mask & (1 << v40_channel))
        dma_m |= 1 << dma8237_channel;
    else
        dma_m &= ~(1 << dma8237_channel);
}

static void
prodest_pc1_v40_dma_reset(prodest_pc1_t *dev)
{
    dev->v40_dma_channel = 0;
    dev->v40_dma_base_only = 0;
    dev->v40_dma_device_control[0] = 0;
    dev->v40_dma_device_control[1] = 0;
    dev->v40_dma_status = 0;
    dev->v40_dma_mask = 0x0f;
    memset(dev->v40_dma_mode, 0, sizeof(dev->v40_dma_mode));
    prodest_pc1_v40_dma_sync_channel(dev, PRODEST_PC1_V40_FDC_CHANNEL,
                                     PRODEST_PC1_8237_FDC_CHANNEL);
    prodest_pc1_v40_dma_sync_channel(dev, PRODEST_PC1_V40_HDC_CHANNEL,
                                     PRODEST_PC1_8237_HDC_CHANNEL);
}

static uint8_t
prodest_pc1_v40_dma_read(uint16_t port, void *priv)
{
    prodest_pc1_t *dev = (prodest_pc1_t *) priv;
    const uint8_t reg = port & 0x0f;
    const uint8_t channel = dev->v40_dma_channel;
    uint32_t addr;
    uint16_t count;
    uint8_t ret = 0xff;

    if (channel == PRODEST_PC1_V40_FDC_CHANNEL && !dev->v40_dma_base_only) {
        dev->v40_dma_cur_addr[channel] = dma[PRODEST_PC1_8237_FDC_CHANNEL].ac & 0xfffff;
        dev->v40_dma_cur_count[channel] = (uint16_t) dma[PRODEST_PC1_8237_FDC_CHANNEL].cc;
        if (dma[PRODEST_PC1_8237_FDC_CHANNEL].cc < 0)
            dev->v40_dma_status |= 1 << channel;
    }
    if (channel == PRODEST_PC1_V40_HDC_CHANNEL && !dev->v40_dma_base_only) {
        dev->v40_dma_cur_addr[channel] = dma[PRODEST_PC1_8237_HDC_CHANNEL].ac & 0xfffff;
        dev->v40_dma_cur_count[channel] = (uint16_t) dma[PRODEST_PC1_8237_HDC_CHANNEL].cc;
        if (dma[PRODEST_PC1_8237_HDC_CHANNEL].cc < 0)
            dev->v40_dma_status |= 1 << channel;
    }

    addr = dev->v40_dma_base_only ? dev->v40_dma_base_addr[channel]
                                  : dev->v40_dma_cur_addr[channel];
    count = dev->v40_dma_base_only ? dev->v40_dma_base_count[channel]
                                   : dev->v40_dma_cur_count[channel];

    switch (reg) {
        case 0x01: ret = (1 << channel) | (dev->v40_dma_base_only ? 0x10 : 0x00); break;
        case 0x02: ret = count & 0xff; break;
        case 0x03: ret = count >> 8; break;
        case 0x04: ret = addr & 0xff; break;
        case 0x05: ret = (addr >> 8) & 0xff; break;
        case 0x06: ret = (addr >> 16) & 0x0f; break;
        case 0x08: ret = dev->v40_dma_device_control[0]; break;
        case 0x09: ret = dev->v40_dma_device_control[1]; break;
        case 0x0a: ret = dev->v40_dma_mode[channel]; break;
        case 0x0b: ret = dev->v40_dma_status; break;
        case 0x0f: ret = dev->v40_dma_mask; break;
        default: break;
    }

#ifdef ENABLE_OLIVETTI_PC1_TRACE
    always_log("OLIVETTI_PC1_DMA R %04X=%02X ch=%u base=%u\n", port, ret,
               channel, dev->v40_dma_base_only);
#endif
    return ret;
}

static void
prodest_pc1_v40_dma_write(uint16_t port, uint8_t val, void *priv)
{
    prodest_pc1_t *dev = (prodest_pc1_t *) priv;
    const uint8_t reg = port & 0x0f;
    uint8_t channel = dev->v40_dma_channel;
    uint32_t *addr_base = &dev->v40_dma_base_addr[channel];
    uint32_t *addr_cur = &dev->v40_dma_cur_addr[channel];
    uint16_t *count_base = &dev->v40_dma_base_count[channel];
    uint16_t *count_cur = &dev->v40_dma_cur_count[channel];

#ifdef ENABLE_OLIVETTI_PC1_TRACE
    always_log("OLIVETTI_PC1_DMA W %04X=%02X ch=%u base=%u\n", port, val,
               channel, dev->v40_dma_base_only);
#endif

    switch (reg) {
        case 0x00:
            if (val & 0x01)
                prodest_pc1_v40_dma_reset(dev);
            return;
        case 0x01:
            dev->v40_dma_channel = val & 0x03;
            dev->v40_dma_base_only = !!(val & 0x04);
            return;
        case 0x02:
            *count_base = (*count_base & 0xff00) | val;
            if (!dev->v40_dma_base_only)
                *count_cur = (*count_cur & 0xff00) | val;
            break;
        case 0x03:
            *count_base = (*count_base & 0x00ff) | ((uint16_t) val << 8);
            if (!dev->v40_dma_base_only)
                *count_cur = (*count_cur & 0x00ff) | ((uint16_t) val << 8);
            break;
        case 0x04:
            *addr_base = (*addr_base & 0xfff00) | val;
            if (!dev->v40_dma_base_only)
                *addr_cur = (*addr_cur & 0xfff00) | val;
            break;
        case 0x05:
            *addr_base = (*addr_base & 0xf00ff) | ((uint32_t) val << 8);
            if (!dev->v40_dma_base_only)
                *addr_cur = (*addr_cur & 0xf00ff) | ((uint32_t) val << 8);
            break;
        case 0x06:
            *addr_base = (*addr_base & 0x0ffff) | ((uint32_t) (val & 0x0f) << 16);
            if (!dev->v40_dma_base_only)
                *addr_cur = (*addr_cur & 0x0ffff) | ((uint32_t) (val & 0x0f) << 16);
            break;
        case 0x08: dev->v40_dma_device_control[0] = val; break;
        case 0x09: dev->v40_dma_device_control[1] = val; break;
        case 0x0a: dev->v40_dma_mode[channel] = val; break;
        case 0x0f: dev->v40_dma_mask = val & 0x0f; break;
        default: return;
    }

    if (channel == PRODEST_PC1_V40_FDC_CHANNEL)
        prodest_pc1_v40_dma_sync_channel(dev, channel, PRODEST_PC1_8237_FDC_CHANNEL);
    else if (channel == PRODEST_PC1_V40_HDC_CHANNEL)
        prodest_pc1_v40_dma_sync_channel(dev, channel, PRODEST_PC1_8237_HDC_CHANNEL);
}

static void
prodest_pc1_keyboard_add(uint16_t val)
{
    prodest_pc1_t *dev = prodest_pc1_active;

    if (dev == NULL)
        return;

    dev->keyboard_queue[dev->keyboard_queue_end] = (uint8_t) val;
    dev->keyboard_queue_end = (dev->keyboard_queue_end + 1) & 0x1f;
}

static void
prodest_pc1_keyboard_send(uint16_t val)
{
    if ((prodest_pc1_active == NULL) ||
        !prodest_pc1_active->keyboard_interface_enabled)
        return;

    kbd_adddata_process(val, prodest_pc1_keyboard_add);
}

static uint8_t
prodest_pc1_port6x_read(uint16_t port, void *priv)
{
    prodest_pc1_t *dev = (prodest_pc1_t *) priv;
    uint8_t ret = 0xff;

    switch (port) {
        case 0x0060:
            picintc(1 << 1);
            if (dev->keyboard_queue_start != dev->keyboard_queue_end) {
                ret = dev->keyboard_queue[dev->keyboard_queue_start];
                dev->keyboard_queue_start = (dev->keyboard_queue_start + 1) & 0x1f;
            }
            break;

        case 0x0061:
            ret = dev->port61 | (ppispeakon ? 0x20 : 0x00);
            break;

        case 0x0062:
            /* The PC1 BIOS normally obtains the emulated PPI port C through
               its V40 NMI shim. No configurable motherboard switches are
               currently known, so expose the inactive state. */
            ret = 0xff;
            break;

        case 0x0064:
            /* 8042-compatible status: output buffer full, input buffer clear. */
            ret = (dev->keyboard_queue_start != dev->keyboard_queue_end) ? 0x01 : 0x00;
            break;

        default:
            break;
    }

    return ret;
}

static void
prodest_pc1_port6x_write(uint16_t port, uint8_t val, void *priv)
{
    prodest_pc1_t *dev = (prodest_pc1_t *) priv;

    switch (port) {
        case 0x0060:
            if (dev->keyboard_led_pending) {
                dev->keyboard_led_pending = 0;
                prodest_pc1_keyboard_add(0xfa);
            } else if (val == 0x01) {
                /* Cold-boot keyboard self-test used by BIOS 1.07. */
                prodest_pc1_keyboard_add(0xaa);
            } else if (val == 0xed) {
                dev->keyboard_led_pending = 1;
                prodest_pc1_keyboard_add(0xfa);
            } else if (val == 0xf5) {
                /* Warm-start firmware disables keyboard transmission before
                   reinitializing the controller. Discard pending key-release
                   bytes (notably Ctrl+Alt+Del) and acknowledge the command. */
                picintc(1 << 1);
                dev->keyboard_queue_start = dev->keyboard_queue_end;
                dev->keyboard_interface_enabled = 0;
                prodest_pc1_keyboard_add(0xfa);
            }
            break;

        case 0x0061:
            dev->port61 = val;
            speaker_update();
            speaker_gated  = val & 0x01;
            speaker_enable = val & 0x02;
            if (speaker_enable)
                was_speaker_enable = 1;
            pit_devs[0].set_gate(pit_devs[0].data, 2, val & 0x01);
            break;

        case 0x0064:
            if (val == 0xae)
                dev->keyboard_interface_enabled = 1;
            break;

        default:
            break;
    }
}

#ifdef ENABLE_OLIVETTI_PC1_TRACE
static uint32_t
prodest_pc1_trace_hash(void)
{
    uint32_t hash = 2166136261U;

    for (uint32_t i = 0; i < 0x4000; i++) {
        hash ^= mem_readb_phys(0xb8000 + i);
        hash *= 16777619U;
    }

    return hash;
}

static void
prodest_pc1_trace_poll(void *priv)
{
    prodest_pc1_t *dev  = (prodest_pc1_t *) priv;
    const uint32_t hash = prodest_pc1_trace_hash();

    always_log("OLIVETTI_PC1_TRACE poll=%u CS:IP=%04X:%04X hash=%08X port68=%02X "
               "V40=%02X,%02X,%02X,%02X,%02X,%02X,%02X,%02X/"
               "%02X,%02X,%02X,%02X,%02X,%02X,%02X,%02X\n",
               dev->trace_polls, CS, (uint16_t) cpu_state.pc, hash, dev->video_control,
               nec_v40_reg_read(0), nec_v40_reg_read(1), nec_v40_reg_read(2), nec_v40_reg_read(3),
               nec_v40_reg_read(4), nec_v40_reg_read(5), nec_v40_reg_read(6), nec_v40_reg_read(7),
               nec_v40_reg_read(8), nec_v40_reg_read(9), nec_v40_reg_read(10), nec_v40_reg_read(11),
               nec_v40_reg_read(12), nec_v40_reg_read(13), nec_v40_reg_read(14), nec_v40_reg_read(15));

    if (dev->trace_polls >= 15) {
        always_log("OLIVETTI_PC1_BOOT %02X %02X %02X %02X %02X %02X %02X %02X "
                   "%02X %02X %02X %02X %02X %02X %02X %02X\n",
                   mem_readb_phys(0x7c00), mem_readb_phys(0x7c01),
                   mem_readb_phys(0x7c02), mem_readb_phys(0x7c03),
                   mem_readb_phys(0x7c04), mem_readb_phys(0x7c05),
                   mem_readb_phys(0x7c06), mem_readb_phys(0x7c07),
                   mem_readb_phys(0x7c08), mem_readb_phys(0x7c09),
                   mem_readb_phys(0x7c0a), mem_readb_phys(0x7c0b),
                   mem_readb_phys(0x7c0c), mem_readb_phys(0x7c0d),
                   mem_readb_phys(0x7c0e), mem_readb_phys(0x7c0f));
    }

    if ((hash != dev->trace_vram_hash) || (dev->trace_polls == 0)) {
        char line[81];

        always_log("OLIVETTI_PC1_SCREEN BEGIN\n");
        for (uint32_t row = 0; row < 25; row++) {
            int visible = 0;

            for (uint32_t col = 0; col < 80; col++) {
                const uint8_t ch = mem_readb_phys(0xb8000 + ((row * 80 + col) << 1));
                line[col] = ((ch >= 0x20) && (ch < 0x7f)) ? (char) ch : ' ';
                visible |= (line[col] != ' ');
            }
            line[80] = 0;
            if (visible)
                always_log("OLIVETTI_PC1_SCREEN %02u |%s|\n", row, line);
        }
        always_log("OLIVETTI_PC1_SCREEN END\n");
        dev->trace_vram_hash = hash;
    }

    dev->trace_polls++;
    timer_advance_u64(&dev->trace_timer, 500000ULL * TIMER_USEC);
}
#endif

static uint8_t
prodest_pc1_bios_read(uint32_t addr, UNUSED(void *priv))
{
    return rom[addr & 0x3fff];
}

static uint16_t
prodest_pc1_bios_readw(uint32_t addr, void *priv)
{
    return prodest_pc1_bios_read(addr, priv) |
           ((uint16_t) prodest_pc1_bios_read(addr + 1, priv) << 8);
}

static uint32_t
prodest_pc1_bios_readl(uint32_t addr, void *priv)
{
    return prodest_pc1_bios_readw(addr, priv) |
           ((uint32_t) prodest_pc1_bios_readw(addr + 2, priv) << 16);
}

static uint8_t
prodest_pc1_io_read(uint16_t port, void *priv)
{
    prodest_pc1_t *dev = (prodest_pc1_t *) priv;

    return (port == 0x0068) ? dev->video_control : 0xff;
}

static void
prodest_pc1_io_write(uint16_t port, uint8_t val, void *priv)
{
    prodest_pc1_t *dev = (prodest_pc1_t *) priv;

    if (port == 0x0068) {
        /* Bit 0 enables the integrated CGA-compatible display. */
        dev->video_control = val;
        v6355_prodest_set_low_mirrors(!(val & 0x01));
    }
}

static void *
prodest_pc1_board_init(UNUSED(const device_t *info))
{
    static const uint32_t mirror_addr[3] = { 0x000f0000, 0x000f4000, 0x000f8000 };
    prodest_pc1_t *dev = (prodest_pc1_t *) calloc(1, sizeof(prodest_pc1_t));

    for (int i = 0; i < 3; i++) {
        mem_mapping_add(&dev->bios_mirror[i], mirror_addr[i], 0x4000,
                        prodest_pc1_bios_read, prodest_pc1_bios_readw, prodest_pc1_bios_readl,
                        NULL, NULL, NULL, rom,
                        MEM_MAPPING_EXTERNAL | MEM_MAPPING_ROM | MEM_MAPPING_ROMCS, dev);
        mem_set_mem_state_both(mirror_addr[i], 0x4000,
                               MEM_READ_ROMCS | MEM_WRITE_ROMCS);
    }

    io_sethandler(0x0068, 1,
                  prodest_pc1_io_read, NULL, NULL,
                  prodest_pc1_io_write, NULL, NULL, dev);
    io_sethandler(0x0060, 8,
                  prodest_pc1_port6x_read, NULL, NULL,
                  prodest_pc1_port6x_write, NULL, NULL, dev);
    io_sethandler(0x00c0, 16,
                  prodest_pc1_v40_dma_read, NULL, NULL,
                  prodest_pc1_v40_dma_write, NULL, NULL, dev);

    prodest_pc1_v40_dma_reset(dev);

    /* The integrated V40 TCU starts with all three timer inputs enabled.
       Unlike an IBM PC's 8253 channel 2, the PC1 firmware does not raise
       the emulated PPI gate before its resident timer diagnostic. */
    pit_devs[0].set_gate(pit_devs[0].data, 2, 1);

    prodest_pc1_active = dev;
    dev->keyboard_interface_enabled = 1;
    keyboard_set_table(scancode_set1);
    keyboard_send = prodest_pc1_keyboard_send;
    keyboard_scan = 1;
    timer_add(&dev->keyboard_timer, prodest_pc1_keyboard_poll, dev, 1);

#ifdef ENABLE_OLIVETTI_PC1_TRACE
    timer_add(&dev->trace_timer, prodest_pc1_trace_poll, dev, 1);
#endif

    return dev;
}

static void
prodest_pc1_board_close(void *priv)
{
    prodest_pc1_t *dev = (prodest_pc1_t *) priv;

    io_removehandler(0x0068, 1,
                     prodest_pc1_io_read, NULL, NULL,
                     prodest_pc1_io_write, NULL, NULL, dev);
    io_removehandler(0x0060, 8,
                     prodest_pc1_port6x_read, NULL, NULL,
                     prodest_pc1_port6x_write, NULL, NULL, dev);
    io_removehandler(0x00c0, 16,
                     prodest_pc1_v40_dma_read, NULL, NULL,
                     prodest_pc1_v40_dma_write, NULL, NULL, dev);
    if (prodest_pc1_active == dev)
        prodest_pc1_active = NULL;
    free(dev);
}

static const device_config_t prodest_pc1_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "v107",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "Prodest 1.06 (20/08/87)",
                .internal_name = "v106",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 16384,
                .files         = { "roms/machines/olivetti_prodest_pc1/pc1_bios_1.06.bin", "" }
            },
            {
                .name          = "Prodest 1.07 (10/12/88)",
                .internal_name = "v107",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 16384,
                .files         = { "roms/machines/olivetti_prodest_pc1/pc1_bios_1.07.bin", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

static const device_config_t prodest_pc1hd_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "v121",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "Prodest PC 1 HD 1.21 (07/01/88)",
                .internal_name = "v121",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 16384,
                .files         = { "roms/machines/olivetti_prodest_pc1/pc1_bios_1.21.bin", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t prodest_pc1_device = {
    .name          = "Olivetti Prodest PC 1",
    .internal_name = "olivetti_prodest_pc1",
    .flags         = 0,
    .local         = 0,
    .init          = prodest_pc1_board_init,
    .close         = prodest_pc1_board_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = prodest_pc1_config
};

const device_t prodest_pc1hd_device = {
    .name          = "Olivetti Prodest PC 1 HD",
    .internal_name = "olivetti_prodest_pc1hd",
    .flags         = 0,
    .local         = 1,
    .init          = prodest_pc1_board_init,
    .close         = prodest_pc1_board_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = prodest_pc1hd_config
};

static int
machine_xt_olivetti_prodest_common_init(const machine_t *model, int with_hdd)
{
    const char *fn;
    int         ret;

    device_context(model->device);
    fn  = device_get_bios_file(model->device, device_get_config_bios("bios"), 0);
    ret = bios_load_linear(fn, 0x000fc000, 16384, 0);
    device_context_restore();

    if (bios_only || !ret)
        return ret;

    machine_common_init(model);
    pit_devs[0].set_out_func(pit_devs[0].data, 1, pit_refresh_timer_xt);
    nmi_init();
    standalone_gameport_type = &gameport_200_device;

    device_add(&nec_v40_device);
    device_add(model->device);
    device_add_inst(&lpt_port_device, 1);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_xt_device);

    if (with_hdd && (hdc_current[0] == HDC_INTERNAL))
        device_add(&xta_prodest_pc1hd_device);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(&v6355d_prodest_pc1_device);

    return ret;
}

int
machine_xt_olivetti_prodest_pc1_init(const machine_t *model)
{
    return machine_xt_olivetti_prodest_common_init(model, 0);
}

int
machine_xt_olivetti_prodest_pc1hd_init(const machine_t *model)
{
    return machine_xt_olivetti_prodest_common_init(model, 1);
}
