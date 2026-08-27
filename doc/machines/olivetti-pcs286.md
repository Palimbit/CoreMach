# Olivetti PCS 286

Status as of 2026-08-27: POST, built-in Setup and Olivetti MS-DOS 3.30a floppy
boot work. Firmware selection, memory limits and CMOS size match the preserved
evidence; keyboard timing and exact onboard-video fidelity remain partial.

The PCS 286S is catalogued separately and must not be treated as the same
motherboard without additional evidence. See also the
[Olivetti PCS family overview](olivetti-pcs-family.md).

## Recommended BluMach configuration

- machine: `[GC103] Olivetti PCS 286` (`olivetti_pcs286`);
- CPU: Intel 80286 at 12 MHz;
- memory: 1, 2, 3 or 4 MiB;
- video: `Internal` Paradise PVGA1A, 256 KiB;
- floppy: one internal 3.5-inch 1.44 MB drive;
- fixed disk: configure an IDE drive only when testing a documented geometry;
- optional math processor: 80287 support is expected but not yet certified.

## Firmware selection

The machine settings expose two preserved firmware sets:

| BluMach choice | Archive label | Internal identification | Runtime files |
|---|---|---|---|
| `v137` | 1.34 | BIOS and Resident Diagnostics 1.37, 1989-11-20 | `olivetti_pcs286_bios_v137.bin` |
| `v142` (default) | 1.42 | BIOS and Resident Diagnostics 1.42, 1990-05-03 | `olivetti_pcs286_bios_v142_low.bin`, `..._high.bin` |

The 1.42 pair is byte-interleaved across `E0000-FFFFF`. The older `v137` file is
a canonical, already combined 128 KiB image and is loaded linearly.

Files historically named `Video at E0000.ROM` and `BIOS at F0000.ROM` are not
Tandy firmware and are not independent video/system ROMs. Concatenating them
reconstructs the older Olivetti 1.37 system image exactly. The ROM strings and
byte equality establish the corrected identification.

## Documented and represented hardware

- Intel 80286 at 12 MHz;
- 1-4 MiB RAM;
- Headland GC101A/GC102 family, represented by the GC10x implementation;
- Olivetti IOC02 gate array and Olivetti-specific ports `61h-63h`;
- Olivetti-flavoured AT 8042 keyboard controller;
- onboard AT floppy controller;
- onboard Paradise PVGA1A with an IMS G176P-40-style RAMDAC;
- 128-byte CMOS address space.

Phoenix diagnostics exercise a Headland memory-remap feature: writes at
`60000-7FFFF` must be observable at `80000-9FFFF`. BluMach currently models
this as a fixed alias because the generic Headland device does not yet expose
the controlling side effect. The initial BIOS Data Area equipment word is also
seeded with the Olivetti 80x25/one-floppy encoding so it agrees with CMOS.

## Setup, software and preserved devices

Setup is resident in the system firmware. Tested firmware can enter it during
POST and retain configuration in the normal workflow. The preservation set
also contains the keyboard-controller ROM, PAL16L8 dumps `2P04` and `3P01`,
physical 128-byte CMOS captures and a working NVR image. PAL equations are
preserved but not yet represented as separate emulated devices.

Preserved software includes:

- Olivetti MS-DOS 3.30a Rev. 1.03 with Customer Diagnostics;
- PCS286 Tutorial in original ImageDisk form and a derived raw image;
- SCO XENIX SLS `xnx264` replacement disks for XENIX 286 2.3.2.

The tutorial includes German, English, French, Italian and Spanish resources.
Original captures and working conversions remain separate.

## Validated behaviour

- firmware 1.37/1.42 recognition and correct ROM layout;
- POST and built-in Setup path;
- 1.44 MB floppy controller and Olivetti MS-DOS boot;
- Olivetti scan-code translation and normal keyboard input;
- 1-4 MiB selection in the machine table;
- 128-byte CMOS addressing;
- memory-controller remap required by Resident Diagnostics.

## Known limitations and next tests

- `KEYBOARD ERROR 4` can still occur because the keyboard BAT completion byte
  misses the BIOS timing window.
- The PVGA1A core is appropriate, but exact PCS 286 video initialisation and
  modes still need a clean visual certification.
- Validate an original-style 40 MB disk from partitioning through repeated boot.
- Run the complete Customer Diagnostics and Tutorial suites.
- Repeat cold boot, Setup save and warm reset with both BIOS revisions.
- Validate the optional 80287 and the 1/2/3/4 MiB matrix.
- Replace the static memory alias when the exact Headland control path is known.

## Principal references

- firmware and PAL archive:
  <https://wiki.pldarchive.co.uk/index.php?title=Olivetti_PCS_286>
- machine and software archive: <https://olivrea.de/olivetti-pcs-286/> and
  <https://olivrea.de/software/>
- SUPSI technical record:
  <https://lastin.dti.supsi.ch/VET/sys/Olivetti/PCS-286/00496C-Olivetti-PCS-286.pdf>
- SCO XENIX supplements: <https://www.ardent-tool.com/XENIX/>
