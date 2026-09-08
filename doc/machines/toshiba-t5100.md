# Toshiba T5100 (experimental)

The Toshiba T5100 is a 1988 portable built around a 16 MHz Intel 80386DX,
2 MiB of standard memory, an optional 2 MiB memory card, an internal IDE hard
disk and a gas-plasma display driven by Toshiba's proprietary AGS/CELT logic.

## Current implementation

This initial BluMach model is intentionally limited to behavior supported by
the Toshiba maintenance manual and observation of system BIOS V2.30:

- the 64 KiB interleaved system BIOS at `F0000-FFFFF`;
- a distinct secondary keyboard-controller endpoint at `8060/8064`, including
  the POST command `BB` and hardware-status command `B4`;
- coherent Toshiba system-control latches at `8080-808F`, with unknown
  electrical side effects left unimplemented;
- the 64 KiB LIM frame at `D0000-DFFFF`, four 16 KiB slots and the eight page
  registers used by the BIOS; and
- generic AT floppy, IDE, RTC, external keyboard and selectable external video
  cores where the documented interfaces permit reuse.

The documented `B4` result is `8C` for the standard 16 MHz machine with one
1.44 MB internal floppy and no external floppy.

## Important limitations

The separate 32 KiB AGS video ROM has not been preserved.  BluMach does not
invent it, borrow the T3200 ROM, or claim to emulate the internal plasma.
Instead this model is limited to the system BIOS's observed fallback path for
an external MDA/CGA-compatible display.  The AGS/CELT logic, internal plasma,
display switching, optional memory-card page decode, Toshiba gate-array side
effects and exact controller firmware remain unavailable or provisional.

The selectable machine remains experimental until real firmware execution
passes POST and operating-system validation.  Enable **Log experimental
firmware progress** only when collecting diagnostic logs.

## Firmware

System BIOS V2.30 uses two 32 KiB ROM images, EVEN `042F` and ODD `043F`.
Firmware is not distributed with BluMach.  The expected files are shown in the
machine's BIOS configuration and must be supplied locally by the user.
