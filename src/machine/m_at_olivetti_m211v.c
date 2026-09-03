/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          Experimental implementation of the Olivetti M211V (XP1023V).
 *
 * BluMach preservation port: rtzor, Project BluMach, 2026.
 *
 * Physical-board evidence (preserved photographs):
 *   - Intel 80C286 at 16 MHz
 *   - C&T F82C241, the 286 controller of the CS8223 LeAPset
 *   - Toshiba TC8565AF floppy controller
 *   - Olivetti PKCA 1.03 keyboard-controller firmware
 *   - Cirrus Logic GD-610/620 "Stingray" mobile VGA firmware
 *   - one 27C010 containing 48 KB VGA ROM, a 16 KB empty window and
 *     64 KB system BIOS (PNVA 1.03 / PVGA 1.03)
 *
 * The LeAPset is documented by C&T as backwards-compatible with NEAT.
 * Until its laptop power-control registers are implemented, this machine
 * uses 86Box's NEAT memory/register model.  Likewise, GD-610/620 is not
 * currently emulated, so the closely related GD5401 VGA core is used with
 * the original 48 KB Stingray option ROM.  These substitutions are kept
 * explicit in the machine name and validation documentation.
 */

#include <stdint.h>
#include <stdio.h>
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/chipset.h>
#include <86box/mem.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/machine.h>
#include <86box/rom.h>
#include <86box/video.h>

#define M211V_SYSTEM_BIOS "roms/machines/olivetti_m211v/pnva_103_system_64k.bin"
#define M211V_VIDEO_BIOS  "roms/machines/olivetti_m211v/pvga_103_stingray_48k.bin"

int
machine_at_olivetti_m211v_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(M211V_SYSTEM_BIOS, 0x000f0000, 65536, 0);
    if (ret)
        ret &= rom_present(M211V_VIDEO_BIOS);

    if (bios_only || !ret)
        return ret;

    machine_at_common_ide_init(model);

    /* The CS8223 LeAPset exposes a NEAT-compatible programming model.
       Power saving, LCD switching and the 82C636 PCU remain pending. */
    device_add(&neat_device);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(&gd5401_onboard_m211v_device);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    return ret;
}
