/*
 * 86Box    A hypervisor and IBM PC system emulator.
 *
 * Initial Olivetti PCS86 machine support.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/mem.h>
#include <86box/device.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/gameport.h>
#include <86box/hdc.h>
#include <86box/hdd.h>
#include <86box/io.h>
#include <86box/isartc.h>
#include <86box/keyboard.h>
#include <86box/lpt.h>
#include <86box/machine.h>
#include <86box/nmi.h>
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/ppi.h>
#include <86box/rom.h>
#include <86box/serial.h>
#include <86box/snd_speaker.h>
#include <86box/video.h>

typedef struct pcs86_t {
    uint8_t control;
    uint8_t port61;
    uint8_t jumpers;
    uint8_t ps2[5];
    uint8_t ps2_queue[2][16];
    uint8_t ps2_queue_start[2];
    uint8_t ps2_queue_end[2];
    uint8_t glue[16];
    uint8_t memory_blocks;
    uint8_t scan_queue[32];
    uint8_t scan_queue_start;
    uint8_t scan_queue_end;
    uint8_t *ems_ram;
    uint32_t ems_size;
    uint16_t ems_pages;
    uint8_t ems_reg[4];
    uint32_t ems_page_offset[4];
    mem_mapping_t ems_mapping[4];
    void *hdd;
    void *video;
    lpt_t *lpt;
    serial_t *uart;
} pcs86_t;

static pcs86_t *pcs86_active;

/*
 * PCS86 jumper bank, read at port 100h (fitted jumper = zero):
 *
 *   00 = 360 KiB, 10 = 1.2 MiB, 01 = 720 KiB, 11 = 1.44 MiB.
 *
 * BluMach normally follows the drive selected in the main configuration,
 * while the machine-specific settings can override these physical jumpers.
 */
static uint8_t
pcs86_floppy_jumper_bits(int drive)
{
    const char *type = fdd_get_internal_name(fdd_get_type(drive));

    if ((type == NULL) || (type[0] == '\0'))
        return 0x03;
    if (!strcmp(type, "525_2dd"))
        return 0x00;
    if (!strncmp(type, "525_", 4))
        return 0x02;
    if (!strcmp(type, "35_2dd"))
        return 0x01;

    return 0x03;
}

static int
pcs86_ems_window(uint32_t addr)
{
    return (int) ((addr - 0x00080000) >> 14);
}

static uint8_t
pcs86_ems_read(uint32_t addr, void *priv)
{
    const pcs86_t *dev = (const pcs86_t *) priv;
    const int window = pcs86_ems_window(addr);

    return dev->ems_ram[dev->ems_page_offset[window] + (addr & 0x3fff)];
}

static uint16_t
pcs86_ems_readw(uint32_t addr, void *priv)
{
    return pcs86_ems_read(addr, priv) |
           ((uint16_t) pcs86_ems_read(addr + 1, priv) << 8);
}

static uint32_t
pcs86_ems_readl(uint32_t addr, void *priv)
{
    return pcs86_ems_readw(addr, priv) |
           ((uint32_t) pcs86_ems_readw(addr + 2, priv) << 16);
}

static void
pcs86_ems_write(uint32_t addr, uint8_t val, void *priv)
{
    pcs86_t *dev = (pcs86_t *) priv;
    const int window = pcs86_ems_window(addr);

    dev->ems_ram[dev->ems_page_offset[window] + (addr & 0x3fff)] = val;
}

static void
pcs86_ems_writew(uint32_t addr, uint16_t val, void *priv)
{
    pcs86_ems_write(addr, val & 0xff, priv);
    pcs86_ems_write(addr + 1, val >> 8, priv);
}

static void
pcs86_ems_writel(uint32_t addr, uint32_t val, void *priv)
{
    pcs86_ems_writew(addr, val & 0xffff, priv);
    pcs86_ems_writew(addr + 2, val >> 16, priv);
}

static uint8_t
pcs86_ems_reg_read(uint16_t port, void *priv)
{
    const pcs86_t *dev = (const pcs86_t *) priv;

    return dev->ems_reg[port & 3];
}

static void
pcs86_ems_reg_write(uint16_t port, uint8_t val, void *priv)
{
    pcs86_t *dev = (pcs86_t *) priv;
    const int window = port & 3;
    const uint8_t page = val & 0x7f;

    dev->ems_reg[window] = val;
    if ((val & 0x80) && (page < dev->ems_pages)) {
        dev->ems_page_offset[window] = (uint32_t) page << 14;
        mem_mapping_enable(&dev->ems_mapping[window]);
    } else
        mem_mapping_disable(&dev->ems_mapping[window]);
}

static void
pcs86_scan_add(uint16_t val)
{
    if (pcs86_active == NULL)
        return;

    pcs86_active->scan_queue[pcs86_active->scan_queue_end] = (uint8_t) val;
    pcs86_active->scan_queue_end = (pcs86_active->scan_queue_end + 1) & 0x1f;
    picint(1 << 1);
}

static void
pcs86_keyboard_send(uint16_t val)
{
    kbd_adddata_process(val, pcs86_scan_add);
}

static uint8_t
pcs86_port60_read(uint16_t port, void *priv)
{
    pcs86_t *dev = (pcs86_t *) priv;
    uint8_t  ret = 0x00;

    (void) port;
    picintc(1 << 1);
    if (dev->scan_queue_start != dev->scan_queue_end) {
        ret = dev->scan_queue[dev->scan_queue_start];
        dev->scan_queue_start = (dev->scan_queue_start + 1) & 0x1f;
        if (dev->scan_queue_start != dev->scan_queue_end)
            picint(1 << 1);
    }
    return ret;
}

static uint8_t
pcs86_port61_read(uint16_t port, void *priv)
{
    const pcs86_t *dev = (const pcs86_t *) priv;

    (void) port;
    return dev->port61 | (ppispeakon ? 0x20 : 0x00);
}

static void
pcs86_port61_write(uint16_t port, uint8_t val, void *priv)
{
    pcs86_t *dev = (pcs86_t *) priv;

    (void) port;
    dev->port61 = val;
    speaker_update();
    speaker_gated  = val & 0x01;
    speaker_enable = val & 0x02;
    if (speaker_enable)
        was_speaker_enable = 1;
    pit_devs[0].set_gate(pit_devs[0].data, 2, val & 0x01);
}

static void
pcs86_ps2_queue(pcs86_t *dev, int channel, uint8_t val)
{
    dev->ps2_queue[channel][dev->ps2_queue_end[channel]] = val;
    dev->ps2_queue_end[channel] = (dev->ps2_queue_end[channel] + 1) & 0x0f;
}

static uint8_t
pcs86_ps2_read(uint16_t port, void *priv)
{
    pcs86_t      *dev = (pcs86_t *) priv;
    const uint8_t reg = (uint8_t) (port - 0x0066);
    uint8_t       ret = dev->ps2[reg];

    if ((port == 0x0067) || (port == 0x0068)) {
        const int channel = port - 0x0067;
        if (dev->ps2_queue_start[channel] != dev->ps2_queue_end[channel]) {
            ret = dev->ps2_queue[channel][dev->ps2_queue_start[channel]];
            dev->ps2_queue_start[channel] =
                (dev->ps2_queue_start[channel] + 1) & 0x0f;
        }
    } else if (port == 0x006a) {
        ret = 0x00;
        if (dev->ps2_queue_start[0] != dev->ps2_queue_end[0])
            ret |= 0x20;
        if (dev->ps2_queue_start[1] != dev->ps2_queue_end[1])
            ret |= 0x04;
    }

    return ret;
}

static void
pcs86_ps2_write(uint16_t port, uint8_t val, void *priv)
{
    pcs86_t      *dev = (pcs86_t *) priv;
    const uint8_t reg = (uint8_t) (port - 0x0066);

    /* Port 66h bit 2 reflects the physical keylock and is read-only. */
    dev->ps2[reg] = (port == 0x0066) ?
        ((val & ~0x04) | (dev->ps2[0] & 0x04)) : val;
    if ((port == 0x0067) || (port == 0x0068)) {
        const int channel = port - 0x0067;

        switch (val) {
            case 0xf2: /* Read device ID. */
                pcs86_ps2_queue(dev, channel, 0xfa);
                if (channel == 0) {
                    pcs86_ps2_queue(dev, channel, 0xab);
                    pcs86_ps2_queue(dev, channel, 0x83);
                } else
                    pcs86_ps2_queue(dev, channel, 0x00);
                break;

            case 0xff: /* Reset. */
                pcs86_ps2_queue(dev, channel, 0xfa);
                pcs86_ps2_queue(dev, channel, 0xaa);
                if (channel == 1)
                    pcs86_ps2_queue(dev, channel, 0x00);
                break;

            default:
                /* Standard PS/2 keyboard/mouse commands and parameters ACK. */
                pcs86_ps2_queue(dev, channel, 0xfa);
                break;
        }
    }
}

static uint8_t
pcs86_board_read(uint16_t port, void *priv)
{
    const pcs86_t *dev = (const pcs86_t *) priv;

    if (port == 0x0065)
        return dev->control;
    if (port == 0x0100)
        return dev->jumpers;

    return 0xff;
}

static void
pcs86_board_write(uint16_t port, uint8_t val, void *priv)
{
    pcs86_t *dev = (pcs86_t *) priv;

    if (port == 0x0065) {
        const uint8_t old_control = dev->control;

        dev->control = val;
        if ((dev->hdd != NULL) && ((old_control ^ val) & 0x01))
            xta_handler(dev->hdd, val & 0x01);
        if ((dev->lpt != NULL) && ((old_control ^ val) & 0x02)) {
            if (val & 0x02) {
                lpt_port_setup(dev->lpt, LPT1_ADDR);
                /* Register this after the generic LPT handler: the PCS86 SPP
                 * ignores control bit 5 instead of entering input mode. */
                io_sethandler(0x037a, 1, NULL, NULL, NULL,
                              pcs86_board_write, NULL, NULL, dev);
            } else {
                io_removehandler(0x037a, 1, NULL, NULL, NULL,
                                 pcs86_board_write, NULL, NULL, dev);
                lpt_port_remove(dev->lpt);
            }
        }
        if ((dev->video != NULL) && ((old_control ^ val) & 0x04))
            paradise_pcs86_set_enabled(dev->video, val & 0x04);
        /* The BIOS uses bit 4 (10h), despite the surviving web notes saying bit 5. */
        if ((dev->uart != NULL) && ((old_control ^ val) & 0x10)) {
            if (val & 0x10)
                serial_setup(dev->uart, COM1_ADDR, COM1_IRQ);
            else
                serial_remove(dev->uart);
        }
    } else if ((port == 0x0064) || (port == 0x006b) ||
               (port == 0x006c) || (port == 0x006f)) {
        /* Bit 0 at 6Bh is a write-one-to-clear memory/parity status bit. */
        dev->glue[port & 0x0f] = (port == 0x006b) ? (val & 0xfe) : val;
        if ((port == 0x006b) && (val & 0x01) && (dev->memory_blocks < 10))
            dev->memory_blocks++;
    } else if ((port == 0x037a) && (dev->lpt != NULL)) {
        /* The PCS86 SPP ignores the bidirectional-direction bit. */
        dev->lpt->ctrl &= ~0x20;
    }
}

static uint8_t
pcs86_port62_read(uint16_t port, void *priv)
{
    const pcs86_t *dev = (const pcs86_t *) priv;
    const uint8_t ret = (dev->memory_blocks >= 10) ? 0xc0 : 0x00;

    (void) port;
    return ret;
}

static uint8_t
pcs86_port63_read(uint16_t port, void *priv)
{
    (void) priv;
    (void) port;
    return 0x08;
}

static uint8_t
pcs86_glue_read(uint16_t port, void *priv)
{
    pcs86_t *dev = (pcs86_t *) priv;

    if (port == 0x0064) {
        uint8_t ret = dev->glue[4] & 0x8f;

        /* Bits 6:4 identify the fitted SIMM pair to the resident BIOS. */
        if (dev->ems_size == (384 << 10))
            ret |= 0x20; /* 2 x 256 KiB: 384 KiB remain available as EMS. */
        else if (dev->ems_size == (1920 << 10))
            ret |= 0x40; /* 2 x 1 MiB: 1920 KiB remain available as EMS. */
        return ret;
    }

    return dev->glue[port & 0x0f];
}

static const device_config_t olivetti_pcs86_config[] = {
    // clang-format off
    {
        .name           = "ems_size",
        .description    = "Onboard EMS expansion",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 1920,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "2 x 1 MB SIMMs (1920 KB EMS)",  .value = 1920 },
            { .description = "2 x 256 KB SIMMs (384 KB EMS)", .value =  384 },
            { .description = "Not installed",                 .value =    0 },
            { .description = ""                                               }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "keylock_locked",
        .description    = "Front-panel key lock",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "fdd0_jumpers",
        .description    = "Jumper bank bits 1-0: floppy drive 0",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = -1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Automatic (follow configured drive)", .value = -1 },
            { .description = "Both fitted: 360 KB",                 .value =  0 },
            { .description = "High fitted: 720 KB",                 .value =  1 },
            { .description = "Low fitted: 1.2 MB",                  .value =  2 },
            { .description = "Both open: 1.44 MB",                  .value =  3 },
            { .description = ""                                                   }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "fdd1_jumpers",
        .description    = "Jumper bank bits 3-2: floppy drive 1",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = -1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Automatic (follow configured drive)", .value = -1 },
            { .description = "Both fitted: 360 KB",                 .value =  0 },
            { .description = "High fitted: 720 KB",                 .value =  1 },
            { .description = "Low fitted: 1.2 MB",                  .value =  2 },
            { .description = "Both open: 1.44 MB",                  .value =  3 },
            { .description = ""                                                   }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "hdd_jumper",
        .description    = "Jumper bank bit 7: hard disk present",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = -1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Automatic (follow configured XTA disk)", .value = -1 },
            { .description = "Open: no hard disk",                     .value =  0 },
            { .description = "Fitted: hard disk present",              .value =  1 },
            { .description = ""                                                     }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t olivetti_pcs86_device = {
    .name          = "Olivetti PCS86",
    .internal_name = "olivetti_pcs86_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = olivetti_pcs86_config
};

int
machine_xt_olivetti_pcs86_init(const machine_t *model)
{
    pcs86_t *dev;
    int      ems_size;
    int      fdd0_jumpers;
    int      fdd1_jumpers;
    int      hdd_jumper;
    int      keylock_locked;
    int      ret;

    /* CSAB05 contains the even bytes and CSAB04 the odd bytes. */
    ret = bios_load_interleaved("roms/machines/olivetti_pcs86/CSAB05_02-17.BIN",
                                "roms/machines/olivetti_pcs86/CSAB04_02-25.BIN",
                                0x000f0000, 65536, 0);
    if (bios_only || !ret)
        return ret;

    device_context(model->device);
    ems_size     = machine_get_config_int("ems_size");
    fdd0_jumpers = machine_get_config_int("fdd0_jumpers");
    fdd1_jumpers = machine_get_config_int("fdd1_jumpers");
    hdd_jumper   = machine_get_config_int("hdd_jumper");
    keylock_locked = machine_get_config_int("keylock_locked");
    device_context_restore();

    dev = (pcs86_t *) calloc(1, sizeof(pcs86_t));
    pcs86_active = dev;
    dev->control = 0x80;
    if (fdd0_jumpers < 0)
        fdd0_jumpers = pcs86_floppy_jumper_bits(0);
    if (fdd1_jumpers < 0)
        fdd1_jumpers = pcs86_floppy_jumper_bits(1);
    dev->jumpers = 0xf0 | (fdd0_jumpers & 0x03) |
                   ((fdd1_jumpers & 0x03) << 2);
    if (hdd_jumper > 0)
        dev->jumpers &= ~0x80;
    /* Port 66h bit 2 is high while the front-panel keylock is open. */
    dev->ps2[0] = keylock_locked ? 0x00 : 0x04;
    dev->ems_size = (uint32_t) ems_size << 10;
    dev->ems_pages = (uint16_t) (dev->ems_size >> 14);
    if (dev->ems_size != 0)
        dev->ems_ram = (uint8_t *) calloc(1, dev->ems_size);

    machine_common_init(model);
    pit_devs[0].set_out_func(pit_devs[0].data, 1, pit_refresh_timer_xt);
    nmi_init();
    device_add(&olivetti_pcs86_rtc_device);
    if (hdc_current[0] == HDC_INTERNAL) {
        dev->hdd = device_add(&xta_pcs86_device);
        /* In automatic mode, bit 7 follows the presence of an XTA image. */
        if (hdd_jumper < 0) {
            for (uint8_t i = 0; i < HDD_NUM; i++) {
                if ((hdd[i].bus_type == HDD_BUS_XTA) && hdd[i].fn[0]) {
                    dev->jumpers &= ~0x80;
                    break;
                }
            }
        }
        /* The BIOS enables the onboard controller through port 65h bit 0. */
        xta_handler(dev->hdd, 0);
    }

    io_sethandler(0x0065, 1, pcs86_board_read, NULL, NULL,
                  pcs86_board_write, NULL, NULL, dev);
    io_sethandler(0x0060, 1, pcs86_port60_read, NULL, NULL,
                  NULL, NULL, NULL, dev);
    io_sethandler(0x0061, 1, pcs86_port61_read, NULL, NULL,
                  pcs86_port61_write, NULL, NULL, dev);
    io_sethandler(0x0100, 1, pcs86_board_read, NULL, NULL,
                  pcs86_board_write, NULL, NULL, dev);
    io_sethandler(0x0066, 5, pcs86_ps2_read, NULL, NULL,
                  pcs86_ps2_write, NULL, NULL, dev);
    io_sethandler(0x0064, 1, pcs86_glue_read, NULL, NULL,
                  pcs86_board_write, NULL, NULL, dev);
    io_sethandler(0x0062, 1, pcs86_port62_read, NULL, NULL,
                  NULL, NULL, NULL, dev);
    io_sethandler(0x0063, 1, pcs86_port63_read, NULL, NULL,
                  NULL, NULL, NULL, dev);
    io_sethandler(0x006b, 2, pcs86_glue_read, NULL, NULL,
                  pcs86_board_write, NULL, NULL, dev);
    io_sethandler(0x006f, 1, pcs86_glue_read, NULL, NULL,
                  pcs86_board_write, NULL, NULL, dev);
    if (dev->ems_ram != NULL) {
        for (uint8_t page = 0; page < 4; page++) {
            mem_mapping_add(&dev->ems_mapping[page],
                            0x00080000 + ((uint32_t) page << 14), 0x4000,
                            pcs86_ems_read, pcs86_ems_readw, pcs86_ems_readl,
                            pcs86_ems_write, pcs86_ems_writew, pcs86_ems_writel,
                            NULL, MEM_MAPPING_INTERNAL, dev);
            mem_mapping_disable(&dev->ems_mapping[page]);
        }
        io_sethandler(0x8400, 4, pcs86_ems_reg_read, NULL, NULL,
                      pcs86_ems_reg_write, NULL, NULL, dev);
    }
    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_actlow_device);

    dev->lpt = device_add_inst(&lpt_port_device, 1);
    lpt_port_remove(dev->lpt);

    /* The onboard 8250-compatible UART is COM1 and is gated by port 65h bit 4. */
    dev->uart = device_add_inst(&ns16450_device, 1);
    serial_remove(dev->uart);

    video_reset(gfxcard[0]);
    if (gfxcard[0] == VID_INTERNAL) {
        dev->video = device_add(&paradise_pvga1a_pcs86_device);
        paradise_pcs86_set_enabled(dev->video, 0);
    }

    keyboard_set_table(scancode_set1);
    keyboard_send = pcs86_keyboard_send;
    keyboard_scan = 1;

    standalone_gameport_type = &gameport_200_device;

    return ret;
}
