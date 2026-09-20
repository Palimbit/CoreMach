# Olivetti PCS 86 portable-engine implementation

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

## Present outcome and evidence boundary

The current PCS 86 vertical slice in BluMach's portable engine executes both
newly authored conformance programs and, as a local-only manual validation,
the original revision 1.09 BIOS from the real-mode reset address. It supplies an
explicit NEC V30 state object, a generic address bus, 640 KiB of conventional
RAM, selectable 0/384/1920 KiB onboard EMS, the
documented 64 KiB system-ROM window, a single 8259A, a complete 8253 mode model,
a functional MM58167, isolated SPP and NS16450 components and the known
motherboard-register map. The current cut also supplies a caller-owned block
medium, an independent floppy drive, real 8237 device transfers and a portable
uPD765-compatible controller at `3F0h-3F7h` using IRQ6 and DMA2. The integrated
XTA path is also a portable component at `320h-323h`, gated by
port `65h`, with IRQ5, DMA3 and caller-owned 615/4/17 block media; it does not
load an option ROM because the PCS 86 firmware contains its disk services.
It remains an
incomplete emulator, but it is no longer stopped at the storage boundary: the
BIOS visibly passes CPU, ROM, DMA, interrupt-controller, Timer,
Clock/Calendar and keyboard diagnostics, reports 640 kB and one floppy drive,
then enters its primary bootstrap. With the preserved validation diskette
mounted read-only, the BIOS transfers and executes its boot sector, the system
files initialize Microsoft MS-DOS 3.30a, `AUTOEXEC.BAT` runs and the pilot
reaches an interactive `A>` prompt. This validates the described boot path and
basic keyboard interaction, not broad DOS application compatibility. The
unpopulated option-ROM region returns ones;
the PCS 86 system EPROMs already contain video initialization, so the engine
does not fabricate a separate ROM. The PVGA1A component owns VGA/Paradise
register state and 256 KiB of planar VRAM and publishes a deterministic,
host-neutral text framebuffer including the CRTC text cursor. It then exercises
the enabled parallel and serial register paths. Host-neutral keyboard events
and the board's dual
keyboard/mouse queues are present. FDC rotational/command timing, asynchronous
DMA arbitration, mouse input and writable-media product integration remain
absent.

The portable Qt application can present the published framebuffer through a
Qt OpenGL surface or its retained software surface. Its optional CRT profile is
a host-side scanline and vignette composition only. Captures and clipboard
copies still use the unmodified machine framebuffer, and neither presentation
choice changes PVGA1A state, guest timing or headless output.

The processor identity, 10 MHz clock, memory size, two 32 KiB firmware halves,
interleaving, ROM address and known firmware hashes come from the canonical
PCS 86 record. Execution of the synthetic test is observed. The small opcode
implementation and one-instruction-per-tick timing are new behavioural subsets,
not claims of complete or cycle-accurate V30 emulation.

## Why this is a selective rewrite

The inherited 808x/Vx0 implementation is a mature core, but its state and
execution path depend directly on the previous product's global CPU state,
memory subsystem, PIC, timers, FPU, debugger and other internal services.
Copying it wholesale would recreate the coupling that the portable engine is
intended to remove. The new component therefore starts with explicit state and
generic bus transactions, while its provenance records the inherited source
that informed the rewrite and preserves the original authors' notices.

The PCS 86 machine follows the same rule. It does not retain the inherited
global `pcs86_active` pointer or instantiate legacy devices. Firmware arrives
as immutable caller-owned blobs. The machine interleaves the two EPROM views
into host-allocated ROM and gives the CPU only a bus, not host files or paths.
CPU writes to the EPROM window are acknowledged by the bus and discarded, as
on the physical read-only device; they neither alter firmware bytes nor abort
the emulated processor.

## First-cut component ledger

| Subsystem | Current level | Boundary |
|---|---|---|
| NEC V30 | Functional instruction-boundary core derived from the inherited interpreter | Complete documented native and 8080 opcode-map classification, snapshot v4 with MD write gate, segmented 20-bit addresses, ModR/M, native and emulated stacks, hardware-vector-validated primary ALU forms, 80186-compatible and NEC extensions, FPO/POLL CPU contract, BRKEM/CALLN/RETEM, interrupt and NMI round trips, prefix shadows, interruptible REP and BUSLOCK transaction attributes; aligned word memory, stack/vector transfers and even-port word I/O use one 16-bit bus transaction while odd words use two byte cycles; a private instance-owned BCU component holds the real six-byte instruction queue, independent PFP and T1/T2/T3/Tw/T4 state for both prefetch and operand/I/O transfers; every instruction-queue read advances that BCU for its documented predecode clock, and a pending prefetch completes before an operand acquires the shared bus; demand fetch is phase-driven and safe bus-free instructions with exact documented timings overlap BCU prefetch with EXU clocks across boundaries; timing-observation v12 reports exact/ranged native execution clocks, bus occupancy, waits, queue reads, prefetch and operand transactions, phase state and prefetch-to-operand handoff, places every native prefix at decode time, and places all eight native `IN`/`OUT`, four direct accumulator-memory `MOV` forms, `XLAT` and the memory forms of all four ModR/M `MOV` opcodes on an exact EXU timeline; remaining operand EXU placement, exact realised values inside ranges, interrupted string fragments, POLL timing, DMA arbitration, scheduler consumption and embedded floating-point execution remain explicit |
| Conventional RAM | Board-owned portable memory region | 640 KiB, zero-initialized and byte-addressable; the 64 KiB below the EMS frame remains visible whenever its corresponding window is disabled or selects an unavailable page |
| System ROM | Evidence-backed map | Two 32 KiB halves interleaved at `F0000h-FFFFFh`; bytes remain external and CPU writes have no effect |
| Scheduler timing | Functional approximation | One retired instruction per engine tick; the scheduler does not yet consume CPU timing observations, so rational PIT/RTC clock accumulators still use a measured functional instruction rate |
| Single 8259A PIC | Derived portable subset | Initialization, masking, edge requests including withdrawal before INTA, fixed-priority nesting, output callback, CPU acknowledge, and non-specific or specific EOI; no cascaded/level modes |
| 8253 PIT | Selective port of measured edge-state core | Deterministic modes 0-5, binary and BCD counts, gates, output edges and stable counter-latch reads; driven from scheduler time without claiming cycle accuracy |
| PCS 86 board glue | Derived minimum map | Reset values and known semantics at `60h-6Fh`, write-only NMI aperture, jumpers at `100h` and the early POST diagnostic latch at disabled `378h`; opaque write-only memory-control state at `70h`; dual keyboard/mouse command queues, IRQ1 scan queue and the keyboard `EDh` LED parameter. One board-level passive-I/O policy returns `FFh` for every unclaimed or non-readable port and ignores writes, including absent expansion slots; this is not a firmware-specific port list |
| Onboard EMS | Functional board implementation derived from the inherited model and original software | Selectable 0/384/1920 KiB backing store (0/24/120 pages), four readable selectors at each `N400h-N403h`, for N=4 through 9, bits 6:0 as page number and bit 7 as window enable. Each 64 KiB frame at `N0000h` is gated by port `6Bh` bit N-3; this relocation is inferred from original Customer and BIOS code, not physically verified. Port `64h` exposes the fitted-SIMM code; the frontend defaults to 1920 KiB |
| 8237 DMA | Functional synchronous subset derived from the inherited core and physical Customer observation | Address/count flip-flop, base/current registers, command, mode, request, masks, status, master clear and device-facing byte transfers; a configurable board latch block decodes `80h-9Fh` on PCS 86, with only `81h/82h/83h/87h` supplying four-bit pages and 20-bit current addresses. The wider independent readback is observed through the passing Spanish Customer test on a physical BIOS 1.08 machine; no asynchronous arbitration or cycle stealing |
| MM58167 RTC | Functional portable component | PCS 86 `B0h-B7h` controls and `E0h-EFh` counter/alarm RAM, BCD millisecond calendar, alarm and periodic IRQs, reset/GO/standby commands, checksum repair and 32-byte caller-owned initial/persistent state. The frontend supplies a deterministic valid calendar plus the BIOS weekday/checksum encoding, while a named-state channel lets Qt retain the bytes or select a depleted-battery cold start; headless can do the same with an explicit state path. The engine remains independent of wall-clock and file APIs. The yearless calendar and physical crystal/battery decay remain approximate. |
| SPP parallel port | Derived portable register core | Data, status and control at gated `378h-37Ah`, disconnected-printer status, output callback and ACK-driven IRQ7; no EPP/ECP, printer backend, DMA or host threads |
| NS16450 UART | Derived portable register core | Divisor latch, IER/IIR, LCR/MCR, LSR/MSR, scratch, modem/data loopback and IRQ4 at gated `3F8h-3FFh`; no 16550 FIFO, host serial backend or baud scheduling |
| Keyboard input | New runtime contract plus PCS 86 translation | Stable physical-key events, supported IBM Set 1 make/break bytes and IRQ1; an optional guest-owned Scroll/Num/Caps state query reflects the keyboard `EDh` command without host lock-state inference; no host scan codes in the engine, mouse input, electrical timing or complete command set |
| PCS 86 video selection | Observed write-only boundary | Empty `C0000h-EFFFFh` option-ROM space returns ones; `46E8h` and `102h` retain the BIOS-observed arbitration writes without undocumented side effects |
| Paradise PVGA1A | Derived portable register/VRAM core | Isolated VGA and Paradise registers, DAC state and 256 KiB planar VRAM; deterministic text rasterizer with CRTC cursor plus standard four-plane 16-colour and chain-4 256-colour XRGB8888 output. Geometry, display start, offset, double-scan and palette selection are register-derived rather than keyed to BIOS mode numbers. CGA-compatible packed shift modes, line compare/panning and scan-event generation remain absent. |
| Complete PPI behaviour | Partial board glue | Sufficient for the validated resident diagnostics and bootstrap path; electrical/timing fidelity and undocumented bits remain unclaimed |
| Floppy controller and storage | Functional boot subset | Caller-owned raw block media, independent 360 KiB/1.2 MiB/720 KiB/1.44 MiB drive geometry, validated 720 KiB/1.44 MiB runtime insertion and ejection, PCS 86 jumpers, active-low disk change, reset/sense/specify/seek/recalibrate/read-ID and DMA read/write-data paths; no rotational timing, flux/track formats, formatting or weak-sector behaviour |
| Integrated XTA | Portable derived rewrite of the inherited generic XTA controller | Onboard `320h-323h` interface; the three unpopulated conventional base slots through `32Fh` naturally receive the board's general passive-I/O response rather than individual mappings. Includes the port-`65h` gate, active-low presence jumper, IRQ5, DMA3 and PIO, caller-owned 512-byte block media, CP3026 615/4/17 geometry, read/write/verify/seek/recalibrate/sense/parameters/buffer/format/diagnostic commands; unknown commands complete with an explicit illegal-command sense code, and there is no option ROM, host path or private disk API |

## Text cursor timing and observed CRTC state

The Western Digital/Paradise PVGA1A data sheet defines CRTC `0Ah[5]` as the
cursor disable bit, `0Ah[4:0]` and `0Bh[4:0]` as the inclusive start and end
scan lines, and `0Bh[6:5]` as a zero-to-three-character cursor skew. It also
states that no cursor is generated when start is greater than end, rather than
describing the EGA split-cursor behaviour. The start address, cursor address and
offset remain in the CRTC character-address space. These are PVGA1A rules, not
assumptions copied from an IBM-only implementation. Source: [Western Digital
PVGA1A Advance Information, 30 October 1990, pp. 19-58 to
19-61](https://www.dosdays.co.uk/media/paradise/PVGA1A_Datasheet.pdf).

At the validated `A>` prompt, a read-only local probe observed `0Ah=0Dh`,
`0Bh=0Eh`, cursor address `0E/0F=0782h`, display start `0C/0D=0000h`, maximum
scan-line register `09h=4Fh`, offset `13h=28h` and Paradise PR3 `00h`. In the
rendered 80-column mode this is a 16-scan-line character cell, an effective
80-character row stride, and an underline at row 24, column 2 immediately after
`A>`. PR3 cursor-doubling is inactive in this observed mode. No firmware, disk
or capture used for the observation is stored in Git.

The runtime passes the current engine tick explicitly to the machine video
operation. The PVGA1A renderer derives the VGA cursor's 50-percent blink phase
from that time, the programmed CRTC totals and the selected 25.175 or 28.322 MHz
clock. The documented VGA cursor rate is VSYNC/16. VCLK2/VCLK3 are external,
board-defined inputs; if selected, the current component uses a deterministic
70 Hz fallback instead of claiming an unknown PCS 86 board clock. Headless and
Qt therefore consume the same framebuffer state for the same emulated time and
neither owns a cursor timer. Blink-rate source: [IBM Personal System/2 Hardware
Interface Technical Reference, Video Subsystems, September
1992](https://ardent-tool.com/docs/pdf/42G2193_PS2_Hardware_Interface_Technical_Reference_Video_Subsystems_Sep92.pdf).

Undefined, reserved, model-inapplicable or externally unavailable instruction
forms return a structured `BM_STATUS_UNSUPPORTED` result. They are not skipped,
approximated as NOPs or redirected to the inherited engine. This keeps each
unsupported boundary visible to tests and debuggers.

## Validation ladder

The current automated ladder uses no historical software:

1. The generic memory test checks little- and big-endian transactions, fetch,
   write protection, unmapped access and direct debug inspection.
2. The PC component tests initialize and service the single PIC, cover withdrawn
   edge requests and all six 8253 modes, gates, BCD and latching, and verify the
   8237 register, mask, request, status, byte-pointer, master-clear and real
   device-to-memory/memory-to-device transfer contracts. A separate
   test verifies the page-port-to-channel map, four-bit effective-address
   masking, full-byte readable values, the PCS 86's 32 independent latches,
   its original Customer pattern sequence and independent reset semantics.
   The PIT test also covers immediate reads of a completed pending mode-4 load.
3. The V30 tests reproduce the BIOS register/flag self-test, exercise its
   segmented checksum-loop pattern and verify maskable-interrupt stack/vector
   entry using newly authored memory images. A focused boundary test verifies
   the delayed acceptance after `EI` and segment-register transfers, `EI; HLT`,
   restart of REP memory and I/O operations, completed-iteration progress and
   the V30's three-prefix retention limit.
   A separate manual adapter streams externally supplied hardware-generated
   SingleStepTests/V20 vectors through the same public state and step contracts.
   The current native-ISA gate covers 250,000 cases with no mismatch across
   D4h/D5h, ADC/SBB primary forms, decimal/ASCII adjustments, CWD, INT3/INTO
   and the eight 82h groups, while intentionally excluding V20 cycle traces
   from V30 timing claims.
   A synthetic timing-contract test separately checks documented fixed,
   taken/not-taken, memory-alignment, entry-stack-alignment and counted-shift
   clocks, prefix cost, explicit data-dependent unknown classification, logical
   bus and device-wait accounting, and queue invalidation on branches and
   accepted interrupts without ROM or disk inputs.
4. A ROM-free PCS 86 EMS test covers all three supported populations, fitted-
   SIMM identification, readable selectors, disabled and out-of-range fallback
   to conventional RAM, independent pages, cross-window aliasing, reset of the
   selectors and rejection of unsupported capacities.
5. The PCS 86 test creates two synthetic 32 KiB halves in memory. Their
   interleaved reset vector performs a far jump from physical `FFFF0h` to
   `F0100h`, writes RAM, initializes the PIC, programs the PIT, exercises the
   board-control register and reads the fixed diagnostic register before halt.
6. Dedicated CPU tests write and read words through `ES:`, `SS:`, `DS:` and
   `CS:`, verifies that the last repeated segment prefix wins, and covers the
   stack, near-call, segment-register and string paths added for POST. A focused
   test covers all D0-D3 rotate/shift operations, TEST register/memory forms and
   the complete compact `XCHG AX,r16` family.
7. CPU and I/O traces verify exact instruction and port checkpoints.
8. Firmware metadata validation rejects a supplied hash that differs from the
   known PCS 86 identity.
9. Focused SPP/NS16450 tests verify register reset, loopback and interrupt
   behaviour. A PCS 86 integration ROM enables both gated devices, performs a
   UART data loopback and reads the keyboard identify response. The session
   tests verify normalized Shift, letter, extended-key make/break delivery,
   the `EDh` LED command and parameter with reset cleanup, rejection of
   unsupported scan sequences, specific PIC EOI handling,
   fixed-priority blocking and reset cleanup.
10. The FDC test releases reset, drains the four sense-interrupt results, reads
   a complete 512-byte sector through DMA2 into guest RAM and verifies
   write-protect reporting. The PCS 86 integration test verifies a configured
   720 KiB drive's jumper encoding and reset state.
11. A synthetic XTA test exercises controller gating, switches, PIO sector read
   and write, DMA3 transfer into guest RAM, IRQ5 signalling, completion and
   sense phases, and explicit illegal-command reporting without historical
   firmware or disk inputs.

Original firmware is intentionally not used in CI and no ROM, disk, manual or
diagnostic asset is present in these public files. The manual firmware probe
accepts two external 32 KiB halves and an optional external raw floppy image,
reports the exact instruction boundary and can emit a framebuffer capture;
it does not weaken the rule that firmware is caller-owned local data.

A local-only XTA probe used the preserved EPROM pair, system diskette and blank
CP3026 image through read-only frontend bindings. At 240,000,000 engine ticks
it remained healthy (`BM_STATUS_OK`) after 239,997,684 instructions and 24,244
successful I/O operations, ending at `F000:2B69` inside the firmware disk
service with a valid framebuffer. This establishes stable firmware interaction
with the installed controller, not XTA installation or a second hard-disk boot.

## Rejected shortcuts and replacement criteria

The implementation rejects embedding firmware, accepting arbitrary firmware
sizes, linking back to legacy globals, copying the whole legacy CPU, treating
unknown instructions as no-ops, and claiming that the clock metadata provides
cycle accuracy. Each would make a short demonstration easier while weakening
auditability or the intended platform boundary.

The opcode subset will be expanded incrementally into a complete portable V30
interpreter with conformance tests. CPU cycle accounting will replace the
measured instruction-domain rate while preserving the existing rational clock
adapters. Asynchronous DMA timing, the remaining board behaviours and
sufficient V30 coverage are the exit criteria
for meaningful comparison against original-firmware POST traces.

The second 80186-compatible CPU cut adds NEC's CHKIND/BOUND, C0h/C1h
immediate rotate/shift groups, PREPARE/ENTER and DISPOSE/LEAVE. Signed bounds,
the BRK 5 return address, full 8-bit counts and lexical levels, and explicit
rejection of NEC-undefined opcode 63h and shift group `/6` are synthetic
machine-independent contracts. The pinned physical V20 corpus has no vectors
for these six opcodes, so this cut relies on NEC's manufacturer manuals and
does not increase the hardware-vector total.

The third 80186-compatible cut adds NEC INM/OUTM through
INSB/INSW/OUTSB/OUTSW. Synthetic bus tests fix ES:DI as the input destination,
permit source overrides only for output, keep DX fixed under REP, cover both DF
directions, zero CX, byte order and port wrapping for word transfers, and
preserve completed progress on a later bus failure. It remains independent of
the PCS 86 firmware and devices.

The next CPU cut follows the NEC interrupt-disable and block-restart rules.
Maskable interrupt acceptance is delayed across the instruction after `EI` and
after segment-register transfers, including `POP`, `LES` and `LDS`. REP memory
and I/O loops can enter an interrupt after a successful iteration and save the
retained prefix address, so `IRET` repeats only the remaining work. The public
architectural snapshot is version 2 and includes this observable inhibit state.
Synthetic tests cover both string paths and the hardware's maximum of three
retained prefixes. NMI, single-step traps, prefetch state and physical V30 bus
timing are not claimed by this cut.

BIOS 1.09 writes `40h` to I/O port `70h` at `F000:0B29` while configuring the
upper conventional-memory path, between accesses to board ports `6Ch`, `6Bh`
and `6Fh`. No verified bit definition is currently recorded. The engine stores
that write in an explicit opaque latch so it remains observable, but does not
borrow the unrelated PC/AT CMOS/NMI convention. The independently configured
EMS block now supplies its documented aperture and backing memory without
assigning any meaning to the still-opaque port-`70h` bits.

With the 8237 register core, external page-latch block, complete MM58167 register
model, exact-state PIT, PVGA1A text renderer and the CPU paths required by their
firmware self-tests, the local BIOS probe executes 6,256,860 instructions and
2,960 successful I/O transactions. The
firmware writes and reads page ports in the observed channel order `87h`,
`83h`, `81h`, `82h`.
The PCS 86 component preserves each readable latch byte, masks only the four
page bits wired to a real DMA channel and combines those with the 8237 current
offset without pretending that the controller can yet own the bus or move a
byte. The MM58167 component handles the control and counter windows,
advances a BCD calendar and passes the firmware's Clock/Calendar diagnostic.
The exact-state PIT and corrected PIC edge withdrawal pass Timer 0. The probe
completes the following long memory test, reads ones while
scanning the explicitly unpopulated `C0000h-EFFFFh` option-ROM region, and
records the firmware's `46E8h=16h`, `102h=01h`, `46E8h=0Eh` selection sequence.
It then programs the isolated PVGA1A registers and renders the firmware's real
diagnostic text. The subsequent interactive-I/O cut provides a disconnected SPP
register model, a disconnected NS16450 with internal loopback, their port-65h
gates and IRQ routes, the board's two protocol queues and host-neutral keyboard
delivery. The next local validation adds a read-only 720 KiB raw image and
reaches the primary bootstrap. All visible resident diagnostics report `Pass`;
the BIOS detects one floppy, reads its boot sector through DMA2 and transfers
execution to it. DMA terminal count completes each requested sector even when
the command EOT exceeds the mounted track. An earlier cut stopped at the first
unclaimed DOS access, `02F2h`; the general passive-I/O policy now models the
board response for all such cycles without claiming that an absent device is
implemented. The PVGA1A status phase, scheduler rate, FDC
timing and PS/2 response timing remain deterministic bring-up approximations;
no printer/serial backend or mouse input is connected.

The main implementation files are `components/cpu/808x/src/cpu_808x.c`,
`components/memory/src/linear_memory.c`, `components/pc/src/dma8237.c`,
`components/pc/src/dma_page_registers.c`, `components/pc/src/fdc765.c`,
`components/pc/src/pic8259.c`,
`components/pc/src/pit8253.c`, `components/pc/src/rtc_mm58167.c`,
`components/pc/src/lpt_spp.c`, `components/pc/src/uart16450.c`,
`components/storage/src/floppy_drive.c`,
`components/video/src/pvga1a.c` and
`systems/olivetti-pcs86/src/olivetti_pcs86.c`. The corresponding public tests
include the focused `cpu_808x_post_test.c`, `cpu_808x_checksum_test.c`,
`cpu_808x_segment_test.c`, `cpu_808x_compare_test.c`, `dma8237_test.c`,
`dma_page_registers_test.c`, `rtc_mm58167_test.c`,
`pc_platform_test.c`, `cpu_808x_shift_test.c`, `cpu_808x_return_test.c`, the
focused sign-extension, string-comparison, decode-trace, far-control-flow,
exchange, negate, `XLAT`, carry-arithmetic and byte-division tests,
architectural-state, AAM/AAD, base-ISA completion and signed-multiply/divide
tests, the 80186-compatible stack/immediate and control-instruction tests, the
80186-compatible I/O-string test, the native and 8080 opcode-map matrices, the
timing-contract test, the external-vector adapter,
`fdc765_test.c`, `pvga1a_test.c`,
`legacy_io_test.c`, `pcs86_reset_test.c` and `pcs86_user_io_test.c`. The
unregistered `pcs86_firmware_probe.c` utility is manual by design so CI never
requires ROMs.
