/* BluMach modifications: rtzor, Project BluMach, 2026.
 * Run with the locally preserved 64 KiB AM52V004 ROM as argv[1].
 * No firmware is embedded or written. Compile with -O2 -fwhole-program.
 */
#include <assert.h>
#include "../../src/machine/m_at_286.c"

int
main(int argc, char **argv)
{
    uint8_t original[65536], candidate[65536];
    assert(argc == 2);
    FILE *fp = fopen(argv[1], "rb");
    assert(fp != NULL);
    assert(fread(original, 1, sizeof(original), fp) == sizeof(original));
    assert(fgetc(fp) == EOF);
    fclose(fp);
    assert(crc32(0L, original, sizeof(original)) == 0xe33a1151UL);

    memcpy(candidate, original, sizeof(candidate));
    assert(pc5286_bios_checksum(candidate, 0) == 0);
    assert(!memcmp(candidate, original, sizeof(candidate)));
    assert(pc5286_bios_checksum(candidate, 1) == 1);
    assert(!memcmp(candidate, original, sizeof(candidate) - 1));
    assert(candidate[65535] == 0x2b);
    unsigned sum = 0;
    for (unsigned i = 0; i < sizeof(candidate); i++)
        sum += candidate[i];
    assert((sum & 255) == 0);
    assert(pc5286_bios_checksum(candidate, 1) == -1);
    assert(candidate[65535] == 0x2b);

    /* Reject changes anywhere, including images which retain the old final
       byte and sum8. A final-byte check alone accepted all of these. */
    for (unsigned i = 0; i < sizeof(candidate); i++) {
        memcpy(candidate, original, sizeof(candidate));
        candidate[i] ^= 1;
        assert(pc5286_bios_checksum(candidate, 1) == -1);
        candidate[i] ^= 1;
        assert(!memcmp(candidate, original, sizeof(candidate)));
    }
    memcpy(candidate, original, sizeof(candidate));
    candidate[100]++;
    candidate[101]--;
    assert(pc5286_bios_checksum(candidate, 1) == -1);
    assert(candidate[65535] == 0x2c);
    puts("PC5286 BIOS: opt-out, exact patch, repeat, every byte mutation and sum8 collision PASS");
    return 0;
}
