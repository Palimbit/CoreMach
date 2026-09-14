# Firmware policy and setup

BluMach emulates hardware but does not distribute proprietary system BIOS,
option ROM, controller firmware, operating-system media or application
software. A catalogue entry or preservation record may identify a required
file by name, size and hash; that metadata does not grant permission to copy or
redistribute the file.

## Providing local firmware

Use firmware obtained from hardware you own or from another source whose terms
allow you to use it. BluMach searches a directory named `roms` beside the
application and the platform-specific data directories inherited for
compatibility. To use an explicit location, start BluMach with:

```text
BluMach --rompath path
```

`-R path` is the equivalent short form. BluMach prints the searched ROM paths
to its log during startup. Required relative filenames and layouts are listed
in the relevant public machine note when they can be disclosed safely.

## Distribution boundary

- Official source archives, binaries and CI artifacts must contain no
  proprietary firmware.
- Local firmware, disk images and restricted research material must not be
  committed to the BluMach repository.
- A separate repository or download does not make firmware redistributable.
- Firmware may be published by BluMach only when an explicit open licence,
  public-domain status or documented permission covers redistribution.
- Checksums and provenance records may be public when they contain no protected
  binary content and disclose no restricted source.

The release package audit checks program structure and startup without firmware.
Machine validation that requires local firmware is performed separately and its
public results must not include the firmware bytes.
