# Olivetti PCS 86 portable-engine implementation

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

## Present outcome and evidence boundary

The first PCS 86 vertical slice in BluMach's portable engine executes a newly
authored synthetic program from the real-mode reset address. It supplies an
explicit NEC V30 state object, a generic address bus, 640 KiB of RAM, and the
documented 64 KiB system-ROM window. It is a bring-up milestone, not a usable
emulator: the original BIOS cannot run because most instructions and all board
devices remain absent.

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
| NEC V30 | New behavioural subset derived from the inherited core | Reset state, segmented 20-bit addresses and seven bring-up instruction forms; no complete ISA or cycle timing |
| Conventional RAM | New generic component | 640 KiB, zero-initialized, byte-addressable bus region |
| System ROM | Evidence-backed map | Two 32 KiB halves interleaved at `F0000h-FFFFFh`; bytes remain external |
| Scheduler timing | Approximate | One instruction per tick; 10 MHz is identity metadata until clock-domain timing lands |
| PIC, PIT, DMA, PPI and board glue | Unavailable | Required for POST and planned for PCS86-2 |
| Video, keyboard and storage | Unavailable | Planned as later vertical cuts |

Unsupported opcodes return a structured `BM_STATUS_UNSUPPORTED` result. They
are not skipped, approximated as NOPs or redirected to the inherited engine.
This makes the incomplete boundary visible to tests and debuggers.

## Validation ladder

The current automated ladder uses no historical software:

1. The generic memory test checks little- and big-endian transactions, fetch,
   write protection, unmapped access and direct debug inspection.
2. The PCS 86 test creates two synthetic 32 KiB halves in memory. Their
   interleaved reset vector performs a far jump from physical `FFFF0h` to
   `F0100h`, initializes `DS`, writes through the bus and halts.
3. CPU introspection verifies reset and final registers, the 10 MHz identity,
   halt state and exact instruction-fetch checkpoints.
4. Firmware metadata validation rejects a supplied hash that differs from the
   known PCS 86 identity.

Original firmware is intentionally not used in CI and no ROM, disk, manual or
diagnostic asset is present in these public files. Running the original BIOS
will only become meaningful after the portable interpreter covers its executed
instruction paths and the minimum POST devices exist.

## Rejected shortcuts and replacement criteria

The implementation rejects embedding firmware, accepting arbitrary firmware
sizes, linking back to legacy globals, copying the whole legacy CPU, treating
unknown instructions as no-ops, and claiming that the clock metadata provides
cycle accuracy. Each would make a short demonstration easier while weakening
auditability or the intended platform boundary.

The opcode subset will be replaced incrementally by a complete portable V30
interpreter with conformance tests. Instruction and bus timing will replace the
instruction tick once the scheduler has explicit clock domains. PIC, PIT, PPI
and board glue are the exit criteria for beginning comparison against original
firmware POST traces.

The main implementation files are `components/cpu/808x/src/cpu_808x.c`,
`components/memory/src/linear_memory.c` and
`systems/olivetti-pcs86/src/olivetti_pcs86.c`. The corresponding public tests
are `tests/engine/linear_memory_test.c` and
`tests/engine/pcs86_reset_test.c`.
