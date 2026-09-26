# PCS286: real-BIOS probe on the portable engine

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

`pcs286-boot-probe` runs an external128KiB BIOS from the actual 80286 reset
vector using the existing portable components. BIOS1.42 now executes locally;
it now proceeds beyond Olivetti KBC80h/84h/CFh, RTC calendar update and
video-status polling with explicit initialized-calendar and PVGA1A options.
Framebuffer capture and scheduled keyboard input are now available; see the latest
console result below. POST completion, Setup and DOS boot remain unvalidated. This is
a bounded diagnostic executable, not the public machine factory.

The [BIOS 1.42 execution map](pcs286-bios-map.md) separates observed,
read-only-inspected, inferred and pending routines. It is deliberately not a
complete disassembly; it grows from real portable-machine boundaries.

## Adopted functional composition

The runner reuses CPU, corrected classic GC103, owned RAM/ROM, AT adapters,
PIC, DMA registers, board control/services, PIT, RTC, keyboard/controller pair,
refresh, port61, the existing SPP and optional existing PVGA1A. No test fixture or firmware is compiled in.
Its component graph is released on exit, including initialization failures.

Optional `--floppy PATH` reads one exact raw 1.44MiB image into caller-owned
memory and exposes it through a write-protected `bm_floppy_drive_t`; the
WD37C65 receives no host path or file handle. Wrong sizes reject before
execution. This boundary is suitable for the canonical clean/local-only
`msdos-330a` input after verifying its manifest hash. The asset remains outside
Git, and no writable overlay is implemented yet.

| Area | Explicit solution and limitation |
|---|---|
| Memory | Existing legacy1/2/3/4MiB profile; external-memory FF holes and ignored ROM/shadow writes. No diagnostic alias or ROM patch. |
| Reset | Initial A20 enabled and KBC outputC3h so FFFFF0h reaches high ROM. Provisional board input, not verified power-on wiring. Subsequent A20/reset uses the board owner; no reset-vector special case. |
| Time | Named `provisional-boundary-quantum`: default1000ns peripheral advancement after each non-failing step attempt, including HALT/HOLD. REP/refresh-only attempts may count. CPU timing stays UNKNOWN; strict clock API unchanged. PIT1193182Hz, RTC32768Hz, prior synthetic KBC/keyboard1.5MHz profile. No measured instruction duration claimed. |
| RTC | Default depleted128-byte CMOS remains a failure-diagnostic choice; explicit `--cmos initialized` seeds only a valid BCD calendar (1980-01-01 Tuesday,01:00:00,A60h/B82h,VRT1), with other bytes zero. `--cmos-file` loads an exact external image read-only. A future machine factory needs a deterministic configuration-derived default, including its checksum; the calendar-only seed is not that profile. No host-date/checksum repair occurs. Probe selects CLASSIC_STOP: retain every DV encoding, run only DV010, otherwise clear/stop phase and UIP. |
| KBC | Empty60h reads return the retained output latch, initially0, as in classic AT. No new byte, ACK, IRQ or pending-reply consumption; callback errors still stop. |
| KBC vendor commands | Explicit `OLIVETTI_PCS286` profile adds80h/84h raw latch read/write and CFh without reply/output change.84h does not drive physical outputs; D1 also records its accepted byte, D0/pulses retain AT behavior. Classic polling/P1 side effects excluded. No firmware-PC condition or fabricated diagnostic result; authentic controller ROM and physical confirmation remain absent. |
| LPT/checkpoint | Existing SPP378h–37Ah, no printer. Port80h is a private board-owned byte latch: reads return the last accepted write and CPU-only reset leaves it intact. BIOS1.42 itself exercises that retention. Physical IOC02 ownership and reset timing remain unqualified; the probe never fabricates a pass value. |
| Unclaimed I/O | Default reject; explicit `--io-holes ff` selects FF reads/ignored writes for absent ports. Installed-device errors never fall back. No first-IOC02-read override. |
| Floppy controller | The functional WD37C65 owns `3F0h`-`3F5h` and `3F7h`; `3F6h` remains ATA. IRQ6 and DREQ2 are explicit lines and data endpoints attach to the portable AT DMA. An optional exact-size 1.44MiB image is loaded into a write-protected in-memory drive. Non-DMA and mechanical timing remain pending. |

DMA bus requests are now handled by the private functional
[DMA coordinator](pcs286-dma-coordinator.md): it waits for CPU HLDA, grants the
selected DMA8/16 owner, services one unit and releases the grant/HOLD in order.
DMA memory accesses traverse the granted AT bus. The retained no-drive POST
produces no DMA transfer; the first run with the canonical diskette stopped at
the next unsupported KBC command before FDC access, so it also does not validate
media boot. No floppy success is invented. ATA, interactive live input and the
full machine lifecycle remain pending.
`--video pvga1a` maps the existing component at A0000h–BFFFFh/3B0h–3DFh.
Headland still owns shadow/EMS/route priority; external absent space remains FF.
Installed video errors do not fall back to FF. Default `--video none` reproduces
the older configuration. VRAM FETCH remains unsupported by the shared component.

## Local use

Build with the existing UCRT64 engine-only configuration. From the checkout:

```powershell
$env:PATH = 'C:/msys64/ucrt64/bin;' + $env:PATH
& ./build/handoff-release/systems/olivetti-pcs286/pcs286-boot-probe.exe `
  --bios 'Z:/library/olivetti/pcs286/firmware/derived/pcs286-bios-1.42-combined.bin' `
  --ram-mib 1 --steps 1000000 --peripheral-ns 1000 `
  --io-holes ff --rtc-divider classic --cmos initialized --video pvga1a `
  --floppy 'Z:/library/olivetti/pcs286/media/floppy/original/Olivetti_MSDOS330a_PCS286.img' `
  > ../_diagnostics/pcs286-bios-trace.jsonl
```

Verify firmware and optional media hash/security against the canonical manifest
before execution. CLI checks sizes and opens inputs read-only; it does not scan
or authenticate arbitrary input.
`--rtc-divider strict` reproduces the original refusal. JSONL includes config,
port80/LPT writes, first/last64 I/O events, last64 CPU boundaries and terminal
registers/failure/counts/peripheral time. No instruction-byte dump. Keep firmware
traces local. Exit1=explicit stop,2=budget,3=CLI/file/output error; none claims boot.
HALT can still receive peripheral events until the budget or a real stop.
Originals stay outside Git; future writable media need an external copy/overlay.

## Observed BIOS1.42 result, 2026-09-25

Canonical `bios-142-combined`,131072bytes, clean/local-only, verified SHA256:
`AFBD051666869F3F58F23E52F9DD468FB9AD9F629D2DBCCFDA5324E83A897621`.

| Attempt | Observed endpoint |
|---|---|
| Initial reject-I/O runner | Step196627: UNMAPPED writing40h to378h; parallel port absent. |
| FF holes; then real SPP connected | Step393404: UNSUPPORTED writing00h to RTC register A through71h. |
| Classic RTC divider | Step490360: empty KBC60h returned IDLE, then stopped CPU returned INVALID_STATE. |
| Retained KBC output latch | Step614569: UNSUPPORTED sendingCFh to KBC64h, F000:F23F. |

Final run: Release/RAM1MiB/budget1000000/quantum1000ns/FF holes/classic RTC,
no video/media.599906 completed CPU boundaries,1364180 memory calls,82292 I/O
calls,614568000ns provisional peripheral time. LPT values40h–47h then00h/47h;
no port80 writes. These are observations, not passed diagnostics. Execution
started at genuine reset without importing CPU architectural state.

### Follow-up, 2026-09-26

CFh now follows the explicit classic no-response policy via the controller's
existing IBF state machine. Same configuration and unchanged BIOS: stop at80h
to64h, UNSUPPORTED, F000:F252 after614584 attempts/599920 completed boundaries,
1364209 memory calls/82294 I/O calls,614583000 provisional peripheral ns.
LPT and port80 observations unchanged. This is still partial BIOS execution.

The next increment implements80h/84h using the explicit separate vendor-latch
policy in the [KBC record](pcs286-at-kbc.md). The user reports having a real
PCS286; chip identity and measurements have not been obtained. A future
targeted DOS behavior probe can test commands; it is not an authentic KBC
program-ROM dump.

## Validation

Latest real-BIOS run after the P2 increment: same configuration,895386 attempts,
862110 CPU boundaries,2027386 memory calls,99457 I/O calls. Stops with
`peripheral_error/UNSUPPORTED`, peripheral time895385742ns, CPU F000:08A8.
Last I/O repeatedly reads3DAh=FFh (video is still absent). The native RTC
snapshot shows phase16449, updating0, registers00h..09h all zero,A=26h,B=00h,
C=40h,D=80h. This is the first update-completion boundary with an invalid
depleted calendar. The clock owner retains the failure; no guest exception or
calendar repair occurs. A synthetic CLI regression reproduces the same class
of failure. LPT/port80 observations remain unchanged; no passed POST claim.

Next: choose/document the functional depleted/initialized RTC policy and attach
the existing video component so3DAh represents an actual device. The last CPU
I/O and the peripheral-clock failure are distinct observations. Media and DMA
service integration still follow; this does not certify80/84 hardware fidelity.

Eleven authored CLI tests cover reset/POST, HALT/budget, I/O policy and installed
errors, bounded trace, immutable/wrong-size input, invalid options, real SPP
readback, RTC divider, empty KBC reads, CFh without a reply,80/84 zero-latch
round-trip without CPU reset and diagnostic preservation of an invalid RTC date.
The probe reports a pure native RTC/register snapshot without advancing time,
acknowledging C/D, clearing failure or modifying CMOS. Component CFh
tests cover default refusal, explicit profile, delays, pending/full output,
IRQ, translation prefix, reset pulse, parameter replacement and failures.
Other component tests add stopped calendar/
PF/UIP, all non-running DV selections, reset/readback/resume and repeated empty
reads with unchanged IRQ/time/pending reply. Default strict RTC and callback-error
tests remain. Firmware never enters tests/source.

GCC16.2 UCRT64 Debug/Release:154 pass+1public Headland skip/155; Python61;
provenance59 components/301 files; catalogue32/5. Initial fixture errors (reset
A20, RTC trial phase, CCB system-bit expectation) were corrected. No MSVC/GUI/
ASan/CI/physical-machine or successful portable-boot result. Local only.
The P2 increment exposed a Release-only test comparison of unspecified C struct
padding. KBC state assertions now compare every semantic field, including the
new latch; no device behavior or expectation was relaxed to fix that test.

## DMA, KBC RAM and RTC POST advance, 2026-09-26

The board now owns readable spare latches at80h,84h-86h,88h and8Ch-8Fh while
the seven channel page registers remain owned by `blumach_at_dma`. This matches
the BIOS1.42 rotating-pattern test at F000:477B-47C3 without affecting DMA
addresses. The generic KBC implements internal RAM read20h-3Fh/write60h-7Fh;
20h/60h retain command-byte side effects and cells21h-3Fh are independent.
Host IBF also accepts the EDh option while the first keyboard ACK remains in
OBF; the second real ACK waits behind keyboard-to-host inhibition.

With the unchanged clean BIOS,1MiB RAM, external physical CMOS with only DOW
normalized outside Git, PVGA1A, FF holes, classic RTC divider and1000ns service
quantum, the final Release run executes7,992,141 attempts and7,488,407 completed
boundaries before the CPU reports `UNSUPPORTED` at F000:514B (`FNINIT`). It
records11,906,554 memory calls,718,408 I/O calls,19 port80 writes and
7,992,140,000ns provisional peripheral time. The framebuffer shows640KiB base,
384KiB extended and visible Pass results through Parity, PIC, DMA, Keyboard,
Clock/Calendar, CPU Protected Mode and CMOS RAM. No error/F1, peripheral
failure, HALT, shutdown, Setup or boot is claimed. The next block is an explicit
80286/x87 coprocessor-detection boundary, not a reason to manufacture an x87
result.

## Absent 80287 and ATA timeout, 2026-09-26

The portable 80286 now models an explicitly unpopulated 80287: ESC still
consumes ModR/M/displacement and calculates its effective address, while inactive
PEREQ causes no source read or destination write. EM/TS vector7 and WAIT MP+TS
behavior are unchanged. See [the focused contract](pcs286-80287-absence.md).

The unchanged BIOS therefore crosses `FNINIT` at F000:514B and `FNSTCW [0006]`
without an invented control word. Release, 1MiB, 9,000,000 attempts, 1000ns,
FF holes, classic RTC, initialized calendar and PVGA1A complete 8,429,458 CPU
boundaries, 13,553,044 memory calls and 953,749 I/O calls. Status remains OK and
the budget ends at F000:3D82 while the firmware polls absent ATA status `1F7h`.
The framebuffer retains every visible Pass through CMOS RAM. There is no F1,
host failure, HALT, shutdown, POST completion, Setup or boot claim. Trace SHA256 is
`F138B6E55D2B6752722252A00A5FD11F3AD89F7848069F90AA0671001D50C304`;
raw framebuffer SHA256 is
`A16EB04FA00B01586E114F2334855E679CB142AD570B3FA0484F99599FAB0AD3`.

A longer observation completes that timeout after 9,116,504 attempts and
8,538,238 completed CPU boundaries. The framebuffer adds visible
`Fixed Disks: Pass`; the absent ATA device remains an unclaimed electrical
response, not a fabricated controller result. Execution then reaches the BIOS
write of command byte `47h` at F000:55E8. The earlier strict rejection of its
reserved bit1 was a host-model error. Trace SHA256 is
`109524EB01A03CD37E9B9C34CC916D786B763662D7058D2ED42122DBE8BACE7F`;
raw framebuffer SHA256 is
`EFD3BF9A277F1B4FA4F09E4C03965CA7773F7793EF767DFFD89DE3BAAD132338`.

## RTC and shared-video increment, 2026-09-26

The calendar choice is caller policy, not a saved Olivetti setup image. Only
the two-digit year80h is seeded; century/configuration bytes remain zero. Guest
writes and invalid-date failures remain intact. No native RTC behavior changed.
The existing PVGA1A retains ownership of VGA/Paradise registers, 256KiB VRAM,
DAC and renderer; no second IMS G171 instance or firmware workaround is added.

The probe explicitly opts into `bm_pvga1a_advance_ns`, including a zero-time call
before the first CPU step. Status1 bits0/3 derive from elapsed dots, CRTC totals,
display ends and vertical retrace. The clock/geometry helpers already used by
the renderer supply internal25.175/28.322MHz,8/9-dot characters and clock divide.
The clocked mode uses fractional dots, is independent of call partitioning and
poll count, and rejects unsupported external clock selections without advancing
video. See IBM's [VGA status description, p2-43](https://bitsavers.trailing-edge.com/pdf/ibm/pc/ps2/42G2193_PS2_Hardware_Interface_Technical_Reference_Video_Subsystems_Sep92.pdf)
and the [PVGA1A manufacturer reference](https://www.dosdays.co.uk/media/paradise/PVGA1A_Datasheet.pdf)
already cited by the shared implementation. These define register-level concepts;
they do not qualify the board's timing or our approximations.

This is a functional raster: display-end and retrace windows, not physical sync,
blanking/skew/preset-row counters, IRQ generation or full extended timing.
The zero retrace-length encoding is treated as16lines in this model. Programming
registers changes the geometry used to interpret the retained phase. Status
reads still reset the normal attribute flip-flop but do not advance raster time.
DEBUG reads preserve that flip-flop, DAC indexes and VRAM latches; DEBUG writes
reject. Video advances after each successful services quantum. A services error
can leave services at a partial later time: `video_snapshot.time_ns` reports the
last completed video quantum separately rather than claiming synchronization.

**PCS86 compatibility:** no PCS86 caller, scheduler, configuration, rendering or
ROM handling changed. Create/reset retain the prior read-driven status until an
owner explicitly opts in; the PCS286 diagnostic is the only new caller. The
existing status tests remain, and reset-after-opt-in has its own regression.
Migrating PCS86 to clocked status requires its own scheduling and BIOS validation.
No new PCS86 BIOS/GUI validation is claimed by the shared unit/integration tests.

Authored tests cover initial/repeated polling, active display/horizontal inactive/
vertical retrace, mono/color ports, both clocks, nine-dot/clock-divide and overflow
bits, sub-dot partitions and UINT64_MAX intervals, external-clock refusal, DEBUG
attribute/DAC/VRAM purity, and default/reset compatibility. CLI tests cover
Headland VRAM and indexed I/O round trips, real retrace polling, unsupported
clock stop and initialized-calendar rollover; guest corruption still fails.
All14CLI cases use authored bytes, never preserved BIOS data.

Observed first run: unchanged clean BIOS1.42, Release,1MiB,3000000-step budget,
1000ns quantum, FF holes, classic RTC divider, initialized calendar, PVGA1A:
2605348attempts,2458720boundaries,4782948memory calls,429264I/O calls,
2605347000ns. It reaches a keyboard write FFh to60h after ADh to64h; the endpoint
returns IDLE(1), then CPU reports INVALID_STATE(-3). This is not a successful
write, guest exception, timer failure or passed POST. RTC snapshot has valid
date and seconds02h. LPT writes later include32h,00h,32h,00h,32h; port80 has no
writes. These are observations only. Next examine disabled-keyboard host command
acceptance against the classic handler and AT documentation, then DMA/storage.

Final diagnostic repetition has identical counts, RTC state and stop. Pure video
snapshot reports720x400, misc67h and elapsed2605347000ns; this is programmed
geometry, not a rendered-frame or monitor-validation result. GCC16.2 UCRT64:
Debug154pass+1Headlandskip/155 (53.58s), Release same (38.14s), Python64 including
14CLI cases; provenance59/301 without errors, catalogue32/5, diff check clean.

## Keyboard host-write correction, 2026-09-26

Keyboard-bound data now enables the interface when the controller consumes IBF,
following `kbc_ibf_process` in the classic controller. This applies to every byte,
not only reset FFh. Controller parameters are excluded; real keyboard replies and
all host failures retain their own semantics. See the [KBC policy and evidence](pcs286-at-kbc.md#host-transmission-while-inhibited-2026-09-26).
The authored CLI suite now has15 cases, including ADh/FFh without AEh followed by
actual FAh/AAh responses and HALT. BIOS1.42: 4000000 attempts, 3760925 CPU boundaries, 7387391 memory calls, 863301 I/O calls, 4000000000 peripheral ns; reason=budget, status=0, CS:IP=F000:C26C, last failed endpoint=0000h/status0. No completed POST, visible-screen, Setup or DOS claim. The final F000:C268/C26C loop polls OBF and accepts scan byte3Bh (F1 in the modeled set1 path); input is not yet connected to the probe.

Validation for this increment: Debug/Release154pass+1Headlandskip/155; Python65 including15 authored CLI cases; provenance59/301, catalogue32/5, diff check. No change to PCS86 or shared video. All changes remain local.

## Framebuffer and keyboard connection, 2026-09-26

The diagnostic now exports the existing PVGA1A renderer's pixels and submits
normalized input through `bm_pcs286_services_input`. CPU, keyboard/controller,
PVGA1A and PCS86 implementations are unchanged in this increment.

`--frame NEW.ppm` captures at termination. `--capture STEP:NEW.ppm` captures after
exactly STEP attempts, before the next attempt. Both require `--video pvga1a`.
Files are P6 RGB PPM, created exclusively: an existing destination is refused,
never truncated. Capture reads the programmed geometry and uses the renderer's
VRAM, fonts and palette without advancing emulation. A host export failure is
reported as such, never delivered to the guest. A partial new file can remain
after an output failure; a final export failure preserves the engine stop reason
and changes the process exit code to3.

`--key STEP:NAME:down|up` sends a named physical key event through the existing
keyboard/controller pair, including its scan-set conversion and delays. Supported
names are A–Z,0–9,F1–F10,ENTER,ESC,SPACE,UP,DOWN,LEFT,RIGHT. Up to32 scheduled
actions are accepted, with `1 <= STEP < --steps`. Captures precede keys at the
same boundary; keys retain command-line order. There is no BIOS address predicate,
direct scan-code injection, automatic F1 or simulated controller response.
This is a reproducible diagnostic interface, not an interactive frontend.

For the existing local invocation, choose a4M-step budget and append:

```text
--capture 3000000:../_diagnostics/before-f1.ppm
--key 3000000:F1:down --key 3050000:F1:up
--frame ../_diagnostics/after-f1.ppm
```

Eighteen authored CLI cases now include actual F1 make/break through translation,
exact RGB pixels from an8x1 planar fixture, identical CPU/I/O results with and
without captures, existing-file protection and malformed/capacity/out-of-budget
schedule rejection. The initial black-frame test fixture omitted CRTC17 display
enable and attribute palette/plane enable; the fixture was corrected without
changing the renderer or weakening pixel assertions. No firmware bytes in tests.

GCC16.2 UCRT64 engine-only Debug and Release each execute154 tests successfully
with the existing public Headland acceptance skipped (155 registered).
Python68 including18CLI cases, provenance59components/301files and catalogue
32machines/5locales pass. No GUI, MSVC, physical-board or remote-CI validation.

BIOS1.42: 4000000 attempts, 3760925 CPU boundaries, 7387387 memory calls, 863306 I/O calls, 4000000000 peripheral ns. Budget/status0, CPU F000:C26A, no endpoint failure, HALT or shutdown. Both720x400 captures visibly show VGA Error : 1 / Press F1 to continue. F1 press/release accepted at3M/3.05M attempts, but the BIOS remains in its input wait; no successful continuation, completed POST, Setup or DOS claim.

The original BIOS1.42 is already in use: the128KiB motherboard image, mapped by the inherited Headland ROMCS routes at E0000..FFFFF and FE0000..FFFFFF. Classic machine_at_olivetti_pcs286_init and paradise_pvga1a_pcs286_init explicitly use integrated video firmware without a separate C000 option ROM. Read-only inspection finds no55AA header at512-byte boundaries in this image; that absence alone is not a hardware proof. Only one PVGA1A is instantiated. The adapter external-memory route means bus ownership, not an additional external VGA card. Bounded firmware inspection at F000:5791/57A6 and05C0/088F..08F3 links displayed error bit1 to VGA status transitions and a PIT-measured retrace interval, not an explicit missing-ROM error. Whether a transition timeout or interval comparison fails is still unobserved. This is observed code plus inference, not manufacturer qualification. No BIOS patch, generic VGA ROM, register override or timing workaround was added.

The bounded `kbc_io` tail retains the last64 actual port60 accesses and port64 writes, excluding status polling. It adds no device reads or writes and makes received key bytes reviewable.

Follow-up observed trace, same configuration with3.1M budget: F1 make05h is read at attempt3000012 by F000:82EB while keyboard preparation is still running. BIOS writes CCB65h at3000045 and AEh at3000063. Its final F000:C26E input wait reads break prefixF0h at3050010 and05h at3050020; it expects3Bh. Thus the submitted press is early for that final wait and set2 bytes remain untranslated with CCB64h/65h. Current controller code suppresses translation when bit5(PC_MODE) is set; this is a concrete next qualification target, not yet a proven hardware correction. No CCB override or scan-byte injection added. Summary:3100000attempts,2920584boundaries,5706705memory calls,583192I/O,3100000000ns,budget/status0,F000:C26C. No successful continuation. The VGA error remains separately tied to status transitions/PIT interval by bounded firmware inspection.

## VGA and protected-reset checkpoint follow-up, 2026-09-26

The clocked VGA status no longer forces display-enable inactive merely because
the sequencer blanks pixel output. This removes the observed bit0 timeout and
produces the BIOS-accepted PIT interval `8500h`. Input Status0 now exposes a
functional DAC-entry-zero monitor comparator with the exact transitions traced
in BIOS1.42: dark `04/04/04` sets bit4 and a `10h` primary clears it. These are
general register-level rules with authored unit tests, not firmware-address
conditions. Physical PVGA1A/board analogue thresholds remain unqualified.

After those changes the displayed `VGA Error : 1`, then `VGA Error : 10`, are
both absent. The next visible `Error : 2` came from the protected-mode reset
return at `F000:44A9`–`44F9`: firmware writes zero to80h before KBC CPU reset and
reads it after resuming. The old diagnostic sink returnedFFh. Port80h is now a
private byte latch that retains accepted writes across CPU-only reset; DEBUG
read is observational and DEBUG write rejects. No value is forced at the BIOS
read. Its component and CPU-I/O round-trip tests cover initial/read/write,
overwrite, isolation, invalid accesses and staged refusal.

Retained Release run with unchanged BIOS1.42,1MiB, external physical CMOS with
only invalid day-of-week normalized outside Git, classic RTC, PVGA1A and FF
holes: four million attempts,3,760,921 CPU boundaries,6,128,735 memory calls,
425,866 I/O calls and4,000,000,000 provisional ns. One real port80h write stores
zero; the protected return reads zero and emits LPT progress4Ah. Framebuffer
shows CPU, I/O controller, ROM checksum, refresh and keyboard controller `Pass`
and an in-progress `Base Memory: 384 KB`, with no error/F1 prompt. The run ends
only at its budget, status0, CPU F000:5B7E, without HALT, shutdown or endpoint
failure. It does not yet establish completed memory POST, Setup or boot.

## No-mouse timeout and password query, 2026-09-26

A retained Release run with BIOS 1.42 and no drive reached 15,000,000 attempts
without host failure. It remained in the bounded absent-mouse wait at
`F000:8EF4`; the frame showed every resident diagnostic passing and both fixed
and floppy disks reported `Not Present`.

The first canonical-media run used the verified clean 1.44MiB image in the
write-protected memory drive. It completed the no-mouse timeout and stopped at
attempt 15,562,030 on KBC command A4h from `F000:5684`/the common output helper
at `F000:82DF`. Status was `UNSUPPORTED`; `dma_units=0`, so this run is evidence
of neither a DMA nor a diskette failure. The KBC now implements the documented
no-password result F1h, covered in both AT and PCS286 profiles. A subsequent
real-BIOS media run remains pending.

That subsequent 16,500,000-attempt Release run completed with `status=0` and
entered the visible `BUILT-IN SET-UP` screen. It shows 640KiB base, 384KiB
extended, no 80287 and floppy drive A. The run ended only at its budget at
`F000:96DA`; `dma_units=0`, because the incomplete calendar-only CMOS profile
leads through Setup before any demonstrated boot-sector read. This validates
the A4 no-password branch and Setup entry, not media boot.
