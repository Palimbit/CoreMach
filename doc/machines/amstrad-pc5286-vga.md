# Amstrad PC5286 internal VGA

This experimental device implements a subset of the C&T 82C452 around the
existing 86Box VGA register, planar-memory, rendering and six-bit palette core.
It uses the PC5286 Version 220 option ROM, rather than another VGA's firmware.
The inherited SCAT, KBC and storage remain separate approximations. The
system-BIOS checksum workaround is now restricted to the known dump below;
the cause of the original checksum anomaly remains unknown.

Select **Internal** video to represent LK1 fitted (the manual's default).
Selecting an external card represents LK1 removed: the internal device and
its ROM are not instantiated. No separate jumper setting can contradict that
choice. Firmware must be provided locally; no ROM bytes are distributed.

| Firmware | Runtime filename | Size | SHA-256 |
|---|---|---:|---|
| System AM52V004 | `machines/pc5286/PC5286` | 65536 | `be7b6a2b03e03d1b733d26ad4dd6c64d903d03ce24e56ca4d317b25f6b9d5781` |
| VGA Version 220 | `machines/pc5286/C000.ROM` | 32768 | `cfc27f01deacceccfe06cb0783fafb083a1a610bd879839c828472232004a813` |

## Historical catalogue profiles

The catalogue presents the three configurations documented in contemporary
material: a single-floppy machine without a hard disk, a 40 MB IDE model, and
the 40 MB **Games Pack**. Memory choices are the sold 1 MB configuration and
the documented 2 MB and 4 MB paired-SIMM expansions. The manual's 512 KB map
remains available in the emulator Configure dialog but is not presented as a
commercial model.

The 40 MB profiles create a blank disk only when selected. Its 805/4/26
CP346-compatible geometry is a period-compatible approximation because the
factory drive model has not been identified. The floppy-only profile creates
no unused hard-disk image.

The Games Pack selects the existing generic AdLib device. Contemporary Amstrad
material documents an AdLib-compatible ISA card, two speakers, an AJ-5 analogue
joystick and three bundled games. Independent 3100-015P-4 restoration evidence
identifies a YM3812-class card with an integrated game port, while a PC5286
photograph shows 3100-015P-3. The generic AdLib model provides the compatible
FM interface at 388h but does not model that Amstrad game port or establish
Sound Blaster compatibility.

## MISSING — C&T 82C452 data sheet, printed page 72

We are looking for **printed page 72** (not PDF viewer page 72) of the
**C&T 82C452 Super VGA Graphics Controller Data Sheet, September 1991,
revision 2.1**, publication **DS82**, stock number **010452-002**.
The available copies checked so far jump from printed page 71 to 73.
The register index places these definitions on the missing page:

- **XR0C — Start Address Top**
- **XR0D — Auxiliary Offset**
- **XR0E — Text Mode**

If you have a complete copy, a clear scan or photograph of that page would
help. Please open an issue in the [BluMach repository](https://github.com/BluMach/86Box/issues)
mentioning **PC5286 / 82C452 missing page 72**, with the document edition and
source or link. Including the title/revision page helps identify the copy.

XR0D bit 0 now uses the family-derived fine offset described below. XR0D
bit 1 belongs to the incomplete alternate-timing engine. XR0E bit 1
antialiased text remains unimplemented: the 82C450's register of the same
name has different bits and does not supply a compatible definition.
XR0E bit 0 extended text is already implemented experimentally as described
below; its font-bank selection remains an explicit approximation.

## Evidence and implementation

### Default video during software screen-off

XR28/XR2B now have a bounded output implementation from pp81-82/103.
With SR01 bit5 screen-off, normal BLANK output/polarity (XR28 bits0-1=0)
and XR28 bit2 set, the active region uses XR2B as a direct DAC index,
subject to the DAC mask. XR28 bit3 instead forces RGB black, regardless
of palette entry zero. Conventional active-low DAC blank wiring is an
explicit PC5286 board approximation. CRTC/attribute disable retains the
generic blank output; DPMS remains handled by the core.

The output bypasses VRAM and cursor composition. Character-clock widths
are converted to pixels for downstream clipping, overscan and submission.
Writes to XR28/XR2B request redraw. Other polarity/DE selections and FIFO
underrun remain outside this implementation; no external signal model is
claimed. The blank-video harness passes 65536 selection contracts and
8192 pixel cases, including nonidentity palettes, masks, 8/9/16/18-dot
clocks, forced black, clipping, disable and reset. The device timing callback
is exercised with injected VGA state; complete guest output is not tested.

### Family-derived auxiliary offset

XR0D bit0 adds half a CR13 unit to the regular row increment (four internal
display-address units). The word/doubleword gate follows 82C451 p70;
82C452 p44 identifies the extra bit as less significant than CR13 bit0.
82C450 p71 also assigns bit0 to CR13 and bit1 to XR1E, but describes the
applicable modes as chain/chain4. The selected gating is therefore an
explicit family approximation, not a measured 82C452 result.

The additional increment participates in ordinary row stepping, both
interlaced row steps, and the odd-field offsets at frame start and line
compare. It does not change start-address units, cursor addressing or
FLAG_NO_SHIFT3. Double scanning retains the existing row-advance gate.
Disabling/reset clears the increment. 4096 synthetic timing contracts and
existing memory/text/cursor regressions pass; full-frame timing on hardware
has not been measured. XR0D bit1 is retained for the alternate-timing engine.

The standalone `tools/tests/chips452-scanout.c` also runs the actual
`svga_poll` state machine through four fields for each of 2048 configurations.
It checks addresses delivered to a capture renderer over 393216 active
scanlines: zero/nonzero fine offset, character heights 1-4, double scanning,
interlace, line compare and 256 KiB wraparound. Start and cursor addresses
are checked separately. Host scheduling/output and unrelated renderers are
test doubles; this does not exercise pixels, guest software or physical timing.
Removing any of the ordinary-row, odd-field-start or split fine corrections
causes an assertion failure in separate mutated source copies.

The 82C450 June1993 rev2.0 data sheet p71 resolves the XR0E comparison:
bits0-1 are reserved, bit2 disables cursor blinking and bit3 selects cursor
style. These are not the 82C452 extended-text/antialias bits. The family
summary table identifies register presence, not identical bit definitions.

### VGA state readback

The real-memory SUD harness now also checks CR22 immediately after a real
VGA memory read loads the four latches. Both mono/colour bases and inactive
port rejection pass without changing the latches or attribute phase.

### CMOS storage and offline clear integration

`tools/tests/pc5286-cmos-cycle.py` joins the real NVR load/save/reset code
to the same Qt helper used by the offline CMOS-clear menu, in separate
processes. Generated 128-byte state survives reload/reset, is moved to an
exact backup by the helper, yields fresh CMOS on next load, and is restored
byte-for-byte. Expansion NVR is untouched. This validates storage integration;
it does not exercise the menu click, keyboard delivery or BIOS Setup.

### VGA state readback registers

CR22 reads the existing VGA memory latch selected by GR04 bits 0-1.
CR24 reports the attribute index, palette address source and index/data
phase (bits 0-4, 5 and 7; bit 6 reads zero). Both are read-only, use the
active monochrome/colour CRTC ports and remain accessible when extension
register access is disabled. Reads preserve latch and attribute state.
These behaviors are documented on page 48 of the 82C452 data sheet.
Automatic register tests cover all byte values and four latch selections,
both port bases and extension-access states. They inject VGA state and do
not replace an actual CPU memory-access or guest diagnostic test.

### Setup and CMOS

Use **Action → Send F1** to continue from a BIOS warning, **Action → Send F2**
at the CMOS error prompt, or **Action → Ctrl+Alt+S** during POST. These actions
send keys through the emulated keyboard/KBC, using
the capture override already used by Ctrl+Alt+Del; they do not jump into BIOS
code or modify CMOS. The normal physical keyboard path is unchanged.

`Send F1` emits the AT set-1 make/break code `3Bh`. It was added after both
SendKeys and SendInput failed to cross the host Raw Input/capture boundary in
an unattended1MiB memory-profile run. The menu action successfully continued
that same BIOS warning and reached the boot-floppy diagnostic.

Static inspection of AM52V004 finds the F2 check at ROM offset `97e9` and
the Ctrl+Alt+S path at `b180–b1bc`, gated by the BIOS's temporary `deed` flag.
Both reach offset `8000`. Thus the POST shortcut and error-prompt key are
distinct entry paths. This supports the manual's Ctrl+Alt+S instructions and
the previously observed F2 prompt. The owner later confirmed live Setup entry,
save and disappearance of the CMOS checksum warning; no independent capture
records the selected fields or reset type.

The firmware uses ports `70h/71h`, saves the sum of CMOS `10h–2dh` in
`2eh/2fh`, and contains extended CMOS handling for `40h–7fh`. The machine
retains its 128-byte NVR. A C contract test exercises the existing storage and
register-write helper: dirty marking, preservation across device reset,
save/reload of configuration bytes including the upper half, and rejection
of a truncated file. It does not execute BIOS Setup, test KBC delivery, RTC
timing or prove the motherboard's electrical implementation.

The remaining live check is to record the actual floppy/video/memory fields and
verify their persistence separately across cold and warm cycles. Basic entry,
save and checksum-warning clearance are owner-observed. No CMOS defaults or
checksum bypasses have been added for this machine.

### Post-BIOS SCAT shadow state

The standalone `tools/tests/pc5286-shadow-floppy.asm` diagnostic reads SCAT
registers 40h-4Fh and tests, then restores, one byte in every 16 KiB block from
C0000h through FFFFFh. The generated1.44MB image was rebuilt byte-identically,
checked for its55AA signature and scanned clean before execution.

Unattended captures with recovery CMOS at1MiB and4MiB both report SCAT version04h, ROM enableC0h,
write protect00h, shadow enable4Ah/4Bh/4Ch=00h, extended boundary90h and EMS
control00h. DRAM configuration changes from93h at1MiB to99h at4MiB. All16
C0000h-FFFFFh blocks remain read-only. Thus, for these BIOS configurations,
the POST's **384K Shadow RAM Passed** line accounts for physical memory in the
reserved hole; it does not mean that ROM reads are currently shadowed in RAM.
An owner-saved2MiB configuration provides a second observed state: Setup assigns
512KiB as extended memory and512KiB as EMS at208h/209h, and POST reports moving
both system and video BIOS to shadow RAM. Shadow therefore depends on saved Setup
state. Electrical timing remains outside this check.

With the VM stopped, its manager context menu offers **Reset motherboard
CMOS…**. For PC5286 this models the functional result of the documented LK7
1–2 → 2–3 → 1–2 operation: the existing `nvr/pc5286.nvr` is renamed to a
unique backup alongside it, and the next boot initializes fresh CMOS. Other
devices' state is preserved. Restore while stopped by moving any new CMOS
file aside and renaming the backup to `pc5286.nvr`. The broader Wipe NVRAM
action still removes the entire NVR directory, including these backups.

The new action is shared by machines with the standard AT RTC and a resolved
state filename, including configured BIOS variants. It is disabled while the
manager's VM process is running. File-operation tests cover backup, exact
restore, repeated use, isolation of another device and invalid path rejection.
Electrical discharge timing, post-clear physical byte values and live BIOS
recovery after LK7 remain unvalidated; this is a functional approximation.

### System BIOS compatibility option

Machine Configure exposes **Known PC5286 BIOS checksum workaround** (enabled
by default to preserve the previous boot behavior). Set `pc5286_bios_checksum = 0`
under `[F82C710 UPC Super I/O]` to run the original image unchanged. The legacy
section name and serial/parallel IRQ choices are retained so existing profiles
continue to configure the actual UPC device. This is a PC5286 configuration
descriptor; the generic F82C710 device is unchanged.

When enabled, the workaround requires CRC32 `e33a1151` over all 65536 loaded
bytes and final byte `2c`, then changes only RAM offset `ffff` to `2b`.
CRC32 is a compatibility fingerprint, not cryptographic authentication; the
preservation SHA-256 is listed above. Unknown images are left untouched and
application, opt-out or rejection is logged. No firmware file is rewritten.
This does not correct or clear the separate CMOS checksum/Setup warning.

`tools/tests/pc5286-bios-checksum.c` tests the actual helper with a locally
supplied ROM: opt-out, one-byte correction, repeated invocation, a mutation at
every offset and a two-byte alteration preserving sum8. It embeds no firmware.
The test and full UCRT64 build pass. Two isolated 1 MiB/internal-VGA launches
confirm both configuration branches in logs, but were not visible on the
interactive desktop. Unpatched visual POST and the new Configure UI remain
unvalidated; the earlier visible POST evidence still applies to the old build.

The Amstrad PC5086/PC5286 user manual documents A0000–BFFFF video memory,
C0000–C7FFF video ROM and LK1 (PC Guide 8-9 and 11-9/11-10).
Owner photographs show an F82C452, an ADV476KP35E, eight memory packages and
oscillators marked 25.17M, 28.32M and 32.00M. The following remain hypotheses:

- 256 KiB fitted VRAM, accepted as the working assumption by the user. Eight
  64K×4 devices would provide that capacity; NEC D41464L-10 is not a confirmed
  reading of the photographs.
- Nominal 25.175/28.322 MHz clocks and 32 MHz MCLK. CLK2 is approximated as
  32 MHz; its actual connection is unknown. No claim of accurate advanced-mode
  refresh rates is made.
- Chip revision register 14h is the September 1991 data sheet's production
  revision, not a measured register dump from this board. DIP inputs read zero
  until board straps are known.

The model implements ISA setup at 46E8h, awake/extension enable at 102h/103h,
global ID A5h at 104h, independent extension-port relocation between 3D6/7 and
3B6/7, ROM decode control, VGA apertures, CPU single/dual paging, packed
chain-four addressing, clock division, top start/cursor address bits,
attribute-port mapping and write-protection groups. The option ROM entry at
C000:0003 branches through 0048 to 222C. Static inspection of the routines
around 1603, 1637 and 1681 confirms extension setup and save/restore accesses.
Linear disassembly also decodes data as instructions: only traced entry points
and coherent routines are evidence.

The chip's frame-interrupt pending flag and frame divider are modeled. **No PIC
IRQ is asserted**: the board's IRQ routing has not been established. Do not
infer an IRQ9 connection merely from ISA VGA convention.

CGA/Hercules register emulation, traps,
alternate timings, auxiliary offset/antialiased text, external sync/genlock, bus
alias decoding, contention and cycle-accurate DRAM timing
remain incomplete. Some extension registers only retain values. This is not
a complete 82C452 implementation and must not be described as validated.

### Remaining VGA work and dependencies

| Area | Current limitation | Next requirement |
|---|---|---|
| CGA/MDA/Hercules compatibility | Compatibility registers, alternate timings and traps are incomplete. Register storage alone does not provide these display modes. | Implement the coordinated port, addressing and timing paths; resolve the alternate-offset dependency before claiming complete support. |
| External sync / genlock | Control values are retained; external sync edges and counter delays are not modeled. | Establish the PC5286 connections and external signal source; the register descriptions alone do not establish board support. |
| Frame IRQ | Frame divider and pending status are tested; no PIC line is driven. | Identify the board's IRQ routing. Do not assume IRQ9. |
| Higher display modes | BIOS63h720x540x16 and64h800x600x16 render clean planar bars. The recovered VESA TSR modes are rejected by BIOS220. | Confirm the physical board's dot clocks and refresh; BluMach currently reports56Hz/52Hz. |
| Sliding Unit Delay writes (XR20-XR24) | Experimental modes 0, 1 and 3 implemented; synthetic memory tests pass. | Confirm carry-register readback and masked-plane updates with independent software or hardware. |
| Forced blanking / default video | Normal-polarity software screen-off implemented and pixel-tested; other signal choices incomplete. | Conventional DAC blank wiring remains an explicit board approximation; see default-video section. |

### Sliding Unit Delay writes (experimental)

XR20 bit0 enables SUD in VGA write modes 0, 1 and 3; bit1 chooses left or
right shift, with the count from GR03. Modes 0/3 combine CPU bytes with
XR21; mode1 combines the four memory latches with XR21-XR24. The shifted
byte replaces VGA rotation, then the normal mode-specific set/reset,
logical operation and bit mask apply. Mode1 retains its latch-copy behavior
without those operations. Mode2 and disabled SUD use the ordinary VGA path.

The existing core validates the address, selects writable planes, charges
the byte-write timing and marks VRAM dirty before calling a device-specific
plane-write hook. Other VGA devices initialize that hook to NULL.

**Approximation:** hold registers retain the complete previous input byte.
Mode1 updates all four holds on an accepted write, even for masked planes;
a write with no enabled planes or an invalid address does not consume carry.
Pages 79-80/108 describe carry and alignment but do not establish all of
these readback details. No independent hardware or driver trace validates
this interpretation; memory-content tests are not a silicon fidelity claim.

`tools/tests/chips452-sud.c` includes the real VGA memory code and compares
1,048,576 writes with a separate bit-by-bit reference. It also exercises long
streams, memory-read latch loading, the last VRAM group, extended packed
paging, disabled SUD and rejected aperture writes. Existing register/text/
cursor tests remain applicable. No guest execution is claimed. XR0D bit0 is now implemented as described in the family-derived offset section;
antialiased text remains unimplemented.

XR02 bit6 enables palette accesses at 83C6h-83C9h in addition to 3C6h-3C9h
(data sheet p.67). The PC5286 VGA DAC model exposes the same four registers
at both bases; this does not add an external DAC overlay register bank.
With the bit clear or the adapter asleep, upper-base accesses are ignored
and reads return FFh. XR15 group6 now blocks palette reads as well as writes
at both bases, reflecting PALRD/PALWR suppression (p.75); blocked reads do
not advance the DAC's internal read state.

Entering XR02's separate attribute index/data mapping clears the flip-flop
immediately. It remains clear after either-port writes, as required on p.67.
The existing EGA alternating mapping and normal VGA mapping are preserved.
Device reset now also clears the attribute flip-flop explicitly.

XR20 and XR27-XR37 now discard the reserved bits documented on pages 79-86.
The sliding and sync registers still do not activate those engines.

### Post-BIOS advanced-register observation

An 8086-compatible standalone probe snapshots XR00-XR3F before issuing an
INT 10h mode set and restores the extension index. On the saved 2 MiB PC5286
profile, 3D6h is active and the only nonzero values are XR00=14h, XR04=01h,
XR06=4Ah and XR2D=2Dh. XR29, XR2A and XR30-XR3A are all zero: the BIOS leaves
external sync, frame interrupts and the graphics cursor disabled. XR2D is the
documented external-HSYNC delay and has no active effect while XR29 is zero.

## Experimental graphics cursor

XR30-XR3A now drive a cursor in the standard VGA text and indexed graphics
renderers. A private scanline with an identity palette preserves the original
indices and physical pixel positions. Masked transparency, inversion and the
two replacement colours are composed before the DAC mask and palette lookup.
The shared VGA core is unchanged. While enabled, the background is redrawn
each line so movement, blinking and pattern-only VRAM writes leave no trails.
The private buffer is reused and includes padding for character/panning reads.

Position uses 12-bit X/Y, negative X from the sign flag and low six bits,
32 pixels or horizontal doubling to 64, and 8/16-frame on/off phases from
XR37. Display double scanning does not repeat cursor pattern rows. Reset
disables the cursor and clears its independent blink phase. Status-pin output
and any external DAC overlay wiring are not modeled.

**Addressing is an explicit hypothesis, not verified hardware behaviour.**
The preliminary data sheet describes an inclusive end address on page 84,
but equality termination and two counter increments per line on page 107;
the skipped-plane layout and advertised 512-line maximum do not unambiguously
identify the counter's units. This implementation separates the end counter
from the physical fetch: the start is interpreted as a plane address,
`physical_start = ((XR30 << 8) | XR31) << 4`; each row occupies 16 physical
bytes, fetching ABCD at +0..3 and EFGH at +8..11. Bits are emitted MSB first
in A/C, B/D, E/G, F/H pairs. Fetches wrap at the assumed 256 KiB of VRAM.
The inclusive end-field interpretation gives
`height = 2 * (((XR32 - XR31) & 255) + 1)`, including end-field wrap.
Thus equal start/end fields give two lines and the maximum is 512. These
choices require a known driver trace or physical-chip comparison, including
negative-X endpoint and blink-phase behaviour. Tests establish internal
consistency of this model; they do not resolve the documentary ambiguity.

A standalone 8086 guest demonstration now exercises the complete implemented
path. It enters BIOS mode 13h, places a pattern at physical VRAM 3F000h through
the documented extended packed map, restores XR10/XR0B, and programs XR30-XR3A.
An unattended capture shows a clean 32x32 white outline with a transparent
interior and two inversion rows over sixteen colour bands, without visible
framebuffer corruption. This validates guest I/O, extended mapping and visible
composition in BluMach; it does not resolve the silicon address/end ambiguity.

## BIOS-supported high planar modes

An 8086 boot diagnostic requested60h-65h,6Eh-72h and78h-7Ah through INT10h
on the saved2MiB PC5286 profile. Only63h and64h remain selected; every other
request in that matrix returns to03h. Mode63h reads GR06=05h and XR0E=00h,
establishing a graphics configuration independently of its BDA geometry. The
earlier static inference that the font-loader's60h..65h range made these text
modes was therefore incorrect.

Direct four-plane guest writes produce clean16-colour bars in63h720x540 and
64h800x600. BluMach reports56Hz and52Hz. Those rates are emulator observations,
not physical PC5286 measurements; board clocks and timing remain pending. This
does not add the rejected generic VESA452 TSR modes to BIOS220.

## Validation

The PC5286 RAM selector uses the documented populations 512, 1024, 2048 and
4096 KiB through the common `machine_memory_t.valid` list. Qt presents discrete
choices and configuration loading normalizes unsupported sizes downward to a
supported value (clamped at the ends). The existing 1024 KiB test profile is
unchanged. A missing memory setting uses the generic minimum fallback,
512 KiB; it is not a documented shipping default. Owner observations confirm
that all four populations complete the standalone VGA diagnostic. BIOS reports
show512/640/640/640KiB conventional and0/384/1024/3072KiB above1MiB for the
512/1024/2048/4096KiB selections respectively.

The clean standalone `tools/tests/pc5286-memory-move-floppy.asm` additionally
uses BIOS `INT15h/AH=87h` to save, write, read and restore one word per64KiB
window. Its conventional-memory control passes. The1MiB profile verifies
6 windows /384KiB remapped from the reserved area above1MiB; the2MiB profile
verifies16 windows /1024KiB and the4MiB profile48 windows /3072KiB.
All match `AH=88h`, with no alias observed before the boundary. The identical
diagnostic first passes on a Phoenix AT reference. Earlier v4/v5 failures were
isolated to missing descriptor limits and access rights in the test, rather
than the PC5286. This establishes the modeled extended-memory remap/limits and
a working BIOS A20 path. An additional owner-saved2MiB configuration selects
512KiB extended memory plus512KiB EMS at208h/209h. The standalone
`tools/tests/pc5286-ems-floppy.asm` maps D0000h and D4000h to the same EMS page,
verifies distinct patterns in both directions, and restores the sampled memory,
page registers and SCAT control register. It passes and repeats after a hard
reset. The test exposed and now covers a SCAT bug: global EMS activation enabled
the mapping without changing high-memory window state to internal access.
The recovered C&T SCATEMM1.4.0 driver also loads under FreeDOS on this saved
configuration. A DOS COM probe exercises INT67h functions40h-48h and4Ch:
version40h, frameD000h,0038h total pages and0020h free pages; two-page
allocation, mapping swap, page-map save/restore and release all pass, then
repeat after a hard reset. The32 free pages equal the512KiB EMS selected in
Setup. Other Setup splits,218h/219h, parity and physical SIMM wiring remain
unvalidated.

`tools/tests/chips452-registers.c` tests setup isolation, ROM enable, extension
relocation, immutable ID, page boundaries, protected writes and interrupt
pending/clear. VGA rendering and the memory mapper are test doubles in this
test; it cannot establish BIOS boot or display correctness.
The extended-field tests exercise all 256 byte values in 21 registers,
all 32 frame-divider periods over two acknowledged cycles, disabled interrupt
controls, and reset clearing. They do not test physical IRQ routing.
They also check upper/lower DAC decode, enable/protect combinations, blocked
read side effects and attribute mapping transitions/sequences. The DAC and
attribute-core operations are test doubles in these register-contract checks.

`tools/tests/chips452-cursor.c` additionally includes the real VGA renderers.
It checks all 262144 pattern/mask/background combinations, pattern byte/bit
order and skipped bytes, clipping/zoom, position high bits, assumed end/wrap
boundaries, both blink rates, palette collisions, post-composition DAC masking,
movement without background changes, double scanning and a narrow output
target. Transparent composition is compared with planar 4bpp, packed 8bpp
high/low resolution and 80/40-column text renderer output. Full UCRT64 build
and the test with GCC undefined-behaviour trap instrumentation pass. This
does not execute the BIOS, a Windows driver or physical hardware.

```sh
gcc -O2 -fwhole-program -fsanitize=undefined -fsanitize-undefined-trap-on-error \
    -Isrc/include -Isrc/cpu -Ibuild/blumach/src/include \
    tools/tests/chips452-cursor.c -o build/chips452-cursor.exe
build/chips452-cursor.exe
```

Example UCRT64 command from the source root:

```sh
gcc -O2 -fwhole-program -Isrc/include -Isrc/cpu -Ibuild/blumach/src/include \
    tools/tests/chips452-registers.c -o build/chips452-registers.exe
build/chips452-registers.exe
```

The first build exposed an existing CMake issue: a post-build command for the
executable was attached from the Qt subdirectory. It is moved into the
executable's own directory without changing its runtime-deployment recipe.

Runtime observation on 2026-09-05: the internal device displays AM52V004 POST,
including 640 KiB conventional and 384 KiB extended RAM checks, then stops at
the CMOS checksum/Setup prompt with the initially blank NVR. One toolbar
Ctrl+Alt+Del and one hard reset recover readable video. This establishes
experimental text output and limited reset recovery, not a complete boot.
Host-level synthetic F1/F2 input did not visibly reach the guest; the Windows
Raw Input path may explain this. The later **Action → Send F1** command reached
the emulated KBC and continued the same warning screen. Standard VGA graphics
and saved CMOS have since been observed; advanced graphics, directed cold/warm
CMOS cycles, complete keyboard behavior and OS boot remain unvalidated.

## Sources

- [C&T 82C452 data sheet, September 1991 revision 2.1](https://www.dosdays.co.uk/media/c_and_t/82C452_VGA_Controller_Sep91.pdf)
- [Amstrad user manual archive](https://acpc.me/ACME/AMSTRAD_PRO/AMSTRAD_PC/LITTERATURE/MANUELS/)
- [FreddyV/Banjo firmware provenance](https://banjosmods.wordpress.com/2021/03/18/amstrad-pc5286-bios-dump/)
- [Owner board photographs, post 16](https://www.forosdeelectronica.com/threads/valores-fuente-de-alimentaci%C3%B3n-de-amstrad-pc5286.151834/)
- [ADV476 manufacturer data sheet](https://www.analog.com/media/en/technical-documentation/obsolete-data-sheets/35026349641821151adv476.pdf)
- [IBM PS/2 and PC BIOS Interface Technical Reference, May 1988](https://bitsavers.org/pdf/ibm/pc/ps2/15F0306_PS2_and_PC_BIOS_Interface_Technical_Reference_May88.pdf)
- [C&T 82C235 Single Chip AT data-sheet index](https://www.datasheetarchive.com/?q=82c235)

Canonical preservation record: `amstrad/pc5286`, machine ID `pc5286`.
Restricted firmware, photographs and manuals remain outside the Git worktree.

## Extended text (experimental)

XR0E bit0 enables a row-major font fetch in the existing40/80 text renderers.
BIOS220's loader writes successive characters one plane byte apart, with
256 plane bytes between glyph rows, then enables this bit. Text attributes,
blink,9th-column duplication and cursor composition reuse the VGA renderers.
Reset/disabling restores normal VGA font addressing; writes request redraw.

The data sheet specifies one font. Using the SR03 bank normally selected with
attribute bit3 clear is an explicit approximation, not verified chip behaviour.
Partial BIOS loads retain their actual bank+32*DX addressing; no ROM fixups.
XR0E bit1 antialiasing remains unimplemented; XR0D bit0 uses the family-derived offset.
tools/tests/chips452-text.c covers synthetic font layout, eight banks, all256
characters and32 rows, real text renderers and both cursors, with existing
cursor/register regressions. No guest or physical validation is claimed.

The 82C452 family summary marks XR0D for the 451, but does not mark XR0E
for the 451. The 451 comparison supports only the auxiliary offset here;
it supplies no evidence for the 452 text-mode register.

## Observed guest diagnostic v2, 2026-09-06

Owner-supplied screenshots confirm standalone floppy boot, four64KiB
planar memory PASS with two patterns, clean16-colour bars in BIOS12h,
palette-index bands with black side margins in BIOS13h, and return to
03h text. Tested profile:28616MHz interpreter,1MiB RAM, internal VGA,
256KiB modeled VRAM, AM52V004/C000v220, saved CMOS and RAM checksum
workaround. These tests do not establish physical board VRAM capacity.
Final screenshots show a BIOS banner; completion of subsequent resets
and a repeat run are not yet confirmed. Machine remains experimental.
