/* BluMach T5100 host-side platform contract tests. GPL-2.0-or-later. */
#include <assert.h>
#include <stdarg.h>
#include <string.h>
#define T5100_CALLBACK_TEST
#include "../src/machine/m_at_t5100.c"

cpu_state_t cpu_state;
static uint8_t test_ram[0x100000 + 32];
uint8_t *ram = test_ram;
void pclog(const char *fmt, ...) { (void) fmt; }
void mem_mapping_set_exec(mem_mapping_t *map, uint8_t *exec) { map->exec = exec; }
void mem_mapping_enable(mem_mapping_t *map) { map->enable = 1; }
void mem_mapping_disable(mem_mapping_t *map) { map->enable = 0; }

int
main(void)
{
    t5100_t dev = { 0 };

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
