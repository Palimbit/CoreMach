/* BluMach T5100 host-side platform contract tests. GPL-2.0-or-later. */
#include <assert.h>
#include <stdarg.h>
#include <string.h>
#define T5100_CALLBACK_TEST
#include "../src/machine/m_at_t5100.c"

cpu_state_t cpu_state;
static uint8_t test_ram[0x100000 + 32];
uint8_t *ram = test_ram;
static int test_fn;
void pclog(const char *fmt, ...) { (void) fmt; }
int keyboard_recv_ui(uint16_t scan) { return scan == 0x11d && test_fn; }
void mem_mapping_set_exec(mem_mapping_t *map, uint8_t *exec) { map->exec = exec; }
void mem_mapping_enable(mem_mapping_t *map) { map->enable = 1; }
void mem_mapping_disable(mem_mapping_t *map) { map->enable = 0; }

int
main(void)
{
    t5100_t dev = { 0 };
    uint8_t compat_rom[0x4000] = { 0 };

    compat_rom[0] = 0x55;
    compat_rom[1] = 0xaa;
    compat_rom[2] = 0x20;
    assert(!t5100_prepare_compat_rom(NULL, sizeof(compat_rom)));
    assert(!t5100_prepare_compat_rom(compat_rom, 0x3fff));
    assert(t5100_prepare_compat_rom(compat_rom, sizeof(compat_rom)));
    assert(!memcmp(compat_rom + 0x000a, "AGS", 3));
    assert(compat_rom[0x3fd0] == 0xcb);
    static const uint16_t far_slots[] = {
        0x3fe0, 0x3fe4, 0x3fe8, 0x3fec, 0x3ff0, 0x3ff4
    };
    for (unsigned index = 0; index < sizeof(far_slots) / sizeof(far_slots[0]);
         index++) {
        uint16_t slot = far_slots[index];
        assert(compat_rom[slot] == 0xd0 && compat_rom[slot + 1] == 0x3f);
        assert(compat_rom[slot + 2] == 0x00 && compat_rom[slot + 3] == 0xc0);
    }
    uint8_t checksum = 0;
    for (size_t offset = 0; offset < sizeof(compat_rom); offset++)
        checksum += compat_rom[offset];
    assert(checksum == 0);

    assert(t5100_kbc2_in(0x8064, &dev) == 0x00);
    t5100_kbc2_out(0x8064, 0xbb, &dev);
    assert(t5100_kbc2_in(0x8064, &dev) == 0x01);
    assert(t5100_kbc2_in(0x8060, &dev) == 0x00);
    assert(t5100_kbc2_in(0x8064, &dev) == 0x00);
    assert(t5100_kbc2_in(0x8060, &dev) == 0xff);

    t5100_kbc2_out(0x8064, 0xb4, &dev);
    assert(t5100_kbc2_in(0x8060, &dev) == 0x8c);
    t5100_kbc2_out(0x8064, 0xaa, &dev);
    assert(t5100_kbc2_in(0x8064, &dev) == 0x00);

    atomic_store(&t5100_display_active, 0);
    test_fn = 0;
    assert(!t5100_display_hotkey(1, 0x14f));
    test_fn = 1;
    assert(t5100_display_hotkey(1, 0x14f));
    assert(t5100_display_hotkey(1, 0x14f));
    assert(t5100_kbc2_in(0x8066, &dev) == 0x01);
    assert(t5100_kbc2_in(0x8066, &dev) == 0x01);
    assert(!dev.external_display && atomic_load(&t5100_display_active) == 0);
    t5100_kbc2_out(0x8064, 0xbc, &dev);
    assert(dev.external_display && atomic_load(&t5100_display_active) == 1);
    assert(t5100_kbc2_in(0x8066, &dev) == 0x00);
    assert(t5100_display_hotkey(0, 0x14f));

    assert(t5100_display_hotkey(1, 0x147));
    assert(t5100_kbc2_in(0x8066, &dev) == 0x09);
    t5100_kbc2_out(0x8064, 0xbc, &dev);
    assert(!dev.external_display && atomic_load(&t5100_display_active) == 0);
    assert(t5100_display_hotkey(0, 0x147));

    assert(t5100_display_hotkey(1, 0x150));
    assert(t5100_kbc2_in(0x8066, &dev) == 0x02);
    t5100_kbc2_out(0x8064, 0xbc, &dev);
    assert(dev.extended_display);
    assert(t5100_display_hotkey(0, 0x150));
    assert(t5100_display_hotkey(1, 0x150));
    t5100_kbc2_out(0x8064, 0xbc, &dev);
    assert(!dev.extended_display);
    assert(t5100_display_hotkey(0, 0x150));
    assert(!t5100_display_hotkey(1, 0x14d)); /* Fn+Right remains unsupported. */

    t5100_system_out(0x8084, 0x13, &dev);
    t5100_system_out(0x808c, 0xa5, &dev);
    assert(t5100_system_in(0x8084, &dev) == 0x13);
    assert(t5100_system_in(0x808c, &dev) == 0xa5);

    t5100_post_out(0x0378, 0x82, &dev);
    assert(dev.post_seen && dev.post_code == 0x82);

    memset(test_ram, 0x6d, sizeof(test_ram));
    t5100_lim_out(0x0208, 0x80, &dev);
    assert(t5100_lim_in(0x0208, &dev) == 0x80);
    assert(dev.lim_mapping[0].enable && dev.lim_mapping[0].exec == ram + 0xa0000);
    t5100_lim_write(0xd0000, 0x42, &dev);
    assert(t5100_lim_read(0xd0000, &dev) == 0x42);
    assert(ram[0xa0000] == 0x42);

    t5100_lim_out(0x0208, 0x97, &dev);
    t5100_lim_write(0xd3fff, 0x24, &dev);
    assert(ram[0xfffff] == 0x24);
    t5100_lim_out(0x4208, 0x80, &dev);
    t5100_lim_write(0xd4000, 0x81, &dev);
    assert(ram[0xa0000] == 0x81);

    t5100_lim_out(0x0218, 0x80, &dev);
    assert(t5100_lim_in(0x0208, &dev) == 0x97);
    assert(t5100_lim_in(0x0218, &dev) == 0x80);
    assert(!dev.lim_mapping[0].enable && dev.lim_mapping[0].exec == NULL);
    assert(t5100_lim_read(0xd0000, &dev) == 0xff);

    t5100_lim_out(0x0208, 0x00, &dev);
    assert(!dev.lim_mapping[0].enable);
    return 0;
}
