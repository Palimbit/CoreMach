# Olivetti M15 engineering notes

## Outcome and evidence boundary

BluMach has an **experimental**, usable M15: it runs the local system BIOS,
passes the Resident Diagnostics, presents text and both documented graphics
resolutions on a fixed four-level green LCD, boots 720 KB media, and runs the
graphical tutorial from the M15 Starter Kit. It is a compatibility model, not
a recovered schematic or a cycle-accurate motherboard.

This note uses four deliberately separate categories. *Documented* describes
manufacturer material; *observed* comes from board photographs, BIOS analysis
or controlled execution; *inferred* is a conclusion that best explains those
observations; and *approximate* names an intentionally compatible substitute.
Passing a diagnostic validates the guest-visible path, not every transistor or
timing relationship of the original portable.

## Why this machine was difficult

The M15 is a compact proprietary portable rather than an IBM XT with a new
case. Its board shows an Intel 80C88, a Yamaha V6355D-F display controller, an
OKI MSM6242 RTC, a WD82C50 serial device, a NEC µPD72065C floppy controller,
an Epson data separator and three unidentified Hitachi ASICs. That inventory
does not reveal their address decoding, electrical timing, keyboard protocol
or memory control.

The available system firmware is only a 16 KB effective BIOS inside a larger
container. It is sufficient to observe register accesses and diagnostic
expectations, but it is not a service manual. In particular, it did not settle
the V6355D panel wiring, the upper half of the character repertoire, the
function of the Hitachi devices, or the relationship between photographed RAM
layouts. The documented external 5.25-inch FDU also does not justify inventing
an internal third drive or a colour CRT connector: the M15 has a fixed LCD and
no external video output.

## Evidence became a model

The BIOS establishes a CGA-compatible video route at 3D4h, 3D8h, 3D9h and
3DFh, plus V6355D selector/data ports at 3DDh and 3DEh. It initializes
extended registers 00h–69h, recognizes modes 00h–07h and clears 16 KB through
the colour video path. BluMach therefore uses the V6355D compatibility device
and its known memory aliases, then gives the M15 variant a fixed LCD
presentation that maps RGBI luminance to four green levels. This is enough to
make the firmware's text and graphics observable, but it is not a model of
the LCD's drive waveform, contrast or response time.

The BIOS also supplies a useful physical clue that ordinary CGA lacks: a table
of 128 8×8 ASCII glyphs. The initializer copies only codes 00h–7Fh from the
mapped system ROM, preserving a generic CP437 fallback for 80h–FFh. This makes
the validated visible text look like an M15 without claiming that the original
text hardware used that fallback or that a second character ROM has been
found.

The board configuration was reconstructed from BIOS behavior rather than a
forum switch table. Firmware reads multiplexed state from ports 60h and 62h
and computes memory in 16 KB blocks. The M15 keyboard-controller variant
reports that encoding for 256 and 512 KB. It also supplies the minimal response
needed by the observed keyboard probe; it is not presented as a full model of
the detachable keyboard controller.

The BIOS exposes an RTC window at 100h–10Fh, allowing an MSM6242-compatible
functional model. The serial, parallel and floppy paths reuse XT-compatible
devices because their complete M15-specific decode and signal wiring remain
unknown. The floppy result is still meaningful: the BIOS diagnostics and a
720 KB FAT12 boot exercise the compatible FDC, DMA and IRQ path.

Finally, the resident timer test rejects the normal XT period. Its acceptance
windows show a relationship close to CPU/3, so the common PIT receives a
97/128 period ratio for this machine. This is a BIOS-calibrated compatibility
choice, not a measurement of a real 80C88 clock tree.

## Failures that changed the design

Several early failures were useful because they ruled out tempting shortcuts.

- A black LCD initially looked like failed video. The BIOS had in fact written
  its diagnostics, but the V6355D lacked glyph data. A temporary generic font
  proved scanout; the BIOS-resident ASCII table then replaced it for the
  validated range.
- The Resident Diagnostics stopped at the timer test with the standard XT PIT.
  Tracing the common failure handler and all three counter limits led to the
  calibrated period instead of bypassing the diagnostic.
- `FDD Fail` first appeared with a 360 KB 5.25-inch image. A 720 KB, 80-track,
  double-sided FAT12 image completed the BIOS reads, showing a media mismatch
  rather than evidence of a failed emulated µPD72065C.
- The original Starter Kit tutorial initially ended with `Illegal function
  call` in `P0START`. It was not an illegal 80C88 instruction or a RAM failure:
  the BASIC runtime requested `SCREEN 2` after BIOS mode reporting had returned
  mode 7. The root cause was initialization order: the M15 switch provider
  announced 00h before the V6355D existed. The BIOS decodes 20h as 80-column
  CGA mode, so the model now presents that documented startup setting directly.
- `GRAFTABL` searches for an Olivetti video-ROM signature at C000:0006 and
  invokes INT 10h/AX=0060h when it finds one. Other Olivetti video ROMs make
  the search plausible, but the M15 firmware, documentation and compatibility
  model provide no evidence for an M15 option ROM there. The tutorial works
  without a synthetic C000h ROM, so BluMach deliberately does not add one.

## Compromise ledger

| Area | Current treatment | Boundary for a replacement |
| --- | --- | --- |
| System BIOS and initial mode | Local firmware is mapped as observed; BIOS mode/switch behavior drives the startup configuration. | A provenance-verified revision, service material or physical trace may refine mappings. |
| V6355D and LCD | Existing V6355D compatibility core plus M15 four-green-level presentation. | Panel schematic or repeatable traces for extended registers, timing and electrical response. |
| Text font | BIOS 00h–7Fh glyphs, generic fallback above 7Fh. | Character-ROM dump or evidence of the authentic high-code loading path. |
| Memory and keyboard probe | M15-specific switch decoder and the observed minimum probe response. | Keyboard-controller firmware, protocol traces or board schematics. |
| RTC | Functional MSM6242-compatible register window. | Timing traces and evidence for alarm, battery and control behavior. |
| Floppy, UART and LPT | Compatible XT devices on guest-visible routes. | Full M15 I/O map and µPD72065C/SED9420 signal behavior. |
| PIT | BIOS-calibrated period ratio. | Measurement of the original oscillator and peripheral clocks. |

## Validation ladder

The implementation was tested progressively rather than declaring success from
a boot screen alone:

1. Structural firmware checks established the reset path, port families and
   resident font location.
2. Resident Diagnostics exercised CPU, ROM, timer, DMA, interrupts, LCD/video
   routing and both supported RAM configurations.
3. The user observed 40/80-column text and 320×200/640×200 graphics, including
   the four-level green presentation.
4. A write-protected 720 KB FAT12 medium validated BIOS floppy reads and DOS
   boot; the original Starter Kit medium then reached its graphical tutorial.
5. The historical catalogue creates both RAM configurations, accepts media and
   supports soft and hard reset.
6. Catalogue source validation and unit tests check the public profile and all
   five translations; a UCRT64 build links the complete application.

No destructive test writes to preserved media, and no claim is made for an
unobserved external video output, hard disk, third internal drive or proprietary
ASIC behavior.

## Public/private boundary and source map

No ROM, disk, manual scan or proprietary diagnostic program is included in the
repository. This public account names evidence categories and guest-visible
results without reproducing restricted bytes or treating locally held material
as redistributable.

The main implementation points are:

- `src/machine/m_xt.c`: BIOS mapping, M15 initialization and ASCII font load;
- `src/machine/machine_table.c`: machine identity and internal devices;
- `src/video/vid_cga_v6355.c`: V6355D LCD presentation;
- `src/device/kbc_xt.c`: memory-switch encoding and keyboard probe subset;
- `src/device/isartc.c`: MSM6242-compatible RTC window;
- `src/pit.c`: calibrated PIT-period support;
- `src/qt/catalog/source/machines/olivetti/olivetti-m15/`: public profile,
  translations and supported creation choices.

Future work should replace one ledger entry at a time when stronger evidence
arrives. Until then, the experimental label is intentional: the model preserves
the demonstrated behavior without pretending that its compatibility devices are
a recovered M15 motherboard.
