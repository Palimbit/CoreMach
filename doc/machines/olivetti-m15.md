# Olivetti M15

Status as of 2026-09-05: experimental but functional. BluMach passes the
resident diagnostics, boots 720 KB media at both supported memory sizes and
runs the graphical tutorial supplied with the M15 Starter Kit. Board-level
timing and several custom interfaces remain explicit approximations.

## Historical identity

Olivetti introduced the M15 in 1987 as a portable IBM-compatible computer. It
is distinct from both the desktop M19 and the later M15 Plus. The documented
base unit combines an Intel 80C88 at 4.77 MHz, 512 KB of RAM, a detachable
78-key keyboard and two internal 3.5-inch 720 KB floppy drives. A 256 KB
configuration is also represented and validated.

The machine has no internal hard disk. Olivetti documented a self-powered
external 5.25-inch floppy unit that could operate alongside both internal
drives, but the current BluMach profile deliberately exposes only the two
internal drives.

## Display

The M15 uses a fixed, non-backlit green monochrome LCD driven by a Yamaha
V6355D-F. It supports 40×25 and 80×25 text, 320×200 and 640×200 graphics, and
four green levels. The M15 has no external video output, so an external colour
CRT is neither documented nor offered by the machine profile.

BIOS 1.08 directly programs the CGA-compatible and V6355D extended registers.
BluMach maps the internal controller through the firmware's colour and
monochrome compatibility paths, mirrors its 16 KB video RAM where required,
and converts RGBI luminance to four green levels. Characters `00h`–`7Fh` come
from the 8×8 glyph table in the mapped system BIOS; characters `80h`–`FFh`
currently retain a generic CP437 fallback.

## Recommended BluMach configuration

- machine: `[8088] Olivetti M15 (experimental)` (`olivetti_m15`);
- CPU: Intel 80C88-compatible core at 4.77 MHz;
- memory: 256 or 512 KB;
- video: fixed internal V6355D green LCD;
- floppy: two internal 3.5-inch 720 KB drives;
- hard disk: none.

The historical catalogue creates this configuration through one declarative
form. Both memory choices, floppy insertion and soft and hard reset have been
validated from the graphical interface.

## Firmware

The locally tested image identifies as system BIOS 1.08 and is dated
1987-05-09. BluMach expects it as:

`roms/machines/olivetti_m15/OLIV_M15.BIN`

The available 64 KB container has 16 KB of effective code mapped at
`FC000h-FFFFFh`. Firmware provenance and redistribution rights have not been
verified, so no BIOS or proprietary software image is included with BluMach.

## Validated behaviour

- cold POST and Resident Diagnostics;
- 256 and 512 KB memory configurations;
- 40- and 80-column text presentation;
- 320×200 and 640×200 graphics with visible green-level patterns;
- two internal 720 KB floppy drives and FAT12 boot;
- soft and hard reset;
- the unmodified M15 *Keyboard Drivers & Getting to know* Starter Kit disk,
  including its graphical tutorial.

## Known approximations and pending work

- The available CPU core is an NMOS 8088 model. The PIT period is calibrated
  to the BIOS acceptance windows as an approximation of the M15's 80C88-era
  timing.
- The four green levels reproduce visible luminance relationships, not the
  original LCD's electrical timing, contrast or physical response. Extended
  V6355D panel registers `66h` and `67h` still lack verified semantics.
- The keyboard command needed by the official disk and the BIOS memory-switch
  encoding are implemented, but the complete detachable-keyboard protocol is
  not.
- UART, parallel and floppy paths use compatible devices. The floppy path is
  functionally validated but is not a transistor- or timing-level model of the
  physical μPD72065C chain.
- The OKI MSM6242 clock is functionally represented at the BIOS-observed I/O
  range. The three Hitachi custom-logic devices and the complete board decode
  remain unmapped.
- The authentic text-mode mapping for character codes `80h`–`FFh` is unknown.
- The unofficially reported 384, 448 and 544 KB switch settings are not exposed
  without better documentary or physical evidence.
- The historical purpose of an Olivetti video-ROM signature searched by
  `GRAFTABL` remains open. The M15 boots the tutorial without a C000h option ROM,
  so BluMach does not synthesize one.

## Principal references

- *Olivetti M15 Installation and Operations Guide*.
- *Olivetti M15 Technical Specifications*, publication 3928093 K/1 (1987):
  <https://www.valoroso.it/file-share/documenti-manuali/Olivetti-M15-technical-specifications.pdf>
- internal photographs and board identification:
  <https://oldcrap.org/2024/04/18/olivetti-m15/>
- Yamaha V6355D register research:
  <https://www.seasip.info/VintagePC/v6355d.html>
- MAME's earlier `olivm15` compatibility model:
  <https://github.com/mamedev/mame/blob/master/src/mame/pc/pc.cpp>
