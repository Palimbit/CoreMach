/* Toshiba A-form-factor expansion endpoint. SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/pic.h>
#include <86box/toshiba_aform.h>

struct toshiba_aform_slot_t {
    char     machine[16];
    uint32_t signals;
};

static toshiba_aform_slot_t *active_slot;

/* Add another case only when that exact Toshiba model has primary evidence. */
static int
toshiba_aform_machine_is_supported(const char *machine)
{
    return machine && !strcmp(machine, "t5200");
}

static void *
toshiba_aform_slot_init(const device_t *info)
{
    const toshiba_aform_slot_params_t *requested = device_get_common_priv();
    toshiba_aform_slot_t              *slot;

    (void) info;
    if (!requested || !requested->machine ||
        !toshiba_aform_machine_is_supported(requested->machine) || active_slot)
        return NULL;

    slot = calloc(1, sizeof(*slot));
    if (!slot)
        return NULL;

    strncpy(slot->machine, requested->machine, sizeof(slot->machine) - 1);
    slot->signals = requested->signals;
    active_slot = slot;
    return slot;
}

static void
toshiba_aform_slot_close(void *priv)
{
    if (active_slot == priv)
        active_slot = NULL;
    free(priv);
}

const device_t toshiba_aform_slot_device = {
    .name          = "Toshiba A-form-factor expansion endpoint",
    .internal_name = "toshiba_aform_slot",
    .flags         = 0,
    .local         = 0,
    .init          = toshiba_aform_slot_init,
    .close         = toshiba_aform_slot_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

int
toshiba_aform_slot_is_for_machine(const toshiba_aform_slot_t *slot,
                                  const char *machine)
{
    return slot && machine && !strcmp(slot->machine, machine);
}

toshiba_aform_slot_t *
toshiba_aform_slot_get(const char *machine)
{
    return toshiba_aform_slot_is_for_machine(active_slot, machine) ?
               active_slot :
               NULL;
}

int
toshiba_aform_slot_has_signals(const toshiba_aform_slot_t *slot,
                               uint32_t required)
{
    return slot && ((slot->signals & required) == required);
}

int
toshiba_aform_slot_set_io_handler(toshiba_aform_slot_t *slot,
                                  uint16_t base, uint16_t size,
                                  uint8_t (*inb)(uint16_t, void *),
                                  void (*outb)(uint16_t, uint8_t, void *),
                                  void *priv)
{
    if (!toshiba_aform_slot_has_signals(slot, TOSHIBA_AFORM_SIGNAL_IO) ||
        !size)
        return 0;

    io_sethandler(base, size, inb, NULL, NULL, outb, NULL, NULL, priv);
    return 1;
}

int
toshiba_aform_slot_add_mapping(toshiba_aform_slot_t *slot,
                               mem_mapping_t *mapping,
                               uint32_t base, uint32_t size,
                               uint8_t (*read_b)(uint32_t, void *),
                               void (*write_b)(uint32_t, uint8_t, void *),
                               uint8_t *exec, uint32_t flags, void *priv)
{
    uint32_t required = TOSHIBA_AFORM_SIGNAL_MEMORY;

    if (flags & MEM_MAPPING_IS_ROM)
        required |= TOSHIBA_AFORM_SIGNAL_OPTION_ROM;
    if (!mapping || !size || !toshiba_aform_slot_has_signals(slot, required))
        return 0;

    mem_mapping_add(mapping, base, size, read_b, NULL, NULL, write_b, NULL,
                    NULL, exec, flags | MEM_MAPPING_EXTERNAL, priv);
    return 1;
}

int
toshiba_aform_slot_set_irq(toshiba_aform_slot_t *slot, int irq, int asserted)
{
    uint32_t signal;

    switch (irq) {
        case 5: signal = TOSHIBA_AFORM_SIGNAL_IRQ5; break;
        case 9: signal = TOSHIBA_AFORM_SIGNAL_IRQ9; break;
        default: return 0;
    }
    if (!toshiba_aform_slot_has_signals(slot, signal))
        return 0;

    if (asserted)
        picint(1 << irq);
    else
        picintc(1 << irq);
    return 1;
}
