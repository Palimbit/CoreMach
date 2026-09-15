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
checksum, passes its first conventional-memory alias check and then stops
after clearing and scanning a selected 64 KiB memory window. The measured
boundary is the unsupported `ES:` segment override at `F000:0137`.

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
| NEC V30 | New behavioural subset derived from the inherited core | Reset state, segmented 20-bit addresses, ModR/M effective addresses, arithmetic flags, branches, checksum and initial memory-check operations, `AND r/m16,imm16`, `STOSW`/`SCASW` with `REP`, basic IN/OUT and interrupt entry; no complete ISA or cycle timing |
| Conventional RAM | New generic component | 640 KiB, zero-initialized, byte-addressable bus region |
| System ROM | Evidence-backed map | Two 32 KiB halves interleaved at `F0000h-FFFFFh`; bytes remain external |
| Scheduler timing | Approximate | One instruction per tick; 10 MHz is identity metadata until clock-domain timing lands |
| Single 8259A PIC | Derived portable subset | Initialization, masking, edge requests, output callback, CPU acknowledge and EOI; no cascaded/level modes |
| 8253 PIT | Derived portable subset | Deterministic binary modes 0, 2 and 3; no BCD, latching or clock-domain integration |
| PCS 86 board glue | Derived minimum map | Reset values and known semantics at `60h-6Fh`, `A0h`, `100h` and the POST diagnostic latch at `378h`; opaque write-only memory-control state at `70h`; queues and attached peripherals are absent |
| EMS selectors | Deliberate boundary | Write-only page-selector latches at `8400h-8403h`; no aperture or backing SIMMs are claimed or exposed |
| DMA, RTC and complete PPI behaviour | Unavailable | Still required before meaningful original-BIOS POST comparison |
| Video, keyboard and storage | Unavailable | Planned as later vertical cuts |

Unsupported opcodes return a structured `BM_STATUS_UNSUPPORTED` result. They
are not skipped, approximated as NOPs or redirected to the inherited engine.
This makes the incomplete boundary visible to tests and debuggers.

## Validation ladder

The current automated ladder uses no historical software:

1. The generic memory test checks little- and big-endian transactions, fetch,
   write protection, unmapped access and direct debug inspection.
2. The PC component test initializes and services the single PIC, then programs
   an 8253 channel and advances it through a deterministic output transition.
3. The V30 tests reproduce the BIOS register/flag self-test, exercise its
   segmented checksum-loop pattern and verify maskable-interrupt stack/vector
   entry using newly authored memory images.
4. The PCS 86 test creates two synthetic 32 KiB halves in memory. Their
   interleaved reset vector performs a far jump from physical `FFFF0h` to
   `F0100h`, writes RAM, initializes the PIC, programs the PIT, exercises the
   board-control register and reads the fixed diagnostic register before halt.
5. CPU and I/O traces verify exact instruction and port checkpoints.
6. Firmware metadata validation rejects a supplied hash that differs from the
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
instruction tick once the scheduler has explicit clock domains. DMA, RTC, the
remaining board behaviours and sufficient V30 coverage are the exit criteria
for meaningful comparison against original-firmware POST traces.

BIOS 1.09 writes `40h` to I/O port `70h` at `F000:0B29` while configuring the
upper conventional-memory path, between accesses to board ports `6Ch`, `6Bh`
and `6Fh`. No verified bit definition is currently recorded. The engine stores
that write in an explicit opaque latch so it remains observable, but does not
borrow the unrelated PC/AT CMOS/NMI convention. It likewise accepts the known
EMS selector range `8400h-8403h` without claiming that the deferred EMS aperture
or backing memory exists.

With `AND r/m16,imm16` and forward `REP STOSW`/`REPE SCASW` implemented, the
local BIOS probe executes 196,784 instructions and 27 successful I/O
transactions before stopping at `F000:0137` on the unsupported `ES:` prefix.
That boundary follows a complete 64 KiB clear-and-scan operation. Segment
overrides and immediate writes through ModR/M are the next CPU work; DMA and RTC
should follow only as the executed firmware path reaches them.

The main implementation files are `components/cpu/808x/src/cpu_808x.c`,
`components/memory/src/linear_memory.c`, `components/pc/src/pic8259.c`,
`components/pc/src/pit8253.c` and
`systems/olivetti-pcs86/src/olivetti_pcs86.c`. The corresponding public tests
include the focused `cpu_808x_post_test.c`, `cpu_808x_checksum_test.c`,
`pc_platform_test.c` and `pcs86_reset_test.c`. The unregistered
`pcs86_firmware_probe.c` utility is manual by design so CI never requires ROMs.
