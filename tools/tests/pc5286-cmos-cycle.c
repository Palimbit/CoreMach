/* BluMach. Author: rtzor. Project: BluMach.
 * Process boundary fixture for the NVR -> Qt clear -> NVR reload cycle.
 * Use only a disposable directory managed by pc5286-cmos-cycle.py.
 */
#define main persistence_contracts_main
#include "pc5286-cmos-persistence.c"
#undef main

int main(int argc, char **argv)
{
    assert(argc == 3 && strlen(argv[1]) < 900);
    strcpy(usr_path, argv[1]); path_slash(usr_path);
    uint8_t locks[128] = {0};
    local_t local = {.def=0xff, .cent=0x32, .lock=locks};
    nvr_t chip = {.size=128, .fn="pc5286.nvr", .data=&local, .reset=nvr_reset};
    saved_nvr = &chip;
    assert(nvr_load() == 1);
    if (!strcmp(argv[2], "seed")) {
        assert(chip.is_new);
        for (unsigned i=0x0e; i<128; i++)
            nvr_reg_common_write(i, (uint8_t)(i ^ 0x5a), &chip, &local);
        assert(nvr_save() == 1);
    } else if (!strcmp(argv[2], "check")) {
        assert(!chip.is_new);
        nvr_at_reset(&chip);
        for (unsigned i=0x0e; i<128; i++) assert(chip.regs[i] == (uint8_t)(i ^ 0x5a));
    } else {
        assert(!strcmp(argv[2], "fresh") && chip.is_new);
        assert(chip.regs[0x10] == 0xff && chip.regs[0x7f] == 0xff);
    }
    puts(argv[2]);
    return 0;
}
