/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Experimental implementation of the Toshiba T5200.
 *
 * Author:  rtzor.
 *
 *          Copyright 2026 rtzor.
 *          SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdint.h>
#include <stddef.h>
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
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/fdd.h>
#include <86box/hdc.h>
#include <86box/keyboard.h>
#include <86box/lpt.h>
#include <86box/machine.h>
#include <86box/mem.h>
#include <86box/nvr.h>
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/rom.h>
#include <86box/serial.h>
#include <86box/video.h>

extern uint8_t *ram;

static void *t5200_video;

/*
 * The maintenance manual documents Ctrl+Home as the recovery action when the
 * plasma is blank and the CRT indicator is lit.  No preserved T5200 source yet
 * identifies the reverse key or the 8749/8042 notification protocol, so keep
 * this interception deliberately one-way and leave CMOS untouched.  Both Ctrl
 * keys are accepted because the original 91-key keyboard labels the modifier
 * simply Ctrl; Home is accepted in its ordinary and E0-normalized forms.
 */
int
t5200_display_hotkey(int down, uint16_t scan)
{
    static int swallowed;
    const int  home = (scan == 0x47 || scan == 0x147);

    if (strcmp(machine_get_internal_name(), "t5200") || t5200_video == NULL) {
        swallowed = 0;
        return 0;
    }
    if (!home)
        return 0;
    if (!down && swallowed) {
        swallowed = 0;
        return 1;
    }
    if (down && (keyboard_recv_ui(0x01d) || keyboard_recv_ui(0x11d))) {
        if (!swallowed) {
            swallowed = 1;
            paradise_t5200_panel_set(t5200_video, 1);
        }
        return 1;
    }
    return 0;
}

#ifndef T5200_HOTKEY_TEST

/*
 * The documented T4758A integrates the AT DMA, PIC and PIT functions; T9761
 * supplies the FDC, UART and I/O decode. Their register-level integration and
 * the GA-MCNT3/BLAT/CLAT/BCNT2 timing are not published. Until a hardware
 * trace exists, use the standard AT decomposition rather than an unrelated
 * third-party chipset. The system and VGA firmware remain local test inputs.
 */
static const device_config_t t5200_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "System BIOS",
        .type           = CONFIG_BIOS,
        .default_string = "award_v130",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "Award V1.30 (1988)",
                .internal_name = "award_v130",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/t5200/t5200-award-v130.bin", "" }
            },
            { .files_no = 0 }
        }
    },
    {
        .name           = "display_output",
        .description    = "Presented physical outputs",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "External VGA and internal 11.5-inch gas plasma", .value = 0 },
            { .description = "External color VGA only",                       .value = 1 },
            { .description = "" }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t t5200_device = {
    .name          = "Toshiba T5200",
    .internal_name = "t5200",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = t5200_config
};

/*
 * Award V1.30 exercises this register before conventional RAM testing. Its
 * electrical owner is not yet known (82385/cache logic or a Toshiba GA), but
 * the firmware-visible transition is observed: 04h makes bit 2 visible; the
 * following 00h clears bit 2 and makes bit 0 visible; a second 00h clears bit
 * 0. This is a deliberately narrow behavioural approximation, not a cache or
 * gate-array implementation.
 */
typedef struct t5200_cache_control_t {
    uint8_t status;
} t5200_cache_control_t;

static uint8_t
t5200_cache_control_read(uint16_t port, void *priv)
{
    t5200_cache_control_t *dev = (t5200_cache_control_t *) priv;

    (void) port;
    return dev->status;
}

static void
t5200_cache_control_write(uint16_t port, uint8_t val, void *priv)
{
    t5200_cache_control_t *dev = (t5200_cache_control_t *) priv;

    (void) port;

    switch (val) {
        case 0x04:
            dev->status = 0x04;
            break;
        case 0x00:
            dev->status = (dev->status & 0x04) ? 0x01 : 0x00;
            break;
        default:
            dev->status = 0x00;
            break;
    }
}

static void
t5200_cache_control_reset(void *priv)
{
    t5200_cache_control_t *dev = (t5200_cache_control_t *) priv;

    dev->status = 0x00;
}

static void *
t5200_cache_control_init(const device_t *info)
{
    t5200_cache_control_t *dev = (t5200_cache_control_t *) calloc(1, sizeof(*dev));

    (void) info;
    io_sethandler(0x80a4, 1, t5200_cache_control_read, NULL, NULL,
                  t5200_cache_control_write, NULL, NULL, dev);
    return dev;
}

static const device_t t5200_cache_control_device = {
    .name          = "Toshiba T5200 cache-control approximation",
    .internal_name = "t5200_cache_control",
    .flags         = 0,
    .local         = 0,
    .init          = t5200_cache_control_init,
    .close         = free,
    .reset         = t5200_cache_control_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

/*
 * The firmware uses 8080h-80A8h as a small platform-control register file.
 * Apart from 80A4h above, the electrical owner and the meaning of individual
 * bits are not documented.  Keeping the byte latches separate from memory
 * mapping is intentional: it lets firmware read back its configuration
 * writes, without claiming to model the Toshiba memory controller yet.
 */
typedef struct t5200_platform_control_t {
    uint8_t latch[11];
} t5200_platform_control_t;

static uint8_t
t5200_platform_control_read(uint16_t port, void *priv)
{
    t5200_platform_control_t *dev = (t5200_platform_control_t *) priv;

    return dev->latch[(port - 0x8080) >> 2];
}

static void
t5200_platform_control_write(uint16_t port, uint8_t val, void *priv)
{
    t5200_platform_control_t *dev = (t5200_platform_control_t *) priv;

    dev->latch[(port - 0x8080) >> 2] = val;
}

static void
t5200_platform_control_reset(void *priv)
{
    t5200_platform_control_t *dev = (t5200_platform_control_t *) priv;

    memset(dev->latch, 0, sizeof(dev->latch));
}

static void *
t5200_platform_control_init(const device_t *info)
{
    static const uint16_t ports[] = {
        0x8080, 0x8084, 0x8088, 0x808c, 0x8090,
        0x8094, 0x8098, 0x809c, 0x80a8
    };
    t5200_platform_control_t *dev = (t5200_platform_control_t *) calloc(1, sizeof(*dev));

    (void) info;

    for (size_t i = 0; i < (sizeof(ports) / sizeof(ports[0])); i++)
        io_sethandler(ports[i], 1, t5200_platform_control_read, NULL, NULL,
                      t5200_platform_control_write, NULL, NULL, dev);

    return dev;
}

static const device_t t5200_platform_control_device = {
    .name          = "Toshiba T5200 platform-control register approximation",
    .internal_name = "t5200_platform_control",
    .flags         = 0,
    .local         = 0,
    .init          = t5200_platform_control_init,
    .close         = free,
    .reset         = t5200_platform_control_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

/*
 * Award V1.30 probes these sixteen selectors independently and then uses the
 * four 16 KiB slots at D0000h.  Their encoding matches the documented Toshiba
 * EMS selector family also used by the T3100e: bit 7 enables a page, low seven
 * bits select it, and the selector bank supplies the remaining page bits.
 *
 * This is deliberately a firmware-visible EMS model, not an assertion about
 * which T5200 gate array owns the decode or its electrical timing.  Page
 * backing is limited to installed extended RAM, so selectors outside that RAM
 * remain unmapped instead of aliasing arbitrary memory.
 */
static const uint16_t t5200_ems_ports[] = {
    0x0208, 0x4208, 0x8208, 0xc208,
    0x0218, 0x4218, 0x8218, 0xc218,
    0x0258, 0x4258, 0x8258, 0xc258,
    0x0268, 0x4268, 0x8268, 0xc268
};

typedef struct t5200_ems_t {
    uint8_t       page[16];
    uint32_t      page_exec[4];
    mem_mapping_t mapping[4];
} t5200_ems_t;

static int
t5200_ems_port_index(uint16_t port)
{
    for (size_t i = 0; i < (sizeof(t5200_ems_ports) / sizeof(t5200_ems_ports[0])); i++) {
        if (t5200_ems_ports[i] == port)
            return (int) i;
    }

    return -1;
}

static uint32_t
t5200_ems_slot_address(int slot)
{
    return 0xd0000 + ((uint32_t) (slot & 3) * 0x4000);
}

static uint32_t
t5200_ems_page_address(int selector, uint8_t value)
{
    uint32_t page;

    if (!(value & 0x80))
        return 0;

    page = (uint32_t) (value & 0x7f) + (0x80 * (selector >> 2));

    /* The generic AT allocation keeps extended RAM above 1 MiB. */
    if ((page * 0x4000) + 0x100000 >= ((uint32_t) mem_size * 1024))
        return 0;

    return ((uint32_t) mem_size * 1024) - (0x4000 * (page + 1));
}

static uint8_t
t5200_ems_read(uint32_t addr, void *priv)
{
    const t5200_ems_t *dev  = (const t5200_ems_t *) priv;
    const int          slot = (int) ((addr - 0xd0000) >> 14);

    return ram[dev->page_exec[slot] + (addr & 0x3fff)];
}

static uint16_t
t5200_ems_readw(uint32_t addr, void *priv)
{
    const t5200_ems_t *dev  = (const t5200_ems_t *) priv;
    const int          slot = (int) ((addr - 0xd0000) >> 14);
    uint16_t           value;

    memcpy(&value, &ram[dev->page_exec[slot] + (addr & 0x3fff)], sizeof(value));
    return value;
}

static uint32_t
t5200_ems_readl(uint32_t addr, void *priv)
{
    const t5200_ems_t *dev  = (const t5200_ems_t *) priv;
    const int          slot = (int) ((addr - 0xd0000) >> 14);
    uint32_t           value;

    memcpy(&value, &ram[dev->page_exec[slot] + (addr & 0x3fff)], sizeof(value));
    return value;
}

static void
t5200_ems_write(uint32_t addr, uint8_t value, void *priv)
{
    const t5200_ems_t *dev  = (const t5200_ems_t *) priv;
    const int          slot = (int) ((addr - 0xd0000) >> 14);

    ram[dev->page_exec[slot] + (addr & 0x3fff)] = value;
}

static void
t5200_ems_writew(uint32_t addr, uint16_t value, void *priv)
{
    const t5200_ems_t *dev  = (const t5200_ems_t *) priv;
    const int          slot = (int) ((addr - 0xd0000) >> 14);

    memcpy(&ram[dev->page_exec[slot] + (addr & 0x3fff)], &value, sizeof(value));
}

static void
t5200_ems_writel(uint32_t addr, uint32_t value, void *priv)
{
    const t5200_ems_t *dev  = (const t5200_ems_t *) priv;
    const int          slot = (int) ((addr - 0xd0000) >> 14);

    memcpy(&ram[dev->page_exec[slot] + (addr & 0x3fff)], &value, sizeof(value));
}

static uint8_t
t5200_ems_port_read(uint16_t port, void *priv)
{
    const t5200_ems_t *dev   = (const t5200_ems_t *) priv;
    const int          select = t5200_ems_port_index(port);
    const uint8_t      value  = (select >= 0) ? dev->page[select] : 0xff;

#ifdef T5200_POST_TRACE
    always_log("T5200 EMS R %04X = %02X\n", port, value);
#endif

    return value;
}

static void
t5200_ems_port_write(uint16_t port, uint8_t value, void *priv)
{
    t5200_ems_t *dev    = (t5200_ems_t *) priv;
    const int     select = t5200_ems_port_index(port);
    const int     slot   = select & 3;

    if (select < 0)
        return;

    dev->page[select] = value;
    dev->page_exec[slot] = t5200_ems_page_address(select, value);

#ifdef T5200_POST_TRACE
    always_log("T5200 EMS W %04X = %02X -> slot %u, physical %06X\n",
               port, value, slot, dev->page_exec[slot]);
#endif

    if (dev->page_exec[slot]) {
        mem_mapping_set_exec(&dev->mapping[slot], ram + dev->page_exec[slot]);
        mem_mapping_enable(&dev->mapping[slot]);
    } else {
        mem_mapping_disable(&dev->mapping[slot]);
    }
}

static void
t5200_ems_reset(void *priv)
{
    t5200_ems_t *dev = (t5200_ems_t *) priv;

    memset(dev->page, 0, sizeof(dev->page));
    memset(dev->page_exec, 0, sizeof(dev->page_exec));
    for (size_t i = 0; i < 4; i++)
        mem_mapping_disable(&dev->mapping[i]);
}

static void *
t5200_ems_init(const device_t *info)
{
    t5200_ems_t *dev = (t5200_ems_t *) calloc(1, sizeof(*dev));

    (void) info;

    for (size_t i = 0; i < (sizeof(t5200_ems_ports) / sizeof(t5200_ems_ports[0])); i++) {
        io_sethandler(t5200_ems_ports[i], 1, t5200_ems_port_read, NULL, NULL,
                      t5200_ems_port_write, NULL, NULL, dev);
    }

    for (size_t i = 0; i < 4; i++) {
        mem_mapping_add(&dev->mapping[i], t5200_ems_slot_address((int) i), 0x4000,
                        t5200_ems_read, t5200_ems_readw, t5200_ems_readl,
                        t5200_ems_write, t5200_ems_writew, t5200_ems_writel,
                        NULL, MEM_MAPPING_EXTERNAL, dev);
        mem_mapping_disable(&dev->mapping[i]);
    }

    return dev;
}

static const device_t t5200_ems_device = {
    .name          = "Toshiba T5200 EMS selector approximation",
    .internal_name = "t5200_ems",
    .flags         = 0,
    .local         = 0,
    .init          = t5200_ems_init,
    .close         = free,
    .reset         = t5200_ems_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

uint8_t
machine_t5200_kbc_status(void)
{
    /*
     * T5200 maintenance manual, system-test hardware status (page 3-7):
     *   80h reserved, observed set; 40h CPU speed (clear = 20 MHz);
     *   20h media (set = 2DD, clear = 2HD); 10h drive (clear = 2 MB);
     *   08h reserved, observed set; 04h normal A/B routing;
     *   02h external FDD (clear = off); 01h reserved, observed clear.
     *
     * Empty media exposes the same no-density-hole state as 2DD.  This
     * produces the documented ACh example for empty/2DD media and the
     * directly inferred 8Ch state for the preserved 2HD disk.  It is a
     * sampled board-status model, not an image-specific boot override.
     */
    uint8_t status = 0x8c;

    if (drive_empty[0] || (fdd_hole(0) == 0))
        status |= 0x20;

    return status;
}

#ifdef T5200_POST_TRACE
/*
 * Test-only opt-in I/O observer. I/O traps return all ones on reads, so combining
 * them with the installed devices leaves the read value unchanged; writes are
 * delivered to every registered handler. It is therefore suitable for finding
 * the last BIOS checkpoint without emulating an undocumented Toshiba port.
 */
static void
t5200_post_trace(uint16_t size, uint16_t port, uint8_t write, uint8_t val, void *priv)
{
    (void) priv;

    always_log("T5200 POST %04X:%08X %c%u %04X %02X\n",
               CS, cpu_state.pc, write ? 'W' : 'R', size, port, val);
}

static void *t5200_post_trace_handles[4];

static void
t5200_post_trace_init(void)
{
    static const uint16_t ports[] = { 0x0070, 0x0071, 0x0378, 0x03bc };

    for (size_t i = 0; i < (sizeof(ports) / sizeof(ports[0])); i++) {
        t5200_post_trace_handles[i] = io_trap_add(t5200_post_trace, NULL);
        io_trap_remap(t5200_post_trace_handles[i], 1, ports[i], 1);
    }
}
#endif

int
machine_at_t5200_init(const machine_t *model)
{
    const char *bios;
    int         ret;

    const device_t *device = machine_get_device(machine);

    if (!device_available(device))
        return 0;

    device_context(device);
    bios = device_get_bios_file(device, device_get_config_bios("bios"), 0);
    ret  = bios_load_linear(bios, 0x000e0000, 131072, 0);
    device_context_restore();

    if (bios_only || !ret)
        return ret;

    machine_at_common_ide_init(model);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add(&t5200_cache_control_device);
    device_add(&t5200_platform_control_device);
    device_add(&t5200_ems_device);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    /* T9761 documents NS16450-compatible serial I/O and parallel I/O. */
    device_add_inst(&ns16450_device, 1);
    device_add_inst(&ns16450_device, 2);
    device_add_inst(&lpt_port_device, 1);

    /* The dedicated T5200 device loads the preserved external VGA option ROM. */
    t5200_video = NULL;
    if (gfxcard[0] == VID_INTERNAL)
        t5200_video = device_add(&paradise_pvga1a_t5200_device);

#ifdef T5200_POST_TRACE
    t5200_post_trace_init();
#endif

    return ret;
}
#endif
