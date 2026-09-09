# Engineering the experimental Toshiba T5100

This document explains how BluMach reached a bootable Toshiba T5100 model,
including the dead ends, the evidence that changed the design, and the places
where the implementation deliberately stops. It is not a claim that the
machine has been fully recovered. It is a record of how a useful emulator can
be built when the system firmware survives but several pieces of proprietary
logic and one essential ROM do not.

The short operational reference is [toshiba-t5100.md](toshiba-t5100.md). This
longer account is about engineering method: what we knew, what we observed,
what we inferred, what we approximated, and why.

## The result, stated before the story

BluMach's T5100 is an **experimental, bootable compatibility model**. With the
documented Toshiba V2.30 system BIOS, 2 MiB of RAM, an internal 1.44 MB floppy,
an IDE disk using the CP-3044 type-7 geometry, and a locally supplied IBM EGA
option ROM used as a compatibility input, it can:

- display and operate Toshiba Setup on the emulated internal plasma path;
- complete the modeled POST sequence through code `32`;
- boot Toshiba MS-DOS 3.30 from the integrated hard disk;
- boot the read-only Atlas `TESTCE3` diagnostic floppy and reach its main menu;
- expose 1 MiB as extended memory and a bounded 384 KiB LIM subset; and
- preserve the tested Setup state across a cold power cycle.

That list is intentionally narrower than the real computer. The implementation
does **not** reproduce the original AGS ROM or the complete AGS/CELT display
system. It does not yet reproduce the optional 2 MiB memory card, the upper EMS
allocation, exact Conner drive timing, complete floppy gate-array behavior,
the masked keyboard-controller firmware, or the full reset/resume matrix. The
internal display currently runs standard EGA guest timing at 640x350 rather
than the panel's native 640x400 conversion.

The useful result is therefore not “the T5100 is solved.” It is: “the surviving
firmware now has enough of its documented and observed environment to boot,
and every deliberate substitution is named.”

## Evidence vocabulary

Four words carry specific weight throughout this account:

- **Documented** means a Toshiba publication, component data sheet or other
  identified source describes the behavior or configuration.
- **Observed** means it was seen in static firmware analysis, an instrumented
  run, a guest diagnostic, or a persisted disk/NVR result.
- **Inferred** means several facts support the conclusion, but neither a source
  nor a hardware trace proves the exact electrical implementation.
- **Approximate** means BluMach knowingly supplies compatible behavior without
  claiming that the original circuit worked the same way internally.

These labels matter because an emulator can pass a BIOS test for the wrong
reason. A returned byte may satisfy one conditional while still being a poor
model of the device that produced it. Booting is strong functional evidence;
it is not automatic evidence of fidelity.

## Why this machine was unusually difficult

At first glance the T5100 looks like a 386-class AT compatible: a 16 MHz
80386DX, an optional 80387, floppy and IDE storage, EGA-compatible graphics and
MS-DOS. That description is accurate commercially and misleading technically.

The machine surrounds familiar PC functions with Toshiba-specific logic:

- a second keyboard-controller endpoint at `8060h/8064h`;
- system-control registers at `8080h–808Fh`;
- eight LIM page registers spread across `0208h/0218h` and high aliases;
- an FDC gate path that shares the usual `3F7h` neighborhood with IDE;
- a proprietary AGS graphics adapter and CELT plasma translation logic; and
- a separate 32 KiB AGS ROM that the system BIOS expects at `C0000h–C7FFFh`.

The V2.30 system BIOS survived as two ROM lanes. The AGS ROM did not. The
maintenance documentation describes boards, memory maps and service behavior,
but it is not a programming specification for every gate array. Several
controllers are mask-programmed devices whose firmware is also unavailable.

This creates an awkward boundary. A generic AT assembled from convenient
devices can execute plenty of instructions, but it is not a T5100. Conversely,
waiting for every missing ROM and gate-array manual would leave a historically
important machine permanently unavailable. The project therefore chose a
third course: implement only evidence-bounded contracts, reuse generic cores
only behind machine-specific routing, and label the result experimental.

## Recovering a usable specification from the BIOS

### Reconstructing the 64 KiB system image

The V2.30 firmware consists of two 32 KiB EPROM dumps labelled EVEN `042F` and
ODD `043F`. Static analysis first interleaved them as:

```text
output[2 × i]     = EVEN[i]
output[2 × i + 1] = ODD[i]
```

The resulting 64 KiB image occupies `F0000h–FFFFFh`, has an eight-bit sum of
zero, identifies the machine and V2.30 revision, and contains a valid reset
vector. Reset jumps from `FFFF:0000` to `F000:E05B`; a relative branch then
wraps the 16-bit instruction pointer to `F000:0062`.

This did more than confirm that the two files belonged together. It converted
an archive of uncertain utility into a coherent executable artifact with a
known layout and reproducible integrity checks. It still did not prove the
historical provenance of the physical chips, so the preservation record keeps
“internally coherent” separate from “authenticated from a documented board.”

### Treating firmware as a behavioral witness

The next step was not to emulate every I/O instruction found by a linear
disassembler. BIOS images contain data tables, padding and embedded strings
that can decode into plausible nonsense. Instead, the analysis followed the
candidate reset path, recorded direct control flow, and inspected I/O accesses
in context.

That process exposed a set of concrete contracts:

| Area | Firmware evidence | Minimum consequence |
|---|---|---|
| Primary KBC | standard `60h/64h`, self-test and command traffic | reuse an AT KBC core, but do not assume it covers Toshiba's second controller |
| Secondary KBC | commands `BBh` and `B4h` through `8060h/8064h` | provide a distinct endpoint with correct status/data consumption |
| System control | read/modify/write traffic at `8084h`, plus accesses in `8080h–808Fh` | values must be coherent; unexplained bits must not gain invented side effects |
| LIM memory | eight registers, four 16 KiB frame slots at `D0000h–DFFFFh`, destructive pattern tests | pages must be independent memory, not write-only port placeholders |
| Hard disk | ATA PIO at `1F0h–1F7h`, alternate status/control, diagnostics and identify paths | use the ATA command engine behind T5100-specific port ownership |
| Floppy | uPD765-family command traffic plus Toshiba control at `3F7h` | reuse the compatible command core, preserve the Toshiba gate boundary |
| Video | `AGS` signature at `C000:000A` and indirect handoff through `C000:3FF0` | system BIOS alone cannot supply internal video |
| POST | stage bytes mirrored to `378h` and `3BCh` | observe progress before working video exists |

This table became the first implementation plan. Importantly, it also said
what not to do. A constant value for every unknown I/O read might move POST
forward, but it would erase the distinction between a discovered contract and
a guessed device.

## The first machine skeleton

The initial platform deliberately used existing, tested 86Box infrastructure
for the portions that were genuinely compatible:

- the 80386DX execution core at the documented 16 MHz;
- standard AT interrupt, DMA and timer infrastructure;
- the AT RTC/NVR core;
- the normal AT keyboard path for the primary controller; and
- initially, external CGA as the system BIOS's documented fallback display.

Machine-specific behavior lived in `m_at_t5100.c` instead of being scattered
as `if (machine == t5100)` conditions through unrelated devices. This kept the
approximation visible and made it possible to tighten one subsystem without
pretending that the others had become exact.

### The second keyboard controller

The maintenance manual's hardware-status table documents `8Ch` for the tested
configuration: plasma fitted, 16 MHz CPU, one internal 2HD/1.44 MB drive,
normal A/B assignment and no external drive. BIOS analysis showed two commands
needed during the exercised paths:

- `BBh` returns `00h` for the observed normal POST path; bit 2 is treated by
  the BIOS as an error or Fn condition.
- `B4h` returns the documented hardware-status byte `8Ch`.

The model gives `8064h` a one-byte output-buffer status and `8060h` the pending
data. Reading the data consumes it; an empty read returns `FFh`. Unknown
commands are not acknowledged. This is enough for the observed firmware
contract, but it is not an emulation of the missing 8742 program. In
particular, no unobserved Fn-key protocol is manufactured from the two known
responses.

### System-control latches

The BIOS performs coherent read/modify/write sequences against `8084h` and
touches neighboring Toshiba registers. The initial model therefore stores and
returns sixteen bytes for `8080h–808Fh`.

Calling these bytes “latches” is deliberate. Their electrical effects are not
known in full. The real `8084h` bit 4 participates in AGS ROM selection; the
current compatibility ROM is mapped for the boot interval rather than being
electrically gated by a faithful reconstruction of that bit. Other unknown
bits do not secretly alter clocks, memory or power state. Coherent storage is
better than arbitrary constants, but it remains an approximation.

### A bounded LIM implementation

The T5100 exposes a 64 KiB LIM frame at `D0000h–DFFFFh`, divided into four
16 KiB slots. The BIOS addresses eight page registers:

```text
0208  4208  8208  C208
0218  4218  8218  C218
```

The low group and high group can select the same four frame slots. Bit 7
enables a mapping and the remaining bits select a page. Static analysis showed
the BIOS writing and rereading complete 16 KiB regions with several patterns,
which ruled out a superficial register-only implementation.

BluMach backs the 24 base pages that the V2.30 BIOS directly exercises. Each
selected slot points to independent RAM; disabling or selecting an unavailable
page removes the mapping and reads as `FFh`. Bank 1 and pages 24 and above are
left absent because the optional card's physical decode is not documented.

This compromise has a visible consequence: a configuration that allocates a
full megabyte as expanded memory cannot work honestly. Later guest probes
confirmed exactly that boundary, so the tested 2 MiB profile instead assigns
1 MiB to extended memory and retains the 384 KiB base LIM area. The v1 machine
selector is restricted to 2 MiB. Although the real machine accepted an
optional 2 MiB card, exposing 4 MiB before modeling its mapping would turn a
documented option into a fictional implementation.

### POST logging before video

Toshiba mirrors POST bytes to both common printer bases, `378h` and `3BCh`.
When experimental tracing is enabled, the T5100 device observes those writes
without replacing an attached parallel device. A bounded sampler also records
CPU position, BIOS data-area video mode, reported memory and the last POST
code.

This instrumentation was critical while the screen did not exist. It is not a
guest bypass: it neither changes the ROM nor chooses successful branches. The
more invasive temporary probes used during investigation—scheduled key input,
text-VRAM capture and explicit early NVR saves—were removed from production
after their observations were recorded.

## Why POST success did not mean the floppy worked

The first skeleton reached POST code `32`, entered `INT 19h`, and then failed
to boot a known Toshiba DOS 3.30 disk. Disabling IDE made no difference. The
firmware waited in its keyboard/error path at `F000:E853`.

At that point the generic AT FDC had passed enough initialization to look
plausible. It was not sufficient for an actual disk read. The Toshiba TC8565
implements the same fifteen-command programming model as the uPD765, but drive
selection, motor control, data rate and disk-change behavior are external to
that command core. The existing Toshiba T1x00 FDC device already modeled the
relevant family boundary, including Toshiba's use of `3F7h`. Reusing that core
was better supported than creating a new controller from its name alone.

The compromise is explicit: command compatibility and known Toshiba gate
semantics are reused; exact T5100 FDC-GA/VFO timing and firmware are not
claimed. Floppy boot now works, but write behavior remains outside the v1
validation claim.

## The hard-disk controller that was not a separate card

Early discussion referred to a possible “hard-disk I/O card.” The board and
service evidence instead place the disk connection on the T5100 system board.
The design therefore does not invent a proprietary ISA adapter. It instantiates
the common ATA command engine as a T5100 integrated device.

That distinction exposed a subtle port collision. A generic primary IDE device
normally registers `1F0h–1F7h` and both bytes of its side-port region. On this
machine, the ATA alternate-status/device-control register is at `3F6h`, while
`3F7h` belongs to the floppy gate logic. Letting both generic devices claim the
range made ownership ambiguous and could route a single BIOS operation to the
wrong subsystem.

`ide_t5100_device` therefore uses the common ATA engine but registers only:

- command/data registers `1F0h–1F7h`;
- alternate status and device control at `3F6h`; and
- IRQ 14.

It deliberately leaves `3F7h` to the Toshiba floppy device. Instrumented
V2.30 execution then observed ATA commands `70h`, `90h`, `91h`, `10h` and
`40h`; no IDE access reached `3F7h`, while the BIOS data-rate write
`3F7h=02h` reached the FDC path. That observation validated the host routing,
not the internal design or timing of a Conner drive.

For functional testing, a blank 42,649,600-byte image used 980 cylinders,
5 heads and 17 sectors, matching the documented CP-3044 type-7 BIOS geometry.
The Setup screen displays the highest cylinder index, `979`, which initially
looked like an off-by-one discrepancy but is consistent with 980 cylinders
numbered from zero.

The generic disk model can use a CP-3044 timing identity, but BluMach does not
claim to reproduce its exact firmware, defects, acoustics or command latency.

## The apparent boot stall was partly a Setup problem

Once storage routing was credible, the machine still appeared to wait before
booting. Temporary keyboard and CMOS instrumentation showed that two separate
issues had been conflated:

1. `F1` alone entered Toshiba Setup; `Alt+F1` acknowledged the configuration
   warning and continued. What looked like a silent FDC failure could be a
   firmware wait for operator input.
2. Saving default CMOS cleared the immediate checksum/configuration flag, but
   the next cold POST raised a video-related warning again because internal AGS
   was absent.

Selecting the documented fallback `Internal Disable / External CGA (80×25)`,
saving with `F10`, confirming with `Y`, and power-cycling produced a stable
external-video profile. Toshiba DOS then booted without a warning bypass.

That external path was strategically important. It allowed the team to
validate floppy, IDE, DOS, NVR persistence and memory while keeping the missing
internal display isolated as its own blocker. It would have been a mistake to
interpret the fallback as completion of the plasma model.

## Proving storage with a period operating system

The hard disk was not considered working merely because IDENTIFY or a sector
read completed. Toshiba DOS 3.30 was booted from a read-only floppy and used to
exercise the blank type-7 image:

1. `FDISK` recognized fixed disk 1.
2. It created and activated a primary partition beginning at LBA 17.
3. DOS 3.30 limited the partition to 65,518 sectors, just under 32 MiB.
4. `FORMAT C: /S` completed and transferred the system.
5. A proof file was written and read back.
6. The floppy was removed and a fresh emulator process booted from `C:`.
7. The proof file survived and contained the expected text.

Only a disposable, initially blank working image was writable. Preserved
images and original media remained immutable. The installed result was scanned
and retained locally as validation evidence, not added to Git.

This test establishes a useful vertical slice: BIOS geometry, ATA host path,
partitioning, formatting, persistent writes and a second boot agree. It does
not establish cycle accuracy or a bit-for-bit reconstruction of the factory
disk.

## Memory validation: learning from a failure boundary

Toshiba's own software became a second specification. `TEST3` identifies the
T5100 and reports configuration information. Two small project-authored DOS
probes were added because a report from Setup is not the same as usable memory.

### LIM probe

The LIM probe saves, marks, compares and restores two words on every explored
bank-0 page through `0208h/D000h`. Pages 0–23 passed. Pages 24–87 did not exist;
the first failure was page `0018h`, exactly 24 decimal.

That is a partial pass and a valuable failure. It confirms the implemented
384 KiB boundary and demonstrates that the earlier 1 MiB expanded-memory
selection over-promised what the model could deliver. It does not validate all
four slots, an EMS driver, parity or every cell.

### Extended-memory probe

The final Setup profile selects 1 MiB extended memory, 384 KiB LIM and disables
fast ROM. A second probe asks `INT 15h/88h` for the available size, then uses
`INT 15h/87h` to save, write, compare and restore 16 distinct words at 64 KiB
boundaries from `100000h` through `1F0000h`.

All 16 samples passed in both the interpreter and dynamic recompiler. `TEST3`
independently reported 1,024 KiB extended memory. This remains sampled
coverage, not an exhaustive RAM or timing test, but it is sufficient to prefer
the extended-memory profile over a knowingly broken large-EMS profile.

## Diagnostics: useful even when they do not say “PASS”

The recovered Toshiba diagnostic sets were handled as local, scanned software,
not as redistributable test fixtures.

`TEST3` version 6.00 identifies the machine and exercises configuration and
operator-acknowledged display sequences. Disk and printer tests were declined
to protect media and avoid unsupported destructive scope. No complete TEST3
pass is claimed.

`XCHAD` reported `not available on this machine` on the external-CGA profile.
That failure was not patched out. It was consistent with the absent AGS path,
but its exact rejection predicate was not traced, so it remains an observation
rather than proof of which register or ROM check failed.

Atlas `TESTCE3` 4.30 later provided a service-style menu. Its protected-mode
memory subtest reached three passes and zero errors with the 1 MiB extended
allocation. The looping run was stopped from the host after `Ctrl+C` did not
end it. A ROM-checksum subtest returned to its menu, but no final counters were
captured, so no checksum pass is claimed.

After internal compatibility video was added, the same read-only TESTCE3 floppy
reached its main menu on that path. This proves that the internal route can
carry a real boot and readable diagnostic interface; it does not validate
every display pattern or AGS feature.

## The missing AGS ROM: the central design decision

### What survived and what did not

The maintenance manual identifies a separate 32 KiB AGS ROM and describes an
AGS/PEGA2 display subsystem with 256 KiB VRAM, 2 KiB SRAM and four plasma
levels generated through CELT. Static analysis shows that the system BIOS:

1. selects segment `C000h`;
2. requires the ASCII signature `AGS` at offset `000Ah`;
3. sets bit 4 at `8084h`;
4. constructs a nonstandard pre-RAM continuation frame; and
5. jumps through a far pointer stored at `C000:3FF0`.

The two V2.30 system-ROM lanes do not contain this firmware. Toshiba T3200 AGS
ROMs exist, but their presence does not make them T5100 firmware. Substituting
one would hide the most important missing artifact behind a plausible brand
name.

### Why a generic IBM EGA ROM was chosen

The project eventually accepted a v1 compatibility path because no further
documentation or authentic AGS dump could be obtained. The goal was not to
forge a Toshiba ROM. It was to provide standard EGA INT 10h services while
satisfying only the handoff contracts actually observed in the T5100 BIOS.

An identified IBM EGA option ROM is therefore a required **local compatibility
input**. It is attractive for precisely the opposite reason that a T3200 ROM
would be: it makes the substitution obvious. Standard EGA service behavior is
being borrowed; Toshiba firmware identity is not.

No ROM bytes are committed. The source file remains unchanged on disk. After
loading, BluMach adapts its private in-memory copy:

- bytes `000Ah–000Ch` become the `AGS` signature;
- offset `3FD0h` receives a single shared `RETF` instruction;
- the far-pointer slots at `3FE0h`, `3FE4h`, `3FE8h`, `3FECh`, `3FF0h` and
  `3FF4h` point to `C000:3FD0`;
- the normal option-ROM entry at offset `0003h` is left intact; and
- the checksum byte at `3FFFh` is recalculated across the 16 KiB size declared
  by the IBM ROM header.

The distinction is important. This is not a newly authored AGS BIOS and not a
derived firmware file distributed by BluMach. It is a runtime shim around a
local, separately identified EGA ROM.

### The second pointer discovered by failure

The first attempt implemented only the statically recognized `3FF0h` handoff.
POST reached code `27`, then appeared to wander into `0000:0000`. Periodic CPU
samples repeatedly landed around `F000:EF6F`, which initially resembled a
memory-test stall. Disassembly showed that address was an interrupt handler,
not the cause.

Tracing the internal-display path exposed another indirect call from
`F000:399D` through `C000:3FF4`. With an uninitialized pointer, execution had
exactly the observed destination. Adding a second conservative return allowed
POST to continue through `32`, made Setup legible and enabled both hard-disk
and floppy boots.

This episode is why the implementation record includes failed hypotheses. The
fix was not “make POST 27 go away”; it was “identify the missing firmware
contract whose absence explains the control transfer.” A constant success code
or a forced jump in the system BIOS would have produced less evidence and a
more fragile emulator.

## A PEGA2-shaped display wrapper, not recovered AGS/CELT

Firmware handoff alone cannot draw a screen. The T5100 device wraps the mature
generic EGA register, planar-memory, renderer and INT 10h behavior with the
small amount of Toshiba structure supported by available evidence:

- EGA is fixed as the machine's internal display rather than selectable as an
  unrelated expansion card.
- The wrapper allocates 256 KiB of EGA VRAM and a separate 2 KiB SRAM aperture.
- SRAM becomes visible at `A0000h–A07FFh` for the observed AGS control values
  `40h`, `41h` and `50h`.
- Toshiba's extension access requires two consecutive byte reads from the
  active mode-control port (`3B8h` or `3D8h`); only the immediately following
  access is treated as unlocked.
- A small observed subset of control and extended CRTC writes is retained;
  unknown selectors are logged when tracing is enabled rather than guessed.
- The EGA output palette is reduced to four ordered orange levels.

The palette values are presentational RGB choices. The manual establishes four
CELT levels, not their modern sRGB transfer curve, phosphor response or room
appearance. BluMach therefore claims the ordering and reduction to four levels,
not calibrated colorimetry.

The timing callback preserves generic EGA counters. It does not yet reconstruct
how AGS/CELT expanded a 350-line EGA image onto the 640x400 panel, nor how it
handled fonts, line repetition or blanking. The model reports special ISA video
timing and uses a panel-like pixel aspect, but the guest mode remains 640x350.

### The display keys are a BIOS transaction, not a renderer shortcut

TECHaccess documents `Fn+End` for the external display, `Fn+Home` for the
plasma panel and `Fn+Down` for the 350/400-line choice. Static analysis of BIOS
V2.30 supplied the missing mechanism. Its timer service reads the low nibble of
port `8066h` twice, rejects an unstable value, dispatches through an AGS far
pointer selected by that notification and finally writes command `BCh` to
`8064h`. Notifications `01h`, `02h` and `09h` lead respectively to the external,
line-mode and internal handlers.

The implementation preserves that ordering. Right Ctrl represents the Toshiba
`Fn` key; the three documented key combinations publish a stable notification,
but do not immediately change the display. Only the BIOS acknowledgement
commits the requested state. This boundary matters: a direct host-side palette
toggle would look convincing while silently bypassing the firmware behavior we
actually recovered.

Because the AGS ROM and output circuitry remain unavailable, the committed
state is intentionally presentational. External mode restores the generic EGA
RGB palette and aspect; internal mode restores the four-level plasma palette.
`Fn+Down` changes host pixel geometry to approximate the 350/400-line choice
without inventing new guest scan counters. Exact duplicated-line placement,
connector gating, electrical output and the documented `Fn+Right` font change
remain outside the claim.

The deterministic platform test verifies stable `8066h` reads, delayed commit,
the `BCh` acknowledgement boundary, all three notification values, repeat-key
suppression and the six compatibility-ROM far pointers. A Qt 6 UCRT64 build also
passes. A disposable interactive run reached the normal video route but its
launcher exited with code 127 before the timed keys were delivered, so this
revision does not claim an observed live BIOS hotkey round trip.

This is the largest compromise in the machine. It is also the most visible, so
the UI, firmware selector, logs and documentation all call it compatible and
experimental. A future authentic AGS implementation must replace this path by
evidence, not quietly accumulate exceptions inside it.

## What was reused, what was new, and what remains a placeholder

| Component | v1 treatment | Honest boundary |
|---|---|---|
| 80386DX execution | existing core, fixed at 16 MHz | bus/chipset timing not measured |
| AT PIC/DMA/PIT | existing AT platform infrastructure | Toshiba T4758 integration not modeled at register/timing level |
| Primary keyboard | generic Toshiba-parameterized AT KBC | exact primary masked firmware unavailable |
| Secondary keyboard | bounded `8060h/8064h/8066h` endpoint | display notification/acknowledgement is modeled; masked firmware and other commands remain unavailable |
| RTC/NVR | generic AT RTC with 64-byte mask | model-specific shutdown/resume semantics incomplete |
| System registers | sixteen coherent latches | unknown side effects and true AGS gating omitted |
| Base LIM | four slots and 24 independent pages | upper EMS and optional card absent |
| Floppy | Toshiba T1x00 TC8565-compatible path | T5100 FDC-GA/VFO timing and writes incomplete |
| Hard disk | common ATA engine with T5100 port routing | exact CP-342/3044/30104 identity and timing incomplete |
| Video firmware | IBM EGA ROM adapted only in memory, including conservative returns for six observed AGS far slots | not Toshiba AGS firmware |
| Video hardware | EGA/256 KiB/2 KiB wrapper and four-level palette | not exact AGS/CELT or 640x400 conversion |

This table is more important than a binary “supported” label. It identifies
where a future patch should refine an existing boundary and where it must
replace an approximation entirely.

## Validation as a ladder, not a single screenshot

The implementation was tested at several levels because each catches a
different class of false success.

### Host-side contracts

`tests/t5100_platform_test.c` directly checks:

- rejection of invalid compatibility-ROM inputs;
- insertion of the `AGS` signature;
- both far pointers and their `RETF` target;
- repaired option-ROM checksum;
- secondary-KBC status, data consumption, `BBh` and `B4h`;
- system-latch readback;
- independent LIM page mapping and disable behavior; and
- the passive POST observer.

These tests are fast and deterministic. They do not execute the Toshiba BIOS
or prove that a register sequence is historically complete.

### Guest-authored probes

The DOS LIM and extended-memory probes verify that mappings survive the entire
emulated CPU/BIOS path. They preserve the words they touch and report bounded
counts. They deliberately avoid resident EMS/XMS managers. Their source is in
`tests/`; generated COM files and working diagnostic media are not committed.

### Original firmware and period software

The strongest functional checks use unmodified Toshiba V2.30 system firmware,
Toshiba DOS 3.30 and recovered Toshiba diagnostics in local disposable
profiles. The meaningful milestones are not merely “a window appeared”:

- Setup accepts and persists the selected state;
- POST progresses through its own stage codes;
- DOS partitions and formats the modeled disk through BIOS/ATA paths;
- written data survives a fresh process;
- hard-disk boot succeeds without the floppy; and
- TESTCE3 boots read-only through the internal display path.

The production smoke run was repeated after temporary key injection and VRAM
capture code had been removed. That check guards against accidentally shipping
the diagnostic scaffolding as part of the machine.

### Full project checks

The T5100 callback test, catalogue assembly, catalogue unit tests and the full
UCRT64/Qt6 build pass on the current mainline. The canonical machine audit also
reports no custody/schema errors. A clean preservation audit means the record
is internally consistent; it does not elevate the emulation from experimental
to validated.

## Approaches deliberately rejected

Several shortcuts could have produced a faster screenshot. They were rejected
because they would make later fidelity work harder:

- **Do not borrow the T3200 AGS ROM.** It is Toshiba and related in concept,
  but no evidence makes it the T5100's firmware.
- **Do not patch the system BIOS around hardware checks.** The model supplies
  the observed device contracts and lets the original control flow decide.
- **Do not distribute a modified EGA image.** The compatibility transformation
  is private and in-memory; the required source remains a local input.
- **Do not let generic IDE own `3F7h`.** Port ownership is part of the machine,
  not an incidental emulator implementation detail.
- **Do not advertise 4 MiB because the chassis supported it.** The optional
  card's decode is not modeled; v1 exposes only the tested 2 MiB profile.
- **Do not call a diagnostic menu a complete diagnostic pass.** Individual
  observations and untested subtests remain distinct.
- **Do not convert an attractive orange palette into a color-accuracy claim.**
  Four levels are documented; the chosen RGB values are presentational.

## Public and private evidence boundaries

The BluMach source tree contains code, tests, hashes, expected filenames and
links to lawful source locations. It contains no Toshiba ROM bytes, IBM EGA ROM
bytes, DOS image, diagnostic executable, installed hard disk, NVR snapshot or
maintenance-manual scan.

Those local inputs are catalogued in the separate preservation record with
their provenance, hashes, rights and security status. Originals are immutable;
derived and generated evidence records the tool and source chain. Public
documentation describes what was tested without turning restricted evidence
into an accidental software distribution.

## Implementation map

The public change is intentionally small enough that each compatibility
boundary has an obvious home:

| File | Responsibility |
|---|---|
| [`src/machine/m_at_t5100.c`](../../src/machine/m_at_t5100.c) | system ROM loading, in-memory AGS compatibility handoff, EGA/plasma wrapper, BIOS-mediated display selection, secondary KBC, Toshiba latches, LIM window and optional tracing |
| [`src/device/keyboard.c`](../../src/device/keyboard.c) | routes the three supported Toshiba `Fn` combinations to the active T5100 before ordinary scan-code delivery |
| [`src/machine/machine_table.c`](../../src/machine/machine_table.c) | 386DX16 registration, fixed internal video and the supported 2 MiB v1 limit |
| [`src/disk/hdc_ide.c`](../../src/disk/hdc_ide.c) | integrated primary ATA route that leaves `3F7h` to the floppy gate |
| [`tests/t5100_platform_test.c`](../../tests/t5100_platform_test.c) | deterministic host contracts for ROM adaptation, KBC2, latches, POST and LIM mapping |
| [`tests/t5100_ems_probe.asm`](../../tests/t5100_ems_probe.asm) | bounded guest probe for the bank-0 LIM page boundary |
| [`tests/t5100_xmem_probe.asm`](../../tests/t5100_xmem_probe.asm) | sampled INT 15h extended-memory move/read/write/restore probe |
| [`src/qt/catalog/source/machines/toshiba/toshiba-t5100/`](../../src/qt/catalog/source/machines/toshiba/toshiba-t5100/) | five-language historical sheet and declarative creation profile |
| [`doc/machines/toshiba-t5100.md`](toshiba-t5100.md) | concise operational configuration, firmware and limitation reference |

The common CPU, EGA, ATA, FDC and AT-platform cores remain shared. The T5100
files configure, wrap or route them; they do not duplicate those mature
implementations under Toshiba-specific names.

## Reproducing the supported v1 profile

The catalogue template creates the intended baseline:

- Toshiba T5100 machine ID `t5100`;
- Intel 80386DX at 16 MHz, interpreter by default;
- 2 MiB RAM and no coprocessor;
- integrated video, starting on the internal plasma presentation and switchable
  at run time with right Ctrl as `Fn`;
- Toshiba internal floppy path with one 1.44 MB drive;
- T5100 integrated IDE path; and
- a blank 980/5/17 image using the CP-3044 timing profile.

Local V2.30 EVEN/ODD ROM lanes and the identified IBM EGA compatibility ROM
must be available under the filenames shown in the operational reference.
On first boot, Toshiba Setup must select:

- `Internal EGA compatible / External MDA or None`;
- `High resolution`; and
- hard-disk type `7` for the generated image.

Save with `F10`, confirm with `Y`, then power-cycle. DOS and diagnostic media
are not supplied.

## What would justify a v2

The compatibility path has a clear retirement plan. The highest-value inputs
would be:

1. a documented dump of AGS ROM IC7 (`TC57256AD-20`);
2. dumps or behavior traces for both keyboard controllers;
3. the T5100 Gate Array Specification Manual, or equivalent hardware traces
   for MCNT2, BCNT, BDRV, FDC-GA and AGS;
4. measurements of 640x400 panel conversion, gray-level transfer, AGS output
   gating and the physical external-video signal;
5. optional-memory-card traces sufficient to map all EMS/extended modes; and
6. broader floppy write, reset, resume, diagnostic and I/O validation.

An authentic AGS dump would not automatically make the current wrapper exact.
It would provide new executable contracts, which would then need to be traced
against registers, VRAM, SRAM, timing and display output. The v1 code should be
treated as a compatibility scaffold whose approximations can be removed one by
one, not as a description of undocumented Toshiba silicon. It would also make
it possible to replace the conservative hotkey returns, investigate
`Fn+Right` font selection and validate the exact display-side effects rather
than only the recovered BIOS transaction.

## Final assessment

The T5100 became bootable through a sequence of narrower discoveries rather
than one grand hardware model: reconstruct the BIOS, listen to its POST, model
the second KBC, provide real LIM pages, separate `3F6h` from `3F7h`, understand
the Setup warning, prove DOS storage, measure the memory boundary, and finally
satisfy both AGS handoffs around a clearly identified EGA compatibility core.

The most important implementation feature is not that DOS reaches a prompt. It
is that the route to that prompt remains inspectable. We know which behaviors
come from surviving Toshiba firmware, which come from reusable PC-compatible
cores, which were observed only in tests, and which are deliberate stand-ins.
That makes the experimental machine useful today without making tomorrow's
more faithful implementation inherit an undocumented fiction.
