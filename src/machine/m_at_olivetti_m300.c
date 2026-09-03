/*
 * BluMach, a preservation-focused fork of 86Box.
 *
 *          Olivetti M300-02/PCS 11 (BA013/16), M300-02F/PCS 33
 *          (BA013/25), M300-08
 *          (BA319/BA324/BA325), M300-15 (BA320), M300-30 and M300-30P.
 *
 * Author: rtzor
 * Project: BluMach
 *
 * Copyright 2026 rtzor.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/device.h>
#include <86box/chipset.h>
#include <86box/io.h>
#include <86box/keyboard.h>
#include <86box/mem.h>
#include <86box/port_6x.h>
#include <86box/rom.h>
#include <86box/hdc.h>
#include <86box/sio.h>
#include <86box/video.h>
#include <86box/machine.h>

static void
m300_diag_write(uint16_t port, uint8_t val, void *priv)
{
    (void) port;
    (void) priv;

#ifdef ENABLE_OLIVETTI_M300_LOG
    pclog("Olivetti M300 diagnostic code: %02X\n", val);
#else
    (void) val;
#endif
}

static void
ba013_diag_write(uint16_t port, uint8_t val, void *priv)
{
    m300_diag_write(port, val, priv);

    if (val == 0x62)
        port_6x_topcat_refresh_enable();
    if (val == 0x63)
        port_6x_topcat_refresh_disable();
}

static uint8_t
m300_pit_wait_read(uint16_t port, void *priv)
{
    (void) port;
    (void) priv;

    cycles -= 5;
    return 0xff;
}

static void
m300_pit_wait_write(uint16_t port, uint8_t val, void *priv)
{
    (void) port;
    (void) val;
    (void) priv;

    cycles -= 5;
}

static uint8_t
m300_cmos_wait_read(uint16_t port, void *priv)
{
    (void) port;
    (void) priv;

    cycles -= 160;
    return 0xff;
}

static void
m300_cmos_wait_write(uint16_t port, uint8_t val, void *priv)
{
    (void) port;
    (void) val;
    (void) priv;

    cycles -= 160;
}

static int
machine_at_olivetti_m300_init(const machine_t *model, const char *bios_path)
{
    int ret;

    ret = bios_load_linear(bios_path, 0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_ide_init(model);

    device_add((model->init == machine_at_olivetti_m30008_init) ?
               &opti283_m30008_device : &opti283_m30015_device);
    device_add_params(machine_get_kbc_device(machine),
                      (void *) model->kbc_params);
    device_add(&pc87310_device);

    /* Resident Diagnostics publishes the current test on LPT1 data. */
    io_sethandler(0x0378, 1, NULL, NULL, NULL,
                  m300_diag_write, NULL, NULL, NULL);

    /* Buffered motherboard timings measured from Resident Diagnostics. */
    io_sethandler(0x0040, 4,
                  m300_pit_wait_read, NULL, NULL,
                  m300_pit_wait_write, NULL, NULL, NULL);
    io_sethandler(0x0070, 2,
                  m300_cmos_wait_read, NULL, NULL,
                  m300_cmos_wait_write, NULL, NULL, NULL);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(&oti067_m300_device);

    return ret;
}

int
machine_at_olivetti_m30008_init(const machine_t *model)
{
    return machine_at_olivetti_m300_init(model,
                                         "roms/machines/m30008/BIOS.ROM");
}

int
machine_at_olivetti_m30015_init(const machine_t *model)
{
    return machine_at_olivetti_m300_init(model,
                                         "roms/machines/m30015/BIOS.ROM");
}

static const device_config_t olivetti_m30030_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "diag104",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "BIOS 1.09 / Resident Diagnostics 1.03",
                .internal_name = "diag103",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/m30030/BIOS-1.09-DIAG-1.03.BIN", "" }
            },
            {
                .name          = "BIOS 1.09 / Resident Diagnostics 1.04",
                .internal_name = "diag104",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/m30030/BIOS-1.09-DIAG-1.04.BIN", "" }
            },
            {
                .name          = "BIOS 1.09 / Resident Diagnostics 2.00",
                .internal_name = "diag200",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/m30030/BIOS-1.09-DIAG-2.00.BIN", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t olivetti_m30030_device = {
    .name          = "Olivetti M300-30 / M300-30P",
    .internal_name = "olivetti_m30030",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = olivetti_m30030_config
};

int
machine_at_olivetti_m30030_init(const machine_t *model)
{
    int         ret;
    const char *fn;

    device_context(model->device);
    fn  = device_get_bios_file(machine_get_device(machine),
                               device_get_config_bios("bios"), 0);
    ret = bios_load_linear(fn, 0x000e0000, 131072, 0);
    device_context_restore();

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    /* VL82C486 memory and AT-bus controller used by the BA356 family. */
    device_add(&vl82c486_device);

    /* BA356 has dedicated backing for video/system shadow.  Do not expose the
       split DRAM hole again above installed RAM: Diagnostics otherwise counts
       a false extra 256 KB and overwrites the shadow during its memory test. */
    mem_remap_top_nomid(0);

    /* The firmware times the motherboard refresh signal at port 61h bit 4. */
    device_add(&port_6x_vlsi_refresh_device);

    device_add_params(machine_get_kbc_device(machine),
                      (void *) model->kbc_params);

    /* Buffered ISA IDE and PC87311/87312-compatible Super I/O. */
    device_add(&ide_isa_device);
    device_add_params(&pc873xx_device,
                      (void *) (PCX73XX_IDE_PRI | PCX730X_398));

    io_sethandler(0x0378, 1, NULL, NULL, NULL,
                  m300_diag_write, NULL, NULL, NULL);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(&paradise_wd90c31_m30030_device);

    return ret;
}

static const device_config_t olivetti_m30002_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "m30002_r102",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "Resident Diagnostics 1.02 (04/28/92)",
                .internal_name = "m30002_r102",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/m30002/BIOS-R1.02.ROM", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

static const device_config_t olivetti_m30002f_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "m30002f_r501",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "Resident Diagnostics 5.01 (08/24/92)",
                .internal_name = "m30002f_r501",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/m30002f/BIOS.ROM", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

static const device_config_t olivetti_pcs11_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "pcs11_r207",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "Resident Diagnostics 2.04 (06/03/92)",
                .internal_name = "pcs11_r204",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/pcs11/BIOS-R2.04.ROM", "" }
            },
            {
                .name          = "Resident Diagnostics 2.07 (03/01/93)",
                .internal_name = "pcs11_r207",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/pcs11/BIOS-R2.07.ROM", "" }
            },
            { .files_no = 0 }
        }
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
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

static const device_config_t olivetti_pcs33_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "pcs33_r603",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "Resident Diagnostics 6.03 (03/01/93)",
                .internal_name = "pcs33_r603",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/pcs33/BIOS-R6.03.ROM", "" }
            },
            { .files_no = 0 }
        }
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
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t olivetti_m30002_device = {
    .name          = "Olivetti M300-02 (BA013/16)",
    .internal_name = "olivetti_m30002",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = olivetti_m30002_config
};

const device_t olivetti_m30002f_device = {
    .name          = "Olivetti M300-02F (BA013/25)",
    .internal_name = "olivetti_m30002f",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = olivetti_m30002f_config
};

const device_t olivetti_pcs11_device = {
    .name          = "Olivetti PCS 11 (BA013/16)",
    .internal_name = "olivetti_pcs11",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = olivetti_pcs11_config
};

const device_t olivetti_pcs33_device = {
    .name          = "Olivetti PCS 33 (BA013/25)",
    .internal_name = "olivetti_pcs33",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = olivetti_pcs33_config
};

static int
machine_at_olivetti_ba013_init(const machine_t *model)
{
    int         ret;
    const char *fn;

    /*
     * The lower 64 KiB of the 27C010 contain OVC; the upper 64 KiB contain
     * Resident Diagnostics and the system BIOS.  OVC is called directly at
     * E000:3000 and is therefore not a separate C000 option ROM.
     */
    device_context(model->device);
    fn  = device_get_bios_file(machine_get_device(machine),
                               device_get_config_bios("bios"), 0);
    ret = bios_load_linear(fn, 0x000e0000, 131072, 0);
    device_context_restore();

    if (bios_only || !ret)
        return ret;

    machine_at_common_ide_init(model);

    device_add(&vlsi_topcat_device);
    device_add(&port_6x_topcat_device);
    device_add_params(machine_get_kbc_device(machine),
                      (void *) model->kbc_params);
    device_add(&pc87310_device);

    io_sethandler(0x0378, 1, NULL, NULL, NULL,
                  ba013_diag_write, NULL, NULL, NULL);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(&paradise_wd90c11_ba013_device);

    return ret;
}

int
machine_at_olivetti_m290sp_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/m290sp/BIOS-1.08.ROM",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_ide_init(model);
    device_add(&vlsi_topcat_device);
    device_add(&port_6x_topcat_device);
    device_add_params(machine_get_kbc_device(machine),
                      (void *) model->kbc_params);
    device_add(&pc87310_device);
    io_sethandler(0x0378, 1, NULL, NULL, NULL,
                  ba013_diag_write, NULL, NULL, NULL);

    /* BA08-specific WD90C11 power-on state and embedded font. */
    if (gfxcard[0] == VID_INTERNAL)
        device_add(&paradise_wd90c11_m290sp_device);

    return ret;
}

int
machine_at_olivetti_m30002_init(const machine_t *model)
{
    return machine_at_olivetti_ba013_init(model);
}

int
machine_at_olivetti_m30002f_init(const machine_t *model)
{
    return machine_at_olivetti_ba013_init(model);
}

int
machine_at_olivetti_pcs11_init(const machine_t *model)
{
    return machine_at_olivetti_ba013_init(model);
}

int
machine_at_olivetti_pcs33_init(const machine_t *model)
{
    return machine_at_olivetti_ba013_init(model);
}
