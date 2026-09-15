# Olivetti PCS 86 portable-engine implementation

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

## Present outcome and evidence boundary

The current PCS 86 vertical slice in BluMach's portable engine executes both
newly authored conformance programs and, as a local-only manual validation,
the original revision 1.09 BIOS from the real-mode reset address. It supplies an
explicit NEC V30 state object, a generic address bus, 640 KiB of RAM, the
documented 64 KiB system-ROM window, a single 8259A, an 8253 subset and the
minimum known motherboard-register map. It is a bring-up milestone, not a
usable emulator: the BIOS completes its initial CPU/register checks and ROM
checksum, passes its memory and peripheral programming tests and enters the
firmware's onboard-video initialization. The measured boundary is now the
unsupported V30 instruction `SUB r8,r/m8` at `F000:85A1`, after 1,385,830
instructions and 811 successful I/O accesses. Before reaching it, the
firmware also completes its segment-overridden alias check, exercises both the
8237 programming registers and separate XT page latches, clears the MM58167
interrupt state at `B0h-B1h`, completes its long conventional-memory test and
completes the empty option-ROM scan, emits the observed video-selection writes
at `46E8h` and `102h`, programs the new PVGA1A register core and executes its
first video-memory copy path. The unpopulated option-ROM region returns ones;
the PCS 86 system EPROMs already contain video initialization, so the engine
does not fabricate a separate ROM. The PVGA1A component owns VGA/Paradise
register state and 256 KiB of planar VRAM, but rendering, scan timing and
framebuffer publication remain absent. DMA arbitration and transfers remain
absent, and no video output exists yet.

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

## First-cut component ledger

| Subsystem | Current level | Boundary |
|---|---|---|
| NEC V30 | New behavioural subset derived from the inherited core | Reset state, segmented 20-bit addresses, all four segment overrides, ModR/M effective addresses, tested arithmetic/logical and shift paths, direct/indirect near calls, software and maskable interrupt entry, IRET, stack and flag control, byte/word MOVS/STOS/LODS/SCAS with REP/REPE/REPNE and basic IN/OUT; no complete ISA or cycle timing |
| Conventional RAM | New generic component | 640 KiB, zero-initialized, byte-addressable bus region |
| System ROM | Evidence-backed map | Two 32 KiB halves interleaved at `F0000h-FFFFFh`; bytes remain external |
| Scheduler timing | Approximate | One instruction per tick; 10 MHz is identity metadata until clock-domain timing lands |
| Single 8259A PIC | Derived portable subset | Initialization, masking, edge requests, output callback, CPU acknowledge and EOI; no cascaded/level modes |
| 8253 PIT | Derived portable subset | Deterministic binary modes 0, 2 and 3 plus stable counter-latch reads; no BCD or clock-domain integration |
| PCS 86 board glue | Derived minimum map | Reset values and known semantics at `60h-6Fh`, `A0h`, `100h` and the POST diagnostic latch at `378h`; opaque write-only memory-control state at `70h`; queues and attached peripherals are absent |
| EMS selectors | Deliberate boundary | Write-only page-selector latches at `8400h-8403h`; no aperture or backing SIMMs are claimed or exposed |
| 8237 DMA | Programming subset derived from the inherited core | Address/count flip-flop, base/current registers, command, mode, request, masks, status and master clear; a separate XT latch block supplies four-bit pages and observable 20-bit current addresses; no arbitration, bus ownership or data transfers |
| MM58167 RTC | Interrupt front only | `B0h` returns and clears interrupt status; `B1h` stores interrupt control and clears status. `B2h-B7h`, `E0h-EFh`, clock progression, alarms, IRQ output and persistence remain unavailable |
| PCS 86 video selection | Observed write-only boundary | Empty `C0000h-EFFFFh` option-ROM space returns ones; `46E8h` and `102h` retain the BIOS-observed arbitration writes without undocumented side effects |
| Paradise PVGA1A | Derived portable register/VRAM core | Isolated VGA and Paradise registers, DAC state and 256 KiB planar VRAM; deterministic untimed input status, no scan renderer or framebuffer publication |
| Complete PPI behaviour | Unavailable | Still required before complete original-BIOS POST comparison |
| Video presentation, keyboard and storage | Unavailable | Planned as later vertical cuts |

Unsupported opcodes return a structured `BM_STATUS_UNSUPPORTED` result. They
are not skipped, approximated as NOPs or redirected to the inherited engine.
This makes the incomplete boundary visible to tests and debuggers.

## Validation ladder

The current automated ladder uses no historical software:

1. The generic memory test checks little- and big-endian transactions, fetch,
   write protection, unmapped access and direct debug inspection.
2. The PC component tests initialize and service the single PIC, program an
   8253 channel and verify the 8237 register, mask, request, status, byte-pointer
   and master-clear contracts without attaching a storage device. A separate
   test verifies the page-port-to-channel map, four-bit masking, reserved
   latches, effective 20-bit addresses and independent reset semantics.
3. The V30 tests reproduce the BIOS register/flag self-test, exercise its
   segmented checksum-loop pattern and verify maskable-interrupt stack/vector
   entry using newly authored memory images.
4. The PCS 86 test creates two synthetic 32 KiB halves in memory. Their
   interleaved reset vector performs a far jump from physical `FFFF0h` to
   `F0100h`, writes RAM, initializes the PIC, programs the PIT, exercises the
   board-control register and reads the fixed diagnostic register before halt.
5. A dedicated CPU test writes and reads words through `ES:`, `SS:`, `DS:` and
   `CS:`, verifies that the last repeated segment prefix wins, and covers the
   stack, near-call, shift, segment-register and string paths added for POST.
6. CPU and I/O traces verify exact instruction and port checkpoints.
7. Firmware metadata validation rejects a supplied hash that differs from the
   known PCS 86 identity.

Original firmware is intentionally not used in CI and no ROM, disk, manual or
diagnostic asset is present in these public files. The manual firmware probe
accepts two external 32 KiB halves and reports the exact instruction boundary;
it does not weaken the rule that firmware is caller-owned local data.

## Rejected shortcuts and replacement criteria

The implementation rejects embedding firmware, accepting arbitrary firmware
sizes, linking back to legacy globals, copying the whole legacy CPU, treating
unknown instructions as no-ops, and claiming that the clock metadata provides
cycle accuracy. Each would make a short demonstration easier while weakening
auditability or the intended platform boundary.

The opcode subset will be expanded incrementally into a complete portable V30
interpreter with conformance tests. Instruction and bus timing will replace the
instruction tick once the scheduler has explicit clock domains. DMA page and
transfer semantics, RTC, the remaining board behaviours and sufficient V30 coverage are the exit criteria
for meaningful comparison against original-firmware POST traces.

BIOS 1.09 writes `40h` to I/O port `70h` at `F000:0B29` while configuring the
upper conventional-memory path, between accesses to board ports `6Ch`, `6Bh`
and `6Fh`. No verified bit definition is currently recorded. The engine stores
that write in an explicit opaque latch so it remains observable, but does not
borrow the unrelated PC/AT CMOS/NMI convention. It likewise accepts the known
EMS selector range `8400h-8403h` without claiming that the deferred EMS aperture
or backing memory exists.

With the 8237 register core, external page-latch block, MM58167 interrupt front,
PVGA1A register/VRAM core and the CPU paths required by their firmware self-tests,
the local BIOS probe executes 1,385,830 instructions and 811 successful I/O
transactions. The
firmware writes and reads page ports in the observed channel order `87h`,
`83h`, `81h`, `82h`.
The PCS 86 component masks each latch to four bits and combines it with the
8237 current offset without pretending that the controller can yet own the bus
or move a byte. The MM58167 component then handles the firmware's `IN AL,B0h`
and `OUT B1h,AL` sequence with the inherited read-to-clear and write-to-clear
semantics. The probe completes the following long memory test, reads ones while
scanning the explicitly unpopulated `C0000h-EFFFFh` option-ROM region, and
records the firmware's `46E8h=16h`, `102h=01h`, `46E8h=0Eh` selection sequence.
It then programs the isolated PVGA1A registers and reaches video-memory copy
code before stopping at `F000:85A1` with `BM_STATUS_UNSUPPORTED` on
`SUB r8,r/m8`. The PVGA1A status phase is a deterministic bring-up approximation;
no framebuffer or floppy controller is connected. The rest of the RTC remains explicitly
pending rather than being approximated.

The main implementation files are `components/cpu/808x/src/cpu_808x.c`,
`components/memory/src/linear_memory.c`, `components/pc/src/dma8237.c`,
`components/pc/src/dma_page_registers.c`, `components/pc/src/pic8259.c`,
`components/pc/src/pit8253.c`, `components/pc/src/rtc_mm58167.c`,
`components/video/src/pvga1a.c` and
`systems/olivetti-pcs86/src/olivetti_pcs86.c`. The corresponding public tests
include the focused `cpu_808x_post_test.c`, `cpu_808x_checksum_test.c`,
`cpu_808x_segment_test.c`, `cpu_808x_compare_test.c`, `dma8237_test.c`,
`dma_page_registers_test.c`, `rtc_mm58167_test.c`,
`pc_platform_test.c`, `pvga1a_test.c` and `pcs86_reset_test.c`. The
unregistered `pcs86_firmware_probe.c` utility is manual by design so CI never
requires ROMs.
