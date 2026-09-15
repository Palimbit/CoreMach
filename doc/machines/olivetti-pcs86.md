# Olivetti PCS 86

Status as of 2026-08-27: boots Olivetti MS-DOS 3.30a and passes the currently
exercised Resident Diagnostics 1.09 tests. Some motherboard glue, PS/2 timing,
serial I/O and host-visible video behaviour remain incomplete.

See also the [Olivetti PCS family overview](olivetti-pcs-family.md) and the
[portable-engine implementation narrative](olivetti-pcs86-implementation.md).

The inherited product remains the only usable implementation. The parallel
portable engine now executes the original BIOS reset, CPU-register self-test
and complete 64 KiB ROM checksum, and passes its first conventional-memory
alias check. It also enters the firmware's upper-memory setup, clears and scans
the selected 64 KiB window, validates segment-overridden memory aliases and
returns to early board setup. It now programs and reads back the portable
8237 register core and its separate XT page latches during the firmware's DMA
tests. The portable MM58167 front now supplies the firmware-used interrupt
status and control registers at `B0h-B1h`; after clearing that state, the BIOS
completes its long conventional-memory test and reaches the video/option-ROM
scan. After 1,377,642 instructions and 158 successful I/O accesses, the strict
bus reports the unmapped physical address `C0000h` at `F000:0D0E`. No placeholder
ROM or video memory is fabricated. The RTC
counter/calendar window, clock progression, alarms, interrupt generation and
persistence remain absent. The DMA controller does not yet arbitrate or perform
transfers. This is measured bring-up progress, not a completed POST or visible
video. The preceding write of `40h` to port
`70h` is retained in an opaque PCS 86 latch without assigning guessed PC/AT
CMOS semantics, while `8400h-8403h` currently retain only EMS page-selector
writes.

## Recommended BluMach configuration

- machine: `[8086] Olivetti PCS86` (`olivetti_pcs86`);
- CPU: NEC V30 at 10 MHz;
- conventional memory: 640 KiB;
- video: onboard Paradise-compatible implementation;
- floppy: one internal 3.5-inch 1.44 MB drive;
- hard disk: internal XTA when an original-style disk is required;
- optional onboard EMS: none, 384 KiB or 1,920 KiB.

The EMS choices represent no SIMMs, two 256 KiB SIMMs or two 1 MiB SIMMs.
After the first 640 KiB address space is satisfied, the remaining expansion is
presented through four 16 KiB page frames.

## Documented hardware

The PCS 86 is an XT-class design built around an NEC V30 at 10 MHz, but includes
features unusual for a conventional XT: onboard VGA-class video, a 1.44 MB
floppy controller, an integrated XTA fixed-disk interface, two interchangeable
PS/2-style ports and an MM58167 real-time clock.

The current implementation exposes the documented board-control registers,
PS/2 ports at `66h-6Ah`, RTC at `E0h-EFh`, control registers at `B0h-B7h`, XTA
enable control and the onboard EMS windows. Several undocumented latch bits are
still conservative placeholders.

## Firmware

Resident Diagnostics and system BIOS identify as revision 1.09, dated
1989-11-17. Two 32 KiB EPROM captures form one 64 KiB image:

| Required runtime file | Role |
|---|---|
| `roms/machines/olivetti_pcs86/CSAB05_02-17.BIN` | even bytes |
| `roms/machines/olivetti_pcs86/CSAB04_02-25.BIN` | odd bytes |

BluMach interleaves them at `F0000-FFFFF`. The resulting checksum is zero and
the reset vector is `EA 5B E0 00 F0`. System, resident-diagnostic, video and
fixed-disk firmware coexist in this ROM; do not add unrelated video or disk
option ROMs for a factory configuration.

## Storage and boot media

The integrated fixed-disk path is XTA, not ordinary 16-bit IDE. The preserved
working geometry for a Conner CP3026 is 615 cylinders, 4 heads and 17 sectors,
approximately 20 MiB. A generated blank image proves geometry handling but is
not an original disk dump.

Olivetti MS-DOS 3.30a Rev. 1.03 boots from the 1.44 MB floppy drive and is used
for validation. Its presence in the preservation library does not prove that
the particular capture came from the three-disk PCS 86 retail package.

## Validated behaviour

- BIOS checksum and cold POST;
- NEC V30, ROM, DMA, PIC and timer diagnostics;
- 640 KiB conventional memory;
- RTC and basic keyboard operation;
- floppy sector reads, IRQ6 and MS-DOS 3.30a boot;
- onboard EMS with 1,920 KiB fitted;
- XTA controller detection.

## Known limitations and next tests

- A host capture may remain black even when VGA text memory contains the full
  diagnostic or DOS screen; visible framebuffer presentation needs retesting.
- PS/2 electrical timing, mouse behaviour and the full keyboard command set are
  not certified.
- The original DOS EMS driver and INT 67h interface have not been recovered.
- The integrated serial port and several enable bits at port `65h` are partial.
- Installation and repeated boot from an XTA disk require a clean current test.
- Machine-private state should be released when switching models in one session.

## Principal references

- John Elliott's PCS 86 hardware analysis:
  <https://www.seasip.info/VintagePC/pcs86.html>
- preserved ROM record: <https://archive.org/details/olivetti-pcs-86-bios-roms>
- documented retail DOS package:
  <https://www.computinghistory.org.uk/det/12561/MS-DOS-Manuals-for-Olivetti-PCS-86/>
