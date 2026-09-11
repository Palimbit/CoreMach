/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 * Toshiba A-form-factor expansion endpoint.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef EMU_TOSHIBA_AFORM_H
#define EMU_TOSHIBA_AFORM_H

#include <stdint.h>

#include <86box/device.h>
#include <86box/mem.h>

typedef struct toshiba_aform_slot_t toshiba_aform_slot_t;

/* PJ11's documented, ISA-8-compatible subset. PJ12 is intentionally absent. */
#define TOSHIBA_AFORM_SIGNAL_IO          0x00000001u
#define TOSHIBA_AFORM_SIGNAL_MEMORY      0x00000002u
#define TOSHIBA_AFORM_SIGNAL_OPTION_ROM  0x00000004u
#define TOSHIBA_AFORM_SIGNAL_RESET       0x00000008u
#define TOSHIBA_AFORM_SIGNAL_SYSCLK      0x00000010u
#define TOSHIBA_AFORM_SIGNAL_IRQ5        0x00000020u
#define TOSHIBA_AFORM_SIGNAL_IRQ9        0x00000040u

#define TOSHIBA_AFORM_T5200_SIGNALS \
    (TOSHIBA_AFORM_SIGNAL_IO | TOSHIBA_AFORM_SIGNAL_MEMORY | \
     TOSHIBA_AFORM_SIGNAL_OPTION_ROM | TOSHIBA_AFORM_SIGNAL_RESET | \
     TOSHIBA_AFORM_SIGNAL_SYSCLK | TOSHIBA_AFORM_SIGNAL_IRQ5 | \
     TOSHIBA_AFORM_SIGNAL_IRQ9)

typedef struct {
    const char *machine;
    uint32_t    signals;
} toshiba_aform_slot_params_t;

/*
 * This device is a platform-owned endpoint, not a generic ISA bus. A machine
 * must opt in with its documented signal set before a future A-form card can
 * register resources through it.
 */
extern const device_t toshiba_aform_slot_device;

extern toshiba_aform_slot_t *toshiba_aform_slot_get(const char *machine);
extern int toshiba_aform_slot_is_for_machine(const toshiba_aform_slot_t *slot,
                                             const char *machine);
extern int toshiba_aform_slot_has_signals(const toshiba_aform_slot_t *slot,
                                          uint32_t required);
extern int toshiba_aform_slot_set_io_handler(toshiba_aform_slot_t *slot,
                                             uint16_t base, uint16_t size,
                                             uint8_t (*inb)(uint16_t, void *),
                                             void (*outb)(uint16_t, uint8_t, void *),
                                             void *priv);
extern int toshiba_aform_slot_add_mapping(toshiba_aform_slot_t *slot,
                                          mem_mapping_t *mapping,
                                          uint32_t base, uint32_t size,
                                          uint8_t (*read_b)(uint32_t, void *),
                                          void (*write_b)(uint32_t, uint8_t, void *),
                                          uint8_t *exec, uint32_t flags,
                                          void *priv);
extern int toshiba_aform_slot_set_irq(toshiba_aform_slot_t *slot, int irq,
                                      int asserted);

#endif /* EMU_TOSHIBA_AFORM_H */
