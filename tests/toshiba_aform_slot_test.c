/* Toshiba A-form-factor endpoint contract tests. GPL-2.0-or-later. */
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/pic.h>
#include <86box/toshiba_aform.h>

static void    *common_priv;
static uint16_t io_base, io_size;
static uint32_t mapping_base, mapping_size, mapping_flags;
static unsigned  irq_mask;
static int       irq_set;

void *device_get_common_priv(void) { return common_priv; }
void io_sethandler(uint16_t base, uint16_t size,
                   uint8_t (*inb)(uint16_t, void *),
                   uint16_t (*inw)(uint16_t, void *),
                   uint32_t (*inl)(uint16_t, void *),
                   void (*outb)(uint16_t, uint8_t, void *),
                   void (*outw)(uint16_t, uint16_t, void *),
                   void (*outl)(uint16_t, uint32_t, void *), void *priv)
{
    (void) inb; (void) inw; (void) inl; (void) outb; (void) outw; (void) outl;
    (void) priv;
    io_base = base;
    io_size = size;
}
void mem_mapping_add(mem_mapping_t *mapping, uint32_t base, uint32_t size,
                     uint8_t (*read_b)(uint32_t, void *),
                     uint16_t (*read_w)(uint32_t, void *),
                     uint32_t (*read_l)(uint32_t, void *),
                     void (*write_b)(uint32_t, uint8_t, void *),
                     void (*write_w)(uint32_t, uint16_t, void *),
                     void (*write_l)(uint32_t, uint32_t, void *), uint8_t *exec,
                     uint32_t flags, void *priv)
{
    (void) mapping; (void) read_b; (void) read_w; (void) read_l; (void) write_b;
    (void) write_w; (void) write_l; (void) exec; (void) priv;
    mapping_base = base;
    mapping_size = size;
    mapping_flags = flags;
}
void picint_common(uint16_t num, int level, int set, uint8_t *irq_state)
{
    (void) level; (void) irq_state;
    irq_mask = num;
    irq_set = set;
}

#include "../src/device/toshiba_aform.c"

static uint8_t test_in(uint16_t port, void *priv)
{
    (void) port; (void) priv;
    return 0xff;
}
static void test_out(uint16_t port, uint8_t value, void *priv)
{
    (void) port; (void) value; (void) priv;
}
static uint8_t test_read(uint32_t addr, void *priv)
{
    (void) addr; (void) priv;
    return 0xff;
}

int
main(void)
{
    toshiba_aform_slot_params_t params = {
        .machine = "t5200", .signals = TOSHIBA_AFORM_T5200_SIGNALS
    };
    mem_mapping_t mapping = { 0 };
    toshiba_aform_slot_t *slot;

    common_priv = &params;
    slot = toshiba_aform_slot_device.init(&toshiba_aform_slot_device);
    assert(slot);
    assert(toshiba_aform_slot_get("t5200") == slot);
    assert(toshiba_aform_slot_is_for_machine(slot, "t5200"));
    assert(!toshiba_aform_slot_is_for_machine(slot, "t3200"));
    assert(toshiba_aform_slot_has_signals(slot, TOSHIBA_AFORM_SIGNAL_IO |
                                                TOSHIBA_AFORM_SIGNAL_OPTION_ROM |
                                                TOSHIBA_AFORM_SIGNAL_IRQ5));

    assert(toshiba_aform_slot_set_io_handler(slot, 0x300, 0x10, test_in,
                                              test_out, NULL));
    assert(io_base == 0x300 && io_size == 0x10);
    assert(toshiba_aform_slot_add_mapping(slot, &mapping, 0xc8000, 0x2000,
                                          test_read, NULL, NULL,
                                          MEM_MAPPING_ROM, NULL));
    assert(mapping_base == 0xc8000 && mapping_size == 0x2000);
    assert(mapping_flags & MEM_MAPPING_EXTERNAL);
    assert(mapping_flags & MEM_MAPPING_IS_ROM);
    assert(toshiba_aform_slot_set_irq(slot, 5, 1));
    assert(irq_mask == (1u << 5) && irq_set);
    assert(toshiba_aform_slot_set_irq(slot, 5, 0));
    assert(irq_mask == (1u << 5) && !irq_set);
    assert(!toshiba_aform_slot_set_irq(slot, 7, 1));

    toshiba_aform_slot_device.close(slot);
    assert(toshiba_aform_slot_get("t5200") == NULL);
    params.signals = TOSHIBA_AFORM_SIGNAL_IO;
    common_priv = &params;
    toshiba_aform_slot_t *limited = toshiba_aform_slot_device.init(&toshiba_aform_slot_device);
    assert(limited);
    assert(!toshiba_aform_slot_add_mapping(limited, &mapping, 0xc8000, 0x2000,
                                           test_read, NULL, NULL,
                                           MEM_MAPPING_ROM, NULL));
    assert(!toshiba_aform_slot_set_irq(limited, 5, 1));

    toshiba_aform_slot_device.close(limited);
    params.machine = "t3200";
    common_priv = &params;
    assert(!toshiba_aform_slot_device.init(&toshiba_aform_slot_device));
    return 0;
}
