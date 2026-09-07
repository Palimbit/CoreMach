/* BluMach modifications: rtzor, Project BluMach, 2026.
 * AT CMOS persistence contract, using the PC5286's 128-byte size.
 * Compile with -O2 -fwhole-program. argv[1] must be a disposable test directory.
 * No firmware execution: this does not certify BIOS Setup or RTC timing.
 */
#include <assert.h>
#include <direct.h>
#include <sys/stat.h>
#include "../../src/nvr_at.c"
#include "../../src/nvr.c"

char usr_path[1024];
FILE *plat_fopen(const char *path, const char *mode) { return fopen(path, mode); }
int plat_dir_check(char *path)
{
    struct stat s;
    return !stat(path, &s) && (s.st_mode & S_IFDIR);
}
int plat_dir_create(char *path) { return _mkdir(path); }
void path_slash(char *path)
{
    size_t n = strlen(path);
    if (n && path[n - 1] != '/' && path[n - 1] != '\\')
        strcat(path, "/");
}

int main(int argc, char **argv)
{
    assert(argc == 2 && strlen(argv[1]) < 900);
    strcpy(usr_path, argv[1]);
    path_slash(usr_path);
    uint8_t locks[128] = { 0 }, expected[128];
    local_t local = { .def = 0xff, .cent = 0x32, .lock = locks };
    nvr_t chip = { .size = 128, .fn = "pc5286-contract.nvr", .data = &local,
                   .reset = nvr_reset };
    saved_nvr = &chip;
    /* Refuse to overwrite an existing fixture. */
    FILE *fp = fopen(nvr_path(chip.fn), "rb");
    assert(fp == NULL);
    assert(nvr_load() == 1 && chip.is_new);
    assert(chip.regs[0x10] == 0xff);

    nvr_dosave = 0;
    for (unsigned i = 0x0e; i < 128; i++)
        nvr_reg_common_write(i, (uint8_t)(i ^ 0x5a), &chip, &local);
    assert(nvr_dosave);
    memcpy(expected, chip.regs, sizeof(expected));
    /* Device reset must not erase Setup fields, including the upper half. */
    nvr_at_reset(&chip);
    assert(!memcmp(expected + 0x0e, chip.regs + 0x0e, 128 - 0x0e));
    assert(nvr_save() == 1 && !nvr_dosave);
    memset(chip.regs, 0, sizeof(chip.regs));
    assert(nvr_load() == 1 && !chip.is_new);
    assert(!memcmp(expected + 0x0e, chip.regs + 0x0e, 128 - 0x0e));

    /* A truncated save is discarded, not accepted as partially valid CMOS. */
    fp = fopen(nvr_path(chip.fn), "wb");
    assert(fp);
    assert(fwrite(expected, 1, 17, fp) == 17);
    assert(fclose(fp) == 0);
    assert(nvr_load() == 1 && chip.is_new);
    assert(chip.regs[0x10] == 0xff && chip.regs[0x7f] == 0xff);
    assert(remove(nvr_path(chip.fn)) == 0);
    puts("PC5286 CMOS: fresh state, dirty flag, reset retention, 128-byte save/reload, truncated-file rejection PASS");
    return 0;
}
