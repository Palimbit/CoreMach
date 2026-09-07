# Toshiba T3200

The Toshiba T3200 is a transportable 80286 computer with a gas-plasma display,
EGA-compatible graphics, an internal MFM hard disk and two expansion slots.
This implementation models the original T3200, not the later T3200SX or
T3200SXC.

Its status in BluMach is **experimental**. The original Toshiba 4.61 system
BIOS and AGS firmware boot unmodified, Toshiba MS-DOS 3.30 runs, and the
validated configurations below are usable. Some custom-logic details remain
approximations.

## Commercial configuration

Toshiba sold the T3200 as a compact alternative to a desktop PC. Its sales
brochure describes a 12 MHz Intel 80286, 1 MB of memory, a 40 MB hard disk,
a 720 KB 3.5-inch floppy drive, a plasma display and MS-DOS 3.2. Toshiba
TECHaccess later documented original T3200 units with a 1.44 MB drive and an
upgrade from 720 KB. The production date and serial-number boundary for that
change are not known.

The standard memory consists of 640 KB conventional memory and 384 KB available
through a LIM/EMS page frame at D0000h. Toshiba offered a 3 MB expansion card.
The firmware can divide that card between extended and expanded memory in
512 KB steps.

## Configurations available in BluMach

The catalogue creation form presents all supported choices together:

| Choice | Result | Status |
|---|---|---|
| 1 MB | 640 KB conventional + 384 KB EMS | Validated |
| 4 MB, EMS only | 640 KB conventional + 3456 KB EMS | Validated, physical page order approximated |
| 4 MB, extended | 640 KB conventional + 3072 KB extended + 384 KB EMS | Validated endpoint |
| 1.44 MB floppy | Later documented internal drive, default | Validated |
| 720 KB floppy | Early documented internal drive | Validated |

For the 4 MB extended configuration, select 3 MB extended memory in Toshiba
SETUP as well. Intermediate 0.5–2.5 MB divisions and the 512 KB conventional
configuration are documented but are not yet selectable because their exact
mapping has not been implemented.

New profiles contain no hard-disk image. Add an MFM image with the documented
Fujitsu M2227DT physical geometry of 615 cylinders, 8 heads and 17 sectors.
Toshiba BIOS type 4 exposes 612 cylinders, 8 heads and 17 sectors to software.
The native controller uses ports 1F0h–1F3h and IRQ 14.

## Display and keyboard controls

The internal display presents four orange plasma levels. The external output
retains the sixteen EGA colors. BluMach uses one video window with one active
output.

- **Right Ctrl + Home**: request the internal plasma display.
- **Right Ctrl + End**: request the external RGB display.
- **Right Ctrl + Down**: request the BIOS 350/400-line presentation change.
- **Ctrl + SysReq**: open the resident Toshiba XCHAD utility when it is loaded.
- **View > T3200 display**: offers the same display choices in the menu.

Right Ctrl represents the Toshiba Fn key on host keyboards that do not expose
Fn to applications. The original BIOS handles the requests; BluMach does not
patch guest memory or call firmware entry points directly.

The panel is documented as 720×400 pixels, while active graphics modes use
640×350 or 640×400 pixels. BluMach preserves the 640-column active image and
uses the documented 0.30 mm horizontal and 0.36 mm vertical pixel pitch for
presentation. Exact unused-panel placement, phosphor calibration, fonts and
some line-replication details remain approximate.

## Required firmware

Place these files in `machines/t3200/` below the local ROM directory. Firmware
is not distributed with BluMach.

| File | Size | SHA-256 |
|---|---:|---|
| `IC22_033E.BIN` | 32,768 bytes | `4e99a2166216acd19d1fdf3c1f74c361d74892c666aff44db7c613fca35b3055` |
| `IC24_034E.BIN` | 32,768 bytes | `a611dd4fb49648cced2e58d0bab5b649ac991a0d0f8c41d7a97a236584fb07fb` |
| `AGS_IC8_035C.BIN` | 32,768 bytes | `4121dd66832c6222f2bb85644f56ce045cc7a9ec7c76d86e4e35642b0fd6f1e9` |

The two system ROMs interleave into the 64 KB F0000h–FFFFFh image. The
reconstructed system BIOS is revision 4.61 with SHA-256
`0c19932b2086d38091f366b26a826ea8404b18241f0857a4303cbeab346167db`.

## Implemented hardware

BluMach provides a T3200-specific machine model around the reusable 80286,
PIC, PIT, DMA, RTC and FDC cores. The custom parts include:

- Toshiba BIOS and AGS ROM mapping.
- The internal 12 MHz 80286 configuration.
- The 2 KB AGS SRAM aperture and observed 40h/41h access.
- Four LIM frame slots at D0000h–DFFFFh and the 0208h/0218h register groups.
- The optional 3 MB memory-card endpoints.
- Internal plasma and external EGA presentation, including BIOS-mediated
  switching and the observed cursor correction.
- Toshiba keyboard-controller commands needed for drive and display selection,
  A20 and the BIOS protected-mode reset path.
- A ROM-less internal MFM controller compatible with the T3200 BIOS protocol.

The LIM register byte uses bit 7 as mapping enable and bits 6–0 as a local
16 KB page selector. A BIOS floppy read through DMA channel 2 into D0000h has
been validated: the data reached the selected EMS page and remained identical
after remapping that page to DC000h.

## Validation

The implementation has passed these scoped checks:

- Cold CMOS initialization, F1 defaults, saved CMOS reopen and warm reset.
- Toshiba BIOS protected-mode service 87h and explicit A20 off/on/off aliasing.
- Complete 720 KB and 1.44 MB BIOS format/read tests.
- DOS `FORMAT /S` and fresh-process boot for both floppy capacities.
- Toshiba DOS 3.30 boot and complete utility-file reads.
- Every BIOS-visible sector of the 615/8/17 MFM disk, including persistence.
- Fresh MFM partitioning, `FORMAT /S`, installation of all 67 DOS utilities,
  independent hard-disk boot and `CHKDSK`.
- Toshiba EMM.SYS allocation, write, remap, read and release across all
  available EMS pages in the 1 MB and 4 MB endpoints.
- The 3 MB extended-memory endpoint through Toshiba SETUP, BIOS INT 15h and
  fresh-process persistence.
- Internal/external display switching, 350/400-line requests, sixteen external
  colors, four internal plasma levels and resident XCHAD editing.
- Catalogue creation and Configure save/reopen for both floppy choices and both
  4 MB memory modes.

These tests establish useful behavior, not cycle accuracy.

## Known limitations

- The optional memory card's physical page-to-DRAM order, RAS selection,
  simultaneous-group priority and exact DMACK timing are unknown. The current
  backing order is a compatible approximation.
- Intermediate memory divisions and 512 KB conventional mode are unavailable.
- AGS capture/IOCHK/NMI delivery and acknowledgement are incomplete.
- External-monitor DIP profiles, startup lid/keyboard selection, 6 MHz switching
  and exact bus wait states are not modeled.
- PEGA2 timing, border behavior, unused panel placement, fonts and calibrated
  plasma output are not exact.
- The MFM controller is functionally validated but does not reproduce exact
  encoding, ECC, Z80 firmware or mechanical timing.

## Sources

- [Toshiba T3200 Maintenance Manual](https://www.minuszerodegrees.net/manuals/Toshiba/Other/Toshiba%20T3200%20-%20Maintenance%20Manual.pdf)
- [Toshiba T3200 sales brochure](https://macdat.net/files/pdf/toshiba/brochures/t3200.pdf)
- [Toshiba TECHaccess T3200 technical sheet](https://conventionalmemories.com/Toshiba/TECHaccess/tech3wog.htm)

The local preservation record is `library/toshiba/t3200`, stable record ID
`t3200`. Firmware, proprietary software, working disks and validation captures
remain outside the public BluMach worktree.
