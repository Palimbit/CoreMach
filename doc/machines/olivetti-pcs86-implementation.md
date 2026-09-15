# Olivetti PCS 86 portable-engine implementation

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

## Present outcome and evidence boundary

The current PCS 86 vertical slice in BluMach's portable engine executes both
newly authored conformance programs and, as a local-only manual validation,
the original revision 1.09 BIOS from the real-mode reset address. It supplies an
explicit NEC V30 state object, a generic address bus, 640 KiB of RAM, the
documented 64 KiB system-ROM window, a single 8259A, a complete 8253 mode model,
a functional MM58167, isolated SPP and NS16450 components and the known
motherboard-register map. It is a
bring-up milestone, not a usable emulator: the BIOS now visibly passes CPU,
ROM, DMA, interrupt-controller, Timer 0 and Clock/Calendar diagnostics and
reports 640 kB. The measured boundary is the first unimplemented floppy-control
write at `3F2h`, after 6,272,717 retired instructions and 3,251 successful I/O
accesses. Before reaching it, the firmware also completes its memory and
option-ROM scans, programs the PVGA1A and renders a real 720x400 diagnostic
frame. The unpopulated option-ROM region returns ones;
the PCS 86 system EPROMs already contain video initialization, so the engine
does not fabricate a separate ROM. The PVGA1A component owns VGA/Paradise
register state and 256 KiB of planar VRAM and publishes a deterministic
host-neutral text framebuffer. It then exercises the enabled parallel and
serial register paths. Host-neutral keyboard events and the board's dual
keyboard/mouse queues are present, while DMA arbitration and transfers, FDC
operation, mouse input and storage remain absent.

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
| Scheduler timing | Functional approximation | One retired instruction per engine tick; rational PIT/RTC clock accumulators use a measured functional instruction rate, not V30 cycle accounting |
| Single 8259A PIC | Derived portable subset | Initialization, masking, edge requests including withdrawal before INTA, output callback, CPU acknowledge and EOI; no cascaded/level modes |
| 8253 PIT | Selective port of measured edge-state core | Deterministic modes 0-5, binary and BCD counts, gates, output edges and stable counter-latch reads; driven from scheduler time without claiming cycle accuracy |
| PCS 86 board glue | Derived minimum map | Reset values and known semantics at `60h-6Fh`, `A0h`, `100h` and the early POST diagnostic latch at disabled `378h`; opaque write-only memory-control state at `70h`; dual keyboard/mouse command queues and IRQ1 scan queue |
| EMS selectors | Deliberate boundary | Write-only page-selector latches at `8400h-8403h`; no aperture or backing SIMMs are claimed or exposed |
| 8237 DMA | Programming subset derived from the inherited core | Address/count flip-flop, base/current registers, command, mode, request, masks, status and master clear; a separate XT latch block supplies four-bit pages and observable 20-bit current addresses; no arbitration, bus ownership or data transfers |
| MM58167 RTC | Functional portable component | PCS 86 `B0h-B7h` controls and `E0h-EFh` counter/alarm RAM, BCD millisecond calendar, alarm and periodic IRQs, reset/GO/standby commands, checksum repair and 32-byte caller-owned persistence; yearless calendar and physical crystal/battery behaviour remain approximate |
| SPP parallel port | Derived portable register core | Data, status and control at gated `378h-37Ah`, disconnected-printer status, output callback and ACK-driven IRQ7; no EPP/ECP, printer backend, DMA or host threads |
| NS16450 UART | Derived portable register core | Divisor latch, IER/IIR, LCR/MCR, LSR/MSR, scratch, modem/data loopback and IRQ4 at gated `3F8h-3FFh`; no 16550 FIFO, host serial backend or baud scheduling |
| Keyboard input | New runtime contract plus PCS 86 translation | Stable physical-key events, supported IBM Set 1 make/break bytes and IRQ1; no host scan codes in the engine, mouse input, electrical timing or complete command set |
| PCS 86 video selection | Observed write-only boundary | Empty `C0000h-EFFFFh` option-ROM space returns ones; `46E8h` and `102h` retain the BIOS-observed arbitration writes without undocumented side effects |
| Paradise PVGA1A | Derived portable register/VRAM core | Isolated VGA and Paradise registers, DAC state and 256 KiB planar VRAM; deterministic text rasterizer and host-neutral XRGB8888 framebuffer, but no graphics modes or scan timing |
| Complete PPI behaviour | Unavailable | Still required before complete original-BIOS POST comparison |
| Floppy controller and storage | Unavailable | Next vertical cut begins at the observed `3F2h` write |

Unsupported opcodes return a structured `BM_STATUS_UNSUPPORTED` result. They
are not skipped, approximated as NOPs or redirected to the inherited engine.
This makes the incomplete boundary visible to tests and debuggers.

## Validation ladder

The current automated ladder uses no historical software:

1. The generic memory test checks little- and big-endian transactions, fetch,
   write protection, unmapped access and direct debug inspection.
2. The PC component tests initialize and service the single PIC, cover withdrawn
   edge requests and all six 8253 modes, gates, BCD and latching, and verify the
   8237 register, mask, request, status, byte-pointer
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
5. Dedicated CPU tests write and read words through `ES:`, `SS:`, `DS:` and
   `CS:`, verifies that the last repeated segment prefix wins, and covers the
   stack, near-call, segment-register and string paths added for POST. A focused
   test covers all D0-D3 rotate/shift operations, TEST register/memory forms and
   the complete compact `XCHG AX,r16` family.
6. CPU and I/O traces verify exact instruction and port checkpoints.
7. Firmware metadata validation rejects a supplied hash that differs from the
   known PCS 86 identity.
8. Focused SPP/NS16450 tests verify register reset, loopback and interrupt
   behaviour. A PCS 86 integration ROM enables both gated devices, performs a
   UART data loopback and reads the keyboard identify response. The session
   test also verifies normalized make/break delivery and reset cleanup.

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
interpreter with conformance tests. CPU cycle accounting will replace the
measured instruction-domain rate while preserving the existing rational clock
adapters. DMA transfer semantics, the remaining board behaviours and sufficient V30 coverage are the exit criteria
for meaningful comparison against original-firmware POST traces.

BIOS 1.09 writes `40h` to I/O port `70h` at `F000:0B29` while configuring the
upper conventional-memory path, between accesses to board ports `6Ch`, `6Bh`
and `6Fh`. No verified bit definition is currently recorded. The engine stores
that write in an explicit opaque latch so it remains observable, but does not
borrow the unrelated PC/AT CMOS/NMI convention. It likewise accepts the known
EMS selector range `8400h-8403h` without claiming that the deferred EMS aperture
or backing memory exists.

With the 8237 register core, external page-latch block, complete MM58167 register
model, exact-state PIT, PVGA1A text renderer and the CPU paths required by their
firmware self-tests, the local BIOS probe executes 6,256,860 instructions and
2,960 successful I/O transactions. The
firmware writes and reads page ports in the observed channel order `87h`,
`83h`, `81h`, `82h`.
The PCS 86 component masks each latch to four bits and combines it with the
8237 current offset without pretending that the controller can yet own the bus
or move a byte. The MM58167 component handles the control and counter windows,
advances a BCD calendar and passes the firmware's Clock/Calendar diagnostic.
The exact-state PIT and corrected PIC edge withdrawal pass Timer 0. The probe
completes the following long memory test, reads ones while
scanning the explicitly unpopulated `C0000h-EFFFFh` option-ROM region, and
records the firmware's `46E8h=16h`, `102h=01h`, `46E8h=0Eh` selection sequence.
It then programs the isolated PVGA1A registers and renders the firmware's real
diagnostic text. The subsequent interactive-I/O cut provides a disconnected SPP
register model, a disconnected NS16450 with internal loopback, their port-65h
gates and IRQ routes, the board's two protocol queues and host-neutral keyboard
delivery. The same local firmware then reaches `F000:352C`, where
`OUT 3F2h,AL` stops on the absent floppy controller after 6,272,717 instructions
and 3,251 successful I/O transactions. The PVGA1A status phase, scheduler rate
and PS/2 response timing remain deterministic bring-up approximations; no
printer/serial backend, mouse input, floppy controller or DMA transfer path is
connected.

The main implementation files are `components/cpu/808x/src/cpu_808x.c`,
`components/memory/src/linear_memory.c`, `components/pc/src/dma8237.c`,
`components/pc/src/dma_page_registers.c`, `components/pc/src/pic8259.c`,
`components/pc/src/pit8253.c`, `components/pc/src/rtc_mm58167.c`,
`components/pc/src/lpt_spp.c`, `components/pc/src/uart16450.c`,
`components/video/src/pvga1a.c` and
`systems/olivetti-pcs86/src/olivetti_pcs86.c`. The corresponding public tests
include the focused `cpu_808x_post_test.c`, `cpu_808x_checksum_test.c`,
`cpu_808x_segment_test.c`, `cpu_808x_compare_test.c`, `dma8237_test.c`,
`dma_page_registers_test.c`, `rtc_mm58167_test.c`,
`pc_platform_test.c`, `cpu_808x_shift_test.c`, `pvga1a_test.c`,
`legacy_io_test.c`, `pcs86_reset_test.c` and `pcs86_user_io_test.c`. The
unregistered `pcs86_firmware_probe.c` utility is manual by design so CI never
requires ROMs.
