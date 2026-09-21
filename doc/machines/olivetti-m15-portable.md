# Olivetti M15: portable-engine bring-up

This is an **experimental** portable-engine machine, selectable in the Qt6
portable frontend. It is separate from the legacy `olivetti_m15` pilot and
does not read legacy VM configuration. It has booted a locally supplied
720 KiB M15 Starter Kit disk to its language selector and tutorial in a
manual probe; this is not parity with the older pilot or a validated release.

For a local Qt6 trial, open `BluMach-portable` and select Olivetti M15. Supply
the 64 KiB local system BIOS and, optionally, a 720 KiB floppy image for A:.
The frontend starts with 512 KiB RAM and 80-column display switches. The
floppy is opened read-only; no firmware or media are bundled with BluMach.
The BIOS performs a lengthy memory test before reading A:, and performance
of this implementation is currently below real time on the tested host.
The status bar reports emulated seconds and floppy read operations so a blank
early frame can be distinguished from a stopped session. In a local Qt6 trial,
the user confirmed that the tutorial became visible and could be navigated.
The early black frame and its difference from the inherited pilot remain
unexplained; do not treat eventual boot as a correction of that behavior.

## Implemented and tested

- A typed, session-owned M15 configuration with 256/512 KiB RAM, a caller-owned
  64 KiB firmware blob and two internal 720 KiB floppy-drive definitions.
- Intel 8088 interpreted execution at a nominal 4.77 MHz, ROM at F0000h,
  an instance-owned V6355D with mirrored 16 KiB VRAM at B0000h–BFFFFh,
  PIC, PIT, DMA, FDC and a separate
  MSM6242 register-calendar component at ports 100h–10Fh.
- M15-specific startup display and memory switches at ports 60h/62h, Port B
  control, and the BIOS keyboard-probe command 05h/response 82h. A bounded,
  instance-owned keyboard queue accepts normalized physical-key events and
  delivers the supported main keys as XT-compatible guest bytes through 60h,
  status bit 0 at 64h and IRQ1 on a deterministic virtual-millisecond cadence.
  The guest acknowledges the data strobe via Port B bit 7; enabling its clock
  via bit 6 produces the inherited AA self-test byte.
- Synthetic reset-vector and I/O probes for both RAM sizes, reset, RTC virtual
  seconds and allocation-failure cleanup. The RTC has independent tests for
  BCD/calendar rollover, leap day, 12/24-hour display, HOLD/STOP, 30-second
  adjustment, state injection and two simultaneous instances. These tests
  contain no proprietary firmware.
- V6355D CRTC/mode/extended-register ports, 3Bxh compatibility alias, indexed
  register auto-increment, 40/80-column text and CGA-compatible 320/640-pixel
  raster paths. The controller emits RGBI indices; the M15 presentation maps
  those to four green levels in a host-neutral framebuffer. The lower 128
  glyphs come from the caller-supplied M15 firmware blob; the upper 128 remain
  unresolved, currently blank. Synthetic tests cover register access, memory
  mirrors, glyph and graphics pixels, virtual status, geometry and a session
  framebuffer without reading or executing the locally preserved BIOS.
- A manual, local-firmware probe passes the BIOS PIT test and boots the
  720 KiB Starter Kit disk to the language selector with 256 and 512 KiB;
  F1 advances to the logo/tutorial. A frontend adapter now registers M15,
  reports both internal floppies and supports read-only media replacement.
- A synthetic firmware test exercises the AA startup byte, Enter make/break,
  status polling, Port B acknowledgement, IRQ1 request, reset, unsupported
  keys and queue-full behavior. It does not validate a real M15 keyboard.

The Intel 8088 implementation is an approximation for the documented 80C88;
its CMOS-specific behavior and timings have not been verified. The PIT ratio
of CPU/3 is inferred from earlier BIOS diagnostics, not a measured board clock.
The RTC implementation uses the inherited M15 register placement and the OKI
MSM6242B functional data sheet as a guide; the exact photographed M15 chip
revision and its board wiring remain unverified. BUSY timing, interrupt/pulse
output and TEST mode are intentionally absent. Its date starts from a
deterministic 1980-01-01 unless the caller supplies battery-backed state;
the frontend does not yet persist that state.

## Required before calling the portable M15 validated

1. Compare the V6355D output with the inherited pilot and locally approved
   firmware. The current status timing is a deterministic approximation, not
   a measured scan clock. MDA attributes, the hardware pointer, composite
   output, undocumented 16-colour modes and upper glyphs remain unresolved.
2. Determine the physical M15 detachable-keyboard serial protocol and the
   codes of national/extended keys. The current XT-compatible byte stream is
   an inherited pilot approximation, not a claim that the M15 keyboard is IBM
   XT. Right-side modifiers, navigation keys and special sequences are
   deliberately unsupported pending evidence. No timeout silently releases
   an unacknowledged byte; a guest that never acknowledges Port B will stall.
3. Compare POST, interrupt and port traces with the validated legacy pilot
   using locally approved firmware. Do not package firmware or media.
4. Confirm Qt6 rendering, keyboard, pause/reset/stop, media eject/insert and
   a second boot using the local disk. Diagnose why the early screen appears
   much later than in the legacy pilot, and profile the Intel 8088 path against
   the existing V30 path before changing timing or skipping BIOS work.
5. Test whether the M15 BIOS actually uses MSM6242 IRQ/pulse, BUSY or TEST;
   implement those only where evidence makes them necessary.

The canonical custody and historical evidence remain under
`Z:\library\olivetti\m15`; the matching workspace library directory is a
working replica, not an additional source of truth.

Chip reference: OKI, *MSM6242B Direct Bus Connected CMOS Real Time
Clock/Calendar*, [register and control description](https://manuals.plus/m/1d812496264b12c09a1ccadd0d0ff3b202ee7aa0bfb3f5fb25b99b8fcd4861bc).
