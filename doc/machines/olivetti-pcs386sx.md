# Olivetti PCS 386SX

Status as of 2026-08-31: this is a usable, bootable **partial emulation**.
Firmware 1.14 completes POST, reaches its built-in Setup path and boots
Olivetti MS-DOS at 1, 2, 4 and 8 MiB. Internal video, keyboard, RTC/CMOS,
floppy, a 100 MB IDE disk and the optional 80387SX have been exercised. This
does not yet imply cycle accuracy or broad software compatibility: exact
HT101SX/HT113 documentation, EMS validation and tests on physical hardware are
still missing.

See also the [Olivetti PCS family overview](olivetti-pcs-family.md).

## Recommended BluMach configuration

- machine: `[HT101SX] Olivetti PCS 386SX` (`olivetti_pcs386sx`);
- CPU: Intel 80386SX at 16 MHz;
- memory: 1-8 MiB in 1 MiB steps;
- video: `Internal` Paradise PVGA1A, 256 KiB;
- floppy: one internal 3.5-inch 1.44 MB drive;
- documented fixed-disk capacities to validate: 20, 40 and 100 MB;
- optional math processor: Intel 80387SX.

Selecting video `None` gives a black 0 Hz display even though the guest CPU
continues executing. A factory-style profile must retain `Internal` video.

## Firmware

BIOS and Resident Diagnostics identify as revision 1.14, dated 1991-10-30.
Two 64 KiB EPROM captures are required:

| Runtime file | Role |
|---|---|
| `roms/machines/olivetti_pcs386sx/olivetti_pcs386sx_v114_lo.bin` | low/even byte lane |
| `roms/machines/olivetti_pcs386sx/olivetti_pcs386sx_v114_hi.bin` | high/odd byte lane |

BluMach interleaves them over `E0000-FFFFF`. The first 24 KiB of the resulting
image contain the valid Olivetti OVC 1.06 video firmware, so the onboard PVGA1A
must not map an unrelated option ROM at `C0000`.

The historical file ending `_deint.bin` is byte-identical to the low EPROM. It
is not a complete deinterleaved BIOS and must not be offered as another revision.

## Documented and represented hardware

Board photographs identify a discrete Headland **HT101SX + HT113 + GC102-PC**
set, Mitsubishi M5L8042 keyboard controller, TI TL16C451FN serial/parallel
controller, WD37C65CJM floppy controller, Paradise PVGA1A and IMS G176P-40.
The current model represents that population directly:

- separate HT101SX system-controller, HT113 memory-controller and GC102-PC
  data-buffer devices, linked as one board chipset;
- Olivetti IOC02 and the machine-specific `61h-63h` glue;
- the Olivetti AT-keyboard-controller profile, because the original M5L8042
  mask ROM has not been preserved;
- one TL16C450-compatible UART and one bidirectional Centronics port, matching
  the documented functional blocks of the TL16C451FN;
- the AT-compatible uPD765 core under the WD37C65CJM component identity;
- onboard PVGA1A initialised by the OVC 1.06 firmware embedded in the system
  ROM;
- 128-byte Phoenix CMOS.

The PCS 386SX no longer instantiates the generic HT18/HT21 core, PC87310 or the
PCS 286's fixed `60000-7FFFF` to `80000-9FFFF` RAM alias. It also does not add
the HT18 fast-A20 port `92h`, CR5/CR6 or sleep-state behaviour: neither known
PCS 386SX BIOS nor the independent HT101SX BIOS uses those interfaces.

No exact public HT101SX or HT113 data sheet is known. BluMach therefore makes
the current reconstruction explicit: HT113 owns the firmware-visible
configuration and EMS ports `1ECh-1EFh`, conventional memory, 384 KiB
relocation, shadow RAM and EMS mappings; HT101SX preserves the system-controller
package identity; GC102-PC is currently a transparent 16-bit data path with
parity state but no guessed parity NMI. This ownership can be revised without
returning to the unrelated HT18 model if stronger primary evidence appears.

## CMOS and 80387SX

A new CMOS image is seeded for one 1.44 MB floppy, 80x25 colour, 640 KiB base
memory and the selected extended-memory size. The Phoenix checksum covers
registers `10h-2Dh` and is stored in `2Eh/2Fh`.

The math-processor equipment bit follows the emulator's physical FPU choice.
Existing PCS 386SX CMOS images are updated and checksummed when an 80387SX is
added or removed. With `fpu_type = 387`, Resident Diagnostics reports
`Math Processor (i80387SX) Pass` and MS-DOS 3.30a boots.

## Keyboard, reset and IOC02 behaviour

Phoenix command `AEh` enables the keyboard and then expects one `3Bh` byte from
the Olivetti controller. BluMach queues that response once per POST. Command
`FEh` produces a reset pulse even when the generic P2.0 state was already low.

Phoenix leaves PCMODE and XLAT active together. For this Olivetti controller,
scan set 2 must still be translated to the BIOS-visible set 1; F1 and F2 then
reach their expected codes. Warm Ctrl+Alt+Delete completes a second POST and
boots again. An instrumented reset-entry test set independent sentinel values
in CRI, MAR, CR0-CR4 and an EMS mapping register, then issued the real 8042
`FEh` reset command. All values were still present when soft reset began; a
hard-reset control recreated the cold defaults. This proves BluMach's retention
behaviour, but matching it to a physical HT113 remains a separate validation
task.

IOC02 register `68h` selects the function visible at `6Ah`. When selector bits
0-4 are zero, a write to `6Ah` does not latch. Modelling that isolation changes
the final diagnostic state from `AX=031Eh` (`I/O Controller Error : 2`) to
`AX=001Eh` (pass). A read before any write exposes ready value `04h`; a normal
write-first POST retains the transformed read-back protocol.

The service chord is Left Shift+Ctrl+Alt+Delete. Its error path is corrected,
but final physical confirmation that holding Left Shift selects Setup remains
pending because synthetic host key injection is not equivalent to the keyboard
hook used by the emulator.

## Preserved software

- Customer Utility Disk Release 1.51 (`PCS386CU.IMD`): bootable 1.44 MB FAT12,
  containing mainboard, CPU/80387, memory, keyboard, floppy, hard-disk, serial,
  parallel, mouse and video diagnostics;
- Tutorial PCS386 Release 2.2 (`PCS386TU.IMD`);
- Olivetti MS-DOS 3.30a Rev. 1.03 for boot validation.

The Customer Utility files contain German, Italian, French, Spanish and English
message resources, although the preserved boot disk defaults to German. The
original IMD boots MS-DOS 4.01 Rev. 1.06 and reaches its date/time prompts.
No independent English- or Spanish-default capture has yet been located.

Both PCS386 IMD captures contain one sector marked unavailable. Preserve and
mount the IMD originals for authoritative tests; flat conversions fill missing
data and are working derivatives only.

## Validated behaviour

- ROM checksum, POST and Phoenix Resident Diagnostics path;
- 1, 2, 4 and 8 MiB configurations and onboard video at 70 Hz;
- parity, PIC, DMA, keyboard, clock/calendar, protected mode and CMOS;
- 80387SX detection and diagnostic pass;
- MS-DOS 3.30a boot, 100 MB IDE access and repeated warm boot;
- Customer Utility 1.51 disk boot through MS-DOS 4.01 date/time prompts;
- IOC02 normal and service-path diagnostic transactions;
- HT113 register retention at soft-reset entry and cold defaults after a hard
  reset, verified with an instrumented, non-mutating probe.

## Known limitations and next tests

- The HT101SX/HT113 division and some register semantics are reconstructed from
  BIOS traces and related Headland documentation, not an exact data sheet.
- Validate EMS page mapping with trusted software, original diagnostics or a
  physical machine; also establish the GC102-PC parity/NMI wiring.
- Preserve or read the original M5L8042 mask ROM. Until then, the keyboard
  controller is a machine-specific functional profile, not an MCU-level model.
- Compare warm-reset retention with a physical HT113 system.
- Build a broader software-compatibility matrix. Current application-level
  evidence is limited mainly to firmware diagnostics and MS-DOS boot.
- Complete the interactive keyboard matrix, physical Setup chord and Tutorial
  2.2 tests, and validate the official 20 and 40 MB disks in addition to the
  tested 100 MB configuration.
- Re-test `U_MGR` only from a trusted or repaired diagnostic image. The
  preserved module hangs, but the damaged-disk history and historical
  DOS/Barrotes_1310 detection mean that this is not yet evidence of an emulator
  defect.

## Implementation history

- `58cfdcb7d`: initial PCS 386SX port;
- `ad8a8280e`: 8042 `FEh` reset pulse;
- `e68fc4d5e`: Olivetti `AEh` to `3Bh` handshake;
- `776315855`: scan translation with Olivetti PCMODE;
- `c9e04540b`: warm-reset KBC and IOC02 state;
- `c7e3814d0`: IOC02 read-first ready state for the service path;
- `047b4a140`: selector-aware IOC02 writes and 80387SX CMOS state;
- `10a1075f9`: initial HT101SX board-level model;
- `a6b4bc119`: replace HT18/PC87310 and the copied PCS 286 alias with the
  discrete HT101SX + HT113 + GC102-PC, TL16C451FN and WD37C65CJM model;
- `7dd52b481`: validate HT113 state retention across the 8042 warm-reset path.

## Principal references

- machine archive: <https://olivrea.de/olivetti-pcs-386sx/>
- software archive: <https://olivrea.de/software/>
- photographed restoration and board inventory:
  <https://www.jonathandupre.fr/articles/33-ordinateurs-old-school/315-olivetti-pcs-386sx/>
- independent HT101SX/HT113/GC102 board and BIOS evidence:
  <https://www.vogons.org/viewtopic.php?t=86204>
- TI TL16C451/TL16C452 data sheet:
  <https://www.ti.com/lit/ds/symlink/tl16c451.pdf>
